/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_DM_UTILS_H
#define ANJAY_DM_DM_UTILS_H

#include <assert.h>
#include <limits.h>

#include <avsystem/commons/avs_defs.h>

#include <anj/dm.h>

#include "dm_utils_core.h"

typedef struct {
    dm_output_ctx_t *output_ctx;
    fluf_uri_path_t path;
} dm_output_internal_ctx_t;

typedef struct {
    dm_input_ctx_t *input_ctx;
    bool callback_called_flag;
    fluf_io_out_entry_t *provided_entry;
    size_t buff_indicator;
} dm_input_internal_ctx_t;

const char *_dm_debug_make_path__(char *buffer,
                                  size_t buffer_size,
                                  const fluf_uri_path_t *uri);

#define DM_DEBUG_MAKE_PATH(path) \
    (_dm_debug_make_path__(&(char[32]){ 0 }[0], 32, (path)))

typedef int dm_foreach_object_handler_t(dm_t *dm,
                                        const dm_installed_object_t *obj,
                                        void *data);

int _dm_foreach_object(dm_t *dm,
                       dm_foreach_object_handler_t *handler,
                       void *data);

typedef int dm_foreach_instance_handler_t(dm_t *dm,
                                          const dm_installed_object_t *obj,
                                          fluf_iid_t iid,
                                          void *data);

int _dm_foreach_instance(dm_t *dm,
                         const dm_installed_object_t *obj,
                         dm_foreach_instance_handler_t *handler,
                         void *data);

int _dm_instance_present(dm_t *dm,
                         const dm_installed_object_t *obj,
                         fluf_iid_t iid);

typedef int dm_foreach_resource_handler_t(dm_t *dm,
                                          const dm_installed_object_t *obj,
                                          fluf_iid_t iid,
                                          fluf_rid_t rid,
                                          dm_resource_kind_t kind,
                                          dm_resource_presence_t presence,
                                          void *data);

int _dm_foreach_resource(dm_t *dm,
                         const dm_installed_object_t *obj,
                         fluf_iid_t iid,
                         dm_foreach_resource_handler_t *handler,
                         void *data);

/**
 * Checks if the specific resource is supported and present, and what is its
 * kind. This function internally calls @ref _dm_foreach_resource, so it
 * is not optimal to use for multiple resources within the same Object Instance.
 *
 * NOTE: It is REQUIRED that the presence of the Object and Object Instance is
 * checked beforehand, this function does not perform such checks.
 *
 * @param dm           Data model object to operate on
 * @param obj          Definition of an Object in which the queried resource is
 * @param iid          ID of Object Instance in which the queried resource is
 * @param rid          ID of the queried Resource
 * @param out_kind     Pointer to a variable in which the kind of the resource
 *                     will be stored. May be NULL, in which case only the
 *                     presence is checked.
 * @param out_presence Pointer to a variable in which the presence information
 *                     about the resource will be stored. May be NULL, in which
 *                     case only the kind is checked.
 *
 * @returns 0 for success, or a non-zero error code in case of error.
 *
 * NOTE: Two scenarios are possible if the resource is not currently present in
 * the object:
 * - If the resource is not Supported (i.e., it has not been enumerated by the
 *   list_resources handler at all), the function fails, returning
 *   FLUF_COAP_CODE_NOT_FOUND
 * - If the resource is Supported, but not Present (i.e., it has been enumerated
 *   by the list_resources handler with presence set to DM_RES_ABSENT),
 *   the function succeeds (returns 0), but *out_presence is set to
 *   DM_RES_ABSENT
 */
int _dm_resource_kind_and_presence(dm_t *dm,
                                   const dm_installed_object_t *obj,
                                   fluf_iid_t iid,
                                   fluf_rid_t rid,
                                   dm_resource_kind_t *out_kind,
                                   dm_resource_presence_t *out_presence);

int _dm_resource_instance_existence(dm_t *dm,
                                    const dm_installed_object_t *obj,
                                    fluf_iid_t iid,
                                    fluf_rid_t rid,
                                    fluf_riid_t riid);

