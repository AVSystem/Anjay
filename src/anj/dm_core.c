/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#include "dm_core.h"
#include "dm_utils/dm_utils.h"
#include "dm_utils/dm_utils_core.h"

static int validate_version(const dm_object_def_t *const *def_ptr) {
    const char *version = (*def_ptr)->version;
    if (!version) {
        return 0;
    }
    // accepted format is X.Y where X and Y are digits
    if (!isdigit(version[0]) || version[1] != '.' || !isdigit(version[2])
            || version[3] != '\0') {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    return 0;
}

int dm_initialize(dm_t *dm, dm_installed_object_t *objects, size_t max_count) {
    dm->objects = objects;
    dm->objects_count = 0;
    dm->objects_count_max = max_count;
    return 0;
}

int dm_register_object(dm_t *dm, const dm_object_def_t *const *def_ptr) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(def_ptr, "def_ptr is NULL");

    if ((*def_ptr)->oid == FLUF_ID_INVALID) {
        dm_log(ERROR,
               _("Object ID ") "%" PRIu16 _(
                       " is forbidden by the LwM2M 1.1 specification"),
               FLUF_ID_INVALID);
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    if (dm->objects_count == dm->objects_count_max) {
        dm_log(ERROR, _("Too many objects registered"));
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    if (validate_version(def_ptr)) {
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    for (size_t i = 0; i < dm->objects_count; ++i) {
        if (_dm_installed_object_oid(&dm->objects[i]) == (*def_ptr)->oid) {
            dm_log(ERROR,
                   _("object ") "%" PRIu16 _(" is already registered"),
                   (*def_ptr)->oid);
            return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        }
    }

    size_t i;
    for (i = dm->objects_count;
         i > 0
         && _dm_installed_object_oid(&dm->objects[i - 1]) > (*def_ptr)->oid;
         --i) {
        dm->objects[i] = dm->objects[i - 1];
    }
    dm->objects[i].def = def_ptr;
    dm->objects_count++;
    dm_log(INFO, _("successfully registered object ") "/%" PRIu16,
           (*def_ptr)->oid);
    return 0;
}

int dm_unregister_object(dm_t *dm, const dm_object_def_t *const *def_ptr) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(def_ptr, "def_ptr is NULL");

    if (!_dm_find_object_by_oid(dm, (*def_ptr)->oid)) {
        dm_log(ERROR,
               _("object ") "%" PRIu16 _(" is not currently registered"),
               (*def_ptr)->oid);
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    // if def_ptr points to the last object, we don't need to move anything
    for (size_t i = 0; i < dm->objects_count - 1; ++i) {
        if (_dm_installed_object_oid(&dm->objects[i]) >= (*def_ptr)->oid) {
            dm->objects[i] = dm->objects[i + 1];
        }
    }
    dm->objects_count--;

    return 0;
}

dm_installed_object_t *_dm_find_object_by_oid(dm_t *dm, fluf_oid_t oid) {
    for (size_t i = 0; i < dm->objects_count; ++i) {
        if (_dm_installed_object_oid(&dm->objects[i]) == oid) {
            return &dm->objects[i];
        }
    }
    return NULL;
}

int _dm_verify_instance_present(dm_t *dm,
                                const dm_installed_object_t *obj,
                                fluf_iid_t iid) {
    return _dm_map_present_result(_dm_instance_present(dm, obj, iid));
}

int _dm_verify_resource_present(dm_t *dm,
                                const dm_installed_object_t *obj,
                                fluf_iid_t iid,
                                fluf_rid_t rid,
                                dm_resource_kind_t *out_kind) {
    dm_resource_presence_t presence;
    int retval = _dm_resource_kind_and_presence(dm, obj, iid, rid, out_kind,
                                                &presence);
    if (retval) {
        return retval;
    }
    if (presence == DM_RES_ABSENT) {
        return FLUF_COAP_CODE_NOT_FOUND;
    }
    return 0;
}

typedef struct {
    fluf_riid_t riid_to_find;
    bool found;
} resource_instance_present_args_t;

static int dm_resource_instance_present_clb(dm_t *dm,
                                            const dm_installed_object_t *obj,
                                            fluf_iid_t iid,
                                            fluf_rid_t rid,
                                            fluf_riid_t riid,
                                            void *args_) {
    (void) dm;
    (void) obj;
    (void) iid;
    (void) rid;
    resource_instance_present_args_t *args =
            (resource_instance_present_args_t *) args_;
    if (riid == args->riid_to_find) {
        args->found = true;
        return DM_FOREACH_BREAK;
    }
    return DM_FOREACH_CONTINUE;
}

static int dm_resource_instance_present(dm_t *dm,
                                        const dm_installed_object_t *obj,
                                        fluf_iid_t iid,
                                        fluf_rid_t rid,
                                        fluf_riid_t riid) {
    resource_instance_present_args_t args = {
        .riid_to_find = riid,
        .found = false
    };
    int result = _dm_foreach_resource_instance(
            dm, obj, iid, rid, dm_resource_instance_present_clb, &args);
    if (result < 0) {
        return result;
    }
    return args.found ? 1 : 0;
}

int _dm_verify_resource_instance_present(dm_t *dm,
                                         const dm_installed_object_t *obj,
                                         fluf_iid_t iid,
                                         fluf_rid_t rid,
                                         fluf_riid_t riid) {
    return _dm_map_present_result(
            dm_resource_instance_present(dm, obj, iid, rid, riid));
}

int _dm_foreach_object(dm_t *dm,
                       dm_foreach_object_handler_t *handler,
                       void *data) {
    for (size_t i = 0; i < dm->objects_count; ++i) {
        dm_installed_object_t *obj = &dm->objects[i];
        int result = handler(dm, obj, data);
        if (result == DM_FOREACH_BREAK) {
            dm_log(TRACE, _("foreach_object: break on ") "/%" PRIu16,
                   _dm_installed_object_oid(obj));
            return 0;
        } else if (result) {
            dm_log(DEBUG,
                   _("foreach_object_handler failed for ") "/%" PRIu16 _(
                           " (") "%d" _(")"),
                   _dm_installed_object_oid(obj), result);
            return result;
        }
    }

    return 0;
}

typedef struct {
    dm_list_ctx_emit_t *emit;
    dm_t *dm;
    const dm_installed_object_t *obj;
    int32_t last_iid;
    dm_foreach_instance_handler_t *handler;
    void *handler_data;
    int result;
} dm_foreach_instance_ctx_t;

static void foreach_instance_emit(dm_list_ctx_t *ctx_, uint16_t iid) {
    dm_foreach_instance_ctx_t *ctx = (dm_foreach_instance_ctx_t *) ctx_;
    if (ctx->result) {
        return;
    }
    if (iid == FLUF_ID_INVALID) {
        dm_log(ERROR, "%" PRIu16 _(" is not a valid Instance ID"), iid);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    if (iid <= ctx->last_iid) {
        dm_log(ERROR,
               _("list_instances MUST return Instance IDs in strictly "
                 "ascending order; ") "%" PRIu16
                       _(" returned after ") "%" PRId32,
               iid, ctx->last_iid);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    ctx->last_iid = iid;
    ctx->result = ctx->handler(ctx->dm, ctx->obj, iid, ctx->handler_data);
    if (ctx->result == DM_FOREACH_BREAK) {
        dm_log(TRACE, _("foreach_instance: break on ") "/%" PRIu16 "/%" PRIu16,
               _dm_installed_object_oid(ctx->obj), iid);
    } else if (ctx->result) {
        dm_log(DEBUG,
               _("foreach_instance_handler failed for ") "/%" PRIu16 "/%" PRIu16
                       _(" (") "%d" _(")"),
               _dm_installed_object_oid(ctx->obj), iid, ctx->result);
    }
}

int _dm_foreach_instance(dm_t *dm,
                         const dm_installed_object_t *obj,
                         dm_foreach_instance_handler_t *handler,
                         void *data) {
    if (!obj) {
        dm_log(ERROR, _("attempt to iterate through NULL Object"));
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    dm_foreach_instance_ctx_t ctx = {
        .emit = foreach_instance_emit,
        .obj = obj,
        .last_iid = -1,
        .handler = handler,
        .handler_data = data,
        .result = 0
    };
    int result = _dm_call_list_instances(dm, obj, (dm_list_ctx_t *) &ctx);
    if (result < 0) {
        dm_log(WARNING,
               _("list_instances handler for ") "/%" PRIu16 _(
                       " failed (") "%d" _(")"),
               _dm_installed_object_oid(obj), result);
        return result;
    }
    return ctx.result == DM_FOREACH_BREAK ? 0 : ctx.result;
}

typedef struct {
    fluf_iid_t iid_to_find;
    bool found;
} instance_present_args_t;

static int instance_present_clb(dm_t *dm,
                                const dm_installed_object_t *obj,
                                fluf_iid_t iid,
                                void *args_) {
    (void) dm;
    (void) obj;
    instance_present_args_t *args = (instance_present_args_t *) args_;
    if (iid >= args->iid_to_find) {
        args->found = (iid == args->iid_to_find);
        return DM_FOREACH_BREAK;
    }
    return DM_FOREACH_CONTINUE;
}

int _dm_instance_present(dm_t *dm,
                         const dm_installed_object_t *obj,
                         fluf_iid_t iid) {
    instance_present_args_t args = {
        .iid_to_find = iid,
        .found = false
    };
    int retval = _dm_foreach_instance(dm, obj, instance_present_clb, &args);
    if (retval < 0) {
        return retval;
    }
    return args.found ? 1 : 0;
}

struct dm_resource_list_ctx_struct {
    dm_t *dm;
    const dm_installed_object_t *obj;
    fluf_iid_t iid;
    int32_t last_rid;
    dm_foreach_resource_handler_t *handler;
    void *handler_data;
    int result;
};

static bool presence_valid(dm_resource_presence_t presence) {
    return presence == DM_RES_ABSENT || presence == DM_RES_PRESENT;
}

void dm_emit_res(dm_resource_list_ctx_t *ctx,
                 fluf_rid_t rid,
                 dm_resource_kind_t kind,
                 dm_resource_presence_t presence) {
    if (ctx->result) {
        return;
    }
    if (rid == FLUF_ID_INVALID) {
        dm_log(ERROR, "%" PRIu16 _(" is not a valid Resource ID"), rid);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    if (rid <= ctx->last_rid) {
        dm_log(ERROR,
               _("list_resources MUST return Resource IDs in strictly "
                 "ascending order; ") "%" PRIu16
                       _(" returned after ") "%" PRId32,
               rid, ctx->last_rid);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    ctx->last_rid = rid;
    if (!_dm_res_kind_valid(kind)) {
        dm_log(ERROR, "%d" _(" is not valid dm_resource_kind_t"), (int) kind);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    if (!presence_valid(presence)) {
        dm_log(ERROR, "%d" _(" is not valid dm_resource_presence_t"),
               (int) presence);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    ctx->result = ctx->handler(ctx->dm, ctx->obj, ctx->iid, rid, kind, presence,
                               ctx->handler_data);
    if (ctx->result == DM_FOREACH_BREAK) {
        dm_log(TRACE,
               _("foreach_resource: break on ") "/%" PRIu16 "/%" PRIu16
                                                "/%" PRIu16,
               _dm_installed_object_oid(ctx->obj), ctx->iid, rid);
    } else if (ctx->result) {
        dm_log(DEBUG,
               _("foreach_resource_handler failed for ") "/%" PRIu16 "/%" PRIu16
                                                         "/%" PRIu16
                                                                 _(" (") "%d" _(
                                                                         ")"),
               _dm_installed_object_oid(ctx->obj), ctx->iid, rid, ctx->result);
    }
}

int _dm_foreach_resource(dm_t *dm,
                         const dm_installed_object_t *obj,
                         fluf_iid_t iid,
                         dm_foreach_resource_handler_t *handler,
                         void *data) {
    if (!obj) {
        dm_log(ERROR, _("attempt to iterate through NULL Object"));
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    dm_resource_list_ctx_t resource_list_ctx = {
        .obj = obj,
        .iid = iid,
        .last_rid = -1,
        .handler = handler,
        .handler_data = data,
        .result = 0
    };
    int result = _dm_call_list_resources(dm, obj, iid, &resource_list_ctx);
    if (result < 0) {
        dm_log(ERROR,
               _("list_resources handler for ") "/%" PRIu16 "/%" PRIu16 _(
                       " failed (") "%d" _(")"),
               _dm_installed_object_oid(obj), iid, result);
        return result;
    }
    return resource_list_ctx.result == DM_FOREACH_BREAK
                   ? 0
                   : resource_list_ctx.result;
}

typedef struct {
    fluf_rid_t rid_to_find;
    dm_resource_kind_t kind;
    dm_resource_presence_t presence;
} resource_present_args_t;

static int kind_and_presence_clb(dm_t *dm,
                                 const dm_installed_object_t *obj,
                                 fluf_iid_t iid,
                                 fluf_rid_t rid,
                                 dm_resource_kind_t kind,
                                 dm_resource_presence_t presence,
                                 void *args_) {
    (void) dm;
    (void) obj;
    (void) iid;
    resource_present_args_t *args = (resource_present_args_t *) args_;
    if (rid >= args->rid_to_find) {
        if (rid == args->rid_to_find) {
            args->kind = kind;
            args->presence = presence;
        }
        return DM_FOREACH_BREAK;
    }
    return DM_FOREACH_CONTINUE;
}

int _dm_resource_kind_and_presence(dm_t *dm,
                                   const dm_installed_object_t *obj,
                                   fluf_iid_t iid,
                                   fluf_rid_t rid,
                                   dm_resource_kind_t *out_kind,
                                   dm_resource_presence_t *out_presence) {
    resource_present_args_t args = {
        .rid_to_find = rid,
        .kind = (dm_resource_kind_t) -1,
        .presence = DM_RES_ABSENT
    };
    assert(!_dm_res_kind_valid(args.kind));
    int retval =
            _dm_foreach_resource(dm, obj, iid, kind_and_presence_clb, &args);
    if (retval) {
        return retval;
    }
    // if resource not exists, _dm_foreach_resource return success but
    // kind and presence are not set
    if (args.kind == (dm_resource_kind_t) -1
            || args.presence == DM_RES_ABSENT) {
        return FLUF_COAP_CODE_NOT_FOUND;
    }
    if (!_dm_res_kind_valid(args.kind) || !presence_valid(args.presence)) {
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }
    if (out_kind) {
        *out_kind = args.kind;
    }
    if (out_presence) {
        *out_presence = args.presence;
    }
    return 0;
}

typedef struct {
    fluf_riid_t riid_to_find;
    bool found;
} resource_instance_exist_args_t;

static int resource_instance_existence_clb(dm_t *dm,
                                           const dm_installed_object_t *obj,
                                           fluf_iid_t iid,
                                           fluf_rid_t rid,
                                           fluf_riid_t riid,
                                           void *args_) {
    (void) dm;
    (void) obj;
    (void) iid;
    (void) rid;
    resource_instance_exist_args_t *args =
            (resource_instance_exist_args_t *) args_;
    if (riid == args->riid_to_find) {
        args->found = true;
        return DM_FOREACH_BREAK;
    }
    return DM_FOREACH_CONTINUE;
}

int _dm_resource_instance_existence(dm_t *dm,
                                    const dm_installed_object_t *obj,
                                    fluf_iid_t iid,
                                    fluf_rid_t rid,
                                    fluf_riid_t riid) {
    resource_instance_exist_args_t riid_args = {
        .riid_to_find = riid,
        .found = false
    };
    int result = _dm_foreach_resource_instance(
            dm, obj, iid, rid, resource_instance_existence_clb, &riid_args);
    if (result) {
        return result;
    }
    return riid_args.found ? 0 : FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
}

typedef struct {
    dm_list_ctx_emit_t *emit;
    dm_t *dm;
    const dm_installed_object_t *obj;
    fluf_iid_t iid;
    fluf_rid_t rid;
    int32_t last_riid;
    dm_foreach_resource_instance_handler_t *handler;
    void *handler_data;
    int result;
} dm_foreach_resource_instance_ctx_t;

static void foreach_resource_instance_emit(dm_list_ctx_t *ctx_, uint16_t riid) {
    dm_foreach_resource_instance_ctx_t *ctx =
            (dm_foreach_resource_instance_ctx_t *) ctx_;
    if (ctx->result) {
        return;
    }
    if (riid == FLUF_ID_INVALID) {
        dm_log(ERROR, "%" PRIu16 _(" is not a valid Resource Instance ID"),
               riid);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    if (riid <= ctx->last_riid) {
        dm_log(ERROR,
               _("list_resource_instances MUST return Resource Instance "
                 "IDs in strictly ascending order; ") "%" PRIu16
                       _(" returned after ") "%" PRId32,
               riid, ctx->last_riid);
        ctx->result = FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }
    ctx->last_riid = riid;
    ctx->result = ctx->handler(ctx->dm, ctx->obj, ctx->iid, ctx->rid, riid,
                               ctx->handler_data);
    if (ctx->result == DM_FOREACH_BREAK) {
        dm_log(TRACE,
               _("foreach_resource_instance: break on ") "/%" PRIu16 "/%" PRIu16
                                                         "/%" PRIu16
                                                         "/%" PRIu16,
               _dm_installed_object_oid(ctx->obj), ctx->iid, ctx->rid, riid);
    } else if (ctx->result) {
        dm_log(DEBUG,
               _("foreach_resource_handler failed for ") "/%" PRIu16 "/%" PRIu16
                                                         "/%" PRIu16 "/%" PRIu16
                                                                 _(" (") "%d" _(
                                                                         ")"),
               _dm_installed_object_oid(ctx->obj), ctx->iid, ctx->rid, riid,
               ctx->result);
    }
}

int _dm_foreach_resource_instance(
        dm_t *dm,
        const dm_installed_object_t *obj,
        fluf_iid_t iid,
        fluf_rid_t rid,
        dm_foreach_resource_instance_handler_t *handler,
        void *data) {
    if (!obj) {
        dm_log(ERROR, _("attempt to iterate through NULL Object"));
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    dm_foreach_resource_instance_ctx_t ctx = {
        .emit = foreach_resource_instance_emit,
        .obj = obj,
        .iid = iid,
        .rid = rid,
        .last_riid = -1,
        .handler = handler,
        .handler_data = data,
        .result = 0
    };
    int result = _dm_call_list_resource_instances(dm, obj, iid, rid,
                                                  (dm_list_ctx_t *) &ctx);
    if (result < 0) {
        dm_log(ERROR,
               _("list_resource_instances handler for ") "/%" PRIu16 "/%" PRIu16
                                                         "/%" PRIu16 _(
                                                                 " faile"
                                                                 "d (") "%d" _(")"),
               _dm_installed_object_oid(obj), iid, rid, result);
        return result;
    }
    return ctx.result == DM_FOREACH_BREAK ? 0 : ctx.result;
}

int _dm_find_object(dm_t *dm,
                    const fluf_uri_path_t *uri,
                    const dm_installed_object_t **out_obj_ptr) {
    if (!fluf_uri_path_has(uri, FLUF_ID_OID)) {
        dm_log(DEBUG, _("Provided URI does not contain Object ID"));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    if (!(*out_obj_ptr = _dm_find_object_by_oid(dm, uri->ids[FLUF_ID_OID]))) {
        dm_log(DEBUG, _("Object not found: ") "%" PRIu16,
               uri->ids[FLUF_ID_OID]);
        return FLUF_COAP_CODE_NOT_FOUND;
    }
    return 0;
}

static int
foreach_resource_instance_res_count_clb(dm_t *dm,
                                        const dm_installed_object_t *obj,
                                        fluf_iid_t iid,
                                        fluf_rid_t rid,
                                        fluf_riid_t riid,
                                        void *count_) {
    (void) dm;
    (void) obj;
    (void) iid;
    (void) rid;
    (void) riid;
    size_t *count = (size_t *) count_;
    (*count)++;
    return 0;
}

static int resource_count(dm_t *dm,
                          const dm_installed_object_t *obj,
                          fluf_iid_t iid,
                          fluf_rid_t rid,
                          fluf_riid_t riid,
                          size_t *out_count,
                          dm_resource_kind_t kind,
                          dm_resource_presence_t presence) {
    if (presence == DM_RES_ABSENT || !_dm_res_kind_readable(kind)) {
        // just skip, no error
        return 0;
    }
    if (_dm_res_kind_multiple(kind)) {
        if (riid != FLUF_ID_INVALID) {
            int result =
                    _dm_resource_instance_existence(dm, obj, iid, rid, riid);
            if (result) {
                return result;
            }
            *out_count += 1;
            return 0;
        }
        return _dm_foreach_resource_instance(
                dm, obj, iid, rid, foreach_resource_instance_res_count_clb,
                out_count);
    }
    *out_count += 1;
    return 0;
}

static int foreach_resource_res_count_clb(dm_t *dm,
                                          const dm_installed_object_t *obj,
                                          fluf_iid_t iid,
                                          fluf_rid_t rid,
                                          dm_resource_kind_t kind,
                                          dm_resource_presence_t presence,
                                          void *args) {
    return resource_count(dm, obj, iid, rid, FLUF_ID_INVALID, (size_t *) args,
                          kind, presence);
}

static int foreach_instance_res_count_clb(dm_t *dm,
                                          const dm_installed_object_t *obj,
                                          fluf_iid_t iid,
                                          void *args) {
    (void) iid;
    return _dm_foreach_resource(dm, obj, _dm_installed_object_oid(obj),
                                foreach_resource_res_count_clb, args);
}

static int foreach_object_res_count_clb(dm_t *dm,
                                        const dm_installed_object_t *obj,
                                        void *args) {
    return _dm_foreach_instance(dm, obj, foreach_instance_res_count_clb, args);
}

int dm_get_readable_res_count(dm_t *dm,
                              fluf_uri_path_t *uri,
                              size_t *out_count) {
    if (fluf_uri_path_length(uri) == 0) {
        return _dm_foreach_object(dm, foreach_object_res_count_clb, out_count);
    }

    const dm_installed_object_t *obj;
    int result = _dm_find_object(dm, uri, &obj);
    if (result) {
        return result;
    }

    if (fluf_uri_path_is(uri, FLUF_ID_OID)) {
        return _dm_foreach_instance(dm, obj, foreach_instance_res_count_clb,
                                    out_count);
    }
    if (fluf_uri_path_is(uri, FLUF_ID_IID)) {
        return _dm_foreach_resource(dm, obj, uri->ids[FLUF_ID_IID],
                                    foreach_resource_res_count_clb, out_count);
    }
    dm_resource_kind_t kind;
    dm_resource_presence_t presence;
    if ((result = _dm_resource_kind_and_presence(dm, obj, uri->ids[FLUF_ID_IID],
                                                 uri->ids[FLUF_ID_RID], &kind,
                                                 &presence))) {
        return result;
    }
    if (fluf_uri_path_is(uri, FLUF_ID_RID)) {
        return resource_count(dm, obj, uri->ids[FLUF_ID_IID],
                              uri->ids[FLUF_ID_RID], FLUF_ID_INVALID, out_count,
                              kind, presence);
    }
    return resource_count(dm, obj, uri->ids[FLUF_ID_IID], uri->ids[FLUF_ID_RID],
                          uri->ids[FLUF_ID_RIID], out_count, kind, presence);
}

static int foreach_instance_register_clb(dm_t *dm,
                                         const dm_installed_object_t *obj,
                                         fluf_iid_t iid,
                                         void *reg_ctx_) {
    (void) dm;
    dm_register_ctx_t *reg_ctx = (dm_register_ctx_t *) reg_ctx_;
    int res;
    if ((res = reg_ctx->callback(
                 reg_ctx->arg, &FLUF_MAKE_INSTANCE_PATH(
                                       _dm_installed_object_oid(obj), iid)))) {
        return res;
    }
    return 0;
}

static int foreach_object_register_clb(dm_t *dm,
                                       const dm_installed_object_t *obj,
                                       void *reg_ctx_) {
    dm_register_ctx_t *reg_ctx = (dm_register_ctx_t *) reg_ctx_;
    int res;
    if ((res = reg_ctx->callback(reg_ctx->arg,
                                 &FLUF_MAKE_OBJECT_PATH(
                                         _dm_installed_object_oid(obj))))) {
        return res;
    }
    return _dm_foreach_instance(dm, obj, foreach_instance_register_clb,
                                reg_ctx);
}

int dm_register_prepare(dm_t *dm, dm_register_ctx_t *ctx) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(ctx, "ctx is NULL");
    AVS_ASSERT(ctx->callback, "ctx->callback is NULL");
    return _dm_foreach_object(dm, foreach_object_register_clb, ctx);
}

typedef struct {
    dm_discover_ctx_t *ctx;
    fluf_id_type_t discover_to;
} dm_discover_internal_ctx_t;

static int
foreach_resource_instance_discover_clb(dm_t *dm,
                                       const dm_installed_object_t *obj,
                                       fluf_iid_t iid,
                                       fluf_rid_t rid,
                                       fluf_riid_t riid,
                                       void *disc_int_ctx_) {
    (void) dm;
    dm_discover_internal_ctx_t *disc_int_ctx =
            (dm_discover_internal_ctx_t *) disc_int_ctx_;
    int res;
    if ((res = disc_int_ctx->ctx->callback(
                 disc_int_ctx->ctx->arg,
                 &FLUF_MAKE_RESOURCE_INSTANCE_PATH(
                         _dm_installed_object_oid(obj), iid, rid, riid)))) {
        return res;
    }
    return 0;
}

static int resource_discover(dm_t *dm,
                             const dm_installed_object_t *obj,
                             fluf_iid_t iid,
                             fluf_rid_t rid,
                             dm_discover_internal_ctx_t *disc_int_ctx,
                             dm_resource_kind_t kind,
                             dm_resource_presence_t presence) {
    int res;
    if (presence == DM_RES_ABSENT) {
        return 0;
    }
    if ((res = disc_int_ctx->ctx->callback(
                 disc_int_ctx->ctx->arg,
                 &FLUF_MAKE_RESOURCE_PATH(_dm_installed_object_oid(obj), iid,
                                          rid)))) {
        return res;
    }
    if (disc_int_ctx->discover_to == FLUF_ID_RID) {
        return 0;
    }
    if (_dm_res_kind_multiple(kind)) {
        return _dm_foreach_resource_instance(
                dm, obj, iid, rid, foreach_resource_instance_discover_clb,
                disc_int_ctx);
    }
    return 0;
}

static int foreach_resource_discover_clb(dm_t *dm,
                                         const dm_installed_object_t *obj,
                                         fluf_iid_t iid,
                                         fluf_rid_t rid,
                                         dm_resource_kind_t kind,
                                         dm_resource_presence_t presence,
                                         void *disc_int_ctx_) {
    dm_discover_internal_ctx_t *disc_int_ctx =
            (dm_discover_internal_ctx_t *) disc_int_ctx_;
    return resource_discover(dm, obj, iid, rid, disc_int_ctx, kind, presence);
}

static int instance_discover(dm_t *dm,
                             const dm_installed_object_t *obj,
                             fluf_iid_t iid,
                             dm_discover_internal_ctx_t *disc_int_ctx) {
    int res;
    if ((res = _dm_verify_instance_present(dm, obj, iid))) {
        return res;
    }
    if ((res = disc_int_ctx->ctx->callback(
                 disc_int_ctx->ctx->arg,
                 &FLUF_MAKE_INSTANCE_PATH(_dm_installed_object_oid(obj),
                                          iid)))) {
        return res;
    }
    if (disc_int_ctx->discover_to == FLUF_ID_IID) {
        return 0;
    }
    return _dm_foreach_resource(dm, obj, iid, foreach_resource_discover_clb,
                                disc_int_ctx);
}

static int foreach_instance_discover_clb(dm_t *dm,
                                         const dm_installed_object_t *obj,
                                         fluf_iid_t iid,
                                         void *disc_int_ctx_) {
    dm_discover_internal_ctx_t *disc_int_ctx =
            (dm_discover_internal_ctx_t *) disc_int_ctx_;
    return instance_discover(dm, obj, iid, disc_int_ctx);
}

static fluf_id_type_t infer_depth(fluf_uri_path_t *uri, const uint8_t *depth) {
    uint8_t actual_depth;
    if (depth == NULL) {
        if (fluf_uri_path_is(uri, FLUF_ID_OID)) {
            actual_depth = 2;
        } else {
            actual_depth = 1;
        }
    } else {
        actual_depth = *depth;
    }
    AVS_ASSERT(fluf_uri_path_length(uri) != 0,
               "depth with root is not allowed by specification of discover "
               "operation");
    fluf_id_type_t type = (fluf_id_type_t) (fluf_uri_path_length(uri) - 1);
    return type + actual_depth > FLUF_ID_RIID
                   ? FLUF_ID_RIID
                   : (fluf_id_type_t) (type + actual_depth);
}

int dm_discover_resp_prepare(dm_t *dm,
                             fluf_uri_path_t *uri,
                             const uint8_t *depth,
                             dm_discover_ctx_t *ctx) {
    AVS_ASSERT(dm, "dm is NULL");
    AVS_ASSERT(uri, "uri is NULL");
    AVS_ASSERT(ctx, "ctx is NULL");
    AVS_ASSERT(ctx->callback, "ctx->callback is NULL");
    AVS_ASSERT(fluf_uri_path_length(uri) != 0, "uri can't point to root");
    AVS_ASSERT(!fluf_uri_path_is(uri, FLUF_ID_RIID),
               "uri can't point to Resource Instance");
    if (depth) {
        AVS_ASSERT(*depth <= 3, "depth can't be greater than 3");
    }

    const dm_installed_object_t *obj;
    int res = _dm_find_object(dm, uri, &obj);
    if (res) {
        return res;
    }

    dm_discover_internal_ctx_t disc_int_ctx = {
        .ctx = ctx,
        .discover_to = infer_depth(uri, depth)
    };

    if (fluf_uri_path_is(uri, FLUF_ID_OID)) {
        if ((res = ctx->callback(ctx->arg,
                                 &FLUF_MAKE_OBJECT_PATH(
                                         _dm_installed_object_oid(obj))))) {
            return res;
        }
        if (disc_int_ctx.discover_to == FLUF_ID_OID) {
            return 0;
        }
        return _dm_foreach_instance(dm, obj, foreach_instance_discover_clb,
                                    &disc_int_ctx);
    }
    if (fluf_uri_path_is(uri, FLUF_ID_IID)) {
        return instance_discover(dm, obj, uri->ids[FLUF_ID_IID], &disc_int_ctx);
    }
    dm_resource_kind_t kind;
    dm_resource_presence_t presence;
    if ((res = _dm_resource_kind_and_presence(dm, obj, uri->ids[FLUF_ID_IID],
                                              uri->ids[FLUF_ID_RID], &kind,
                                              &presence))) {
        return res;
    }
    return resource_discover(dm, obj, uri->ids[FLUF_ID_IID],
                             uri->ids[FLUF_ID_RID], &disc_int_ctx, kind,
                             presence);
}

const char *_dm_debug_make_path__(char *buffer,
                                  size_t buffer_size,
                                  const fluf_uri_path_t *uri) {
    assert(uri);
    int result = 0;
    char *ptr = buffer;
    char *buffer_end = buffer + buffer_size;
    size_t length = fluf_uri_path_length(uri);
    if (!length) {
        result = avs_simple_snprintf(buffer, buffer_size, "/");
    } else {
        for (size_t i = 0; result >= 0 && i < length; ++i) {
            result = avs_simple_snprintf(ptr, (size_t) (buffer_end - ptr),
                                         "/%" PRIu16, (unsigned) uri->ids[i]);
            ptr += result;
        }
    }
    if (result < 0) {
        AVS_UNREACHABLE("should never happen");
        return "<error>";
    }
    return buffer;
}

#ifdef UNIT_TESTING
#    include <static_functions_tests/core_statics.c>
#endif
