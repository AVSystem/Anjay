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

#ifndef DEMO_H
#define DEMO_H
#include <anjay/access_control.h>
#include <anjay/attr_storage.h>
#include <anjay/anjay.h>

#include <avsystem/commons/list.h>
#include <avsystem/commons/stream/stream_file.h>

#include <poll.h>

#include "objects.h"

typedef struct {
    char data[1]; // actually a VLA, but struct cannot be empty
} anjay_demo_string_t;

typedef struct {
    anjay_t *anjay;
    bool running;

    AVS_LIST(anjay_demo_string_t) allocated_strings;
    server_connection_args_t *connection_args;

    iosched_t *iosched;

    anjay_attr_storage_t *attr_storage;

    const anjay_dm_object_def_t **apn_conn_profile_obj;
    const anjay_dm_object_def_t **cell_connectivity_obj;
    const anjay_dm_object_def_t **conn_monitoring_obj;
    const anjay_dm_object_def_t **conn_statistics_obj;
    const anjay_dm_object_def_t **download_diagnostics_obj;
    const anjay_dm_object_def_t **device_obj;
    const anjay_dm_object_def_t **ext_dev_info_obj;
    const anjay_dm_object_def_t **firmware_update_obj;
    const anjay_dm_object_def_t **security_obj;
    const anjay_dm_object_def_t **server_obj;
    const anjay_dm_object_def_t **location_obj;
    const anjay_dm_object_def_t **geopoints_obj;
    const anjay_dm_object_def_t **ip_ping_obj;
    const anjay_dm_object_def_t **test_obj;
    const anjay_dm_object_def_t *const *access_control_obj;
} anjay_demo_t;

void demo_reload_servers(anjay_demo_t *demo);

#endif
