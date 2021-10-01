/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H

#include <anjay/dm.h>

#include <anjay_modules/anjay_notify.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_THREAD_SAFETY

typedef struct anjay_unlocked_dm_object_def_struct
        anjay_unlocked_dm_object_def_t;

typedef struct anjay_unlocked_dm_list_ctx_struct anjay_unlocked_dm_list_ctx_t;
typedef struct anjay_unlocked_dm_resource_list_ctx_struct
        anjay_unlocked_dm_resource_list_ctx_t;
typedef struct anjay_unlocked_output_ctx_struct anjay_unlocked_output_ctx_t;
typedef struct anjay_unlocked_ret_bytes_ctx_struct
        anjay_unlocked_ret_bytes_ctx_t;
typedef struct anjay_unlocked_input_ctx_struct anjay_unlocked_input_ctx_t;
typedef struct anjay_unlocked_execute_ctx_struct anjay_unlocked_execute_ctx_t;

typedef enum {
    ANJAY_DM_OBJECT_USER_PROVIDED,
    ANJAY_DM_OBJECT_UNLOCKED
} anjay_dm_installed_object_type_t;

typedef struct {
    anjay_dm_installed_object_type_t type;
    union {
        const anjay_dm_object_def_t *const *user_provided;
        const anjay_unlocked_dm_object_def_t *const *unlocked;
    } impl;
} anjay_dm_installed_object_t;

static inline void _anjay_dm_installed_object_init_unlocked(
        anjay_dm_installed_object_t *obj_ptr,
        const anjay_unlocked_dm_object_def_t *const *def_ptr) {
    assert(obj_ptr);
    assert(!obj_ptr->impl.unlocked);
    obj_ptr->type = ANJAY_DM_OBJECT_UNLOCKED;
    obj_ptr->impl.unlocked = def_ptr;
}

static inline const anjay_unlocked_dm_object_def_t *const *
_anjay_dm_installed_object_get_unlocked(
        const anjay_dm_installed_object_t *obj_ptr) {
    assert(obj_ptr);
    assert(obj_ptr->type == ANJAY_DM_OBJECT_UNLOCKED);
    assert(obj_ptr->impl.unlocked);
    assert(*obj_ptr->impl.unlocked);
    return obj_ptr->impl.unlocked;
}

static inline bool _anjay_dm_installed_object_is_valid_unlocked(
        const anjay_dm_installed_object_t *obj_ptr) {
    return obj_ptr && (obj_ptr->type == ANJAY_DM_OBJECT_UNLOCKED)
           && obj_ptr->impl.unlocked && (*obj_ptr->impl.unlocked);
}

#else // ANJAY_WITH_THREAD_SAFETY

typedef anjay_dm_object_def_t anjay_unlocked_dm_object_def_t;
typedef const anjay_dm_object_def_t *const *anjay_dm_installed_object_t;

static inline void _anjay_dm_installed_object_init_unlocked(
        anjay_dm_installed_object_t *obj_ptr,
        const anjay_unlocked_dm_object_def_t *const *def_ptr) {
    assert(obj_ptr);
    assert(!*obj_ptr);
    *obj_ptr = def_ptr;
}

static inline const anjay_unlocked_dm_object_def_t *const *
_anjay_dm_installed_object_get_unlocked(
        const anjay_dm_installed_object_t *obj_ptr) {
    assert(obj_ptr);
    assert(*obj_ptr);
    assert(**obj_ptr);
    return *obj_ptr;
}

static inline const bool _anjay_dm_installed_object_is_valid_unlocked(
        const anjay_dm_installed_object_t *obj_ptr) {
    return obj_ptr && (*obj_ptr) && (**obj_ptr);
}

typedef anjay_dm_list_ctx_t anjay_unlocked_dm_list_ctx_t;
typedef anjay_dm_resource_list_ctx_t anjay_unlocked_dm_resource_list_ctx_t;
typedef anjay_output_ctx_t anjay_unlocked_output_ctx_t;
typedef anjay_ret_bytes_ctx_t anjay_unlocked_ret_bytes_ctx_t;
typedef anjay_input_ctx_t anjay_unlocked_input_ctx_t;
typedef anjay_execute_ctx_t anjay_unlocked_execute_ctx_t;

