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

#include <avsystem/coap/code.h>

#include <avsystem/commons/avs_stream_membuf.h>

#include "anjay_dm_read.h"

#include "../anjay_access_utils_private.h"
#include "../coap/anjay_content_format.h"
#include "../io/anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

static int
read_resource_instance_internal(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_riid_t riid,
                                anjay_output_ctx_t *out_ctx) {
    int result;
    (void) ((result = _anjay_output_set_path(
                     out_ctx,
                     &MAKE_RESOURCE_INSTANCE_PATH((*obj)->oid, iid, rid, riid)))
            || (result = _anjay_dm_call_resource_read(anjay, obj, iid, rid,
                                                      riid, out_ctx, NULL)));
    return result;
}

static int read_resource_instance_clb(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_riid_t riid,
                                      void *out_ctx_) {
    anjay_output_ctx_t *out_ctx = (anjay_output_ctx_t *) out_ctx_;
    int result = read_resource_instance_internal(anjay, obj, iid, rid, riid,
                                                 out_ctx);
    if ((result == ANJAY_ERR_METHOD_NOT_ALLOWED
         || result == ANJAY_ERR_NOT_FOUND)
            && !(result = _anjay_output_clear_path(out_ctx))) {
        dm_log(DEBUG,
               "%s" _(" when attempted to read ") "/%u/%u/%u/%u" _(
                       ", skipping"),
               AVS_COAP_CODE_STRING((uint8_t) -result), (*obj)->oid, iid, rid,
               riid);
    }
    return result;
}

static int read_multiple_resource(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_output_ctx_t *out_ctx) {
    int result;
    (void) ((result = _anjay_output_set_path(
                     out_ctx, &MAKE_RESOURCE_PATH((*obj)->oid, iid, rid)))
            || (result = _anjay_output_start_aggregate(out_ctx))
            || (result = _anjay_dm_foreach_resource_instance(
                        anjay, obj, iid, rid, read_resource_instance_clb,
                        out_ctx)));
    return result;
}

static int read_resource_internal(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_dm_resource_kind_t kind,
                                  anjay_output_ctx_t *out_ctx) {
    if (_anjay_dm_res_kind_multiple(kind)) {
        return read_multiple_resource(anjay, obj, iid, rid, out_ctx);
    } else {
        int result;
        (void) ((result = _anjay_output_set_path(
                         out_ctx, &MAKE_RESOURCE_PATH((*obj)->oid, iid, rid)))
                || (result = _anjay_dm_call_resource_read(anjay, obj, iid, rid,
                                                          ANJAY_ID_INVALID,
                                                          out_ctx, NULL)));
        return result;
    }
}

