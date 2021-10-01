/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifndef DEMO_H
#define DEMO_H

#include <stdatomic.h>

#include <anjay/access_control.h>
#include <anjay/anjay.h>
#include <anjay/anjay_config.h>
#include <anjay/attr_storage.h>

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_stream_file.h>
#include <avsystem/commons/avs_time.h>

#ifdef ANJAY_WITH_MODULE_FW_UPDATE
#    include "firmware_update.h"
#endif // ANJAY_WITH_MODULE_FW_UPDATE
#include "objects.h"

typedef struct {
    char data[1]; // actually a VLA, but struct cannot be empty
} anjay_demo_string_t;

typedef int anjay_demo_object_get_instances_t(const anjay_dm_object_def_t **,
                                              AVS_LIST(anjay_iid_t) *);
typedef void anjay_demo_object_deleter_t(const anjay_dm_object_def_t **);
typedef void anjay_demo_object_notify_t(anjay_t *,
                                        const anjay_dm_object_def_t **);

typedef struct {
    const anjay_dm_object_def_t **obj_ptr;
    anjay_demo_object_get_instances_t *get_instances_func;
    anjay_demo_object_notify_t *time_dependent_notify_func;
    anjay_demo_object_deleter_t *release_func;
} anjay_demo_object_t;

typedef void anjay_update_handler_t(anjay_t *anjay);

struct anjay_demo_struct {
    anjay_t *anjay;

    AVS_LIST(anjay_demo_string_t) allocated_strings;
    server_connection_args_t *connection_args;
#ifdef AVS_COMMONS_STREAM_WITH_FILE
#    ifdef ANJAY_WITH_MODULE_ATTR_STORAGE
    const char *attr_storage_file;
#    endif // ANJAY_WITH_MODULE_ATTR_STORAGE
#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
    const char *dm_persistence_file;
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
#endif     // AVS_COMMONS_STREAM_WITH_FILE

    avs_sched_handle_t notify_time_dependent_job;
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    fw_update_logic_t fw_update;
#endif // ANJAY_WITH_MODULE_FW_UPDATE

    AVS_LIST(anjay_demo_object_t) objects;

    AVS_LIST(anjay_update_handler_t *) installed_objects_update_handlers;

    // for testing purposes only: causes a Registration Update to be scheduled
    // immediately before calling anjay_delete
    bool schedule_update_on_exit;
};

const anjay_dm_object_def_t **demo_find_object(anjay_demo_t *demo,
                                               anjay_oid_t oid);

void demo_reload_servers(anjay_demo_t *demo);

#endif
