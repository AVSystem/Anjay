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

#ifndef ANJAY_INTERFACE_BOOTSTRAP_H
#define ANJAY_INTERFACE_BOOTSTRAP_H

#include <anjay/core.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_outbuf.h>

#include "../dm_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_BOOTSTRAP

typedef struct {
    bool allow_server_initiated_bootstrap;
    bool in_progress;
    anjay_conn_session_token_t bootstrap_session_token;
    anjay_notify_queue_t notification_queue;
    anjay_sched_handle_t purge_bootstrap_handle;
    anjay_sched_handle_t client_initiated_bootstrap_handle;
    avs_time_monotonic_t client_initiated_bootstrap_last_attempt;
    avs_time_duration_t client_initiated_bootstrap_holdoff;
} anjay_bootstrap_t;

int _anjay_bootstrap_notify_regular_connection_available(anjay_t *anjay);

bool _anjay_bootstrap_server_initiated_allowed(anjay_t *anjay);

int _anjay_bootstrap_perform_action(anjay_t *anjay,
                                    const anjay_request_t *request);

int _anjay_bootstrap_request_if_appropriate(anjay_t *anjay);

void _anjay_bootstrap_init(anjay_bootstrap_t *bootstrap,
                           bool allow_server_initiated_bootstrap);

void _anjay_bootstrap_cleanup(anjay_t *anjay);

#else

#    define _anjay_bootstrap_notify_regular_connection_available(anjay) \
        ((void) 0)

#    define _anjay_bootstrap_server_initiated_allowed(...) (false)

#    define _anjay_bootstrap_perform_action(...) (-1)

#    define _anjay_bootstrap_request_if_appropriate(anjay) (-1)

#    define _anjay_bootstrap_init(...) ((void) 0)

#    define _anjay_bootstrap_cleanup(anjay) ((void) 0)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INTERFACE_BOOTSTRAP_H */
