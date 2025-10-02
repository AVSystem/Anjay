/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/coap/code.h>

#include <anjay_modules/anjay_dm_utils.h>

#include "../anjay_core.h"
#include "../anjay_io_core.h"
#include "../anjay_utils_private.h"

#ifdef ANJAY_WITH_ATTR_STORAGE
#    include "../attr_storage/anjay_attr_storage.h"
#endif // ANJAY_WITH_ATTR_STORAGE
#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    include "anjay_modules/anjay_lwm2m_gateway.h"
#endif // ANJAY_WITH_LWM2M_GATEWAY

VISIBILITY_SOURCE_BEGIN

#define dm_log(...) _anjay_log(anjay_dm, __VA_ARGS__)

#ifdef ANJAY_WITH_THREAD_SAFETY

static int
unlocking_object_read_default_attrs(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_def,
                                    anjay_ssid_t ssid,
                                    anjay_dm_oi_attributes_t *out) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.object_read_default_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.object_read_default_attrs(
                             anjay_locked, obj_def.impl.user_provided, ssid,
                             out);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_object_write_default_attrs(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t obj_def,
                                     anjay_ssid_t ssid,
                                     const anjay_dm_oi_attributes_t *attrs) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.object_write_default_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.object_write_default_attrs(
                             anjay_locked, obj_def.impl.user_provided, ssid,
                             attrs);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_list_instances(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_def,
                                    anjay_unlocked_dm_list_ctx_t *ctx) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.list_instances);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.list_instances(anjay_locked,
                                               obj_def.impl.user_provided,
                                               &(anjay_dm_list_ctx_t) {
                                                   .anjay_locked = anjay_locked,
                                                   .unlocked_ctx = ctx
                                               });
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_instance_reset(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_def,
                                    anjay_iid_t iid) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.instance_reset);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.instance_reset(anjay_locked,
                                               obj_def.impl.user_provided, iid);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_instance_create(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t obj_def,
                                     anjay_iid_t iid) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.instance_create);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result =
            (*obj_def.impl.user_provided)
                    ->handlers.instance_create(anjay_locked,
                                               obj_def.impl.user_provided, iid);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_instance_remove(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t obj_def,
                                     anjay_iid_t iid) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.instance_remove);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result =
            (*obj_def.impl.user_provided)
                    ->handlers.instance_remove(anjay_locked,
                                               obj_def.impl.user_provided, iid);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_instance_read_default_attrs(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj_def,
                                      anjay_iid_t iid,
                                      anjay_ssid_t ssid,
                                      anjay_dm_oi_attributes_t *out) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.instance_read_default_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.instance_read_default_attrs(
                             anjay_locked, obj_def.impl.user_provided, iid,
                             ssid, out);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_instance_write_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj_def,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)
                   ->handlers.instance_write_default_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.instance_write_default_attrs(
                             anjay_locked, obj_def.impl.user_provided, iid,
                             ssid, attrs);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_list_resources(anjay_unlocked_t *anjay,
                         const anjay_dm_installed_object_t obj_def,
                         anjay_iid_t iid,
                         anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.list_resources);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.list_resources(anjay_locked,
                                               obj_def.impl.user_provided, iid,
                                               &(anjay_dm_resource_list_ctx_t) {
                                                   .unlocked_ctx = ctx
                                               });
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_resource_read(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj_def,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   anjay_unlocked_output_ctx_t *ctx) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.resource_read);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_read(anjay_locked,
                                              obj_def.impl.user_provided, iid,
                                              rid, riid,
                                              &(anjay_output_ctx_t) {
                                                  .anjay_locked = anjay_locked,
                                                  .unlocked_ctx = ctx
                                              });
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_resource_write(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_def,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_riid_t riid,
                                    anjay_unlocked_input_ctx_t *ctx) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.resource_write);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_write(anjay_locked,
                                               obj_def.impl.user_provided, iid,
                                               rid, riid,
                                               &(anjay_input_ctx_t) {
                                                   .anjay_locked = anjay_locked,
                                                   .unlocked_ctx = ctx
                                               });
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_resource_execute(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj_def,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_unlocked_execute_ctx_t *ctx) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.resource_execute);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_execute(
                             anjay_locked, obj_def.impl.user_provided, iid, rid,
                             &(anjay_execute_ctx_t) {
                                 .anjay_locked = anjay_locked,
                                 .unlocked_ctx = ctx
                             });
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_resource_reset(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_def,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.resource_reset);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_reset(anjay_locked,
                                               obj_def.impl.user_provided, iid,
                                               rid);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_list_resource_instances(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_def,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_unlocked_dm_list_ctx_t *ctx) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.list_resource_instances);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.list_resource_instances(
                             anjay_locked, obj_def.impl.user_provided, iid, rid,
                             &(anjay_dm_list_ctx_t) {
                                 .anjay_locked = anjay_locked,
                                 .unlocked_ctx = ctx
                             });
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_resource_read_attrs(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_def,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_ssid_t ssid,
                              anjay_dm_r_attributes_t *out) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.resource_read_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_read_attrs(anjay_locked,
                                                    obj_def.impl.user_provided,
                                                    iid, rid, ssid, out);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_resource_write_attrs(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_def,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_ssid_t ssid,
                               const anjay_dm_r_attributes_t *attrs) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.resource_write_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_write_attrs(anjay_locked,
                                                     obj_def.impl.user_provided,
                                                     iid, rid, ssid, attrs);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_transaction_begin(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t obj_def) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.transaction_begin);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.transaction_begin(anjay_locked,
                                                  obj_def.impl.user_provided);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_transaction_validate(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_def) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.transaction_validate);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result =
            (*obj_def.impl.user_provided)
                    ->handlers.transaction_validate(anjay_locked,
                                                    obj_def.impl.user_provided);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_transaction_commit(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_def) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.transaction_commit);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.transaction_commit(anjay_locked,
                                                   obj_def.impl.user_provided);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int
