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

#ifndef ANJAY_OBSERVE_INTERNAL_H
#define ANJAY_OBSERVE_INTERNAL_H

#include "observe_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

struct anjay_observe_entry_struct {
    const anjay_observe_key_t key;
    anjay_sched_handle_t notify_task;
    avs_time_real_t last_confirmable;

    // last_sent has ALWAYS EXACTLY one element,
    // but is stored as a list to allow easy moving from unsent
    AVS_LIST(anjay_observe_resource_value_t) last_sent;

    // pointer to some element of the
    // anjay_observe_connection_entry_t::unsent list
    // may or may not be the same as
    // anjay_observe_connection_entry_t::unsent_last
    // (depending on whether the last unsent value in the server refers
    // to this resource+format or not)
    AVS_LIST(anjay_observe_resource_value_t) last_unsent;
};

struct anjay_observe_connection_entry_struct {
    anjay_connection_key_t key;
    AVS_RBTREE(anjay_observe_entry_t) entries;
    anjay_sched_handle_t flush_task;

    AVS_LIST(anjay_observe_resource_value_t) unsent;
    // pointer to the last element of unsent
    AVS_LIST(anjay_observe_resource_value_t) unsent_last;
};

static inline const anjay_observe_entry_t *
_anjay_observe_entry_query(const anjay_observe_key_t *key) {
    return AVS_CONTAINER_OF(key, anjay_observe_entry_t, key);
}

void _anjay_observe_cleanup_connection(anjay_sched_t *sched,
                                       anjay_observe_connection_entry_t *conn);

int _anjay_observe_key_cmp(const anjay_observe_key_t *left,
                           const anjay_observe_key_t *right);
int _anjay_observe_entry_cmp(const void *left, const void *right);

int _anjay_observe_schedule_pmax_trigger(anjay_t *anjay,
                                         anjay_observe_entry_t *entry);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_OBSERVE_INTERNAL_H */
