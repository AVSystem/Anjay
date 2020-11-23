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

#include "anjay_dm_write.h"

#include "../anjay_access_utils_private.h"

#include <avsystem/commons/avs_stream_inbuf.h>

VISIBILITY_SOURCE_BEGIN

static int write_single_resource(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 const anjay_uri_path_t *path,
                                 bool is_array,
                                 anjay_input_ctx_t *in_ctx) {
    assert(_anjay_uri_path_has(path, ANJAY_ID_RID));
    if (is_array || _anjay_uri_path_has(path, ANJAY_ID_RIID)) {
        dm_log(LAZY_DEBUG,
               _("cannot write ") "%s" _(" because the path does not point "
                                         "insude a multiple resource"),
               ANJAY_DEBUG_MAKE_PATH(path));
        return ANJAY_ERR_BAD_REQUEST;
    }
    return _anjay_dm_call_resource_write(anjay, obj, path->ids[ANJAY_ID_IID],
                                         path->ids[ANJAY_ID_RID],
                                         ANJAY_ID_INVALID, in_ctx, NULL);
}

static int write_multiple_resource(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_uri_path_t *first_path,
                                   bool is_array,
                                   anjay_input_ctx_t *in_ctx,
                                   anjay_dm_write_type_t write_type) {
    anjay_uri_path_t path = *first_path;
    assert(_anjay_uri_path_has(&path, ANJAY_ID_RID));
    if (!is_array && _anjay_uri_path_leaf_is(&path, ANJAY_ID_RID)) {
        dm_log(LAZY_DEBUG,
               "%s" _(" is a multiple resource, but the payload attempted to "
                      "treat it as single"),
               ANJAY_DEBUG_MAKE_PATH(&path));
        return ANJAY_ERR_BAD_REQUEST;
    }

    int result = 0;

    if (write_type != ANJAY_DM_WRITE_TYPE_UPDATE
            && (result = _anjay_dm_call_resource_reset(
                        anjay, obj, path.ids[ANJAY_ID_IID],
                        path.ids[ANJAY_ID_RID], NULL))) {
        return result;
    }

    while (!result) {
        if (!_anjay_uri_path_leaf_is(&path, ANJAY_ID_RIID)
                || (result = _anjay_dm_call_resource_write(
                            anjay, obj, path.ids[ANJAY_ID_IID],
                            path.ids[ANJAY_ID_RID], path.ids[ANJAY_ID_RIID],
                            in_ctx, NULL))
                || (result = _anjay_input_next_entry(in_ctx))) {
            break;
        }
        if ((result = _anjay_input_get_path(in_ctx, &path, NULL))) {
            if (result == ANJAY_GET_PATH_END) {
                result = 0;
            }
            break;
        }
        if (path.ids[ANJAY_ID_IID] != first_path->ids[ANJAY_ID_IID]
                || path.ids[ANJAY_ID_RID] != first_path->ids[ANJAY_ID_RID]) {
            break;
        }
    }
    return result;
}

