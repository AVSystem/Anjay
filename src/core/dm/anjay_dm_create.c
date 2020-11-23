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

#include <anjay_init.h>

#include <avsystem/commons/avs_utils.h>

#include "anjay_dm_create.h"
#include "anjay_dm_write.h"

#include "../anjay_access_utils_private.h"
#include "../io/anjay_vtable.h"

#include <inttypes.h>

VISIBILITY_SOURCE_BEGIN

static int
setup_create_response(anjay_oid_t oid,
                      anjay_iid_t iid,
                      avs_coap_streaming_request_ctx_t *request_ctx) {
    AVS_STATIC_ASSERT(((anjay_oid_t) -1) == 65535, oid_is_u16);
    AVS_STATIC_ASSERT(((anjay_iid_t) -1) == 65535, iid_is_u16);
    char oid_str[6];
    char iid_str[6];
    int result =
            avs_simple_snprintf(oid_str, sizeof(oid_str), "%" PRIu16, oid) < 0
                    ? -1
                    : 0;
    if (!result) {
        result = avs_simple_snprintf(iid_str, sizeof(iid_str), "%" PRIu16, iid)
                                 < 0
                         ? -1
                         : 0;
    }
    if (!result) {
        anjay_msg_details_t msg_details = {
            .msg_code =
                    _anjay_dm_make_success_response_code(ANJAY_ACTION_CREATE),
            .format = AVS_COAP_FORMAT_NONE,
            .location_path = ANJAY_MAKE_STRING_LIST(oid_str, iid_str)
        };
        if (!msg_details.location_path
                || !_anjay_coap_setup_response_stream(request_ctx,
                                                      &msg_details)) {
            result = -1;
        }
        AVS_LIST_CLEAR(&msg_details.location_path);
    }
    return result;
}

static int dm_create_select_iid_clb(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj,
                                    anjay_iid_t iid,
                                    void *new_iid_ptr_) {
    (void) anjay;
    (void) obj;
    anjay_iid_t *new_iid_ptr = (anjay_iid_t *) new_iid_ptr_;
    if (iid == *new_iid_ptr) {
        ++*new_iid_ptr;
        return ANJAY_FOREACH_CONTINUE;
    } else {
        return ANJAY_FOREACH_BREAK;
    }
}

int _anjay_dm_select_free_iid(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              anjay_iid_t *new_iid_ptr) {
    *new_iid_ptr = 0;
    int result =
            _anjay_dm_foreach_instance(anjay, obj, dm_create_select_iid_clb,
                                       new_iid_ptr);
    if (!result && *new_iid_ptr == ANJAY_ID_INVALID) {
        dm_log(ERROR, _("65535 object instances already exist"));
        return ANJAY_ERR_BAD_REQUEST;
    }
    return result;
}

static int dm_create_inner(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           anjay_iid_t iid,
                           anjay_input_ctx_t *in_ctx) {
    assert(iid != ANJAY_ID_INVALID);
    int result = _anjay_dm_call_instance_create(anjay, obj, iid, NULL);
    if (result) {
        dm_log(DEBUG,
               _("Instance Create handler for object ") "%" PRIu16 _(" failed"),
               (*obj)->oid);
        return result ? result : ANJAY_ERR_INTERNAL;
    } else if ((result = _anjay_dm_write_created_instance(anjay, obj, iid,
                                                          in_ctx))) {
        dm_log(DEBUG,
               _("Writing Resources for newly created ") "/%" PRIu16 "/%" PRIu16
                       _(" failed; removing"),
               (*obj)->oid, iid);
    }
    return result;
}

static int dm_create_with_explicit_iid(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       anjay_iid_t iid,
                                       anjay_input_ctx_t *in_ctx) {
    if (iid == ANJAY_ID_INVALID) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    int result = _anjay_dm_instance_present(anjay, obj, iid);
    if (result > 0) {
        dm_log(DEBUG,
               _("Instance ") "/%" PRIu16 "/%" PRIu16 _(" already exists"),
               (*obj)->oid, iid);
        return ANJAY_ERR_BAD_REQUEST;
    } else if (result) {
        dm_log(DEBUG,
               _("Instance Present handler for ") "/%" PRIu16
                                                  "/%" PRIu16 _(" failed"),
               (*obj)->oid, iid);
        return result;
    }
    result = dm_create_inner(anjay, obj, iid, in_ctx);
    if (!result) {
        (void) ((result = _anjay_input_next_entry(in_ctx))
                || (result = _anjay_input_get_path(in_ctx, NULL, NULL)));
        if (result == ANJAY_GET_PATH_END) {
            return 0;
        } else {
            dm_log(DEBUG, _("More than one Object Instance or broken input "
                            "stream while processing Object Create"));
            return result ? result : ANJAY_ERR_BAD_REQUEST;
        }
    }
    return result;
}

int _anjay_dm_create(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj,
                     const anjay_request_t *request,
                     anjay_input_ctx_t *in_ctx) {
    dm_log(LAZY_DEBUG, _("Create ") "%s", ANJAY_DEBUG_MAKE_PATH(&request->uri));
    assert(_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_OID));

    if (!_anjay_instance_action_allowed(
                anjay, &REQUEST_TO_ACTION_INFO(anjay, request))) {
        return ANJAY_ERR_UNAUTHORIZED;
    }

    anjay_uri_path_t path = MAKE_ROOT_PATH();
    int result = _anjay_input_get_path(in_ctx, &path, NULL);
    if (!result || result == ANJAY_GET_PATH_END) {
        if (_anjay_uri_path_has(&path, ANJAY_ID_IID)) {
            result =
                    dm_create_with_explicit_iid(anjay, obj,
                                                path.ids[ANJAY_ID_IID], in_ctx);
        } else {
            path = MAKE_OBJECT_PATH((*obj)->oid);
            (void) ((result = _anjay_dm_select_free_iid(
                             anjay, obj, &path.ids[ANJAY_ID_IID]))
                    || (result = _anjay_input_update_root_path(in_ctx, &path))
                    || (result = dm_create_inner(
                                anjay, obj, path.ids[ANJAY_ID_IID], in_ctx)));
        }
    }
    if (!result) {
        dm_log(LAZY_DEBUG, _("created: ") "%s", ANJAY_DEBUG_MAKE_PATH(&path));
        if ((result = setup_create_response((*obj)->oid, path.ids[ANJAY_ID_IID],
                                            request->ctx))) {
            dm_log(DEBUG, _("Could not prepare response message."));
        }
    }
    if (!result) {
        anjay_notify_queue_t notify_queue = NULL;
        (void) ((result = _anjay_notify_queue_instance_created(
                         &notify_queue, request->uri.ids[ANJAY_ID_OID],
                         path.ids[ANJAY_ID_IID]))
                || (result = _anjay_notify_flush(anjay, &notify_queue)));
    }
    return result;
}
