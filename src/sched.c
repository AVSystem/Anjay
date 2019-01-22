/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <anjay_config.h>

#include <inttypes.h>
#include <stdbool.h>
#include <time.h>

#include <avsystem/commons/list.h>

#include <anjay/core.h>

#include <anjay_modules/sched.h>
#include <anjay_modules/time_defs.h>

#define ANJAY_SCHED_C

#include "anjay_core.h"
#include "sched_internal.h"
#include "utils_core.h"

#define sched_log(...) _anjay_log(anjay_sched, __VA_ARGS__)

VISIBILITY_SOURCE_BEGIN

anjay_sched_t *_anjay_sched_get(anjay_t *anjay) {
    return anjay->sched;
}

anjay_sched_t *_anjay_sched_new(anjay_t *anjay) {
    anjay_sched_t *sched =
            (anjay_sched_t *) avs_calloc(1, sizeof(anjay_sched_t));
    if (sched) {
        sched->anjay = anjay;
    }
    return sched;
}

static anjay_sched_entry_t *fetch_task(anjay_sched_t *sched,
                                       const avs_time_monotonic_t *now) {
    if (sched->entries
            && !avs_time_monotonic_before(*now, sched->entries->when)) {
        return AVS_LIST_DETACH(&sched->entries);
    } else {
        return NULL;
    }
}

static anjay_sched_handle_t sched_delayed(anjay_sched_t *sched,
                                          avs_time_duration_t delay,
                                          AVS_LIST(anjay_sched_entry_t) entry);

static void execute_task(anjay_sched_t *sched,
                         AVS_LIST(anjay_sched_entry_t) entry) {
    /* make sure the task is detached */
    assert(AVS_LIST_NEXT(entry) == NULL);

    sched_log(TRACE, "executing task %p", (void *) entry);

    if (entry->handle_ptr) {
        *entry->handle_ptr = NULL;
    }

    entry->clb(sched->anjay, &entry->clb_data);
    AVS_LIST_DELETE(&entry);
}

ssize_t _anjay_sched_run(anjay_sched_t *sched) {
    int running = 1;
    ssize_t tasks_executed = 0;

    avs_time_monotonic_t now = avs_time_monotonic_now();

    while (running) {
        anjay_sched_entry_t *task = fetch_task(sched, &now);
        if (!task) {
            running = 0;
        } else {
            execute_task(sched, task);
            ++tasks_executed;
        }
    }

    avs_time_duration_t delay = AVS_TIME_DURATION_ZERO;
    _anjay_sched_time_to_next(sched, &delay);
    sched_log(TRACE,
              "%lu scheduled tasks remain; next after "
              "%" PRId64 ".%09" PRId32,
              (unsigned long) AVS_LIST_SIZE(sched->entries), delay.seconds,
              delay.nanoseconds);
    return tasks_executed;
}

void _anjay_sched_delete(anjay_sched_t **sched_ptr) {
    if (!sched_ptr || !*sched_ptr) {
        return;
    }

    (*sched_ptr)->shut_down = true;

    /* execute any remaining tasks */
    _anjay_sched_run(*sched_ptr);
    AVS_LIST_CLEAR(&(*sched_ptr)->entries) {
        if ((*sched_ptr)->entries->handle_ptr) {
            *(*sched_ptr)->entries->handle_ptr = NULL;
        }
    }
    avs_free(*sched_ptr);
    *sched_ptr = NULL;
}

static anjay_sched_handle_t insert_entry(anjay_sched_t *sched,
                                         AVS_LIST(anjay_sched_entry_t) entry) {
    anjay_sched_entry_t **entry_ptr = NULL;

    if (!sched || sched->shut_down) {
        sched_log(DEBUG, "scheduler already shut down");
        return NULL;
    }

    AVS_LIST_FOREACH_PTR(entry_ptr, &sched->entries) {
        if (avs_time_monotonic_before(entry->when, (*entry_ptr)->when)) {
            break;
        }
    }

    AVS_LIST_INSERT(entry_ptr, entry);
    sched_log(TRACE, "%p inserted; %lu tasks scheduled", (void *) entry,
              (unsigned long) AVS_LIST_SIZE(sched->entries));
    return entry;
}

