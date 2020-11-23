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

#ifndef ANJAY_OBSERVE_CORE_H
#define ANJAY_OBSERVE_CORE_H

#include <avsystem/commons/avs_persistence.h>
#include <avsystem/commons/avs_rbtree.h>

#include "../anjay_servers_private.h"
#include "../coap/anjay_msg_details.h"
#include "../io/anjay_batch_builder.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_OBSERVE

typedef struct anjay_observation_struct anjay_observation_t;
typedef struct anjay_observe_connection_entry_struct
        anjay_observe_connection_entry_t;

typedef enum {
    NOTIFY_QUEUE_UNLIMITED,
    NOTIFY_QUEUE_DROP_OLDEST
} notify_queue_limit_mode_t;

typedef struct {
    AVS_LIST(anjay_observe_connection_entry_t) connection_entries;
    bool confirmable_notifications;

    notify_queue_limit_mode_t notify_queue_limit_mode;
    size_t notify_queue_limit;
} anjay_observe_state_t;

typedef struct {
    anjay_observation_t *const ref;
    anjay_msg_details_t details;
    avs_coap_notify_reliability_hint_t reliability_hint;
    avs_time_real_t timestamp;

    // Array size is ref->paths_count for "normal" entry, or 0 for error entry
    // (determined based on is_error_value()). values[i] is a value
    // corresponding to ref->paths[i]. Note that each values[i] element might
    // contain multiple entries itself if ref->paths[i] is hierarchical (e.g.
    // Object Instance).
    anjay_batch_t *values[];
} anjay_observation_value_t;

void _anjay_observe_init(anjay_observe_state_t *observe,
                         bool confirmable_notifications,
                         size_t stored_notification_limit);

void _anjay_observe_cleanup(anjay_observe_state_t *observe);

void _anjay_observe_gc(anjay_t *anjay);

int _anjay_observe_handle(anjay_t *anjay, const anjay_request_t *request);

void _anjay_observe_interrupt(anjay_connection_ref_t ref);

int _anjay_observe_sched_flush(anjay_connection_ref_t ref);

int _anjay_observe_notify(anjay_t *anjay,
                          const anjay_uri_path_t *path,
                          anjay_ssid_t ssid,
                          bool invert_ssid_match);

#    ifdef ANJAY_WITH_OBSERVATION_STATUS
anjay_resource_observation_status_t _anjay_observe_status(anjay_t *anjay,
                                                          anjay_oid_t oid,
                                                          anjay_iid_t iid,
                                                          anjay_rid_t rid);
#    endif // ANJAY_WITH_OBSERVATION_STATUS

#else // ANJAY_WITH_OBSERVE

#    define _anjay_observe_init(...) ((void) 0)
#    define _anjay_observe_cleanup(...) ((void) 0)
#    define _anjay_observe_gc(...) ((void) 0)
#    define _anjay_observe_interrupt(...) ((void) 0)
#    define _anjay_observe_sched_flush(...) 0

#    ifdef ANJAY_WITH_OBSERVATION_STATUS
#        define _anjay_observe_status(...)         \
            ((anjay_resource_observation_status) { \
                .is_observed = false,              \
                .min_period = -1                   \
            })
#    endif // ANJAY_WITH_OBSERVATION_STATUS

#endif // ANJAY_WITH_OBSERVE

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_OBSERVE_CORE_H */
