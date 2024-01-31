/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anj/dm.h>

#include "../dm_core.h"
#include "../dm_utils/dm_utils_core.h"

static int read_resource_instance_internal(dm_t *dm,
                                           const dm_installed_object_t *obj,
                                           fluf_iid_t iid,
                                           fluf_rid_t rid,
                                           fluf_riid_t riid,
                                           dm_output_ctx_t *output_ctx) {
    dm_output_internal_ctx_t internal_out_ctx = {
        .output_ctx = output_ctx,
        .path = FLUF_MAKE_RESOURCE_INSTANCE_PATH(_dm_installed_object_oid(obj),
                                                 iid, rid, riid),
    };
    return _dm_call_resource_read(dm, obj, iid, rid, riid, &internal_out_ctx);
}

static int read_resource_instance_clb(dm_t *dm,
                                      const dm_installed_object_t *obj,
                                      fluf_iid_t iid,
                                      fluf_rid_t rid,
                                      fluf_riid_t riid,
                                      void *out_ctx_) {
    dm_output_ctx_t *out_ctx = (dm_output_ctx_t *) out_ctx_;
    return read_resource_instance_internal(dm, obj, iid, rid, riid, out_ctx);
}

static int read_multiple_resource(dm_t *dm,
                                  const dm_installed_object_t *obj,
                                  fluf_iid_t iid,
                                  fluf_rid_t rid,
                                  dm_output_ctx_t *out_ctx) {
    return _dm_foreach_resource_instance(dm, obj, iid, rid,
                                         read_resource_instance_clb, out_ctx);
}

static int read_resource_internal(dm_t *dm,
                                  const dm_installed_object_t *obj,
                                  fluf_iid_t iid,
                                  fluf_rid_t rid,
                                  dm_resource_kind_t kind,
                                  dm_output_ctx_t *output_ctx) {
    if (_dm_res_kind_multiple(kind)) {
        return read_multiple_resource(dm, obj, iid, rid, output_ctx);
    }
    dm_output_internal_ctx_t internal_out_ctx = {
        .output_ctx = output_ctx,
        .path = FLUF_MAKE_RESOURCE_PATH(_dm_installed_object_oid(obj), iid,
                                        rid),
    };
    return _dm_call_resource_read(dm, obj, iid, rid, FLUF_ID_INVALID,
                                  &internal_out_ctx);
}

static int verify_resource(dm_resource_kind_t kind,
                           dm_resource_presence_t presence) {
    if (!(_dm_res_kind_readable(kind))) {
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    if (presence != DM_RES_PRESENT) {
        return FLUF_COAP_CODE_NOT_FOUND;
    }
    return 0;
}

static int read_resource_instance(dm_t *dm,
                                  const dm_installed_object_t *obj,
                                  fluf_iid_t iid,
                                  fluf_rid_t rid,
                                  fluf_riid_t riid,
                                  dm_output_ctx_t *out_ctx) {
    assert(riid != FLUF_ID_INVALID);
    dm_resource_kind_t kind;
    dm_resource_presence_t presence;
    int result =
            _dm_resource_kind_and_presence(dm, obj, iid, rid, &kind, &presence);
    if (result) {
        return result;
    }
    if ((result = verify_resource(kind, presence))) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" not present or not readable"),
               _dm_installed_object_oid(obj), iid, rid);
        return result;
    }
    if (!_dm_res_kind_multiple(kind)) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" points to resource instance but is not "
                             "a multiple resource"),
               _dm_installed_object_oid(obj), iid, rid, riid);
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    if ((result = _dm_resource_instance_existence(dm, obj, iid, rid, riid))) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" not present"),
               _dm_installed_object_oid(obj), iid, rid, riid);
        return result;
    }
    return read_resource_instance_internal(dm, obj, iid, rid, riid, out_ctx);
}

static int read_resource(dm_t *dm,
                         const dm_installed_object_t *obj,
                         fluf_iid_t iid,
                         fluf_rid_t rid,
                         dm_output_ctx_t *out_ctx) {

    dm_resource_kind_t kind;
    dm_resource_presence_t presence;
    int result =
            _dm_resource_kind_and_presence(dm, obj, iid, rid, &kind, &presence);
    if (result) {
        return result;
    }
    result = verify_resource(kind, presence);
    if (result) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" not present or not readable"),
               _dm_installed_object_oid(obj), iid, rid);
        return result;
    }
    return read_resource_internal(dm, obj, iid, rid, kind, out_ctx);
}

