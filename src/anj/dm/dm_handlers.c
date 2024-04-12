/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "dm_core.h"
#include "dm_utils_core.h"

int _dm_call_list_instances(dm_t *dm,
                            const dm_installed_object_t *obj,
                            dm_list_ctx_t *instance_list_ctx) {
    dm_log(TRACE, _("list_instances ") "/%" PRIu16,
           _dm_installed_object_oid(obj));
    if (!(*obj->def)->handlers.list_instances) {
        dm_log(DEBUG,
               "list_instances" _(" handler not set for object ") "/%" PRIu16,
               _dm_installed_object_oid(obj));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    return (*obj->def)->handlers.list_instances(dm, obj->def,
                                                instance_list_ctx);
}

int _dm_call_list_resources(dm_t *dm,
                            const dm_installed_object_t *obj,
                            fluf_iid_t iid,
                            dm_resource_list_ctx_t *resource_list_ctx) {
    dm_log(TRACE, _("list_resources ") "/%" PRIu16 "/%" PRIu16,
           _dm_installed_object_oid(obj), iid);
    if (!(*obj->def)->handlers.list_resources) {
        dm_log(DEBUG,
               "list_resources" _(" handler not set for object ") "/%" PRIu16,
               _dm_installed_object_oid(obj));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    return (*obj->def)->handlers.list_resources(dm, obj->def, iid,
                                                resource_list_ctx);
}

int _dm_call_list_resource_instances(dm_t *dm,
                                     const dm_installed_object_t *obj,
                                     fluf_iid_t iid,
                                     fluf_rid_t rid,
                                     dm_list_ctx_t *list_ctx) {
    dm_log(TRACE,
           _("list_resource_instances ") "/%" PRIu16 "/%" PRIu16 "/%" PRIu16,
           _dm_installed_object_oid(obj), iid, rid);
    if (!(*obj->def)->handlers.list_resource_instances) {
        dm_log(DEBUG,
               "list_resource_instances" _(
                       " handler not set for object ") "/%" PRIu16,
               _dm_installed_object_oid(obj));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    return (*obj->def)->handlers.list_resource_instances(dm, obj->def, iid, rid,
                                                         list_ctx);
}

int _dm_call_resource_read(dm_t *dm,
                           const dm_installed_object_t *obj,
                           fluf_iid_t iid,
                           fluf_rid_t rid,
                           fluf_riid_t riid,
                           dm_output_internal_ctx_t *internal_out_ctx) {
    dm_log(TRACE, _("resource_read ") "%s",
           DM_DEBUG_MAKE_PATH(&FLUF_MAKE_RESOURCE_INSTANCE_PATH(
                   _dm_installed_object_oid(obj), iid, rid, riid)));
    if (!(*obj->def)->handlers.resource_read) {
        dm_log(DEBUG,
               "resource_read" _(" handler not set for object ") "/%" PRIu16,
               _dm_installed_object_oid(obj));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    return (*obj->def)->handlers.resource_read(
            dm, obj->def, iid, rid, riid, (dm_output_ctx_t *) internal_out_ctx);
}

int dm_list_instances_SINGLE(dm_t *dm,
                             const dm_object_def_t *const *obj_ptr,
                             dm_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    dm_emit(ctx, 0);
    return 0;
}

int _dm_call_resource_write(dm_t *dm,
                            const dm_installed_object_t *obj,
                            fluf_iid_t iid,
                            fluf_rid_t rid,
                            fluf_riid_t riid,
                            dm_input_internal_ctx_t *internal_in_ctx) {
    dm_log(TRACE, _("resource_write ") "%s",
           DM_DEBUG_MAKE_PATH(&FLUF_MAKE_RESOURCE_INSTANCE_PATH(
                   _dm_installed_object_oid(obj), iid, rid, riid)));
    if (!(*obj->def)->handlers.resource_write) {
        dm_log(DEBUG,
               "resource_read" _(" handler not set for object ") "/%" PRIu16,
               _dm_installed_object_oid(obj));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    return (*obj->def)->handlers.resource_write(
            dm, obj->def, iid, rid, riid, (dm_input_ctx_t *) internal_in_ctx);
}

int _dm_call_resource_execute(dm_t *dm,
                              const dm_installed_object_t *obj,
                              fluf_iid_t iid,
                              fluf_rid_t rid) {
    dm_log(TRACE, _("resource_execute ") "/%" PRIu16 "/%" PRIu16 "/%" PRIu16,
           _dm_installed_object_oid(obj), iid, rid);
    if (!(*obj->def)->handlers.resource_execute) {
        dm_log(DEBUG,
               "resource_read" _(" handler not set for object ") "/%" PRIu16,
               _dm_installed_object_oid(obj));
        return FLUF_COAP_CODE_METHOD_NOT_ALLOWED;
    }
    return (*obj->def)->handlers.resource_execute(dm, obj->def, iid, rid, NULL);
}