unlocking_transaction_rollback(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_def) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)->handlers.transaction_rollback);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result =
            (*obj_def.impl.user_provided)
                    ->handlers.transaction_rollback(anjay_locked,
                                                    obj_def.impl.user_provided);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

#    ifdef ANJAY_WITH_LWM2M11
static int unlocking_resource_instance_read_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj_def,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)
                   ->handlers.resource_instance_read_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_instance_read_attrs(
                             anjay_locked, obj_def.impl.user_provided, iid, rid,
                             riid, ssid, out);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int unlocking_resource_instance_write_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t obj_def,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs) {
    assert(obj_def.type == ANJAY_DM_OBJECT_USER_PROVIDED);
    assert(obj_def.impl.user_provided);
    assert(*obj_def.impl.user_provided);
    assert((*obj_def.impl.user_provided)
                   ->handlers.resource_instance_write_attrs);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = (*obj_def.impl.user_provided)
                     ->handlers.resource_instance_write_attrs(
                             anjay_locked, obj_def.impl.user_provided, iid, rid,
                             riid, ssid, attrs);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}
#    endif // ANJAY_WITH_LWM2M11

static const anjay_unlocked_dm_handlers_t UNLOCKING_HANDLER_WRAPPERS = {
    unlocking_object_read_default_attrs,
    unlocking_object_write_default_attrs,
    unlocking_list_instances,
    unlocking_instance_reset,
    unlocking_instance_create,
    unlocking_instance_remove,
    unlocking_instance_read_default_attrs,
    unlocking_instance_write_default_attrs,
    unlocking_list_resources,
    unlocking_resource_read,
    unlocking_resource_write,
    unlocking_resource_execute,
    unlocking_resource_reset,
    unlocking_list_resource_instances,
    unlocking_resource_read_attrs,
    unlocking_resource_write_attrs,
    unlocking_transaction_begin,
    unlocking_transaction_validate,
    unlocking_transaction_commit,
    unlocking_transaction_rollback,
#    ifdef ANJAY_WITH_LWM2M11
    unlocking_resource_instance_read_attrs,
    unlocking_resource_instance_write_attrs,
#    endif // ANJAY_WITH_LWM2M11
};

