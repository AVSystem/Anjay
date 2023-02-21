/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

// NOTE: Some compat headers need to be included before avs_log.h,
// so we can't use anjay_init.h here.
#include <anjay/anjay_config.h>
#include <avsystem/commons/avs_commons_config.h>

#ifdef ANJAY_WITH_EVENT_LOOP

#    ifdef AVS_COMMONS_POSIX_COMPAT_HEADER
#        include AVS_COMMONS_POSIX_COMPAT_HEADER
#    else // AVS_COMMONS_POSIX_COMPAT_HEADER
#        ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
#            include <poll.h>
#        else // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
#            include <sys/select.h>
#        endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
#    endif     // AVS_COMMONS_POSIX_COMPAT_HEADER

#    include <anjay_init.h>

#    include "anjay_core.h"

VISIBILITY_SOURCE_BEGIN

#    ifndef AVS_COMMONS_POSIX_COMPAT_HEADER
typedef int sockfd_t;
#    endif // AVS_COMMONS_POSIX_COMPAT_HEADER

#    ifndef INVALID_SOCKET
#        define INVALID_SOCKET (-1)
#    endif

static bool should_event_loop_still_run(anjay_t *anjay) {
    int status = ANJAY_EVENT_LOOP_INTERRUPT;
    if (atomic_compare_exchange_strong(&anjay->atomic_fields.event_loop_status,
                                       &status,
                                       ANJAY_EVENT_LOOP_IDLE)) {
        // interrupt has been just handled
        return false;
    } else {
        // expected now contains the value of event_loop_status
        return status == ANJAY_EVENT_LOOP_RUNNING;
    }
}

typedef struct {
    anjay_t *const anjay_locked;
    const avs_time_duration_t max_wait_time;
    const bool allow_interrupt;
#    ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    struct pollfd *pollfds;
    size_t pollfds_size;
#    endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
} event_loop_state_t;

static void event_loop_state_cleanup(event_loop_state_t *state) {
    (void) state;
#    ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    avs_free(state->pollfds);
    state->pollfds = NULL;
    state->pollfds_size = 0;
#    endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
}

typedef enum {
    HANDLE_SOCKETS_ERROR = -1,
    HANDLE_SOCKETS_CONTINUE = 0,
    HANDLE_SOCKETS_BREAK = 1
} handle_sockets_result_t;