typedef anjay_dm_handlers_t anjay_unlocked_dm_handlers_t;

#endif // ANJAY_WITH_THREAD_SAFETY

typedef int anjay_unlocked_dm_object_read_default_attrs_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out);
typedef int anjay_unlocked_dm_object_write_default_attrs_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs);
typedef int
anjay_unlocked_dm_list_instances_t(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj,
                                   anjay_unlocked_dm_list_ctx_t *ctx);
typedef int
anjay_unlocked_dm_instance_reset_t(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj,
                                   anjay_iid_t iid);
typedef int
anjay_unlocked_dm_instance_remove_t(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj,
                                    anjay_iid_t iid);
typedef int
anjay_unlocked_dm_instance_create_t(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj,
                                    anjay_iid_t iid);
typedef int anjay_unlocked_dm_instance_read_default_attrs_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out);
typedef int anjay_unlocked_dm_instance_write_default_attrs_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs);
typedef int
anjay_unlocked_dm_list_resources_t(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj,
                                   anjay_iid_t iid,
                                   anjay_unlocked_dm_resource_list_ctx_t *ctx);
typedef int
anjay_unlocked_dm_resource_read_t(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_unlocked_output_ctx_t *ctx);
typedef int
anjay_unlocked_dm_resource_write_t(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   anjay_unlocked_input_ctx_t *ctx);
typedef int
anjay_unlocked_dm_resource_execute_t(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t obj,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     anjay_unlocked_execute_ctx_t *ctx);
typedef int
anjay_unlocked_dm_resource_reset_t(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid);
typedef int anjay_unlocked_dm_list_resource_instances_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_unlocked_dm_list_ctx_t *ctx);
typedef int
anjay_unlocked_dm_resource_read_attrs_t(anjay_unlocked_t *anjay,
                                        const anjay_dm_installed_object_t obj,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid,
                                        anjay_ssid_t ssid,
                                        anjay_dm_r_attributes_t *out);
typedef int
anjay_unlocked_dm_resource_write_attrs_t(anjay_unlocked_t *anjay,
                                         const anjay_dm_installed_object_t obj,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid,
                                         anjay_ssid_t ssid,
                                         const anjay_dm_r_attributes_t *attrs);
typedef int
anjay_unlocked_dm_transaction_begin_t(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj);
typedef int
anjay_unlocked_dm_transaction_validate_t(anjay_unlocked_t *anjay,
                                         const anjay_dm_installed_object_t obj);
typedef int
anjay_unlocked_dm_transaction_commit_t(anjay_unlocked_t *anjay,
                                       const anjay_dm_installed_object_t obj);
typedef int
anjay_unlocked_dm_transaction_rollback_t(anjay_unlocked_t *anjay,
                                         const anjay_dm_installed_object_t obj);

#ifdef ANJAY_WITH_THREAD_SAFETY
typedef struct {
    anjay_unlocked_dm_object_read_default_attrs_t *object_read_default_attrs;
    anjay_unlocked_dm_object_write_default_attrs_t *object_write_default_attrs;
    anjay_unlocked_dm_list_instances_t *list_instances;
    anjay_unlocked_dm_instance_reset_t *instance_reset;
    anjay_unlocked_dm_instance_create_t *instance_create;
    anjay_unlocked_dm_instance_remove_t *instance_remove;
    anjay_unlocked_dm_instance_read_default_attrs_t
            *instance_read_default_attrs;
    anjay_unlocked_dm_instance_write_default_attrs_t
            *instance_write_default_attrs;
    anjay_unlocked_dm_list_resources_t *list_resources;
    anjay_unlocked_dm_resource_read_t *resource_read;
    anjay_unlocked_dm_resource_write_t *resource_write;
    anjay_unlocked_dm_resource_execute_t *resource_execute;
    anjay_unlocked_dm_resource_reset_t *resource_reset;
    anjay_unlocked_dm_list_resource_instances_t *list_resource_instances;
    anjay_unlocked_dm_resource_read_attrs_t *resource_read_attrs;
    anjay_unlocked_dm_resource_write_attrs_t *resource_write_attrs;
    anjay_unlocked_dm_transaction_begin_t *transaction_begin;
    anjay_unlocked_dm_transaction_validate_t *transaction_validate;
    anjay_unlocked_dm_transaction_commit_t *transaction_commit;
    anjay_unlocked_dm_transaction_rollback_t *transaction_rollback;
} anjay_unlocked_dm_handlers_t;
#endif // ANJAY_WITH_THREAD_SAFETY