static bool has_handler_locked(const anjay_dm_handlers_t *def,
                               anjay_dm_handler_t handler_type) {
#    define HANDLER_CASE(HandlerName)    \
    case ANJAY_DM_HANDLER_##HandlerName: \
        return def->HandlerName

    switch (handler_type) {
        HANDLER_CASE(object_read_default_attrs);
        HANDLER_CASE(object_write_default_attrs);
        HANDLER_CASE(list_instances);
        HANDLER_CASE(instance_reset);
        HANDLER_CASE(instance_create);
        HANDLER_CASE(instance_remove);
        HANDLER_CASE(instance_read_default_attrs);
        HANDLER_CASE(instance_write_default_attrs);
        HANDLER_CASE(list_resources);
        HANDLER_CASE(resource_read);
        HANDLER_CASE(resource_write);
        HANDLER_CASE(resource_execute);
        HANDLER_CASE(resource_reset);
        HANDLER_CASE(list_resource_instances);
        HANDLER_CASE(resource_read_attrs);
        HANDLER_CASE(resource_write_attrs);
        HANDLER_CASE(transaction_begin);
        HANDLER_CASE(transaction_validate);
        HANDLER_CASE(transaction_commit);
        HANDLER_CASE(transaction_rollback);
#    ifdef ANJAY_WITH_LWM2M11
        HANDLER_CASE(resource_instance_read_attrs);
        HANDLER_CASE(resource_instance_write_attrs);
#    endif // ANJAY_WITH_LWM2M11
    }
#    undef HANDLER_CASE
    AVS_UNREACHABLE("unknown handler type passed");
    return false;
}

#endif // ANJAY_WITH_THREAD_SAFETY

static bool has_handler_unlocked(const anjay_unlocked_dm_handlers_t *def,
                                 anjay_dm_handler_t handler_type) {
#define HANDLER_CASE(HandlerName)        \
    case ANJAY_DM_HANDLER_##HandlerName: \
        return def->HandlerName

    switch (handler_type) {
        HANDLER_CASE(object_read_default_attrs);
        HANDLER_CASE(object_write_default_attrs);
        HANDLER_CASE(list_instances);
        HANDLER_CASE(instance_reset);
        HANDLER_CASE(instance_create);
        HANDLER_CASE(instance_remove);
        HANDLER_CASE(instance_read_default_attrs);
        HANDLER_CASE(instance_write_default_attrs);
        HANDLER_CASE(list_resources);
        HANDLER_CASE(resource_read);
        HANDLER_CASE(resource_write);
        HANDLER_CASE(resource_execute);
        HANDLER_CASE(resource_reset);
        HANDLER_CASE(list_resource_instances);
        HANDLER_CASE(resource_read_attrs);
        HANDLER_CASE(resource_write_attrs);
        HANDLER_CASE(transaction_begin);
        HANDLER_CASE(transaction_validate);
        HANDLER_CASE(transaction_commit);
        HANDLER_CASE(transaction_rollback);
#ifdef ANJAY_WITH_LWM2M11
        HANDLER_CASE(resource_instance_read_attrs);
        HANDLER_CASE(resource_instance_write_attrs);
#endif // ANJAY_WITH_LWM2M11
    }
#undef HANDLER_CASE
    AVS_UNREACHABLE("unknown handler type passed");
    return false;
}