static int write_resource_impl(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_input_ctx_t *in_ctx,
                               anjay_notify_queue_t *notify_queue,
                               bool allow_non_writable,
                               anjay_dm_write_type_t write_type) {
    anjay_uri_path_t path;
    bool is_array;
    int result = _anjay_input_get_path(in_ctx, &path, &is_array);
    if (result == ANJAY_GET_PATH_END) {
        /* there was no header describing the resource, and that is fatal */
        return ANJAY_ERR_BAD_REQUEST;
    } else if (result) {
        return result;
    }
    assert(_anjay_uri_path_has(&path, ANJAY_ID_RID));
    assert(path.ids[ANJAY_ID_OID] == (*obj)->oid);

    anjay_dm_resource_kind_t kind;
    if (!(result = _anjay_dm_resource_kind_and_presence(
                  anjay, obj, path.ids[ANJAY_ID_IID], path.ids[ANJAY_ID_RID],
                  &kind, NULL))
            && !_anjay_dm_res_kind_writable(kind)
            && (!allow_non_writable
                || !(_anjay_dm_res_kind_readable(kind)
                     || _anjay_dm_res_kind_bootstrappable(kind)))) {
        result = ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    if (!result) {
        if (_anjay_dm_res_kind_multiple(kind)) {
            result = write_multiple_resource(anjay, obj, &path, is_array,
                                             in_ctx, write_type);
        } else {
            result = write_single_resource(anjay, obj, &path, is_array, in_ctx);
        }
    }
    if (!result && notify_queue) {
        result = _anjay_notify_queue_resource_change(notify_queue,
                                                     path.ids[ANJAY_ID_OID],
                                                     path.ids[ANJAY_ID_IID],
                                                     path.ids[ANJAY_ID_RID]);
    }
    int next_result = _anjay_input_next_entry(in_ctx);
    return result ? result : next_result;
}

static int write_resource(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_input_ctx_t *in_ctx,
                          anjay_notify_queue_t *notify_queue,
                          anjay_dm_write_type_t write_type) {
    return write_resource_impl(anjay, obj, in_ctx, notify_queue, false,
                               write_type);
}

int _anjay_dm_write_resource(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_input_ctx_t *in_ctx,
                             anjay_notify_queue_t *notify_queue) {
    return write_resource_impl(anjay, obj, in_ctx, notify_queue, true,
                               ANJAY_DM_WRITE_TYPE_REPLACE);
}

typedef enum {
    WRITE_INSTANCE_FAIL_ON_UNSUPPORTED,
    WRITE_INSTANCE_IGNORE_UNSUPPORTED
} write_instance_hint_t;

/**
 * Writes to instance whose location is determined by the path extracted
 * from Input Context (@p in_ctx).
 */
static int write_instance(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_input_ctx_t *in_ctx,
                          anjay_notify_queue_t *notify_queue,
                          write_instance_hint_t hint,
                          anjay_dm_write_type_t write_type) {
    int result;
    do {
        anjay_uri_path_t path;
        if ((result = _anjay_input_get_path(in_ctx, &path, NULL))) {
            if (result == ANJAY_GET_PATH_END) {
                result = 0;
            }
            break;
        }
        if (!_anjay_uri_path_has(&path, ANJAY_ID_IID)
                || path.ids[ANJAY_ID_IID] != iid) {
            /* more than one instance in the payload is not allowed */
            return ANJAY_ERR_BAD_REQUEST;
        }
        if (!_anjay_uri_path_has(&path, ANJAY_ID_RID)) {
            /* no resources, meaning the instance is empty */
            break;
        }
        result = write_resource(anjay, obj, in_ctx, notify_queue, write_type);
        if (result == ANJAY_ERR_NOT_FOUND
                && hint == WRITE_INSTANCE_IGNORE_UNSUPPORTED) {
            result = 0;
        }
    } while (!result);
    return result;
}

int _anjay_dm_write(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj,
                    const anjay_request_t *request,
                    anjay_input_ctx_t *in_ctx) {
    dm_log(LAZY_DEBUG, _("Write ") "%s", ANJAY_DEBUG_MAKE_PATH(&request->uri));
    if (!_anjay_uri_path_has(&request->uri, ANJAY_ID_IID)) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    int result =
            _anjay_dm_verify_instance_present(anjay, obj,
                                              request->uri.ids[ANJAY_ID_IID]);
    if (result) {
        return result;
    }
    if (!_anjay_instance_action_allowed(
                anjay, &REQUEST_TO_ACTION_INFO(anjay, request))) {
        return ANJAY_ERR_UNAUTHORIZED;
    }

    anjay_notify_queue_t notify_queue = NULL;
    anjay_dm_write_type_t write_type =
            _anjay_dm_write_type_from_request_action(request->action);
    if (_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_IID)) {
        if (write_type != ANJAY_DM_WRITE_TYPE_UPDATE
                && (result = _anjay_dm_call_instance_reset(
                            anjay, obj, request->uri.ids[ANJAY_ID_IID],
                            NULL))) {
            return result;
        }
        result = write_instance(anjay, obj, request->uri.ids[ANJAY_ID_IID],
                                in_ctx, &notify_queue,
                                WRITE_INSTANCE_FAIL_ON_UNSUPPORTED, write_type);
    } else if (_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_RID)) {
        result = write_resource(anjay, obj, in_ctx, &notify_queue, write_type);
    } else if (_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_RIID)) {
        dm_log(ERROR, _("Write on Resource Instances is not supported in this "
                        "version of Anjay"));
        result = ANJAY_ERR_BAD_REQUEST;
    }
    if (!result) {
        result = _anjay_notify_perform(anjay, notify_queue);
    }
    _anjay_notify_clear_queue(&notify_queue);
    return result;
}

int _anjay_dm_write_created_instance(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     anjay_input_ctx_t *in_ctx) {
    return write_instance(
            anjay, obj, iid, in_ctx, NULL, WRITE_INSTANCE_IGNORE_UNSUPPORTED,
            _anjay_dm_write_type_from_request_action(ANJAY_ACTION_CREATE));
}
