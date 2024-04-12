/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/dm.h>
#include <anj/dm_io.h>

#include "dm_core.h"
#include "dm_utils.h"
#include "dm_utils_core.h"

static int
preverify_resource_before_writing(dm_t *dm,
                                  const dm_installed_object_t *obj,
                                  const fluf_uri_path_t *payload_path,
                                  dm_resource_kind_t *out_kind,
                                  dm_resource_presence_t *out_presence) {
    assert(payload_path);
    assert(out_kind);
    assert(fluf_uri_path_has(payload_path, FLUF_ID_RID));
    assert(payload_path->ids[FLUF_ID_OID] == _dm_installed_object_oid(obj));
    int result = _dm_resource_kind_and_presence(dm, obj,
                                                payload_path->ids[FLUF_ID_IID],
                                                payload_path->ids[FLUF_ID_RID],
                                                out_kind, out_presence);
    if (result) {
        return result;
    }
    if (!_dm_res_kind_writable(*out_kind)) {
        dm_log(DEBUG, "%s" _(" is not writable"),
               DM_DEBUG_MAKE_PATH(payload_path));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }

    if (fluf_uri_path_has(payload_path, FLUF_ID_RIID)
            && !_dm_res_kind_multiple(*out_kind)) {
        dm_log(DEBUG,
               _("cannot write ") "%s" _(" because the path does not point "
                                         "inside a multiple resource"),
               DM_DEBUG_MAKE_PATH(payload_path));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }

    return 0;
}

static int call_resource_write(dm_t *dm,
                               const dm_installed_object_t *obj,
                               const fluf_uri_path_t *path,
                               dm_input_ctx_t *input_ctx) {
    fluf_io_out_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    dm_input_internal_ctx_t internal_input_ctx = {
        .input_ctx = input_ctx,
        .provided_entry = &entry,
    };

    return _dm_call_resource_write(dm, obj, path->ids[FLUF_ID_IID],
                                   path->ids[FLUF_ID_RID],
                                   path->ids[FLUF_ID_RIID],
                                   &internal_input_ctx);
}

static int write_resource_instance(dm_t *dm,
                                   const dm_installed_object_t *obj,
                                   const fluf_uri_path_t *path,
                                   dm_input_ctx_t *in_ctx) {
    assert(fluf_uri_path_is(path, FLUF_ID_RIID));

    int result = _dm_verify_resource_instance_present(dm, obj,
                                                      path->ids[FLUF_ID_IID],
                                                      path->ids[FLUF_ID_RID],
                                                      path->ids[FLUF_ID_RIID]);
    if (result) {
        return result;
    }

    return call_resource_write(dm, obj, path, in_ctx);
}

static int write_single_resource(dm_t *dm,
                                 const dm_installed_object_t *obj,
                                 const fluf_uri_path_t *path,
                                 dm_input_ctx_t *in_ctx) {
    assert(fluf_uri_path_has(path, FLUF_ID_RID));
    return call_resource_write(dm, obj, path, in_ctx);
}

static int write_resource_instance_clb(dm_t *dm,
                                       const dm_installed_object_t *obj,
                                       fluf_iid_t iid,
                                       fluf_rid_t rid,
                                       fluf_riid_t riid,
                                       void *in_ctx_) {
    dm_input_ctx_t *in_ctx = (dm_input_ctx_t *) in_ctx_;
    return call_resource_write(
            dm, obj,
            &FLUF_MAKE_RESOURCE_INSTANCE_PATH(_dm_installed_object_oid(obj),
                                              iid, rid, riid),
            in_ctx);
}

static int write_multiple_resource(dm_t *dm,
                                   const dm_installed_object_t *obj,
                                   const fluf_uri_path_t *first_path,
                                   dm_input_ctx_t *in_ctx) {
    fluf_uri_path_t path = *first_path;
    assert(fluf_uri_path_has(&path, FLUF_ID_RID));
    return (_dm_foreach_resource_instance(dm, obj, first_path->ids[FLUF_ID_IID],
                                          first_path->ids[FLUF_ID_RID],
                                          write_resource_instance_clb, in_ctx));
}

static int write_resource(dm_t *dm,
                          const dm_installed_object_t *obj,
                          const fluf_uri_path_t *path,
                          dm_resource_kind_t kind,
                          dm_input_ctx_t *in_ctx) {
    if (_dm_res_kind_multiple(kind)) {
        return write_multiple_resource(dm, obj, path, in_ctx);
    }
    return write_single_resource(dm, obj, path, in_ctx);
}

static int write_instance_resource_clb(dm_t *dm,
                                       const dm_installed_object_t *obj,
                                       fluf_iid_t iid,
                                       fluf_rid_t rid,
                                       dm_resource_kind_t kind,
                                       dm_resource_presence_t presence,
                                       void *args_) {
    dm_input_ctx_t *args = (dm_input_ctx_t *) args_;
    if (presence == DM_RES_ABSENT) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" is not present, skipping"),
               _dm_installed_object_oid(obj), iid, rid);
        return 0;
    }
    bool write_allowed = _dm_res_kind_writable(kind);
    if (!write_allowed) {
        dm_log(DEBUG,
               "/%" PRIu16 "/%" PRIu16
               "/%" PRIu16 _(" is not writeable, skipping"),
               _dm_installed_object_oid(obj), iid, rid);
        return 0;
    }

    return write_resource(
            dm, obj,
            &FLUF_MAKE_RESOURCE_PATH(_dm_installed_object_oid(obj), iid, rid),
            kind, args);
}

static int write_instance(dm_t *dm,
                          const dm_installed_object_t *obj,
                          fluf_iid_t iid,
                          dm_input_ctx_t *in_ctx) {
    return _dm_foreach_resource(dm, obj, iid, write_instance_resource_clb,
                                in_ctx);
}

/** Writes to data model.
 * Actually only WRITE_TYPE_UPDATE (without creating instances or resource
 * instances) is supported. WRITE_TYPE_REPLACE is not possible. */
int dm_write(dm_t *dm, const fluf_uri_path_t *uri, dm_input_ctx_t *in_ctx) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(uri, "uri is NULL");
    AVS_ASSERT(in_ctx, "input_ctx is NULL");
    AVS_ASSERT(in_ctx->callback, "input_ctx->callback is NULL");

    dm_log(DEBUG, _("Write ") "%s", DM_DEBUG_MAKE_PATH(uri));
    if (!fluf_uri_path_has(uri, FLUF_ID_IID)) {
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }

    const dm_installed_object_t *obj;
    int result = _dm_find_object(dm, uri, &obj);
    if (result) {
        return result;
    }

    if ((result =
                 _dm_verify_instance_present(dm, obj, uri->ids[FLUF_ID_IID]))) {
        return result;
    }

    if (fluf_uri_path_is(uri, FLUF_ID_IID)) {
        return write_instance(dm, obj, uri->ids[FLUF_ID_IID], in_ctx);
    }
    dm_resource_kind_t kind;
    dm_resource_presence_t presence;
    if ((result = preverify_resource_before_writing(dm, obj, uri, &kind,
                                                    &presence))) {
        return result;
    }
    if (presence == DM_RES_ABSENT) {
        return FLUF_COAP_CODE_NOT_FOUND;
    }
    if (fluf_uri_path_is(uri, FLUF_ID_RID)) {
        return write_resource(dm, obj, uri, kind, in_ctx);
    }
    assert(fluf_uri_path_is(uri, FLUF_ID_RIID));
    return write_resource_instance(dm, obj, uri, in_ctx);
}