static const anjay_unlocked_dm_handlers_t *
get_handler(const anjay_dm_installed_object_t *obj_ptr,
            anjay_dm_handler_t handler_type) {
#ifdef ANJAY_WITH_ATTR_STORAGE
    switch (handler_type) {
    case ANJAY_DM_HANDLER_object_read_default_attrs:
    case ANJAY_DM_HANDLER_object_write_default_attrs:
        if (!_anjay_dm_implements_any_object_default_attrs_handlers(obj_ptr)) {
            return &_ANJAY_ATTR_STORAGE_HANDLERS;
        }
        break;

    case ANJAY_DM_HANDLER_instance_read_default_attrs:
    case ANJAY_DM_HANDLER_instance_write_default_attrs:
        if (!_anjay_dm_implements_any_instance_default_attrs_handlers(
                    obj_ptr)) {
            return &_ANJAY_ATTR_STORAGE_HANDLERS;
        }
        break;

    case ANJAY_DM_HANDLER_resource_read_attrs:
    case ANJAY_DM_HANDLER_resource_write_attrs:
        if (!_anjay_dm_implements_any_resource_attrs_handlers(obj_ptr)) {
            return &_ANJAY_ATTR_STORAGE_HANDLERS;
        }
        break;

#    ifdef ANJAY_WITH_LWM2M11
    case ANJAY_DM_HANDLER_resource_instance_read_attrs:
    case ANJAY_DM_HANDLER_resource_instance_write_attrs:
        if (!_anjay_dm_implements_any_resource_instance_attrs_handlers(
                    obj_ptr)) {
            return &_ANJAY_ATTR_STORAGE_HANDLERS;
        }
        break;
#    endif // ANJAY_WITH_LWM2M11

    default:
        break;
    }
#endif // ANJAY_WITH_ATTR_STORAGE
#ifdef ANJAY_WITH_THREAD_SAFETY
    switch (obj_ptr->type) {
    case ANJAY_DM_OBJECT_USER_PROVIDED:
        assert(obj_ptr->impl.user_provided);
        assert(*obj_ptr->impl.user_provided);
        if (has_handler_locked(&(*obj_ptr->impl.user_provided)->handlers,
                               handler_type)) {
            return &UNLOCKING_HANDLER_WRAPPERS;
        }
        return NULL;

    case ANJAY_DM_OBJECT_UNLOCKED:
        assert(obj_ptr->impl.unlocked);
        assert(*obj_ptr->impl.unlocked);
        if (has_handler_unlocked(&(*obj_ptr->impl.unlocked)->handlers,
                                 handler_type)) {
            return &(*obj_ptr->impl.unlocked)->handlers;
        }
        return NULL;
    }
    AVS_UNREACHABLE("Invalid value of anjay_dm_installed_object_type_t");
    return NULL;
#else  // ANJAY_WITH_THREAD_SAFETY
    assert(*obj_ptr);
    assert(**obj_ptr);
    if (has_handler_unlocked(&(**obj_ptr)->handlers, handler_type)) {
        return &(**obj_ptr)->handlers;
    }
    return NULL;
#endif // ANJAY_WITH_THREAD_SAFETY
}

bool _anjay_dm_handler_implemented(const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_dm_handler_t handler_type) {
#ifdef ANJAY_WITH_THREAD_SAFETY
    switch (obj_ptr->type) {
    case ANJAY_DM_OBJECT_USER_PROVIDED:
        assert(obj_ptr->impl.user_provided);
        assert(*obj_ptr->impl.user_provided);
        return has_handler_locked(&(*obj_ptr->impl.user_provided)->handlers,
                                  handler_type);

    case ANJAY_DM_OBJECT_UNLOCKED:
        assert(obj_ptr->impl.unlocked);
        assert(*obj_ptr->impl.unlocked);
        return has_handler_unlocked(&(*obj_ptr->impl.unlocked)->handlers,
                                    handler_type);
    }
    AVS_UNREACHABLE("Invalid value of anjay_dm_installed_object_type_t");
    return false;
#else  // ANJAY_WITH_THREAD_SAFETY
    assert(*obj_ptr);
    assert(**obj_ptr);
    return has_handler_unlocked(&(**obj_ptr)->handlers, handler_type);
#endif // ANJAY_WITH_THREAD_SAFETY
}

