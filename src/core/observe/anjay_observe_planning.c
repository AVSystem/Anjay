/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "../anjay_servers_utils.h"

#include "anjay_observe_internal.h"

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_WITH_OBSERVE
typedef int foreach_relevant_connection_cb_t(
        AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr, void *data);

typedef struct {
    unsigned conn_type_mask;
    anjay_transport_set_t transport_set;
    foreach_relevant_connection_cb_t *cb;
    void *cb_data;
} foreach_relevant_connection_helper_arg_t;

static int foreach_relevant_connection_helper(anjay_unlocked_t *anjay,
                                              anjay_server_info_t *server,
                                              void *arg_) {
    (void) anjay;
    foreach_relevant_connection_helper_arg_t *arg =
            (foreach_relevant_connection_helper_arg_t *) arg_;
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        if (arg->conn_type_mask & (1U << conn_type)) {
            anjay_connection_ref_t ref = {
                .server = server,
                .conn_type = conn_type
            };
            AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr =
                    _anjay_observe_find_connection_state(ref);
            if (conn_ptr && *conn_ptr
                    && _anjay_socket_transport_included(
                               arg->transport_set,
                               _anjay_connection_transport(ref))) {
                int result = arg->cb(conn_ptr, arg->cb_data);
                if (result) {
                    return result;
                }
            }
        }
    }
    return 0;
}

static int foreach_relevant_connection(anjay_unlocked_t *anjay,
                                       anjay_ssid_t ssid,
                                       unsigned conn_type_mask,
                                       anjay_transport_set_t transport_set,
                                       foreach_relevant_connection_cb_t *cb,
                                       void *cb_data) {
    foreach_relevant_connection_helper_arg_t arg = {
        .conn_type_mask = conn_type_mask,
        .transport_set = transport_set,
        .cb = cb,
        .cb_data = cb_data
    };
    if (ssid == ANJAY_SSID_ANY) {
        return _anjay_servers_foreach_active(
                anjay, foreach_relevant_connection_helper, &arg);
    } else {
        anjay_server_info_t *server = _anjay_servers_find_active(anjay, ssid);
        if (!server) {
            anjay_log(WARNING, _("no server with SSID = ") "%u", ssid);
            return 0;
        } else {
            int result =
                    foreach_relevant_connection_helper(anjay, server, &arg);
            return result == ANJAY_FOREACH_BREAK ? 0 : result;
        }
    }
}

#else // ANJAY_WITH_OBSERVE

#    define foreach_relevant_connection(Anjay, Ssid, ConnTypeMask, \
                                        TransportSet, Cb, CbData)  \
        ((void) (Anjay), (void) (Ssid), (void) (ConnTypeMask),     \
         (void) (TransportSet))

#endif // ANJAY_WITH_OBSERVE

typedef struct {
    size_t trigger_field_offset;
    avs_time_real_t result;
} next_planned_trigger_cb_arg_t;

#ifdef ANJAY_WITH_OBSERVE
static int
next_planned_trigger_cb(AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr,
                        void *arg_) {
    next_planned_trigger_cb_arg_t *arg = (next_planned_trigger_cb_arg_t *) arg_;
    avs_time_real_t trigger_time = *AVS_APPLY_OFFSET(avs_time_real_t, *conn_ptr,
                                                     arg->trigger_field_offset);
    if (!avs_time_real_valid(arg->result)
            || avs_time_real_before(trigger_time, arg->result)) {
        arg->result = trigger_time;
    }
    return 0;
}
#endif // ANJAY_WITH_OBSERVE

static avs_time_real_t next_planned_trigger(anjay_t *anjay_locked,
                                            anjay_ssid_t ssid,
                                            unsigned conn_type_mask,
                                            anjay_transport_set_t transport_set,
                                            size_t trigger_field_offset) {
    next_planned_trigger_cb_arg_t arg = {
        .trigger_field_offset = trigger_field_offset,
        .result = AVS_TIME_REAL_INVALID
    };
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    foreach_relevant_connection(anjay, ssid, conn_type_mask, transport_set,
                                next_planned_trigger_cb, &arg);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return arg.result;
}

avs_time_real_t anjay_next_planned_notify_trigger(anjay_t *anjay,
                                                  anjay_ssid_t ssid) {
    return next_planned_trigger(
            anjay, ssid, 1 << ANJAY_CONNECTION_PRIMARY, ANJAY_TRANSPORT_SET_ALL,
            offsetof(anjay_observe_connection_entry_t, next_trigger));
}

avs_time_real_t anjay_next_planned_pmax_notify_trigger(anjay_t *anjay,
                                                       anjay_ssid_t ssid) {
    return next_planned_trigger(
            anjay, ssid, 1 << ANJAY_CONNECTION_PRIMARY, ANJAY_TRANSPORT_SET_ALL,
            offsetof(anjay_observe_connection_entry_t, next_pmax_trigger));
}

avs_time_real_t anjay_transport_next_planned_notify_trigger(
        anjay_t *anjay, anjay_transport_set_t transport_set) {
    return next_planned_trigger(
            anjay, ANJAY_SSID_ANY, (1 << ANJAY_CONNECTION_LIMIT_) - 1,
            transport_set,
            offsetof(anjay_observe_connection_entry_t, next_trigger));
}

avs_time_real_t anjay_transport_next_planned_pmax_notify_trigger(
        anjay_t *anjay, anjay_transport_set_t transport_set) {
    return next_planned_trigger(
            anjay, ANJAY_SSID_ANY, (1 << ANJAY_CONNECTION_LIMIT_) - 1,
            transport_set,
            offsetof(anjay_observe_connection_entry_t, next_pmax_trigger));
}

#ifdef ANJAY_WITH_OBSERVE
static int has_unsent_notifications_cb(
        AVS_LIST(anjay_observe_connection_entry_t) *conn_ptr,
        void *out_result) {
    if ((*conn_ptr)->unsent && !(*conn_ptr)->flush_task
            && !avs_coap_exchange_id_valid((*conn_ptr)->notify_exchange_id)) {
        *(bool *) out_result = true;
        return ANJAY_FOREACH_BREAK;
    }
    return 0;
}
#endif // ANJAY_WITH_OBSERVE

bool anjay_has_unsent_notifications(anjay_t *anjay_locked, anjay_ssid_t ssid) {
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    foreach_relevant_connection(anjay, ssid, 1 << ANJAY_CONNECTION_PRIMARY,
                                ANJAY_TRANSPORT_SET_ALL,
                                has_unsent_notifications_cb, &result);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

bool anjay_transport_has_unsent_notifications(
        anjay_t *anjay_locked, anjay_transport_set_t transport_set) {
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    foreach_relevant_connection(anjay, ANJAY_SSID_ANY,
                                (1 << ANJAY_CONNECTION_LIMIT_) - 1,
                                transport_set, has_unsent_notifications_cb,
                                &result);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}
