/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */
#include <anjay_init.h>

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    include <assert.h>
#    include <inttypes.h>
#    include <stdbool.h>
#    include <string.h>

#    include <anjay_modules/anjay_attr_storage_utils.h>
#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_lwm2m_gateway.h>
#    include <anjay_modules/anjay_notify.h>
#    include <anjay_modules/anjay_utils_core.h>
#    include <anjay_modules/dm/anjay_modules.h>

#    include <anjay/anjay.h>
#    include <anjay/lwm2m_gateway.h>
#    include <avsystem/commons/avs_defs.h>
#    include <avsystem/commons/avs_list.h>
#    include <avsystem/commons/avs_memory.h>

#    include "../../core/anjay_dm_core.h"
#    include "../../core/attr_storage/anjay_attr_storage.h"
#    include "../../core/io/anjay_corelnk.h"

VISIBILITY_SOURCE_BEGIN

#    if !defined(ANJAY_WITH_LWM2M11)
#        error "LwM2M Gateway requires LwM2M version 1.1 or above!"
#    endif

#    define gw_log(level, ...) _anjay_log(lwm2m_gateway, level, __VA_ARGS__)

/**
 * Device ID: R, Single, Mandatory
 * type: string, range: N/A, unit: N/A
 * This resource identifies the IoT Device connected to the LwM2M
 * Gateway.
 */
#    define RID_DEVICE_ID 0

/**
 * Prefix: R, Single, Mandatory
 * type: string, range: N/A, unit: N/A
 * This resource defines what prefix MUST be used for access to LwM2M
 * Objects of this IoT Device.
 */
#    define RID_PREFIX 1

/**
 * IoT Device Objects: R, Single, Mandatory
 * type: corelnk, range: N/A, unit: N/A
 * This resource contains the Objects and Object Instances exposed by the
 * LwM2M Gateway on behalf of the IoT Device. It uses the same CoreLnk
 * format as Registration Interface.
 */
#    define RID_IOT_DEVICE_OBJECTS 3

typedef struct lwm2m_gateway_instance_struct {
    anjay_iid_t iid;

    const char *device_id;
    char prefix[ANJAY_GATEWAY_MAX_PREFIX_LEN];
    anjay_dm_t dm;
#    ifdef ANJAY_WITH_ATTR_STORAGE
    anjay_attr_storage_t as;
#    endif // ANJAY_WITH_ATTR_STORAGE
} lwm2m_gateway_instance_t;

typedef struct lwm2m_gateway_object_struct {
    anjay_dm_installed_object_t obj_def_ptr;
    const anjay_unlocked_dm_object_def_t *obj_def;

    AVS_LIST(lwm2m_gateway_instance_t) instances;
} lwm2m_gateway_obj_t;

static inline void
delete_instance(AVS_LIST(lwm2m_gateway_instance_t) *instances,
                lwm2m_gateway_instance_t *inst) {
    AVS_LIST(lwm2m_gateway_instance_t) *inst_ptr =
            AVS_LIST_FIND_PTR(instances, inst);
    AVS_LIST_DELETE(inst_ptr);
}