#define CHECKED_TAIL_CALL_HANDLER(ObjPtr, HandlerName, ...)                   \
    do {                                                                      \
        const anjay_unlocked_dm_handlers_t *handler =                         \
                get_handler((ObjPtr), ANJAY_DM_HANDLER_##HandlerName);        \
        if (handler) {                                                        \
            int AVS_CONCAT(result, __LINE__) =                                \
                    handler->HandlerName(__VA_ARGS__);                        \
            if (AVS_CONCAT(result, __LINE__)) {                               \
                dm_log(DEBUG, #HandlerName _(" failed with code ") "%d (%s)", \
                       AVS_CONCAT(result, __LINE__),                          \
                       AVS_COAP_CODE_STRING(_anjay_make_error_response_code(  \
                               AVS_CONCAT(result, __LINE__))));               \
            }                                                                 \
            return AVS_CONCAT(result, __LINE__);                              \
        } else {                                                              \
            dm_log(DEBUG,                                                     \
                   #HandlerName _(" handler not set for object ") "/%u",      \
                   _anjay_dm_installed_object_oid(ObjPtr));                   \
            return ANJAY_ERR_METHOD_NOT_ALLOWED;                              \
        }                                                                     \
    } while (0)

int _anjay_dm_call_object_read_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out) {
    dm_log(TRACE, _("object_read_default_attrs ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, object_read_default_attrs, anjay,
                              *obj_ptr, ssid, out);
}

int _anjay_dm_call_object_write_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs) {
    dm_log(TRACE, _("object_write_default_attrs ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, object_write_default_attrs, anjay,
                              *obj_ptr, ssid, attrs);
}

int _anjay_dm_call_list_instances(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_unlocked_dm_list_ctx_t *ctx) {
    dm_log(TRACE, _("list_instances ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, list_instances, anjay, *obj_ptr, ctx);
}

int _anjay_dm_call_instance_reset(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid) {
    dm_log(TRACE, _("instance_reset ") DM_LOG_PREFIX "/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, instance_reset, anjay, *obj_ptr, iid);
}

int _anjay_dm_call_instance_create(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_iid_t iid) {
    dm_log(TRACE, _("instance_create ") DM_LOG_PREFIX "/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, instance_create, anjay, *obj_ptr, iid);
}

int _anjay_dm_call_instance_remove(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_iid_t iid) {
    dm_log(TRACE, _("instance_remove ") DM_LOG_PREFIX "/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, instance_remove, anjay, *obj_ptr, iid);
}

int _anjay_dm_call_instance_read_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out) {
    dm_log(TRACE, _("instance_read_default_attrs ") DM_LOG_PREFIX "/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, instance_read_default_attrs, anjay,
                              *obj_ptr, iid, ssid, out);
}

int _anjay_dm_call_instance_write_default_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs) {
    dm_log(TRACE, _("instance_write_default_attrs ") DM_LOG_PREFIX "/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, instance_write_default_attrs, anjay,
                              *obj_ptr, iid, ssid, attrs);
}

int _anjay_dm_call_list_resources(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    dm_log(TRACE, _("list_resources ") DM_LOG_PREFIX "/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, list_resources, anjay, *obj_ptr, iid,
                              ctx);
}

int _anjay_dm_call_resource_read(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_unlocked_output_ctx_t *ctx) {
    anjay_uri_path_t path =
            MAKE_RESOURCE_INSTANCE_PATH(_anjay_dm_installed_object_oid(obj_ptr),
                                        iid, rid, riid);
    (void) path;
#ifdef ANJAY_WITH_LWM2M_GATEWAY
    if (obj_ptr->prefix) {
        strncpy(path.prefix, obj_ptr->prefix, sizeof(path.prefix));
    }
#endif // ANJAY_WITH_LWM2M_GATEWAY
    dm_log(LAZY_TRACE, _("resource_read ") "%s", ANJAY_DEBUG_MAKE_PATH(&path));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_read, anjay, *obj_ptr, iid, rid,
                              riid, ctx);
}

int _anjay_dm_call_resource_write(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_unlocked_input_ctx_t *ctx) {
    anjay_uri_path_t path =
            MAKE_RESOURCE_INSTANCE_PATH(_anjay_dm_installed_object_oid(obj_ptr),
                                        iid, rid, riid);
    (void) path;
#ifdef ANJAY_WITH_LWM2M_GATEWAY
    if (obj_ptr->prefix) {
        strncpy(path.prefix, obj_ptr->prefix, sizeof(path.prefix));
    }
#endif // ANJAY_WITH_LWM2M_GATEWAY
    dm_log(TRACE, _("resource_write ") "%s", ANJAY_DEBUG_MAKE_PATH(&path));
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_write, anjay, *obj_ptr, iid,
                              rid, riid, ctx);
}

int _anjay_dm_call_resource_execute(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_unlocked_execute_ctx_t *execute_ctx) {
    dm_log(TRACE, _("resource_execute ") DM_LOG_PREFIX "/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_execute, anjay, *obj_ptr, iid,
                              rid, execute_ctx);
}

int _anjay_dm_call_resource_reset(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid) {
    dm_log(TRACE, _("resource_reset ") DM_LOG_PREFIX "/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid);
    int result = _anjay_dm_transaction_include_object(anjay, obj_ptr);
    if (result) {
        return result;
    }
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_reset, anjay, *obj_ptr, iid,
                              rid);
}

int _anjay_dm_call_list_resource_instances(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_unlocked_dm_list_ctx_t *ctx) {
    dm_log(TRACE, _("list_resource_instances ") DM_LOG_PREFIX "/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid);
    if (!_anjay_dm_handler_implemented(
                obj_ptr, ANJAY_DM_HANDLER_list_resource_instances)) {
        dm_log(TRACE,
               _("list_resource_instances handler not set for object ") "/%u",
               _anjay_dm_installed_object_oid(obj_ptr));
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, list_resource_instances, anjay, *obj_ptr,
                              iid, rid, ctx);
}

int _anjay_dm_call_resource_read_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out) {
    dm_log(TRACE, _("resource_read_attrs ") DM_LOG_PREFIX "/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_read_attrs, anjay, *obj_ptr,
                              iid, rid, ssid, out);
}

int _anjay_dm_call_resource_write_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs) {
    dm_log(TRACE, _("resource_write_attrs ") DM_LOG_PREFIX "/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_write_attrs, anjay, *obj_ptr,
                              iid, rid, ssid, attrs);
}

#ifdef ANJAY_WITH_LWM2M11
int _anjay_dm_call_resource_instance_read_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        anjay_dm_r_attributes_t *out) {
    dm_log(TRACE,
           _("resource_instance_read_attrs ") DM_LOG_PREFIX "/%u/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid, riid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_instance_read_attrs, anjay,
                              *obj_ptr, iid, rid, riid, ssid, out);
}

