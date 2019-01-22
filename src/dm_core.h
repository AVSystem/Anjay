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

#ifndef ANJAY_DM_H
#define ANJAY_DM_H

#include <limits.h>

#include <avsystem/commons/coap/msg.h>
#include <avsystem/commons/list.h>
#include <avsystem/commons/stream.h>

#include <anjay_modules/dm_utils.h>

#include "coap/coap_stream.h"
#include "dm/dm_attributes.h"
#include "observe/observe_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    const anjay_dm_module_t *def;
    void *arg;
} anjay_dm_installed_module_t;

struct anjay_dm {
    AVS_LIST(const anjay_dm_object_def_t *const *) objects;
    AVS_LIST(anjay_dm_installed_module_t) modules;
};

void _anjay_dm_cleanup(anjay_t *anjay);

typedef struct {
    bool has_min_period;
    bool has_max_period;
    bool has_greater_than;
    bool has_less_than;
    bool has_step;
#ifdef WITH_CUSTOM_ATTRIBUTES
    anjay_dm_custom_request_attribute_flags_t custom;
#endif
    anjay_dm_internal_res_attrs_t values;
} anjay_request_attributes_t;

static inline bool _anjay_double_attr_equal(double left, double right) {
    return isnan(left) ? isnan(right) : (left == right);
}

static inline bool
_anjay_request_attributes_equal(const anjay_request_attributes_t *left,
                                const anjay_request_attributes_t *right) {
    return (left->has_min_period
                    ? (right->has_min_period
                       && left->values.standard.common.min_period
                                  == right->values.standard.common.min_period)
                    : !right->has_min_period)
           && (left->has_max_period
                       ? (right->has_max_period
                          && left->values.standard.common.max_period
                                     == right->values.standard.common
                                                .max_period)
                       : !right->has_max_period)
           && (left->has_greater_than
                       ? (right->has_greater_than
                          && _anjay_double_attr_equal(
                                     left->values.standard.greater_than,
                                     right->values.standard.greater_than))
                       : !right->has_greater_than)
           && (left->has_less_than
                       ? (right->has_less_than
                          && _anjay_double_attr_equal(
                                     left->values.standard.less_than,
                                     right->values.standard.less_than))
                       : !right->has_less_than)
           && (left->has_step ? (right->has_step
                                 && _anjay_double_attr_equal(
                                            left->values.standard.step,
                                            right->values.standard.step))
                              : !right->has_step)
#ifdef WITH_CON_ATTR
           && (left->custom.has_con
                       ? (right->custom.has_con
                          && left->values.custom.data.con
                                     == right->values.custom.data.con)
                       : !right->custom.has_con)
#endif // WITH_CON_ATTR
            ;
}

typedef struct {
    avs_coap_msg_type_t msg_type;
    uint8_t request_code;

    bool is_bs_uri;

    anjay_uri_path_t uri;

    anjay_request_action_t action;
    uint16_t content_format;
    uint16_t requested_format;
    anjay_coap_observe_t observe;

    anjay_request_attributes_t attributes;
} anjay_request_t;

static inline bool _anjay_request_equal(const anjay_request_t *left,
                                        const anjay_request_t *right) {
    return left->msg_type == right->msg_type
           && left->request_code == right->request_code
           && left->is_bs_uri == right->is_bs_uri
           && _anjay_uri_path_equal(&left->uri, &right->uri)
           && left->action == right->action
           && left->content_format == right->content_format
           && left->requested_format == right->requested_format
           && left->observe == right->observe
           && _anjay_request_attributes_equal(&left->attributes,
                                              &right->attributes);
}

typedef struct {
    anjay_ssid_t ssid;
    uint16_t request_msg_id;

    anjay_uri_path_t uri;

    uint16_t requested_format;
    bool observe_serial;
} anjay_dm_read_args_t;

#define REQUEST_TO_DM_READ_ARGS(Anjay, Request)                               \
    (const anjay_dm_read_args_t) {                                            \
        .ssid = _anjay_dm_current_ssid(Anjay),                                \
        .uri = (Request)->uri,                                                \
        .requested_format = (Request)->requested_format,                      \
        .observe_serial = ((Request)->observe == ANJAY_COAP_OBSERVE_REGISTER) \
    }

#define REQUEST_TO_ACTION_INFO(Anjay, Request)                               \
    (const anjay_action_info_t) {                                            \
        .oid = (Request)->uri.oid,                                           \
        .iid = _anjay_uri_path_has_iid(&(Request)->uri) ? (Request)->uri.iid \
                                                        : ANJAY_IID_INVALID, \
        .ssid = _anjay_dm_current_ssid(Anjay),                               \
        .action = (Request)->action                                          \
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
                             const avs_coap_msg_identity_t *request_identity,
                             const anjay_request_t *request);

anjay_input_ctx_t *_anjay_dm_read_as_input_ctx(anjay_t *anjay,
                                               const anjay_uri_path_t *path);

const char *_anjay_debug_make_path__(char *buffer,
                                     size_t buffer_size,
                                     const anjay_uri_path_t *uri);

#define ANJAY_DEBUG_MAKE_PATH(path) \
    (_anjay_debug_make_path__(&(char[32]){ 0 }[0], 32, (path)))

static inline int _anjay_dm_map_present_result(int result) {
    if (!result) {
        return ANJAY_ERR_NOT_FOUND;
    } else if (result > 0) {
        return 0;
    } else {
        return result;
    }
}

int _anjay_dm_check_if_tlv_rid_matches_uri_rid(anjay_input_ctx_t *in_ctx,
                                               anjay_rid_t uri_rid);

AVS_LIST(anjay_dm_installed_module_t) *
_anjay_dm_module_find_ptr(anjay_t *anjay, const anjay_dm_module_t *module);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_H
