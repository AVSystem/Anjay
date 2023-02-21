/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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

static inline bool _anjay_dm_installed_object_is_valid_unlocked(
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
#ifdef ANJAY_WITH_LWM2M11
typedef int anjay_unlocked_dm_resource_instance_read_attrs_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out);
typedef int anjay_unlocked_dm_resource_instance_write_attrs_t(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs);
#endif // ANJAY_WITH_LWM2M11
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
#    ifdef ANJAY_WITH_LWM2M11
    anjay_unlocked_dm_resource_instance_read_attrs_t
            *resource_instance_read_attrs;
    anjay_unlocked_dm_resource_instance_write_attrs_t
            *resource_instance_write_attrs;
#    endif // ANJAY_WITH_LWM2M11
} anjay_unlocked_dm_handlers_t;
#endif // ANJAY_WITH_THREAD_SAFETY

/**
 * A function to be called when the module is uninstalled, that will clean up
 * any resources used by it.
 */
typedef void anjay_dm_module_deleter_t(void *arg);

/**
 * Installs an Anjay module. In practice this just means registering an
 * arbitrary function to be called during <c>anjay_delete()</c>.
 *
 * @param anjay  Anjay object to operate on
 *
 * @param module_deleter Pointer to a module deleter function; this pointer is
 *                       also used as a module identifier
 *
 * @param arg    Opaque pointer that can be later retrieved using
 *               @ref _anjay_dm_module_get_arg and will also be passed to the
 *               <c>notify_callback</c> and <c>deleter</c> functions.
 *
 * @returns 0 for success, or a negative value in case of error.
 */
int _anjay_dm_module_install(anjay_unlocked_t *anjay,
                             anjay_dm_module_deleter_t *module_deleter,
                             void *arg);

int _anjay_dm_module_uninstall(anjay_unlocked_t *anjay,
                               anjay_dm_module_deleter_t *module_deleter);

/**
 * Returns the <c>arg</c> previously passed to @ref _anjay_dm_module_install
 */
void *_anjay_dm_module_get_arg(anjay_unlocked_t *anjay,
                               anjay_dm_module_deleter_t *module_deleter);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_MODULES_H */