typedef void anjay_dm_module_deleter_t(void *arg);

typedef struct {
    /**
     * Global overlay of handlers that may replace handlers natively declared
     * for all LwM2M Objects.
     *
     * When modules are installed, upon calling any of the <c>_anjay_dm_*</c>
     * callback wrapper functions with the <c>current_module</c> argument set to
     * <c>NULL</c>, the following happens:
     *
     * - The installed modules are searched in such order so that most recently
     *   installed module is first.
     * - The first module in which the appropriate handler field (e.g.
     *   <c>overlay_handlers.resource_read</c>) is non-<c>NULL</c>, is selected.
     * - If no such overlay is installed, the function declared in the LwM2M
     *   Object is selected, if non-<c>NULL</c>.
     * - If a function could be selected, it is called.
     * - If no appropriate non-<c>NULL</c> handler was found,
     *   @ref ANJAY_ERR_METHOD_NOT_ALLOWED is returned.
     *
     * Functions declared in an overlay handler may call the appropriate
     * <c>_anjay_dm_*</c> function (e.g. @ref _anjay_dm_resource_read) with the
     * <c>current_module</c> argument set to its own corresponding module
     * definition pointer (the same value that was passed as <c>module</c> to
     * <c>_anjay_dm_module_install</c>). This will cause the "underlying"
     * handler to be called - the above procedure will restart, but skipping all
     * the modules from the beginning to <c>current_module</c>, inclusive - so
     * the underlying original implementation (or another overlay, if multiple
     * are installed) will be called.
     */
    anjay_unlocked_dm_handlers_t overlay_handlers;

    /**
     * A function to be called every time when Anjay is notified of some data
     * model change - this also includes changes made through LwM2M protocol
     * itself.
     */
    anjay_notify_callback_t *notify_callback;

    /**
     * A function to be called when the module is uninstalled, that will clean
     * up any resources used by it.
     */
    anjay_dm_module_deleter_t *deleter;
} anjay_dm_module_t;

/**
 * Installs an Anjay module. See the definition of fields in
 * @ref anjay_dm_module_t for a detailed explanation of what modules can do.
 *
 * @param anjay  Anjay object to operate on
 *
 * @param module Pointer to a module definition structure; this pointer is also
 *               used as a module identifier, so it needs to have at least the
 *               same lifetime as the module itself, and normally it is a
 *               singleton with static lifetime
 *
 * @param arg    Opaque pointer that can be later retrieved using
 *               @ref _anjay_dm_module_get_arg and will also be passed to the
 *               <c>notify_callback</c> and <c>deleter</c> functions.
 *
 * @returns 0 for success, or a negative value in case of error.
 */
int _anjay_dm_module_install(anjay_unlocked_t *anjay,
                             const anjay_dm_module_t *module,
                             void *arg);

int _anjay_dm_module_uninstall(anjay_unlocked_t *anjay,
                               const anjay_dm_module_t *module);

/**
 * Returns the <c>arg</c> previously passed to @ref _anjay_dm_module_install
 */
void *_anjay_dm_module_get_arg(anjay_unlocked_t *anjay,
                               const anjay_dm_module_t *module);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H */
