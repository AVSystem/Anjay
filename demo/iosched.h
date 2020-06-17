/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#ifndef DEMO_SCHEDULER_H
#define DEMO_SCHEDULER_H

extern const short DEMO_POLLIN;
extern const short DEMO_POLLHUP;

/** Frees any resources associated with a handler @p arg. */
typedef void iosched_free_arg_t(void *arg);

typedef void iosched_handler_t(void *arg);
typedef void iosched_poll_handler_t(short revents, void *arg);

struct iosched_entry;
typedef struct iosched_entry iosched_entry_t;

struct iosched_struct;
typedef struct iosched_struct iosched_t;

iosched_t *iosched_create(void);

/**
 * Cancels all pending jobs and releases their arg pointers.
 */
void iosched_release(iosched_t *sched);

/**
 * Schedules a persistent job to be executed each time any of @p events happen
 * on given @p system_fd_ptr.
 *
 * @p free_arg may be NULL if @p arg does not need to be released.
 */
const iosched_entry_t *iosched_poll_entry_new(iosched_t *sched,
                                              const void *system_fd_ptr,
                                              short events,
                                              iosched_poll_handler_t *handler,
                                              void *arg,
                                              iosched_free_arg_t *free_arg);

/**
 * Schedules a job to be executed during the next @ref iosched_run call.
 * Returned pointer is invalidated after the @ref iosched_run call, but can be
 * used to cancel the job before that.
 *
 * @p free_arg may be NULL if @p arg does not need to be released.
 */
const iosched_entry_t *iosched_instant_entry_new(iosched_t *sched,
                                                 iosched_handler_t *handler,
                                                 void *arg,
                                                 iosched_free_arg_t *free_arg);

/**
 * Cancels a job represented by the @p entry and releases its arg using the
 * free_arg handler. If called with an invalid @p entry, does nothing.
 */
void iosched_entry_remove(iosched_t *sched, const iosched_entry_t *entry);

int iosched_run(iosched_t *sched, int timeout_ms);

#endif /* DEMO_SCHEDULER_H */
