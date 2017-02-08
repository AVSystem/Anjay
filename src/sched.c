/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <config.h>

#include <inttypes.h>
#include <stdbool.h>
#include <time.h>

#include <avsystem/commons/list.h>

#include <anjay/anjay.h>

#include <anjay_modules/time.h>

#include "utils.h"
#include "sched.h"

#define sched_log(...) _anjay_log(anjay_sched, __VA_ARGS__)

VISIBILITY_SOURCE_BEGIN

typedef enum {
    SCHED_TASK_ONESHOT,
    SCHED_TASK_RETRYABLE
} anjay_sched_task_type_t;

typedef struct {
    anjay_sched_task_type_t type;

    anjay_sched_handle_t *handle_ptr;
    struct timespec when;
    anjay_sched_clb_t clb;
    void *clb_data;
} anjay_sched_entry_t;

typedef struct {
    anjay_sched_entry_t entry;

    anjay_sched_retryable_backoff_t backoff;
} anjay_sched_retryable_entry_t;

static anjay_sched_retryable_entry_t *
get_retryable_entry(anjay_sched_entry_t *entry) {
    assert(entry->type == SCHED_TASK_RETRYABLE);
    return (anjay_sched_retryable_entry_t*)entry;
}

struct anjay_sched_struct {
    anjay_t *anjay;
    AVS_LIST(anjay_sched_entry_t) entries;
    bool shut_down;
};

anjay_sched_t *_anjay_sched_new(anjay_t *anjay) {
    anjay_sched_t *sched = (anjay_sched_t *) calloc(1, sizeof(anjay_sched_t));
    sched->anjay = anjay;
    return sched;
}

static anjay_sched_entry_t *fetch_task(anjay_sched_t *sched,
                                       const struct timespec *now) {
    if (sched->entries && !_anjay_time_before(now, &sched->entries->when)) {
        return AVS_LIST_DETACH(&sched->entries);
    } else {
        return NULL;
    }
}

static void update_backoff(anjay_sched_retryable_backoff_t *cfg) {
    _anjay_time_add(&cfg->delay, &cfg->delay);

    if (_anjay_time_before(&cfg->max_delay, &cfg->delay)) {
        cfg->delay = cfg->max_delay;
    }
}

static anjay_sched_handle_t
sched_delayed(anjay_sched_t *sched,
              struct timespec delay,
              AVS_LIST(anjay_sched_entry_t) entry);

static void execute_task(anjay_sched_t *sched,
                         AVS_LIST(anjay_sched_entry_t) entry) {
    /* make sure the task is detached */
    assert(AVS_LIST_NEXT(entry) == NULL);

    sched_log(TRACE, "executing task %p", (void*)entry);

    anjay_sched_handle_t handle = NULL;
    if (entry->handle_ptr) {
        handle = *entry->handle_ptr;
        *entry->handle_ptr = NULL;
    }
    int clb_result = entry->clb(sched->anjay, entry->clb_data);
    if (clb_result) {
        sched_log(DEBUG, "non-zero (%d) job exit status (clb=%p)",
                  clb_result, (void *) (intptr_t) entry->clb);
    }

    switch (entry->type) {
    case SCHED_TASK_ONESHOT:
        AVS_LIST_DELETE(&entry);
        return;

    case SCHED_TASK_RETRYABLE: {
            anjay_sched_retryable_backoff_t *backoff =
                    &get_retryable_entry(entry)->backoff;

            if (clb_result == 0
                    || !sched_delayed(sched, backoff->delay, entry)) {
                sched_log(TRACE, "retryable job %p cancel (result = %d)",
                          (void*)entry, clb_result);
                AVS_LIST_DELETE(&entry);
            } else {
                if (entry->handle_ptr) {
                    assert(*entry->handle_ptr == NULL
                           && "handle must not be modified if the job fails");
                    *entry->handle_ptr = handle;
                }
                update_backoff(backoff);
                sched_log(TRACE, "retryable job %p backoff = %d.%09u (result = "
                          "%d)", (void*)entry, (int)backoff->delay.tv_sec,
                          (unsigned)backoff->delay.tv_nsec, clb_result);
            }
        }
        return;
    }
}

ssize_t _anjay_sched_run(anjay_sched_t *sched) {
    int running = 1;
    ssize_t tasks_executed = 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    while (running) {
        anjay_sched_entry_t *task = fetch_task(sched, &now);
        if (!task) {
            running = 0;
        } else {
            execute_task(sched, task);
            ++tasks_executed;
        }
    }

    struct timespec delay = ANJAY_TIME_ZERO;
    _anjay_sched_time_to_next(sched, &delay);
    sched_log(TRACE, "%lu scheduled tasks remain; next after %ld.%09ld",
              (unsigned long)AVS_LIST_SIZE(sched->entries),
              delay.tv_sec, delay.tv_nsec);
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
    free(*sched_ptr);
    *sched_ptr = NULL;
}