static handle_sockets_result_t handle_sockets(event_loop_state_t *state) {
    assert(state->anjay_locked);
    assert(avs_time_duration_valid(state->max_wait_time)
           && !avs_time_duration_less(state->max_wait_time,
                                      AVS_TIME_DURATION_ZERO));
#    ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    size_t numsocks = 0;
    size_t i = 0;
#    else  // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    fd_set infds;
    fd_set outfds;
    fd_set errfds;
    sockfd_t nfds = 0;
#    endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    handle_sockets_result_t result = HANDLE_SOCKETS_CONTINUE;
    AVS_LIST(const anjay_socket_entry_t) entries = NULL;
    AVS_LIST(const anjay_socket_entry_t) *entry_ptr = NULL;
    AVS_LIST(const anjay_socket_entry_t) entry = NULL;
    bool sockets_ready = false;

    ANJAY_MUTEX_LOCK(anjay, state->anjay_locked);
    entries =
            _anjay_collect_socket_entries(anjay, /* include_offline = */ false);

#    ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    numsocks = AVS_LIST_SIZE(entries);
    if (numsocks != state->pollfds_size) {
        struct pollfd *pollfds_new = NULL;
        if (!numsocks) {
            avs_free(state->pollfds);
        } else {
            pollfds_new = (struct pollfd *) avs_realloc(
                    state->pollfds, numsocks * sizeof(*state->pollfds));
        }
        if (pollfds_new || !numsocks) {
            state->pollfds = pollfds_new;
            state->pollfds_size = numsocks;
        } else if (numsocks > state->pollfds_size) {
            anjay_log(ERROR, "Out of memory in anjay_event_loop_run()");
            result = HANDLE_SOCKETS_ERROR;
        }
    }

    if (result == HANDLE_SOCKETS_CONTINUE) {
        AVS_LIST_DELETABLE_FOREACH_PTR(entry_ptr, entry, &entries) {
            assert(i < numsocks);
            state->pollfds[i].events = POLLIN;
            state->pollfds[i].revents = 0;
            state->pollfds[i].fd = INVALID_SOCKET;
            const void *fd_ptr =
                    avs_net_socket_get_system((*entry_ptr)->socket);
            if (fd_ptr) {
                state->pollfds[i].fd = *(const sockfd_t *) fd_ptr;
            }
            if (state->pollfds[i].fd == INVALID_SOCKET) {
                AVS_LIST_DELETE(entry_ptr);
            } else {
                ++i;
            }
        }
    }
#    else  // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    FD_ZERO(&infds);
    FD_ZERO(&outfds);
    FD_ZERO(&errfds);
    nfds = 0;

    AVS_LIST_DELETABLE_FOREACH_PTR(entry_ptr, entry, &entries) {
        sockfd_t fd = INVALID_SOCKET;
        const void *fd_ptr = avs_net_socket_get_system((*entry_ptr)->socket);
        if (fd_ptr) {
            fd = *(const sockfd_t *) fd_ptr;
        }
        if (fd == INVALID_SOCKET || fd >= FD_SETSIZE) {
            AVS_LIST_DELETE(entry_ptr);
        } else {
            FD_SET(fd, &infds);
            FD_SET(fd, &errfds);
            nfds = AVS_MAX(nfds, fd + 1);
        }
    }
#    endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    ANJAY_MUTEX_UNLOCK(state->anjay_locked);

    avs_time_duration_t wait_time;
    if (anjay_sched_time_to_next(state->anjay_locked, &wait_time)
            || !avs_time_duration_less(wait_time, state->max_wait_time)) {
        wait_time = state->max_wait_time;
    }
    assert(avs_time_duration_valid(wait_time)
           && !avs_time_duration_less(wait_time, AVS_TIME_DURATION_ZERO));

    // Wait for the events if necessary, and handle them.
#    ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
    int64_t wait_ms;
    if (result == HANDLE_SOCKETS_CONTINUE) {
        if (avs_time_duration_to_scalar(&wait_ms, AVS_TIME_MS, wait_time)
                || wait_ms > INT_MAX) {
            wait_ms = (int64_t) INT_MAX;
        }
        sockets_ready = (poll(state->pollfds, i, (int) wait_ms) > 0);
    }
    i = 0;
#    else  // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
           // NOTE: This assumes that time_t is a signed integer type
    static const time_t AVS_TIME_MAX =
            (time_t) ((UINT64_C(1) << (8 * sizeof(time_t) - 1)) - 1);
    struct timeval wait_timeval = {
        .tv_sec = AVS_TIME_MAX
    };
    if (wait_time.seconds <= AVS_TIME_MAX) {
        wait_timeval.tv_sec = (time_t) wait_time.seconds;
        wait_timeval.tv_usec = (int32_t) (wait_time.nanoseconds / 1000);
    }
    sockets_ready = (select(nfds, &infds, &outfds, &errfds, &wait_timeval) > 0);
#    endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL

    if (sockets_ready) {
        AVS_LIST_FOREACH(entry, entries) {
            if (state->allow_interrupt
                    && !should_event_loop_still_run(state->anjay_locked)) {
                result = HANDLE_SOCKETS_BREAK;
                break;
            }
#    ifdef AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
            assert(i < numsocks);
            if (!state->pollfds[i++].revents) {
                continue;
            }
#    else  // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
            sockfd_t fd = INVALID_SOCKET;
            const void *fd_ptr = avs_net_socket_get_system(entry->socket);
            if (fd_ptr) {
                fd = *(const sockfd_t *) fd_ptr;
            }
            if (fd == INVALID_SOCKET
                    || !(FD_ISSET(fd, &infds) || FD_ISSET(fd, &errfds))) {
                continue;
            }
#    endif // AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL
            if (anjay_serve(state->anjay_locked, entry->socket)) {
                anjay_log(WARNING, "anjay_serve failed");
            }
        }
    }

    AVS_LIST_CLEAR(&entries);
    return result;
}

