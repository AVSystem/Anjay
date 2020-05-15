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

#ifndef ANJAY_OBSERVE_INTERNAL_H
#define ANJAY_OBSERVE_INTERNAL_H

#include "anjay_observe_core.h"

#include <avsystem/coap/code.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

struct anjay_observation_struct {
    const avs_coap_token_t token;

    const anjay_request_action_t action;

    avs_sched_handle_t notify_task;
    avs_time_real_t last_confirmable;

    // last_sent has ALWAYS EXACTLY one element,
    // but is stored as a list to allow easy moving from unsent
    AVS_LIST(anjay_observation_value_t) last_sent;

    // pointer to some element of the
    // anjay_observe_connection_entry_t::unsent list
    // may or may not be the same as
    // anjay_observe_connection_entry_t::unsent_last
    // (depending on whether the last unsent value in the server refers
    // to this resource+format or not)
    AVS_LIST(anjay_observation_value_t) last_unsent;

    const size_t paths_count;
    const anjay_uri_path_t paths[];
};

typedef struct {
    const anjay_uri_path_t path;

    // List of observations (pointers to elements inside
    // anjay_observe_connection_entry_t::observations) that include "path"
    AVS_LIST(AVS_RBTREE_ELEM(anjay_observation_t)) refs;
} anjay_observe_path_entry_t;

typedef struct {
    avs_stream_t *membuf_stream;
    anjay_output_ctx_t *out_ctx;
    size_t expected_offset;
    avs_time_real_t serialization_time;
    size_t curr_value_idx;
    const anjay_batch_data_output_state_t *output_state;
} anjay_observation_serialization_state_t;

struct anjay_observe_connection_entry_struct {
    const anjay_connection_ref_t conn_ref;

    AVS_RBTREE(anjay_observation_t) observations;
    AVS_RBTREE(anjay_observe_path_entry_t) observed_paths;
    avs_sched_handle_t flush_task;
    avs_coap_exchange_id_t notify_exchange_id;
    anjay_observation_serialization_state_t serialization_state;

    AVS_LIST(anjay_observation_value_t) unsent;
    // pointer to the last element of unsent
    AVS_LIST(anjay_observation_value_t) unsent_last;
};

static inline bool
_anjay_observe_is_error_details(const anjay_msg_details_t *details) {
    return avs_coap_code_get_class(details->msg_code) >= 4;
}

static inline const anjay_observation_t *
_anjay_observation_query(const avs_coap_token_t *token) {
    return AVS_CONTAINER_OF(token, anjay_observation_t, token);
}

void _anjay_observe_cleanup_connection(anjay_observe_connection_entry_t *conn);

int _anjay_observe_token_cmp(const avs_coap_token_t *left,
                             const avs_coap_token_t *right);
int _anjay_observation_cmp(const void *left, const void *right);
int _anjay_observe_path_entry_cmp(const void *left, const void *right);

int _anjay_observe_add_to_observed_paths(
        anjay_observe_connection_entry_t *conn,
        AVS_RBTREE_ELEM(anjay_observation_t) observation);

int _anjay_observe_schedule_pmax_trigger(
        anjay_observe_connection_entry_t *conn_state,
        anjay_observation_t *entry);

void _anjay_observe_cancel_handler(avs_coap_observe_id_t id, void *ref_ptr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_OBSERVE_INTERNAL_H */