static int read_resource(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_dm_resource_kind_t kind,
                         anjay_output_ctx_t *out_ctx) {
    if (!_anjay_dm_res_kind_readable(kind)) {
        dm_log(DEBUG, "/%u/%u/%u" _(" is not readable"), (*obj)->oid, iid, rid);
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    return read_resource_internal(anjay, obj, iid, rid, kind, out_ctx);
}

typedef struct {
    anjay_output_ctx_t *out_ctx;
    anjay_ssid_t requesting_ssid;
} read_instance_resource_clb_args_t;

static int read_instance_resource_clb(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_dm_resource_kind_t kind,
                                      anjay_dm_resource_presence_t presence,
                                      void *args_) {
    read_instance_resource_clb_args_t *args =
            (read_instance_resource_clb_args_t *) args_;
    if (presence == ANJAY_DM_RES_ABSENT) {
        dm_log(DEBUG, "/%u/%u/%u" _(" is not present, skipping"), (*obj)->oid,
               iid, rid);
        return 0;
    }
    bool read_allowed = _anjay_dm_res_kind_readable(kind);
    if (!read_allowed && args->requesting_ssid == ANJAY_SSID_BOOTSTRAP) {
        read_allowed = _anjay_dm_res_kind_bootstrappable(kind)
                       || _anjay_dm_res_kind_writable(kind);
    }
    if (!read_allowed) {
        dm_log(DEBUG, "/%u/%u/%u" _(" is not readable, skipping"), (*obj)->oid,
               iid, rid);
        return 0;
    }

    int result =
            read_resource_internal(anjay, obj, iid, rid, kind, args->out_ctx);
    if ((result == ANJAY_ERR_METHOD_NOT_ALLOWED
         || result == ANJAY_ERR_NOT_FOUND)
            && !(result = _anjay_output_clear_path(args->out_ctx))) {
        dm_log(DEBUG,
               "%s" _(" when attempted to read ") "/%u/%u/%u" _(", skipping"),
               AVS_COAP_CODE_STRING((uint8_t) -result), (*obj)->oid, iid, rid);
    }
    return result;
}

static int read_instance(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj,
                         anjay_iid_t iid,
                         anjay_ssid_t requesting_ssid,
                         anjay_output_ctx_t *out_ctx) {
    int result;
    (void) ((result = _anjay_output_set_path(
                     out_ctx, &MAKE_INSTANCE_PATH((*obj)->oid, iid)))
            || (result = _anjay_output_start_aggregate(out_ctx))
            || (result = _anjay_dm_foreach_resource(
                        anjay, obj, iid, read_instance_resource_clb,
                        &(read_instance_resource_clb_args_t) {
                            .out_ctx = out_ctx,
                            .requesting_ssid = requesting_ssid
                        })));
    return result;
}

typedef struct {
    anjay_uri_path_t uri;
    anjay_ssid_t requesting_ssid;
    anjay_output_ctx_t *out_ctx;
} read_instance_clb_args_t;

static int read_instance_clb(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             void *args_) {
    read_instance_clb_args_t *args = (read_instance_clb_args_t *) args_;
    anjay_action_info_t info = {
        .oid = args->uri.ids[ANJAY_ID_OID],
        .iid = iid,
        .ssid = args->requesting_ssid,
        .action = ANJAY_ACTION_READ
    };
    if (!_anjay_instance_action_allowed(anjay, &info)) {
        return ANJAY_FOREACH_CONTINUE;
    }
    return read_instance(anjay, obj, iid, args->requesting_ssid, args->out_ctx);
}

static int read_object(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj,
                       const anjay_uri_path_t *uri,
                       anjay_ssid_t requesting_ssid,
                       anjay_output_ctx_t *out_ctx) {
    assert(_anjay_uri_path_has(uri, ANJAY_ID_OID));
    return _anjay_dm_foreach_instance(anjay, obj, read_instance_clb,
                                      &(read_instance_clb_args_t) {
                                          .uri = *uri,
                                          .requesting_ssid = requesting_ssid,
                                          .out_ctx = out_ctx
                                      });
}

typedef struct {
    anjay_ssid_t requesting_ssid;
    anjay_output_ctx_t *out_ctx;
} read_object_clb_args_t;

static int read_object_clb(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           void *args_) {
    if ((*obj)->oid == ANJAY_DM_OID_SECURITY) {
        return ANJAY_FOREACH_CONTINUE;
    }
    read_object_clb_args_t *args = (read_object_clb_args_t *) args_;
    return read_object(anjay, obj, &MAKE_OBJECT_PATH((*obj)->oid),
                       args->requesting_ssid, args->out_ctx);
}

static int read_root(anjay_t *anjay,
                     anjay_ssid_t requesting_ssid,
                     anjay_output_ctx_t *out_ctx) {
    return _anjay_dm_foreach_object(anjay, read_object_clb,
                                    &(read_object_clb_args_t) {
                                        .requesting_ssid = requesting_ssid,
                                        .out_ctx = out_ctx
                                    });
}

int _anjay_dm_read(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj,
                   const anjay_dm_path_info_t *path_info,
                   anjay_ssid_t requesting_ssid,
                   anjay_output_ctx_t *out_ctx) {
    if (!path_info->is_present) {
        return ANJAY_ERR_NOT_FOUND;
    }
    if (_anjay_uri_path_has(&path_info->uri, ANJAY_ID_IID)) {
        const anjay_action_info_t action_info = {
            .iid = path_info->uri.ids[ANJAY_ID_IID],
            .oid = path_info->uri.ids[ANJAY_ID_OID],
            .ssid = requesting_ssid,
            .action = ANJAY_ACTION_READ
        };
        if (!_anjay_instance_action_allowed(anjay, &action_info)) {
            return ANJAY_ERR_UNAUTHORIZED;
        }
    }
    if (_anjay_uri_path_length(&path_info->uri) == 0) {
        assert(!obj);
        return read_root(anjay, requesting_ssid, out_ctx);
    }
    assert(obj && *obj);
    assert(path_info->uri.ids[ANJAY_ID_OID] == (*obj)->oid);
    if (_anjay_uri_path_leaf_is(&path_info->uri, ANJAY_ID_OID)) {
        return read_object(anjay, obj, &path_info->uri, requesting_ssid,
                           out_ctx);
    } else if (_anjay_uri_path_leaf_is(&path_info->uri, ANJAY_ID_IID)) {
        return read_instance(anjay, obj, path_info->uri.ids[ANJAY_ID_IID],
                             requesting_ssid, out_ctx);
    } else if (_anjay_uri_path_leaf_is(&path_info->uri, ANJAY_ID_RID)) {
        return read_resource(anjay, obj, path_info->uri.ids[ANJAY_ID_IID],
                             path_info->uri.ids[ANJAY_ID_RID], path_info->kind,
                             out_ctx);
    } else {
        assert(_anjay_uri_path_leaf_is(&path_info->uri, ANJAY_ID_RIID));
        dm_log(ERROR, _("Read on Resource Instances is not supported in this "
                        "version of Anjay"));
        return ANJAY_ERR_BAD_REQUEST;
    }
}

int _anjay_dm_read_and_destroy_ctx(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_path_info_t *path_info,
                                   anjay_ssid_t requesting_ssid,
                                   anjay_output_ctx_t **out_ctx_ptr) {
    dm_log(LAZY_DEBUG, _("Read ") "%s", ANJAY_DEBUG_MAKE_PATH(&path_info->uri));
    return _anjay_output_ctx_destroy_and_process_result(
            out_ctx_ptr, _anjay_dm_read(anjay, obj, path_info, requesting_ssid,
                                        *out_ctx_ptr));
}

anjay_msg_details_t
_anjay_dm_response_details_for_read(anjay_t *anjay,
                                    const anjay_request_t *request,
                                    bool requires_hierarchical_format,
                                    anjay_lwm2m_version_t lwm2m_version) {
    assert(request->action == ANJAY_ACTION_READ);
    uint16_t format = request->requested_format;
    if (format == AVS_COAP_FORMAT_NONE) {
        if (requires_hierarchical_format) {
            format = _anjay_default_hierarchical_format(lwm2m_version);
        } else {
            format = _anjay_default_simple_format(anjay, lwm2m_version);
        }
    }
    return (const anjay_msg_details_t) {
        .msg_code = _anjay_dm_make_success_response_code(request->action),
        .format = format
    };
}

int _anjay_dm_read_or_observe(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              const anjay_request_t *request) {
    assert(_anjay_uri_path_has(&request->uri, ANJAY_ID_OID));
    if (request->observe) {
        dm_log(LAZY_DEBUG, _("Observe ") "%s",
               ANJAY_DEBUG_MAKE_PATH(&request->uri));
#ifdef ANJAY_WITH_OBSERVE
        return _anjay_observe_handle(anjay, request);
#else  // ANJAY_WITH_OBSERVE
        dm_log(ERROR, _("Observe support disabled"));
        return ANJAY_ERR_BAD_OPTION;
#endif // ANJAY_WITH_OBSERVE
    }

    anjay_dm_path_info_t path_info;
    int result = _anjay_dm_path_info(anjay, obj, &request->uri, &path_info);
    if (result) {
        return result;
    }
    const anjay_msg_details_t details = _anjay_dm_response_details_for_read(
            anjay, request, path_info.is_hierarchical,
            _anjay_server_registration_info(anjay->current_connection.server)
                    ->lwm2m_version);

    avs_stream_t *response_stream =
            _anjay_coap_setup_response_stream(request->ctx, &details);
    if (!response_stream) {
        return ANJAY_ERR_INTERNAL;
    }

    anjay_output_ctx_t *out_ctx = NULL;
    if ((result = _anjay_output_dynamic_construct(&out_ctx, response_stream,
                                                  &request->uri, details.format,
                                                  ANJAY_ACTION_READ))) {
        return result;
    }
    return _anjay_dm_read_and_destroy_ctx(
            anjay, obj, &path_info, _anjay_dm_current_ssid(anjay), &out_ctx);
}

int _anjay_dm_read_resource_into_ctx(anjay_t *anjay,
                                     const anjay_uri_path_t *path,
                                     anjay_output_ctx_t *ctx) {
    assert(_anjay_uri_path_leaf_is(path, ANJAY_ID_RID));
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, path->ids[ANJAY_ID_OID]);
    if (!obj) {
        dm_log(ERROR, _("unregistered Object ID: ") "%u",
               path->ids[ANJAY_ID_OID]);
        return -1;
    }

    anjay_dm_resource_kind_t kind;
    int result;
    (void) ((result = _anjay_dm_verify_resource_present(
                     anjay, obj, path->ids[ANJAY_ID_IID],
                     path->ids[ANJAY_ID_RID], &kind))
            || (result = read_resource_internal(
                        anjay, obj, path->ids[ANJAY_ID_IID],
                        path->ids[ANJAY_ID_RID], kind, ctx)));
    return result;
}

int _anjay_dm_read_resource_into_stream(anjay_t *anjay,
                                        const anjay_uri_path_t *path,
                                        avs_stream_t *stream) {
    anjay_output_buf_ctx_t ctx = _anjay_output_buf_ctx_init(stream);
    return _anjay_dm_read_resource_into_ctx(anjay, path,
                                            (anjay_output_ctx_t *) &ctx);
}

int _anjay_dm_read_resource_into_buffer(anjay_t *anjay,
                                        const anjay_uri_path_t *path,
                                        char *buffer,
                                        size_t buffer_size,
                                        size_t *out_bytes_read) {
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, buffer_size);

    int result = _anjay_dm_read_resource_into_stream(anjay, path,
                                                     (avs_stream_t *) &stream);
    if (out_bytes_read) {
        *out_bytes_read = avs_stream_outbuf_offset(&stream);
    }
    return result;
}