static AVS_LIST(anjay_sched_entry_t) create_entry(anjay_sched_clb_t clb,
                                                  const void *clb_data,
                                                  size_t clb_data_size) {
    AVS_LIST(anjay_sched_entry_t) entry =
            (anjay_sched_entry_t *) AVS_LIST_NEW_BUFFER(
                    offsetof(anjay_sched_entry_t, clb_data) + clb_data_size);

    if (!entry) {
        sched_log(ERROR, "Could not allocate scheduler task");
        return NULL;
    }

    entry->clb = clb;
    if (clb_data_size) {
        memcpy(&entry->clb_data, clb_data, clb_data_size);
    }

    return entry;
}

static anjay_sched_handle_t sched_delayed(anjay_sched_t *sched,
                                          avs_time_duration_t delay,
                                          AVS_LIST(anjay_sched_entry_t) entry) {
    avs_time_monotonic_t sched_time = avs_time_monotonic_now();
    sched_log(TRACE, "current time %" PRId64 ".%09" PRId32,
              sched_time.since_monotonic_epoch.seconds,
              sched_time.since_monotonic_epoch.nanoseconds);

    if (avs_time_duration_valid(delay)) {
        sched_time = avs_time_monotonic_add(sched_time, delay);
    }
    sched_log(TRACE,
              "job scheduled at %" PRId64 ".%09" PRId32 " (+%" PRId64
              ".%09" PRId32 ")",
              sched_time.since_monotonic_epoch.seconds,
              sched_time.since_monotonic_epoch.nanoseconds, delay.seconds,
              delay.nanoseconds);

    entry->when = sched_time;
    return insert_entry(sched, entry);
}

int _anjay_sched(anjay_sched_t *sched,
                 anjay_sched_handle_t *out_handle,
                 avs_time_duration_t delay,
                 anjay_sched_clb_t clb,
                 const void *clb_data,
                 size_t clb_data_size) {
    if (clb == NULL) {
        sched_log(ERROR, "Attempted to schedule a null callback pointer");
        return -1;
    }
    AVS_ASSERT((!out_handle || *out_handle == NULL),
               "Dangerous non-initialized out_handle");
    AVS_LIST(anjay_sched_entry_t) entry =
            create_entry(clb, clb_data, clb_data_size);
    if (!entry) {
        sched_log(ERROR, "cannot schedule task: out of memory");
        return -1;
    }
    entry->handle_ptr = out_handle;
    anjay_sched_handle_t task = sched_delayed(sched, delay, entry);
    if (!task) {
        AVS_LIST_DELETE(&entry);
        return -1;
    }
    if (out_handle) {
        *out_handle = task;
    }
    return 0;
}

static anjay_sched_entry_t **find_task_entry_ptr(anjay_sched_t *sched,
                                                 anjay_sched_handle_t *handle) {
    // IAR compiler does not support typeof, so AVS_LIST_FIND_PTR
    // returns void**, which is not implicitly-convertible
    return (AVS_LIST(anjay_sched_entry_t) *) AVS_LIST_FIND_PTR(
            &sched->entries, *((anjay_sched_entry_t **) handle));
}

int _anjay_sched_del(anjay_sched_t *sched, anjay_sched_handle_t *handle) {
    if (!sched || !handle || !*handle) {
        return -1;
    }
    sched_log(TRACE, "canceling task %p", *handle);
    int result = 0;
    anjay_sched_entry_t **task_ptr = find_task_entry_ptr(sched, handle);
    if (!task_ptr) {
        sched_log(ERROR, "cannot delete task %p - not found", *handle);
        AVS_UNREACHABLE("Dangling handle detected");
        result = -1;
    } else if (handle != (*task_ptr)->handle_ptr) {
        AVS_UNREACHABLE("Removing task via non-original handle");
        result = -1;
    } else {
        if ((*task_ptr)->handle_ptr) {
            *(*task_ptr)->handle_ptr = NULL;
        }
        AVS_LIST_DELETE(task_ptr);
    }
    return result;
}

int _anjay_sched_time_to_next(anjay_sched_t *sched,
                              avs_time_duration_t *delay) {
    anjay_sched_entry_t *elem;
    avs_time_monotonic_t now = avs_time_monotonic_now();

    AVS_LIST_FOREACH(elem, sched->entries) {
        if (delay) {
            *delay = avs_time_monotonic_diff(elem->when, now);
            if (avs_time_duration_less(*delay, AVS_TIME_DURATION_ZERO)) {
                *delay = AVS_TIME_DURATION_ZERO;
            }
        }
        return 0;
    }

    return -1;
}

#ifdef ANJAY_TEST
#    include "test/sched.c"
#endif // ANJAY_TEST
