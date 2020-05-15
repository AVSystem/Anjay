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

#ifndef ANJAY_INCLUDE_ANJAY_NOTIFY_IO_H
#define ANJAY_INCLUDE_ANJAY_NOTIFY_IO_H

#include <stdbool.h>

#include <anjay/core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    bool instance_set_changed;
    // NOTE: known_added_iids list may not be exhaustive
    AVS_LIST(anjay_iid_t) known_added_iids;
} anjay_notify_queue_instance_entry_t;

typedef struct {
    anjay_iid_t iid;
    anjay_rid_t rid;
} anjay_notify_queue_resource_entry_t;

typedef struct {
    anjay_oid_t oid;
    anjay_notify_queue_instance_entry_t instance_set_changes;
    AVS_LIST(anjay_notify_queue_resource_entry_t) resources_changed;
} anjay_notify_queue_object_entry_t;

typedef AVS_LIST(anjay_notify_queue_object_entry_t) anjay_notify_queue_t;

/**
 * Performs all the actions necessary due to all the changes in the data model
 * specified by the <c>queue</c>.
 *
 * Note that sending Observe notifications and updating the Access Control
 * Object require knowing which server (if any) performed the changes.
 * @ref _anjay_dm_current_ssid will be called to determine it.
 */
int _anjay_notify_perform(anjay_t *anjay, anjay_notify_queue_t queue);

/**
 * Works like @ref _anjay_notify_perform but doesn't call
 * server_modified_notify().
 */
int _anjay_notify_perform_without_servers(anjay_t *anjay,
                                          anjay_notify_queue_t queue);

/**
 * Calls @ref _anjay_notify_perform and @ref _anjay_notify_clear_queue
 * afterwards (regardless of success or failure).
 */
int _anjay_notify_flush(anjay_t *anjay, anjay_notify_queue_t *queue_ptr);

int _anjay_notify_queue_instance_created(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid);

int _anjay_notify_queue_instance_removed(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid);

int _anjay_notify_queue_instance_set_unknown_change(
        anjay_notify_queue_t *out_queue, anjay_oid_t oid);

/**
 * Adds a notification about the change of value of the data model resource
 * specified by <c>oid</c>, <c>iid</c> and <c>rid</c>.
 */
int _anjay_notify_queue_resource_change(anjay_notify_queue_t *out_queue,
                                        anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid);

void _anjay_notify_clear_queue(anjay_notify_queue_t *out_queue);

int _anjay_notify_instance_created(anjay_t *anjay,
                                   anjay_oid_t oid,
                                   anjay_iid_t iid);

typedef int
anjay_notify_callback_t(anjay_t *anjay, anjay_notify_queue_t queue, void *data);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_NOTIFY_IO_H */