static int read_instance_resource_clb(dm_t *dm,
                                      const dm_installed_object_t *obj,
                                      fluf_iid_t iid,
                                      fluf_rid_t rid,
                                      dm_resource_kind_t kind,
                                      dm_resource_presence_t presence,
                                      void *args_) {
    dm_output_ctx_t *args = (dm_output_ctx_t *) args_;

    if (verify_resource(kind, presence)) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" not present or not readable, skipping"),
               _dm_installed_object_oid(obj), iid, rid);
        return 0;
    }
    return read_resource_internal(dm, obj, iid, rid, kind, args);
}

static int read_instance(dm_t *dm,
                         const dm_installed_object_t *obj,
                         fluf_iid_t iid,
                         dm_output_ctx_t *out_ctx) {
    int result = _dm_verify_instance_present(dm, obj, iid);
    if (result) {
        return result;
    }
    return _dm_foreach_resource(dm, obj, iid, read_instance_resource_clb,
                                out_ctx);
}

static int read_instance_clb(dm_t *dm,
                             const dm_installed_object_t *obj,
                             fluf_iid_t iid,
                             void *out_ctx_) {
    dm_output_ctx_t *out_ctx = (dm_output_ctx_t *) out_ctx_;
    return read_instance(dm, obj, iid, out_ctx);
}

static int read_object(dm_t *dm,
                       const dm_installed_object_t *obj,
                       const fluf_uri_path_t *uri,
                       dm_output_ctx_t *out_ctx) {
    assert(fluf_uri_path_has(uri, FLUF_ID_OID));
    return _dm_foreach_instance(dm, obj, read_instance_clb, out_ctx);
}

static int
read_object_clb(dm_t *dm, const dm_installed_object_t *obj, void *out_ctx_) {
    if (_dm_installed_object_oid(obj) == DM_OID_SECURITY) {
        return DM_FOREACH_CONTINUE;
    }
    dm_output_ctx_t *out_ctx = (dm_output_ctx_t *) out_ctx_;
    return read_object(dm, obj,
                       &FLUF_MAKE_OBJECT_PATH(_dm_installed_object_oid(obj)),
                       out_ctx);
}

static int read_root(dm_t *dm, dm_output_ctx_t *out_ctx) {
    return _dm_foreach_object(dm, read_object_clb, out_ctx);
}

int dm_read(dm_t *dm, const fluf_uri_path_t *uri, dm_output_ctx_t *out_ctx) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(uri, "uri is NULL");
    AVS_ASSERT(out_ctx, "input_ctx is NULL");
    AVS_ASSERT(out_ctx->callback, "input_ctx->callback is NULL");

    if (!dm->objects_count) {
        return FLUF_COAP_CODE_NOT_FOUND;
    }
    if (fluf_uri_path_length(uri) == 0) {
        return read_root(dm, out_ctx);
    }
    const dm_installed_object_t *obj;
    int result = _dm_find_object(dm, uri, &obj);
    if (result) {
        return result;
    }
    assert(obj);
    assert(uri->ids[FLUF_ID_OID] == _dm_installed_object_oid(obj));
    if (fluf_uri_path_is(uri, FLUF_ID_OID)) {
        return read_object(dm, obj, uri, out_ctx);
    }
    if (fluf_uri_path_is(uri, FLUF_ID_IID)) {
        return read_instance(dm, obj, uri->ids[FLUF_ID_IID], out_ctx);
    }
    if (fluf_uri_path_is(uri, FLUF_ID_RID)) {
        return read_resource(dm, obj, uri->ids[FLUF_ID_IID],
                             uri->ids[FLUF_ID_RID], out_ctx);
    }
    assert(fluf_uri_path_is(uri, FLUF_ID_RIID));
    return read_resource_instance(dm, obj, uri->ids[FLUF_ID_IID],
                                  uri->ids[FLUF_ID_RID], uri->ids[FLUF_ID_RIID],
                                  out_ctx);
}