static int event_loop_run_with_error_handling(anjay_t *anjay_locked,
                                              avs_time_duration_t max_wait_time,
                                              bool enable_error_handling) {
    if (!avs_time_duration_valid(max_wait_time)
            || avs_time_duration_less(max_wait_time, AVS_TIME_DURATION_ZERO)) {
        anjay_log(ERROR, "max_wait_time needs to be valid and non-negative");
        return -1;
    }
    if (!atomic_compare_exchange_strong(
                &anjay_locked->atomic_fields.event_loop_status,
                &(int) { ANJAY_EVENT_LOOP_IDLE },
                ANJAY_EVENT_LOOP_RUNNING)) {
        anjay_log(ERROR, "Event loop is already running");
        return -1;
    }
    handle_sockets_result_t handle_sockets_result = HANDLE_SOCKETS_CONTINUE;
    event_loop_state_t state = {
        .anjay_locked = anjay_locked,
        .max_wait_time = max_wait_time,
        .allow_interrupt = true
    };
    bool running = should_event_loop_still_run(anjay_locked);
    while (running) {
        handle_sockets_result = handle_sockets(&state);
        switch (handle_sockets_result) {
        case HANDLE_SOCKETS_ERROR:
            atomic_store(&anjay_locked->atomic_fields.event_loop_status,
                         ANJAY_EVENT_LOOP_IDLE);
            // fall through
        case HANDLE_SOCKETS_BREAK:
            running = false;
            break;
        case HANDLE_SOCKETS_CONTINUE:
            anjay_sched_run(anjay_locked);
            running = should_event_loop_still_run(anjay_locked);

            if (enable_error_handling) {
                if (anjay_all_connections_failed(anjay_locked)) {
                    anjay_transport_schedule_reconnect(anjay_locked,
                                                       ANJAY_TRANSPORT_SET_ALL);
                }
            }
        }
    }
    event_loop_state_cleanup(&state);
    return handle_sockets_result == HANDLE_SOCKETS_ERROR ? -1 : 0;
}

int anjay_event_loop_run(anjay_t *anjay_locked,
                         avs_time_duration_t max_wait_time) {
    return event_loop_run_with_error_handling(anjay_locked, max_wait_time,
                                              false);
}

int anjay_event_loop_run_with_error_handling(
        anjay_t *anjay_locked, avs_time_duration_t max_wait_time) {
    return event_loop_run_with_error_handling(anjay_locked, max_wait_time,
                                              true);
}

int anjay_event_loop_interrupt(anjay_t *anjay) {
    return atomic_compare_exchange_strong(
                   &anjay->atomic_fields.event_loop_status,
                   &(int) { ANJAY_EVENT_LOOP_RUNNING },
                   ANJAY_EVENT_LOOP_INTERRUPT)
                   ? 0
                   : -1;
}

int anjay_serve_any(anjay_t *anjay_locked, avs_time_duration_t max_wait_time) {
    if (!avs_time_duration_valid(max_wait_time)
            || avs_time_duration_less(max_wait_time, AVS_TIME_DURATION_ZERO)) {
        anjay_log(ERROR, "max_wait_time needs to be valid and non-negative");
        return -1;
    }
    event_loop_state_t state = {
        .anjay_locked = anjay_locked,
        .max_wait_time = max_wait_time,
        .allow_interrupt = false
    };
    handle_sockets_result_t handle_sockets_result = handle_sockets(&state);
    event_loop_state_cleanup(&state);
    return handle_sockets_result == HANDLE_SOCKETS_ERROR ? -1 : 0;
}

#endif // ANJAY_WITH_EVENT_LOOP