static anjay_sched_handle_t
insert_entry(anjay_sched_t *sched,
             AVS_LIST(anjay_sched_entry_t) entry) {
    anjay_sched_entry_t **entry_ptr = NULL;

    if (!sched || sched->shut_down) {
        sched_log(DEBUG, "scheduler already shut down");
        return NULL;
    }

    AVS_LIST_FOREACH_PTR(entry_ptr, &sched->entries) {
        if (_anjay_time_before(&entry->when, &(*entry_ptr)->when)) {
            break;
        }
    }

    AVS_LIST_INSERT(entry_ptr, entry);
    sched_log(TRACE, "%p inserted; %lu tasks scheduled",
              (void*)entry, (unsigned long)AVS_LIST_SIZE(sched->entries));
    return entry;
}

static AVS_LIST(anjay_sched_entry_t)
create_entry(anjay_sched_task_type_t type,
             anjay_sched_clb_t clb,
             void *clb_data,
             const anjay_sched_retryable_backoff_t *backoff) {
    if (clb == NULL) {
        sched_log(ERROR, "Attempted to schedule a null callback pointer");
        return NULL;
    }

    AVS_LIST(anjay_sched_entry_t) entry =
        (type == SCHED_TASK_ONESHOT
            ? AVS_LIST_NEW_ELEMENT(anjay_sched_entry_t)
            : (anjay_sched_entry_t*)
              AVS_LIST_NEW_ELEMENT(anjay_sched_retryable_entry_t));

    if (!entry) {
        sched_log(ERROR, "Could not allocate scheduler task");
        return NULL;
    }

    entry->when = ANJAY_TIME_ZERO;
    entry->type = type;
    entry->clb = clb;
    entry->clb_data = clb_data;

    if (backoff) {
        get_retryable_entry(entry)->backoff = *backoff;
    }

    return entry;
}

static anjay_sched_handle_t
sched_delayed(anjay_sched_t *sched,
              struct timespec delay,
              AVS_LIST(anjay_sched_entry_t) entry) {
    struct timespec sched_time;

    clock_gettime(CLOCK_MONOTONIC, &sched_time);
    sched_log(TRACE, "current time %" PRId64 ".%09ld",
              (int64_t) sched_time.tv_sec, sched_time.tv_nsec);

    if (_anjay_time_is_valid(&delay)) {
        _anjay_time_add(&sched_time, &delay);
    }
    sched_log(TRACE,
             "job scheduled at %" PRId64 ".%09ld (+%" PRId64 ".%09ld); type %d",
             (int64_t) sched_time.tv_sec, sched_time.tv_nsec,
             (int64_t)delay.tv_sec, delay.tv_nsec, (int)entry->type);

    entry->when = sched_time;
    return insert_entry(sched, entry);
}

static anjay_sched_entry_t **find_task_entry_ptr(anjay_sched_t *sched,
                                                 anjay_sched_handle_t *handle) {
    return AVS_LIST_FIND_PTR(&sched->entries,
                             *((anjay_sched_entry_t **) handle));
}

static int schedule(anjay_sched_t *sched,
                    anjay_sched_handle_t *out_handle,
                    anjay_sched_retryable_backoff_t *backoff_config,
                    struct timespec delay,
                    anjay_sched_clb_t clb,
                    void *clb_data) {
    assert((!out_handle || *out_handle == NULL)
               && "Dangerous non-initialized out_handle");
    AVS_LIST(anjay_sched_entry_t) entry
            = create_entry(backoff_config ? SCHED_TASK_RETRYABLE
                                          : SCHED_TASK_ONESHOT,
                           clb, clb_data, backoff_config);
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

int _anjay_sched(anjay_sched_t *sched,
                 anjay_sched_handle_t *out_handle,
                 struct timespec delay,
                 anjay_sched_clb_t clb,
                 void *clb_data) {
    return schedule(sched, out_handle, NULL, delay, clb, clb_data);
}

int _anjay_sched_retryable(anjay_sched_t *sched,
                           anjay_sched_handle_t *out_handle,
                           struct timespec delay,
                           anjay_sched_retryable_backoff_t config,
                           anjay_sched_clb_t clb,
                           void *clb_data) {
    return schedule(sched, out_handle, &config, delay, clb, clb_data);
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
        assert(0 && "Dangling handle detected");
        result = -1;
    } else if (handle != (*task_ptr)->handle_ptr) {
        assert(0 && "Removing task via non-original handle");
        result = -1;
    } else {
        if ((*task_ptr)->handle_ptr) {
            *(*task_ptr)->handle_ptr = NULL;
        }
        AVS_LIST_DELETE(task_ptr);
    }
    return result;
}

int _anjay_sched_time_to_next(anjay_sched_t *sched, struct timespec *delay) {
    struct timespec now;
    anjay_sched_entry_t *elem;

    clock_gettime(CLOCK_MONOTONIC, &now);

    AVS_LIST_FOREACH(elem, sched->entries) {
        if (delay) {
            _anjay_time_diff(delay, &elem->when, &now);
            if (delay->tv_sec < 0) {
                delay->tv_sec = 0;
                delay->tv_nsec = 0;
            }
        }
        return 0;
    }

    return -1;
}

#ifdef ANJAY_TEST
#include "test/sched.c"
#endif // ANJAY_TEST
