/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef DEMO_H
#define DEMO_H

#include <stdatomic.h>

#include <anjay/access_control.h>
#include <anjay/anjay.h>
#include <anjay/anjay_config.h>

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
#    ifdef ANJAY_WITH_ATTR_STORAGE
    const char *attr_storage_file;
#    endif // ANJAY_WITH_ATTR_STORAGE
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
void demo_advance_time(avs_time_duration_t duration);

#endif
