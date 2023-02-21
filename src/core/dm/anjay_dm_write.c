/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "anjay_dm_write.h"

#include "../anjay_access_utils_private.h"

#include <avsystem/commons/avs_stream_inbuf.h>

VISIBILITY_SOURCE_BEGIN

static int
preverify_resource_before_writing(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj,
                                  const anjay_uri_path_t *request_path,
                                  const anjay_uri_path_t *payload_path,
                                  bool allow_non_writable,
                                  anjay_dm_resource_kind_t *out_kind,
                                  anjay_dm_resource_presence_t *out_presence) {
    assert(payload_path);
    assert(out_kind);
    assert(_anjay_uri_path_has(payload_path, ANJAY_ID_RID));
    assert(payload_path->ids[ANJAY_ID_OID]
           == _anjay_dm_installed_object_oid(obj));

    int result = _anjay_dm_resource_kind_and_presence(
            anjay, obj, payload_path->ids[ANJAY_ID_IID],
            payload_path->ids[ANJAY_ID_RID], out_kind, out_presence);
    if (result) {
        return result;
    }
    if (!_anjay_dm_res_kind_writable(*out_kind)
            && (!allow_non_writable
                || !(_anjay_dm_res_kind_readable(*out_kind)
                     || _anjay_dm_res_kind_bootstrappable(*out_kind)))) {
        anjay_log(LAZY_DEBUG, "%s" _(" is not writable"),
                  ANJAY_DEBUG_MAKE_PATH(payload_path));
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    if (_anjay_uri_path_has(payload_path, ANJAY_ID_RIID)
            && !_anjay_dm_res_kind_multiple(*out_kind)) {
        anjay_log(LAZY_DEBUG,
                  _("cannot write ") "%s" _(" because the path does not point "
                                            "inside a multiple resource"),
                  ANJAY_DEBUG_MAKE_PATH(payload_path));
        return (request_path != NULL
                && _anjay_uri_path_has(request_path, ANJAY_ID_RIID))
                       ? ANJAY_ERR_METHOD_NOT_ALLOWED
                       : ANJAY_ERR_BAD_REQUEST;
    }

    return 0;
}

#ifdef ANJAY_WITH_LWM2M11
/**
 * Writes to resource instance whose location is determined by the path
 * extracted from Input Context (@p in_ctx).
 */
static int write_resource_instance(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj,
                                   const anjay_uri_path_t *path,
                                   anjay_dm_resource_presence_t presence,
                                   anjay_unlocked_input_ctx_t *in_ctx,
                                   anjay_notify_queue_t *notify_queue,
                                   bool create_nonexistent) {
    assert(_anjay_uri_path_leaf_is(path, ANJAY_ID_RIID));

    if (!create_nonexistent && presence == ANJAY_DM_RES_ABSENT) {
        return ANJAY_ERR_NOT_FOUND;
    }

    int result;
    if (!create_nonexistent
            && (result = _anjay_dm_verify_resource_instance_present(
                        anjay, obj, path->ids[ANJAY_ID_IID],
                        path->ids[ANJAY_ID_RID], path->ids[ANJAY_ID_RIID]))) {
        return result;
    }

    result = _anjay_dm_call_resource_write(anjay, obj, path->ids[ANJAY_ID_IID],
                                           path->ids[ANJAY_ID_RID],
                                           path->ids[ANJAY_ID_RIID], in_ctx);

    if (!result && notify_queue) {
        result = _anjay_notify_queue_resource_change(notify_queue,
                                                     path->ids[ANJAY_ID_OID],
                                                     path->ids[ANJAY_ID_IID],
                                                     path->ids[ANJAY_ID_RID]);
    }
    return result;
}
#endif // ANJAY_WITH_LWM2M11

static int return_with_moving_to_next_entry(anjay_unlocked_input_ctx_t *in_ctx,
                                            int result) {
    int next_entry_result = _anjay_input_next_entry(in_ctx);
    if (next_entry_result
            && (!result || result == ANJAY_ERR_NOT_FOUND
                || result == ANJAY_ERR_NOT_IMPLEMENTED)) {
        return next_entry_result;
    }
    return result;
}

static int write_single_resource_and_move_to_next_entry(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        const anjay_uri_path_t *path,
        bool is_array,
        anjay_unlocked_input_ctx_t *in_ctx) {
    assert(_anjay_uri_path_has(path, ANJAY_ID_RID));
    if (is_array || _anjay_uri_path_has(path, ANJAY_ID_RIID)) {
        dm_log(LAZY_DEBUG,
               _("cannot write ") "%s" _(" because the path does not point "
                                         "inside a multiple resource"),
               ANJAY_DEBUG_MAKE_PATH(path));
        return ANJAY_ERR_BAD_REQUEST;
    }
    return return_with_moving_to_next_entry(
            in_ctx, _anjay_dm_call_resource_write(
                            anjay, obj, path->ids[ANJAY_ID_IID],
                            path->ids[ANJAY_ID_RID], ANJAY_ID_INVALID, in_ctx));
}

static int write_multiple_resource_and_move_to_next_entry(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        const anjay_uri_path_t *first_path,
        bool is_array,
        anjay_unlocked_input_ctx_t *in_ctx,
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

    if ((write_type != ANJAY_DM_WRITE_TYPE_UPDATE
         && (result = _anjay_dm_call_resource_reset(anjay, obj,
                                                    path.ids[ANJAY_ID_IID],
                                                    path.ids[ANJAY_ID_RID])))
            || !_anjay_uri_path_leaf_is(&path, ANJAY_ID_RIID)) {
        return return_with_moving_to_next_entry(in_ctx, result);
    }

    while (!result) {
        if ((result = _anjay_dm_call_resource_write(
                     anjay, obj, path.ids[ANJAY_ID_IID], path.ids[ANJAY_ID_RID],
                     path.ids[ANJAY_ID_RIID], in_ctx))) {
            return return_with_moving_to_next_entry(in_ctx, result);
        }
        if ((result = _anjay_input_next_entry(in_ctx))) {
            break;
        }
        if ((result = _anjay_input_get_path(in_ctx, &path, NULL))) {
            if (result == ANJAY_GET_PATH_END) {
                result = 0;
            }
            break;
        }
        if (path.ids[ANJAY_ID_IID] != first_path->ids[ANJAY_ID_IID]
                || path.ids[ANJAY_ID_RID] != first_path->ids[ANJAY_ID_RID]
                || !_anjay_uri_path_leaf_is(&path, ANJAY_ID_RIID)) {
            break;
        }
    }
    return result;
}

static int
write_resource_and_move_to_next_entry(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t *obj,
                                      const anjay_uri_path_t *path,
                                      anjay_dm_resource_kind_t kind,
                                      bool is_array,
                                      anjay_unlocked_input_ctx_t *in_ctx,
                                      anjay_notify_queue_t *notify_queue,
                                      anjay_dm_write_type_t write_type) {
    int result;
    if (_anjay_dm_res_kind_multiple(kind)) {
        result = write_multiple_resource_and_move_to_next_entry(
                anjay, obj, path, is_array, in_ctx, write_type);
    } else {
        result = write_single_resource_and_move_to_next_entry(anjay, obj, path,
                                                              is_array, in_ctx);
    }
    if (!result && notify_queue) {
        result = _anjay_notify_queue_resource_change(notify_queue,
                                                     path->ids[ANJAY_ID_OID],
                                                     path->ids[ANJAY_ID_IID],
                                                     path->ids[ANJAY_ID_RID]);
    }
    return result;
}

int _anjay_dm_write_resource_and_move_to_next_entry(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_unlocked_input_ctx_t *in_ctx,
        anjay_notify_queue_t *notify_queue) {
    anjay_uri_path_t path;
    bool is_array;
    int result = _anjay_input_get_path(in_ctx, &path, &is_array);
    if (result == ANJAY_GET_PATH_END) {
        /* there was no header describing the resource, and that is fatal */
        return ANJAY_ERR_BAD_REQUEST;
    } else if (result) {
        return result;
    }

    anjay_dm_resource_kind_t kind;
    if ((result = preverify_resource_before_writing(anjay, obj, NULL, &path,
                                                    true, &kind, NULL))) {
        return return_with_moving_to_next_entry(in_ctx, result);
    }

    return write_resource_and_move_to_next_entry(anjay, obj, &path, kind,
                                                 is_array, in_ctx, notify_queue,
                                                 ANJAY_DM_WRITE_TYPE_REPLACE);
}

#ifdef ANJAY_WITH_LWM2M11
static int write_resource_raw(anjay_unlocked_t *anjay,
                              anjay_uri_path_t path,
                              void *value,
                              size_t value_size,
                              anjay_notify_queue_t *notify_queue) {
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, path.ids[ANJAY_ID_OID]);
    if (!obj) {
        return ANJAY_ERR_NOT_FOUND;
    }

    avs_stream_inbuf_t inbuf_stream = AVS_STREAM_INBUF_STATIC_INITIALIZER;
    avs_stream_inbuf_set_buffer(&inbuf_stream, value, value_size);
    avs_stream_t *stream = (avs_stream_t *) &inbuf_stream;
    anjay_input_buf_ctx_t temp_ctx = _anjay_input_buf_ctx_init(stream, &path);

    int result = ANJAY_ERR_INTERNAL;
    if (avs_is_ok(_anjay_dm_transaction_begin(anjay))) {
        result = _anjay_dm_write_resource_and_move_to_next_entry(
                anjay, obj, (anjay_unlocked_input_ctx_t *) &temp_ctx,
                notify_queue);
        result = _anjay_dm_transaction_finish(anjay, result);
    }
    if (result) {
        anjay_log(DEBUG, _("writing to ") "/%u/%u/%u" _(" failed: ") "%d",
                  path.ids[ANJAY_ID_OID], path.ids[ANJAY_ID_IID],
                  path.ids[ANJAY_ID_RID], result);
    }
    return result;
}

