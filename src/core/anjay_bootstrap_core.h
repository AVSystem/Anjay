/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_BOOTSTRAP_CORE_H
#define ANJAY_BOOTSTRAP_CORE_H

#include <anjay/core.h>

#include <anjay_modules/anjay_bootstrap.h>

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_stream_outbuf.h>

#include "anjay_dm_core.h"
#include "anjay_servers_private.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    ANJAY_BOOTSTRAP_ACTION_NONE = 0,
    ANJAY_BOOTSTRAP_ACTION_REQUEST,
} anjay_bootstrap_action_t;

#ifdef ANJAY_WITH_BOOTSTRAP

typedef struct {
    bool allow_legacy_server_initiated_bootstrap;
    bool bootstrap_trigger;
    avs_coap_exchange_id_t outgoing_request_exchange_id;
    bool in_progress;
    anjay_conn_session_token_t bootstrap_session_token;
    anjay_notify_queue_t notification_queue;
    avs_sched_handle_t purge_bootstrap_handle;
    avs_sched_handle_t client_initiated_bootstrap_handle;
    avs_sched_handle_t finish_timeout_handle;
    avs_time_monotonic_t client_initiated_bootstrap_last_attempt;
    avs_time_duration_t client_initiated_bootstrap_holdoff;
} anjay_bootstrap_t;

int _anjay_bootstrap_notify_regular_connection_available(
        anjay_unlocked_t *anjay);

bool _anjay_bootstrap_legacy_server_initiated_allowed(anjay_unlocked_t *anjay);

bool _anjay_bootstrap_scheduled(anjay_unlocked_t *anjay);

int _anjay_bootstrap_perform_action(anjay_connection_ref_t bootstrap_connection,
                                    const anjay_request_t *request);

int _anjay_perform_bootstrap_action_if_appropriate(
        anjay_unlocked_t *anjay,
        anjay_server_info_t *bootstrap_server,
        anjay_bootstrap_action_t action);

void _anjay_bootstrap_init(anjay_bootstrap_t *bootstrap,
                           bool allow_legacy_server_initiated_bootstrap);

void _anjay_bootstrap_cleanup(anjay_unlocked_t *anjay);

#else

#    define _anjay_bootstrap_notify_regular_connection_available(anjay) \
        ((void) 0)

#    define _anjay_bootstrap_legacy_server_initiated_allowed(...) (false)

#    define _anjay_bootstrap_scheduled(anjay) ((void) (anjay), false)

#    define _anjay_bootstrap_perform_action(...) (-1)

#    define _anjay_perform_bootstrap_action_if_appropriate(...) (-1)

#    define _anjay_bootstrap_cleanup(anjay) ((void) 0)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_BOOTSTRAP_CORE_H */