static anjay_iid_t get_new_iid(AVS_LIST(lwm2m_gateway_instance_t) instances) {
    anjay_iid_t iid = 0;
    AVS_LIST(lwm2m_gateway_instance_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static inline lwm2m_gateway_instance_t *find_instance(lwm2m_gateway_obj_t *gw,
                                                      anjay_iid_t iid) {
    AVS_LIST(lwm2m_gateway_instance_t) it;
    AVS_LIST_FOREACH(it, gw->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }
    return NULL;
}

static inline lwm2m_gateway_obj_t *
get_obj(const anjay_dm_installed_object_t *obj_ptr) {
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(obj_ptr),
                            lwm2m_gateway_obj_t, obj_def);
}

static int gateway_list_instances(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr,
                                  anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    AVS_LIST(lwm2m_gateway_instance_t) it;
    AVS_LIST_FOREACH(it, get_obj(&obj_ptr)->instances) {
        _anjay_dm_emit_unlocked(ctx, it->iid);
    }

    return 0;
}

static int init_instance(lwm2m_gateway_instance_t *inst, anjay_iid_t iid) {
    if (avs_simple_snprintf(inst->prefix, sizeof(inst->prefix), "dev%" PRIu16,
                            iid)
            < (int) strlen("dev0")) {
        return -1;
    }
    inst->iid = iid;
    return 0;
}

static lwm2m_gateway_instance_t *
gateway_instance_create(lwm2m_gateway_obj_t *gw, anjay_iid_t iid) {
    assert(iid != ANJAY_ID_INVALID);

    AVS_LIST(lwm2m_gateway_instance_t) created =
            AVS_LIST_NEW_ELEMENT(lwm2m_gateway_instance_t);
    if (!created) {
        _anjay_log_oom();
        return NULL;
    }
    if (init_instance(created, iid)) {
        // failed, clean up newly added instance
        delete_instance(&gw->instances, created);
        return NULL;
    }

#    ifdef ANJAY_WITH_ATTR_STORAGE
    if (_anjay_attr_storage_init(&created->as, &created->dm)) {
        delete_instance(&gw->instances, created);
        return NULL;
    }
#    endif // ANJAY_WITH_ATTR_STORAGE

    AVS_LIST(lwm2m_gateway_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &gw->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return created;
}

static int gateway_list_resources(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;

    lwm2m_gateway_obj_t *gw = get_obj(&obj_ptr);
    lwm2m_gateway_instance_t *inst = find_instance(gw, iid);
    assert(inst);

    _anjay_dm_emit_res_unlocked(ctx, RID_DEVICE_ID, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_PREFIX, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_IOT_DEVICE_OBJECTS, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    return 0;
}

static int gateway_resource_read(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_unlocked_output_ctx_t *ctx) {
    (void) anjay;

    lwm2m_gateway_obj_t *gw = get_obj(&obj_ptr);
    lwm2m_gateway_instance_t *inst = find_instance(gw, iid);
    assert(inst);

    switch (rid) {
    case RID_DEVICE_ID: {
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_string_unlocked(ctx, inst->device_id);
    }
    case RID_PREFIX: {
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_string_unlocked(ctx, inst->prefix);
    }
    case RID_IOT_DEVICE_OBJECTS: {
        assert(riid == ANJAY_ID_INVALID);
        int ret = 0;
        char *dm_buffer = NULL;
        // There is no chance to determine what is the LwM2M Version that the
        // Anjay client registered with to the server, that is now performing
        // this read (anjay_registration_info_t::lwm2m_version). Lets just
        // assume version 1.1.
        if (_anjay_corelnk_query_dm(anjay, &inst->dm, ANJAY_LWM2M_VERSION_1_1,
                                    &dm_buffer)) {
            return ANJAY_ERR_INTERNAL;
        }
        ret = _anjay_ret_string_unlocked(ctx, dm_buffer);
        avs_free(dm_buffer);
        return ret;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_unlocked_dm_object_def_t LWM2M_GATEWAY = {
    .oid = ANJAY_DM_OID_LWM2M_GATEWAY,
    .version = "2.0",
    .handlers = {
        .list_instances = gateway_list_instances,
        .list_resources = gateway_list_resources,
        .resource_read = gateway_resource_read,
    }
};

static void gateway_delete(void *lwm2m_gateway_) {
    lwm2m_gateway_obj_t *gw = (lwm2m_gateway_obj_t *) lwm2m_gateway_;

    AVS_LIST_CLEAR(&gw->instances) {
#    ifdef ANJAY_WITH_ATTR_STORAGE
        _anjay_attr_storage_cleanup(&gw->instances->as);
#    endif // ANJAY_WITH_ATTR_STORAGE
        _anjay_dm_cleanup(&gw->instances->dm);
    }
}

int anjay_lwm2m_gateway_install(anjay_t *anjay_locked) {
    assert(anjay_locked);

    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    AVS_LIST(lwm2m_gateway_obj_t) lwm2m_gateway =
            AVS_LIST_NEW_ELEMENT(lwm2m_gateway_obj_t);
    if (lwm2m_gateway) {
        lwm2m_gateway->obj_def = &LWM2M_GATEWAY;
        _anjay_dm_installed_object_init_unlocked(&lwm2m_gateway->obj_def_ptr,
                                                 &lwm2m_gateway->obj_def);

        if (!_anjay_dm_module_install(anjay, gateway_delete, lwm2m_gateway)) {
            _ANJAY_ASSERT_INSTALLED_OBJECT_IS_FIRST_FIELD(lwm2m_gateway_obj_t,
                                                          obj_def_ptr);

            AVS_LIST(anjay_dm_installed_object_t) entry =
                    &lwm2m_gateway->obj_def_ptr;
            if (_anjay_register_object_unlocked(anjay, &entry)) {
                result = _anjay_dm_module_uninstall(anjay, gateway_delete);
                assert(!result);
                result = -1;
            } else {
                result = 0;
            }
        }
        if (result) {
            AVS_LIST_CLEAR(&lwm2m_gateway);
        }
    } else {
        _anjay_log_oom();
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static lwm2m_gateway_instance_t *add_instance(lwm2m_gateway_obj_t *gw,
                                              anjay_iid_t *inout_iid) {
    if (*inout_iid != ANJAY_ID_INVALID) {
        // verify if user-defined iid is free
        if (find_instance(gw, *inout_iid)) {
            return NULL;
        }
    } else {
        // assign new, free iid
        *inout_iid = get_new_iid(gw->instances);
        if (*inout_iid == ANJAY_ID_INVALID) {
            return NULL;
        }
    }

    lwm2m_gateway_instance_t *inst = gateway_instance_create(gw, *inout_iid);
    if (!inst) {
        return NULL;
    }

    return inst;
}

int anjay_lwm2m_gateway_register_device(anjay_t *anjay_locked,
                                        const char *device_id,
                                        anjay_iid_t *inout_iid) {
    assert(anjay_locked && device_id && inout_iid);
    int retval = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
    } else {
        lwm2m_gateway_instance_t *inst = NULL;
        if ((inst = add_instance(gw, inout_iid))) {
            inst->device_id = device_id;
            gw_log(INFO,
                   _("Registered new device: ") "%s"
                                                " with ID: %" PRIu16,
                   device_id, *inout_iid);
            _anjay_notify_instances_changed_unlocked(
                    anjay, ANJAY_DM_OID_LWM2M_GATEWAY);
            retval = 0;
        } else {
            gw_log(ERROR,
                   _("Failed to register new device: ") "%s"
                                                        " with ID: %" PRIu16,
                   device_id, *inout_iid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return retval;
}

int anjay_lwm2m_gateway_deregister_device(anjay_t *anjay_locked,
                                          anjay_iid_t iid) {
    assert(anjay_locked);
    int retval = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
    } else {
        lwm2m_gateway_instance_t *inst;
        if ((inst = find_instance(gw, iid))) {
            _anjay_dm_cleanup(&inst->dm);
#    ifdef ANJAY_WITH_ATTR_STORAGE
            _anjay_attr_storage_cleanup(&inst->as);
#    endif // ANJAY_WITH_ATTR_STORAGE
            delete_instance(&gw->instances, inst);

            _anjay_notify_instances_changed_unlocked(
                    anjay, ANJAY_DM_OID_LWM2M_GATEWAY);
            gw_log(INFO, _("Device deregistered: ") "%" PRIu16, iid);

            retval = 0;
        } else {
            gw_log(WARNING,
                   _("LwM2M Gateway instance %" PRIu16 " does not exist"), iid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return retval;
}

void _anjay_lwm2m_gateway_iid_to_dm(anjay_unlocked_t *anjay,
                                    anjay_iid_t iid,
                                    const anjay_dm_t **dm) {
    assert(anjay && dm);
    *dm = NULL;

    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(WARNING, _("LwM2M Gateway object not installed"));
    } else {
        AVS_LIST(lwm2m_gateway_instance_t) it;
        AVS_LIST_FOREACH(it, gw->instances) {
            if (it->iid == iid) {
                *dm = &it->dm;
                break;
            }
        }
    }
}

/**
 * This function is extracted from anjay_lwm2m_gateway_register_object() to
 * allow easier to read early returns without goto.
 * It has a lot in common with anjay_register_object() but is tweaked to allow
 * to operate on a different DM than Anjay's.
 */
static int
register_object_unlocked(anjay_unlocked_t *anjay,
                         anjay_iid_t iid,
                         const anjay_dm_object_def_t *const *def_ptr) {
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
        return -1;
    }
    lwm2m_gateway_instance_t *inst;
    if (!(inst = find_instance(gw, iid))) {
        gw_log(ERROR, _("End Device %" PRIu16 " is not registered"), iid);
        return -1;
    }

    AVS_LIST(anjay_dm_installed_object_t) new_elem =
            _anjay_prepare_user_provided_object(def_ptr);
    if (!new_elem) {
        return -1;
    }

    new_elem->prefix = inst->prefix;

    //_anjay_dm_installed_object_init_unlocked(obj_ptr, new_elem);
    if (_anjay_dm_register_object(&inst->dm, &new_elem)) {
        gw_log(ERROR, _("Object registration failed"));
        AVS_LIST_CLEAR(&new_elem);
        return -1;
    }

    // no need to call _anjay_notify_instances_changed_unlocked() or
    // _anjay_schedule_registration_update_unlocked() as the
    // End Devices DM's contents are not reported in Register and Update
    // messages
    gw_log(DEBUG, _("Successfully registered object ") "/%s/%" PRIu16,
           new_elem->prefix, _anjay_dm_installed_object_oid(new_elem));
    return 0;
}

int anjay_lwm2m_gateway_register_object(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const anjay_dm_object_def_t *const *def_ptr) {
    assert(anjay_locked);
    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = register_object_unlocked(anjay, iid, def_ptr);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return result;
}

/**
 * This function is extracted from anjay_lwm2m_gateway_unregister_object() to
 * allow easier to read early returns without goto.
 * It has a lot in common with anjay_unregister_object() but is tweaked to allow
 * to operate on a different DM than Anjay's.
 */
static int
unregister_object_unlocked(anjay_unlocked_t *anjay,
                           anjay_iid_t iid,
                           const anjay_dm_object_def_t *const *def_ptr) {
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
        return -1;
    }
    lwm2m_gateway_instance_t *inst;
    if (!(inst = find_instance(gw, iid))) {
        gw_log(ERROR, _("End Device %" PRIu16 " is not registered"), iid);
        return -1;
    }

    AVS_LIST(anjay_dm_installed_object_t) *obj =
            _anjay_find_and_verify_object_to_unregister(&inst->dm, def_ptr);
    if (!obj) {
        gw_log(ERROR, _("Object not installed for given End Device"));
        return -1;
    }

    assert(AVS_LIST_FIND_PTR(&inst->dm.objects, *obj));
    AVS_LIST(anjay_dm_installed_object_t) detached = AVS_LIST_DETACH(obj);

    _anjay_unregister_object_handle_transaction_state(anjay, detached);
    _anjay_unregister_object_handle_notify_queue(anjay, detached);

    gw_log(INFO, _("Successfully unregistered object ") "/%s/%" PRIu16,
           detached->prefix, _anjay_dm_installed_object_oid(detached));
    AVS_LIST_DELETE(&detached);

    // no need to call _anjay_notify_instances_changed_unlocked() or
    // _anjay_schedule_registration_update_unlocked() as the
    // End Devices DM's contents are not reported in Register and Update
    // messages

    return 0;
}

int anjay_lwm2m_gateway_unregister_object(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const anjay_dm_object_def_t *const *def_ptr) {
    assert(anjay_locked);

    if (!def_ptr || !*def_ptr) {
        gw_log(ERROR, _("invalid object pointer"));
        return -1;
    }

    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = unregister_object_unlocked(anjay, iid, def_ptr);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return result;
}

static lwm2m_gateway_instance_t *
find_instance_by_prefix(anjay_unlocked_t *anjay, const char *prefix) {
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(WARNING, _("LwM2M Gateway object not installed"));
    } else {
        AVS_LIST(lwm2m_gateway_instance_t) it;
        AVS_LIST_FOREACH(it, gw->instances) {
            if (!strcmp(it->prefix, prefix)) {
                return it;
            }
        }
    }
    return NULL;
}

int _anjay_lwm2m_gateway_prefix_to_dm(anjay_unlocked_t *anjay,
                                      const char *prefix,
                                      const anjay_dm_t **dm) {
    assert(anjay && prefix && dm);
    *dm = NULL;

    lwm2m_gateway_instance_t *inst = find_instance_by_prefix(anjay, prefix);
    if (inst) {
        *dm = &inst->dm;
        return 0;
    }
    return -1;
}

#    ifdef ANJAY_WITH_ATTR_STORAGE
int _anjay_lwm2m_gateway_prefix_to_as(anjay_unlocked_t *anjay,
                                      const char *prefix,
                                      anjay_attr_storage_t **as) {
    assert(anjay && prefix && as);
    *as = NULL;

    lwm2m_gateway_instance_t *inst = find_instance_by_prefix(anjay, prefix);
    if (inst) {
        *as = &inst->as;
        return 0;
    }
    return -1;
}
#    endif // ANJAY_WITH_ATTR_STORAGE

static int gateway_notify_changed_unlocked(anjay_unlocked_t *anjay,
                                           anjay_iid_t end_dev,
                                           anjay_oid_t oid,
                                           anjay_iid_t iid,
                                           anjay_rid_t rid) {
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
        return -1;
    }
    lwm2m_gateway_instance_t *inst;
    if (!(inst = find_instance(gw, end_dev))) {
        gw_log(ERROR, _("End Device %" PRIu16 " is not registered"), end_dev);
        return -1;
    }
    return _anjay_notify_changed_gw_unlocked(anjay, inst->prefix, oid, iid,
                                             rid);
}

int anjay_lwm2m_gateway_notify_changed(anjay_t *anjay_locked,
                                       anjay_iid_t end_dev,
                                       anjay_oid_t oid,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = gateway_notify_changed_unlocked(anjay, end_dev, oid, iid, rid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static int gateway_notify_instances_changed_unlocked(anjay_unlocked_t *anjay,
                                                     anjay_iid_t end_dev,
                                                     anjay_oid_t oid) {
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
        return -1;
    }
    lwm2m_gateway_instance_t *inst;
    if (!(inst = find_instance(gw, end_dev))) {
        gw_log(ERROR, _("End Device %" PRIu16 " is not registered"), end_dev);
        return -1;
    }

    return _anjay_notify_instances_changed_gw_unlocked(anjay, inst->prefix,
                                                       oid);
}

int anjay_lwm2m_gateway_notify_instances_changed(anjay_t *anjay_locked,
                                                 anjay_iid_t end_dev,
                                                 anjay_oid_t oid) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = gateway_notify_instances_changed_unlocked(anjay, end_dev, oid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#    ifdef ANJAY_WITH_OBSERVATION_STATUS
anjay_resource_observation_status_t
anjay_lwm2m_gateway_resource_observation_status(anjay_t *anjay_locked,
                                                anjay_iid_t end_dev,
                                                anjay_oid_t oid,
                                                anjay_iid_t iid,
                                                anjay_rid_t rid) {
    assert(anjay_locked);
    anjay_resource_observation_status_t retval = {
        .is_observed = false,
        .min_period = 0,
        .max_eval_period = ANJAY_ATTRIB_INTEGER_NONE,
#        if (ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER > 0)
        .servers_number = 0
#        endif // (ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER > 0)
    };

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    lwm2m_gateway_obj_t *gw =
            (lwm2m_gateway_obj_t *) _anjay_dm_module_get_arg(anjay,
                                                             gateway_delete);
    if (!gw) {
        gw_log(ERROR, _("LwM2M Gateway object not installed"));
        goto exit;
    }
    lwm2m_gateway_instance_t *inst;
    if (!(inst = find_instance(gw, end_dev))) {
        gw_log(ERROR, _("End Device %" PRIu16 " is not registered"), end_dev);
        goto exit;
    }
    _anjay_notify_observation_status_impl_unlocked(anjay, &retval, inst->prefix,
                                                   oid, iid, rid);
exit:
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}
#    endif // ANJAY_WITH_OBSERVATION_STATUS
#    ifdef ANJAY_TEST
#        include "tests/modules/lwm2m_gateway/lwm2m_gateway.c"
#    endif

#endif // ANJAY_WITH_LWM2M_GATEWAY