int _anjay_dm_write_resource_i64(anjay_unlocked_t *anjay,
                                 anjay_uri_path_t path,
                                 int64_t value,
                                 anjay_notify_queue_t *notify_queue) {
    return write_resource_raw(anjay, path, &value, sizeof(value), notify_queue);
}

int _anjay_dm_write_resource_u64(anjay_unlocked_t *anjay,
                                 anjay_uri_path_t path,
                                 uint64_t value,
                                 anjay_notify_queue_t *notify_queue) {
    return write_resource_raw(anjay, path, &value, sizeof(value), notify_queue);
}
#endif // ANJAY_WITH_LWM2M11

/**
 * Writes to instance whose location is determined by the path extracted
 * from Input Context (@p in_ctx).
 */
static int
write_instance_and_move_to_next_entry(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t *obj,
                                      anjay_iid_t iid,
                                      anjay_unlocked_input_ctx_t *in_ctx,
                                      anjay_notify_queue_t *notify_queue,
                                      anjay_dm_write_type_t write_type) {
    int result;
    do {
        anjay_uri_path_t path;
        bool is_array;
        if ((result = _anjay_input_get_path(in_ctx, &path, &is_array))) {
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
        anjay_dm_resource_kind_t kind;
        if (!_anjay_uri_path_has(&path, ANJAY_ID_RID)) {
            return _anjay_input_next_entry(in_ctx);
        }
        bool next_entry_called = false;
        if (!(result = preverify_resource_before_writing(
                      anjay, obj, NULL, &path, false, &kind, NULL))) {
            result = write_resource_and_move_to_next_entry(anjay, obj, &path,
                                                           kind, is_array,
                                                           in_ctx, notify_queue,
                                                           write_type);
            next_entry_called = true;
        }
        if (result == ANJAY_ERR_NOT_FOUND
                || result == ANJAY_ERR_NOT_IMPLEMENTED) {
            result = 0;
        }
        if (!next_entry_called) {
            result = return_with_moving_to_next_entry(in_ctx, result);
        }
    } while (!result);
    return result;
}

int _anjay_dm_write(anjay_unlocked_t *anjay,
                    const anjay_dm_installed_object_t *obj,
                    const anjay_request_t *request,
                    anjay_ssid_t ssid,
                    anjay_unlocked_input_ctx_t *in_ctx) {
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
    if (!_anjay_instance_action_allowed(anjay, &REQUEST_TO_ACTION_INFO(request,
                                                                       ssid))) {
        return ANJAY_ERR_UNAUTHORIZED;
    }

    anjay_notify_queue_t notify_queue = NULL;
    anjay_dm_write_type_t write_type =
            _anjay_dm_write_type_from_request_action(request->action);
    if (_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_IID)) {
        if (write_type != ANJAY_DM_WRITE_TYPE_UPDATE
                && (result = _anjay_dm_call_instance_reset(
                            anjay, obj, request->uri.ids[ANJAY_ID_IID]))) {
            return result;
        }
        result = write_instance_and_move_to_next_entry(
                anjay, obj, request->uri.ids[ANJAY_ID_IID], in_ctx,
                &notify_queue, write_type);
    } else {
        anjay_uri_path_t path;
        bool is_array;
        result = _anjay_input_get_path(in_ctx, &path, &is_array);
        if (result == ANJAY_GET_PATH_END) {
            /* there was no header describing the resource, and that is fatal */
            result = ANJAY_ERR_BAD_REQUEST;
        } else if (!result) {
            anjay_dm_resource_kind_t kind;
            anjay_dm_resource_presence_t presence;
            if (!(result = preverify_resource_before_writing(
                          anjay, obj, &request->uri, &path, false, &kind,
                          &presence))) {
                if (_anjay_uri_path_leaf_is(&request->uri, ANJAY_ID_RID)) {
                    result = write_resource_and_move_to_next_entry(
                            anjay, obj, &path, kind, is_array, in_ctx,
                            &notify_queue, write_type);
                } else {
                    assert(_anjay_uri_path_leaf_is(&request->uri,
                                                   ANJAY_ID_RIID));
#ifdef ANJAY_WITH_LWM2M11
                    result = write_resource_instance(anjay, obj, &path,
                                                     presence, in_ctx,
                                                     &notify_queue, false);
#else  // ANJAY_WITH_LWM2M11
                    dm_log(ERROR,
                           _("Write on Resource Instances is not supported in "
                             "this version of Anjay"));
                    result = ANJAY_ERR_BAD_REQUEST;
#endif // ANJAY_WITH_LWM2M11
                }
            }
        }
    }
    if (!result) {
        result = _anjay_notify_perform(anjay, ssid, &notify_queue);
    }
    _anjay_notify_clear_queue(&notify_queue);
    return result;
}

