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

#ifndef ANJAY_DM_H
#define ANJAY_DM_H

#include <limits.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/list.h>

#include <anjay_modules/dm.h>

#include "coap/msg.h"
#include "coap/stream.h"
#include "observe.h"
#include "dm/attributes.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

struct anjay_dm {
    AVS_LIST(const anjay_dm_object_def_t *const *) objects;
};

void _anjay_dm_cleanup(anjay_dm_t *dm);

const char *_anjay_res_path_string__(char *buffer,
                                     size_t buffer_size,
                                     const anjay_resource_path_t *path);

#define ANJAY_RES_PATH_STRING(path) \
        (_anjay_res_path_string__(&(char[32]){0}[0], 32, (path)))

typedef struct {
    bool has_min_period;
    bool has_max_period;
    bool has_greater_than;
    bool has_less_than;
    bool has_step;
    anjay_dm_attributes_t values;
} anjay_request_attributes_t;

typedef struct anjay_request_details {
    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP
    anjay_connection_type_t conn_type;
    anjay_coap_msg_type_t msg_type;
    uint8_t request_code;
    anjay_coap_msg_identity_t request_identity;

    bool is_bs_uri;

    bool has_oid;
    anjay_oid_t oid;

    bool has_iid;
    anjay_iid_t iid;

    bool has_rid;
    anjay_rid_t rid;

    anjay_request_action_t action;
    uint16_t content_format;
    uint16_t requested_format;
    anjay_coap_observe_t observe;

    anjay_request_attributes_t attributes;
} anjay_request_details_t;

typedef struct {
    anjay_ssid_t ssid;
    uint16_t request_msg_id;

    anjay_oid_t oid;

    bool has_iid;
    anjay_iid_t iid;

    bool has_rid;
    anjay_rid_t rid;

    uint16_t requested_format;
    bool observe_serial;
} anjay_dm_read_args_t;

typedef struct {
    anjay_ssid_t ssid;

    bool has_oid;
    anjay_oid_t oid;

    bool has_iid;
    anjay_iid_t iid;

    bool has_rid;
    anjay_rid_t rid;
} anjay_dm_write_args_t;

#define DETAILS_TO_DM_WRITE_ARGS(Details) \
        (const anjay_dm_write_args_t) { \
            .ssid = (Details)->ssid, \
            .has_oid = (Details)->has_oid, \
            .oid = (Details)->oid, \
            .has_iid = (Details)->has_iid, \
            .iid = (Details)->iid, \
            .has_rid = (Details)->has_rid, \
            .rid = (Details)->rid \
        }

#define DETAILS_TO_DM_READ_ARGS(Details) \
        (const anjay_dm_read_args_t) { \
            .ssid = (Details)->ssid, \
            .oid = (Details)->oid, \
            .has_iid = (Details)->has_iid, \
            .iid = (Details)->iid, \
            .has_rid = (Details)->has_rid, \
            .rid = (Details)->rid, \
            .requested_format = (Details)->requested_format, \
            .observe_serial = \
                    ((Details)->observe == ANJAY_COAP_OBSERVE_REGISTER) \
        }

#define DETAILS_TO_ACTION_INFO(Details) \
        (const anjay_action_info_t) { \
            .oid = (Details)->oid, \
            .iid = (Details)->iid, \
            .ssid = (Details)->ssid, \
            .action = (Details)->action \
        }

int _anjay_dm_transaction_validate(anjay_t *anjay);
int _anjay_dm_transaction_finish_without_validation(anjay_t *anjay, int result);

static inline int _anjay_dm_transaction_rollback(anjay_t *anjay) {
    int result = _anjay_dm_transaction_finish(anjay, INT_MIN);
    return (result == INT_MIN) ? 0 : result;
}

#ifdef WITH_OBSERVE
ssize_t _anjay_dm_read_for_observe(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_read_args_t *details,
                                   anjay_msg_details_t *out_details,
                                   double *out_numeric,
                                   char *buffer,
                                   size_t size);
#endif // WITH_OBSERVE

int _anjay_dm_perform_action(anjay_t *anjay,
                             avs_stream_abstract_t *stream,
                             const anjay_request_details_t *details);

anjay_input_ctx_t *
_anjay_dm_read_as_input_ctx(anjay_t *anjay, const anjay_resource_path_t *path);

const char *_anjay_debug_make_obj_path__(char *buffer,
                                         size_t buffer_size,
                                         anjay_oid_t oid,
                                         bool has_iid,
                                         anjay_iid_t iid,
                                         bool has_rid,
                                         anjay_rid_t rid);

#define ANJAY_DEBUG_MAKE_PATH(details) \
    (_anjay_debug_make_obj_path__(&(char[32]){0}[0], 32, (details)->oid, \
                                (details)->has_iid, (details)->iid, \
                                (details)->has_rid, (details)->rid))

static inline int _anjay_dm_map_present_result(int result) {
    if (!result) {
        return ANJAY_ERR_NOT_FOUND;
    } else if (result > 0) {
        return 0;
    } else {
        return result;
    }
}

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_H