int _anjay_dm_call_resource_instance_write_attrs(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_ssid_t ssid,
        const anjay_dm_r_attributes_t *attrs) {
    dm_log(TRACE,
           _("resource_instance_write_attrs ") DM_LOG_PREFIX "/%u/%u/%u/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr),
           iid, rid, riid);
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, resource_instance_write_attrs, anjay,
                              *obj_ptr, iid, rid, riid, ssid, attrs);
}

#endif // ANJAY_WITH_LWM2M11

int _anjay_dm_call_transaction_begin(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr) {
    dm_log(TRACE, _("begin_object_transaction ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, transaction_begin, anjay, *obj_ptr);
}

int _anjay_dm_call_transaction_validate(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr) {
    dm_log(TRACE, _("validate_object ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, transaction_validate, anjay, *obj_ptr);
}

int _anjay_dm_call_transaction_commit(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr) {
    dm_log(TRACE, _("commit_object ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, transaction_commit, anjay, *obj_ptr);
}

int _anjay_dm_call_transaction_rollback(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr) {
    dm_log(TRACE, _("rollback_object ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    CHECKED_TAIL_CALL_HANDLER(obj_ptr, transaction_rollback, anjay, *obj_ptr);
}

#define MAX_SANE_TRANSACTION_DEPTH 64

avs_error_t _anjay_dm_transaction_begin(anjay_unlocked_t *anjay) {
    dm_log(TRACE, _("transaction_begin"));
    avs_error_t err = AVS_OK;
#ifdef ANJAY_WITH_ATTR_STORAGE
    err = _anjay_attr_storage_transaction_begin(&anjay->attr_storage);
#endif // ANJAY_WITH_ATTR_STORAGE
    if (avs_is_ok(err)) {
        ++anjay->transaction_state.depth;
    }
    assert(anjay->transaction_state.depth < MAX_SANE_TRANSACTION_DEPTH);
    return err;
}

int _anjay_dm_transaction_include_object(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr) {
    dm_log(TRACE, _("transaction_include_object ") DM_LOG_PREFIX "/%u",
           DM_LOG_PREFIX_OBJ_ARG(obj_ptr)
                   _anjay_dm_installed_object_oid(obj_ptr));
    assert(anjay->transaction_state.depth > 0);
    AVS_LIST(const anjay_dm_installed_object_t *) *it;
    AVS_LIST_FOREACH_PTR(it, &anjay->transaction_state.objs_in_transaction) {
        if (**it >= obj_ptr) {
            break;
        }
    }
    if (!*it || **it != obj_ptr) {
        AVS_LIST(const anjay_dm_installed_object_t *) new_entry =
                AVS_LIST_NEW_ELEMENT(const anjay_dm_installed_object_t *);
        if (!new_entry) {
            _anjay_log_oom();
            return -1;
        }
        *new_entry = obj_ptr;
        AVS_LIST_INSERT(it, new_entry);
#if defined(ANJAY_WITH_LWM2M_GATEWAY) && defined(ANJAY_WITH_ATTR_STORAGE)
        if (obj_ptr->prefix) {
            anjay_attr_storage_t *as = NULL;
            if (_anjay_lwm2m_gateway_prefix_to_as(anjay, obj_ptr->prefix, &as)
                    || avs_is_err(_anjay_attr_storage_transaction_begin(as))) {
                return -1;
            }
        }
#endif // defined(ANJAY_WITH_LWM2M_GATEWAY) && defined(ANJAY_WITH_ATTR_STORAGE)
        int result = _anjay_dm_call_transaction_begin(anjay, obj_ptr);
        if (result) {
            // transaction_begin may have added new entries
            while (*it != new_entry) {
                AVS_LIST_ADVANCE_PTR(&it);
            }
            AVS_LIST_DELETE(it);
            return result;
        }
    }
    return 0;
}

static int commit_or_rollback_object(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t *obj,
                                     int predicate) {
    int result;
    if (predicate) {
        if ((result = _anjay_dm_call_transaction_rollback(anjay, obj))) {
            dm_log(ERROR,
                   _("cannot rollback transaction on ") DM_LOG_PREFIX
                   "/%u" _(", object may be left in undefined state"),
                   DM_LOG_PREFIX_OBJ_ARG(obj)
                           _anjay_dm_installed_object_oid(obj));
            return result;
        }
    } else if ((result = _anjay_dm_call_transaction_commit(anjay, obj))) {
        dm_log(ERROR, _("cannot commit transaction on ") DM_LOG_PREFIX "/%u",
               DM_LOG_PREFIX_OBJ_ARG(obj) _anjay_dm_installed_object_oid(obj));
        predicate = result;
    }
    return predicate;
}

int _anjay_dm_transaction_validate(anjay_unlocked_t *anjay) {
    dm_log(TRACE, _("transaction_validate"));
    AVS_LIST(const anjay_dm_installed_object_t *) obj;
    AVS_LIST_FOREACH(obj, anjay->transaction_state.objs_in_transaction) {
        dm_log(TRACE, _("validate_object ") DM_LOG_PREFIX "/%u",
               DM_LOG_PREFIX_OBJ_ARG(*obj)
                       _anjay_dm_installed_object_oid(*obj));
        int result = _anjay_dm_call_transaction_validate(anjay, *obj);
        if (result) {
            dm_log(ERROR, _("Validation failed for ") DM_LOG_PREFIX "/%u",
                   DM_LOG_PREFIX_OBJ_ARG(*obj)
                           _anjay_dm_installed_object_oid(*obj));
            return result;
        }
    }
    return 0;
}

int _anjay_dm_transaction_finish_without_validation(anjay_unlocked_t *anjay,
                                                    int result) {
    dm_log(TRACE, _("transaction_finish"));
    assert(anjay->transaction_state.depth > 0);
    if (--anjay->transaction_state.depth != 0) {
        return result;
    }

#if defined(ANJAY_WITH_LWM2M_GATEWAY) && defined(ANJAY_WITH_ATTR_STORAGE)
    AVS_LIST(anjay_attr_storage_t *) affected_end_devs_attr_storages = NULL;
#endif // defined(ANJAY_WITH_LWM2M_GATEWAY) && defined(ANJAY_WITH_ATTR_STORAGE)

    int final_result = result;
    AVS_LIST_CLEAR(&anjay->transaction_state.objs_in_transaction) {
        int commit_result = commit_or_rollback_object(
                anjay, *anjay->transaction_state.objs_in_transaction, result);
        if (!final_result && commit_result) {
            final_result = commit_result;
        }
#if defined(ANJAY_WITH_LWM2M_GATEWAY) && defined(ANJAY_WITH_ATTR_STORAGE)
        // add affected Attr Storages to a list, if they are not already there
        const char *prefix =
                (*anjay->transaction_state.objs_in_transaction)->prefix;
        if (prefix) {
            anjay_attr_storage_t *as = NULL;
            if (_anjay_lwm2m_gateway_prefix_to_as(anjay, prefix, &as)) {
                // object or end device deleted during transaction
                continue;
            }
            bool found = false;
            AVS_LIST(anjay_attr_storage_t *) it;
            AVS_LIST_FOREACH(it, affected_end_devs_attr_storages) {
                if (*it == as) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                AVS_LIST(anjay_attr_storage_t *) attr_storage =
                        AVS_LIST_NEW_ELEMENT(anjay_attr_storage_t *);
                if (!attr_storage) {
                    _anjay_log_oom();
                    AVS_LIST_CLEAR(&affected_end_devs_attr_storages);
                    return -1;
                }
                *attr_storage = as;
                AVS_LIST_APPEND(&affected_end_devs_attr_storages, attr_storage);
            }
        }
#endif // defined(ANJAY_WITH_LWM2M_GATEWAY) && defined(ANJAY_WITH_ATTR_STORAGE)
    }
#ifdef ANJAY_WITH_ATTR_STORAGE
    if (!final_result) {
        _anjay_attr_storage_transaction_commit(&anjay->attr_storage);
    } else {
        (void) _anjay_attr_storage_transaction_rollback(anjay,
                                                        &anjay->attr_storage);
    }
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    AVS_LIST_CLEAR(&affected_end_devs_attr_storages) {
        if (!final_result) {
            _anjay_attr_storage_transaction_commit(
                    *affected_end_devs_attr_storages);
        } else {
            (void) _anjay_attr_storage_transaction_rollback(
                    anjay, *affected_end_devs_attr_storages);
        }
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY
#endif     // ANJAY_WITH_ATTR_STORAGE
    return final_result;
}

int _anjay_dm_transaction_finish(anjay_unlocked_t *anjay, int result) {
    if (!result && anjay->transaction_state.depth == 1) {
        result = _anjay_dm_transaction_validate(anjay);
    }
    return _anjay_dm_transaction_finish_without_validation(anjay, result);
}

bool _anjay_dm_transaction_object_included(
        anjay_unlocked_t *anjay, const anjay_dm_installed_object_t *obj_ptr) {
    if (anjay->transaction_state.depth > 0) {
        AVS_LIST(const anjay_dm_installed_object_t *) *it;
        AVS_LIST_FOREACH_PTR(it,
                             &anjay->transaction_state.objs_in_transaction) {
            if (**it == obj_ptr) {
                return true;
            } else if (**it > obj_ptr) {
                break;
            }
        }
    }
    return false;
}

int anjay_dm_list_instances_SINGLE(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    anjay_dm_emit(ctx, 0);
    return 0;
}

int anjay_dm_transaction_NOOP(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    (void) obj_ptr;
    return 0;
}