#ifdef ANJAY_WITH_LWM2M11
int _anjay_dm_write_composite(anjay_unlocked_t *anjay,
                              const anjay_request_t *request,
                              anjay_ssid_t ssid,
                              anjay_unlocked_input_ctx_t *in_ctx) {
    if (_anjay_uri_path_has(&request->uri, ANJAY_ID_OID)) {
        dm_log(DEBUG, _("Write Composite with Uri-Path is not allowed"));
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    anjay_notify_queue_t notify_queue = NULL;
    anjay_uri_path_t path;
    bool is_array;
    int result;
    while (!(result = _anjay_input_get_path(in_ctx, &path, &is_array))) {
        dm_log(DEBUG, _("Write Composite ") "%s", ANJAY_DEBUG_MAKE_PATH(&path));
        if (!_anjay_uri_path_has(&path, ANJAY_ID_RID)) {
            dm_log(DEBUG, _("cannot perform Write Composite on "
                            "non-resource/resource instance"));
            result = ANJAY_ERR_BAD_REQUEST;
            goto finish;
        }
        const anjay_dm_installed_object_t *obj =
                _anjay_dm_find_object_by_oid(anjay, path.ids[ANJAY_ID_OID]);

        if (!obj) {
            dm_log(DEBUG, _("Object not found: ") "%u", path.ids[ANJAY_ID_OID]);
            result = ANJAY_ERR_NOT_FOUND;
            goto finish;
        }

        if ((result = _anjay_dm_verify_instance_present(
                     anjay, obj, path.ids[ANJAY_ID_IID]))) {
            goto finish;
        }

        anjay_dm_resource_kind_t kind;
        anjay_dm_resource_presence_t presence;
        if (!(result = preverify_resource_before_writing(
                      anjay, obj, NULL, &path, false, &kind, &presence))) {
            if (_anjay_uri_path_leaf_is(&path, ANJAY_ID_RID)) {
                result = write_resource_and_move_to_next_entry(
                        anjay, obj, &path, kind, is_array, in_ctx,
                        &notify_queue,
                        _anjay_dm_write_type_from_request_action(
                                request->action));
            } else {
                {
                    result = write_resource_instance(anjay, obj, &path,
                                                     presence, in_ctx,
                                                     &notify_queue, true);
                }
                if (!result) {
                    result = _anjay_input_next_entry(in_ctx);
                }
            }
        }
        if (result) {
            goto finish;
        }
    }
    if (result == ANJAY_GET_PATH_END) {
        result = 0;
    }
finish:
    if (!result) {
        result = _anjay_notify_perform(anjay, ssid, &notify_queue);
    }
    _anjay_notify_clear_queue(&notify_queue);
    return result;
}
#endif // ANJAY_WITH_LWM2M11

int _anjay_dm_write_created_instance_and_move_to_next_entry(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj,
        anjay_iid_t iid,
        anjay_unlocked_input_ctx_t *in_ctx) {
    return write_instance_and_move_to_next_entry(
            anjay, obj, iid, in_ctx, NULL,
            _anjay_dm_write_type_from_request_action(ANJAY_ACTION_CREATE));
}
