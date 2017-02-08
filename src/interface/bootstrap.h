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

#ifndef ANJAY_INTERFACE_BOOTSTRAP_H
#define ANJAY_INTERFACE_BOOTSTRAP_H

#include <anjay/anjay.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_outbuf.h>

#include "../dm.h"
#include "../sched.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_BOOTSTRAP

typedef struct {
    bool in_progress;
    bool client_initiated_bootstrap_scheduled;
    anjay_sched_handle_t client_initiated_bootstrap_handle;
    anjay_notify_queue_t notification_queue;
} anjay_bootstrap_t;

int _anjay_bootstrap_finish(anjay_t *anjay);

int _anjay_bootstrap_perform_action(anjay_t *anjay,
                                    avs_stream_abstract_t *stream,
                                    const anjay_request_details_t *details);

int _anjay_bootstrap_account_prepare(anjay_t *anjay);

int _anjay_bootstrap_update_reconnected(anjay_t *anjay);

void _anjay_bootstrap_cleanup(anjay_t *anjay);

#else

#define _anjay_bootstrap_finish(anjay) ((void) 0)

#define _anjay_bootstrap_perform_action(...) (-1)

#define _anjay_bootstrap_account_prepare(anjay) (-1)

#define _anjay_bootstrap_update_reconnected(anjay) (-1)

#define _anjay_bootstrap_cleanup(anjay) ((void) 0)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INTERFACE_BOOTSTRAP_H */

