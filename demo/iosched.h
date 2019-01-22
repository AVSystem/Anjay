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

#ifndef DEMO_SCHEDULER_H
#define DEMO_SCHEDULER_H

#ifdef _WIN32

#    if defined(_WINDOWS_) || defined(_WIN32_WINNT)
#        error "iosched.h needs to be included before windows.h or _mingw.h"
#    endif

#    define WIN32_LEAN_AND_MEAN
#    define _WIN32_WINNT \
        0x600 // minimum requirement: Windows NT 6.0 a.k.a. Vista
#    include <winsock2.h>

#    ifdef ERROR
// Windows headers are REALLY weird. winsock2.h includes windows.h, which
// includes wingdi.h, even with WIN32_LEAN_AND_MEAN. And wingdi.h defines
// a macro called ERROR, which conflicts with avs_log() usage.
#        undef ERROR
#    endif
#    define poll WSAPoll
typedef UINT nfds_t;
typedef SOCKET demo_fd_t;

#else // _WIN32

#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <poll.h>

typedef int demo_fd_t;

#endif // _WIN32

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
 * on given @p fd.
 *
 * @p free_arg may be NULL if @p arg does not need to be released.
 */
const iosched_entry_t *iosched_poll_entry_new(iosched_t *sched,
                                              demo_fd_t fd,
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