typedef int
dm_foreach_resource_instance_handler_t(dm_t *dm,
                                       const dm_installed_object_t *obj,
                                       fluf_iid_t iid,
                                       fluf_rid_t rid,
                                       fluf_riid_t riid,
                                       void *data);

int _dm_foreach_resource_instance(
        dm_t *dm,
        const dm_installed_object_t *obj,
        fluf_iid_t iid,
        fluf_rid_t rid,
        dm_foreach_resource_instance_handler_t *handler,
        void *data);

static inline bool _dm_res_kind_valid(dm_resource_kind_t kind) {
    return kind == DM_RES_R || kind == DM_RES_W || kind == DM_RES_RW
           || kind == DM_RES_RM || kind == DM_RES_WM || kind == DM_RES_RWM
           || kind == DM_RES_E;
}

static inline bool _dm_res_kind_readable(dm_resource_kind_t kind) {
    return kind == DM_RES_R || kind == DM_RES_RW || kind == DM_RES_RM
           || kind == DM_RES_RWM;
}

static inline bool _dm_res_kind_writable(dm_resource_kind_t kind) {
    return kind == DM_RES_W || kind == DM_RES_RW || kind == DM_RES_WM
           || kind == DM_RES_RWM;
}

static inline bool _dm_res_kind_executable(dm_resource_kind_t kind) {
    return kind == DM_RES_E;
}

static inline bool _dm_res_kind_multiple(dm_resource_kind_t kind) {
    return kind == DM_RES_RM || kind == DM_RES_WM || kind == DM_RES_RWM;
}

int _dm_call_list_instances(dm_t *dm,
                            const dm_installed_object_t *obj,
                            dm_list_ctx_t *instance_list_ctx);
int _dm_call_list_resources(dm_t *dm,
                            const dm_installed_object_t *obj,
                            fluf_iid_t iid,
                            dm_resource_list_ctx_t *resource_list_ctx);

int _dm_call_resource_read(dm_t *dm,
                           const dm_installed_object_t *obj,
                           fluf_iid_t iid,
                           fluf_rid_t rid,
                           fluf_riid_t riid,
                           dm_output_internal_ctx_t *internal_out_ctx);
int _dm_call_resource_write(dm_t *dm,
                            const dm_installed_object_t *obj,
                            fluf_iid_t iid,
                            fluf_rid_t rid,
                            fluf_riid_t riid,
                            dm_input_internal_ctx_t *internal_in_ctx);
int _dm_call_resource_execute(dm_t *dm,
                              const dm_installed_object_t *obj,
                              fluf_iid_t iid,
                              fluf_rid_t rid);
int _dm_call_list_resource_instances(dm_t *dm,
                                     const dm_installed_object_t *obj,
                                     fluf_iid_t iid,
                                     fluf_rid_t rid,
                                     dm_list_ctx_t *list_ctx);

dm_installed_object_t *_dm_find_object_by_oid(dm_t *dm, fluf_oid_t oid);
int _dm_verify_resource_present(dm_t *dm,
                                const dm_installed_object_t *obj,
                                fluf_iid_t iid,
                                fluf_rid_t rid,
                                dm_resource_kind_t *out_kind);

int _dm_verify_resource_instance_present(dm_t *dm,
                                         const dm_installed_object_t *obj,
                                         fluf_iid_t iid,
                                         fluf_rid_t rid,
                                         fluf_riid_t riid);

int _dm_verify_instance_present(dm_t *dm,
                                const dm_installed_object_t *obj,
                                fluf_iid_t iid);

static inline fluf_oid_t
_dm_installed_object_oid(const dm_installed_object_t *obj) {
    assert(obj);
    assert((*obj).def);
    return (*obj->def)->oid;
}

static inline const char *
_dm_installed_object_version(const dm_installed_object_t *obj) {
    assert(obj);
    assert((*obj).def);
    return (*obj->def)->version;
}

int _dm_find_object(dm_t *dm,
                    const fluf_uri_path_t *uri,
                    const dm_installed_object_t **out_obj_ptr);

#endif /* ANJAY_DM_DM_UTILS_H */
