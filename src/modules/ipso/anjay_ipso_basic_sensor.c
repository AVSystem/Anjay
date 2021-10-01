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
#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_IPSO_OBJECTS

#    include <assert.h>
#    include <math.h>
#    include <stdbool.h>

#    include <anjay/anjay.h>
#    include <anjay/dm.h>
#    include <anjay/ipso_objects.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_utils_core.h>

#    include <avsystem/commons/avs_defs.h>
#    include <avsystem/commons/avs_log.h>
#    include <avsystem/commons/avs_memory.h>

VISIBILITY_SOURCE_BEGIN

/**
 * Min Measured Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The minimum value measured by the sensor since power ON or reset.
 */
#    define RID_MIN_MEASURED_VALUE 5601

/**
 * Max Measured Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The maximum value measured by the sensor since power ON or reset.
 */
#    define RID_MAX_MEASURED_VALUE 5602

/**
 * Min Range Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The minimum value that can be measured the sensor.
 */
#    define RID_MIN_RANGE_VALUE 5603

/**
 * Max Range Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The maximum value that can be measured by the sensor.
 */
#    define RID_MAX_RANGE_VALUE 5604

/**
 * Reset Min and Max Measured Values: E, Single, Optional
 * type: N/A, range: N/A, unit: N/A
 * Reset the Min and Max Measured Values to Current Value.
 */
#    define RID_RESET_MIN_AND_MAX_MEASURED_VALUES 5605

/**
 * Sensor Value: R, Single, Mandatory
 * type: float, range: N/A, unit: N/A
 * Last or Current Measured Value from the Sensor.
 */
#    define RID_SENSOR_VALUE 5700

/**
 * Sensor Units: R, Single, Optional
 * type: string, range: N/A, unit: N/A
 * Measurement Units Definition.
 */
#    define RID_SENSOR_UNITS 5701

typedef struct {
    anjay_ipso_basic_sensor_impl_t impl;
    bool initialized;

    double curr_value;
    double min_value;
    double max_value;
} anjay_ipso_basic_sensor_instance_t;

typedef struct {
    anjay_dm_installed_object_t obj_def_ptr;
    const anjay_unlocked_dm_object_def_t *obj_def;
    anjay_unlocked_dm_object_def_t def;

    size_t num_instances;
    anjay_ipso_basic_sensor_instance_t instances[];
} anjay_ipso_basic_sensor_t;

static anjay_ipso_basic_sensor_t *
get_obj(const anjay_dm_installed_object_t *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(obj_ptr),
                            anjay_ipso_basic_sensor_t, obj_def);
}

static int
basic_sensor_list_instances(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t obj_ptr,
                            anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    anjay_ipso_basic_sensor_t *obj = get_obj(&obj_ptr);
    assert(obj);

    for (anjay_iid_t iid = 0; iid < obj->num_instances; iid++) {
        if (obj->instances[iid].initialized) {
            _anjay_dm_emit_unlocked(ctx, iid);
        }
    }

    return 0;
}

static int
basic_sensor_list_resources(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t obj_ptr,
                            anjay_iid_t iid,
                            anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;

    anjay_ipso_basic_sensor_t *obj = get_obj(&obj_ptr);
    assert(obj);
    assert(iid < obj->num_instances);
    anjay_ipso_basic_sensor_instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    _anjay_dm_emit_res_unlocked(ctx, RID_MIN_MEASURED_VALUE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_MAX_MEASURED_VALUE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    if (!isnan(inst->impl.min_range_value)) {
        _anjay_dm_emit_res_unlocked(ctx, RID_MIN_RANGE_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    if (!isnan(inst->impl.max_range_value)) {
        _anjay_dm_emit_res_unlocked(ctx, RID_MAX_RANGE_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    _anjay_dm_emit_res_unlocked(ctx, RID_RESET_MIN_AND_MAX_MEASURED_VALUES,
                                ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_SENSOR_VALUE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_SENSOR_UNITS, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);

    return 0;
}

static int update_curr_value(anjay_unlocked_t *anjay,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             anjay_ipso_basic_sensor_instance_t *inst) {
    double value;
    int err;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    err = inst->impl.get_value(iid, inst->impl.user_context, &value);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);

    if (err) {
        return err;
    }

    if (value != inst->curr_value) {
        inst->curr_value = value;
        (void) _anjay_notify_changed_unlocked(anjay, oid, 0, RID_SENSOR_VALUE);
    }

    return 0;
}

static int update_values(anjay_unlocked_t *anjay,
                         anjay_oid_t oid,
                         anjay_iid_t iid,
                         anjay_ipso_basic_sensor_instance_t *inst) {
    if (update_curr_value(anjay, oid, iid, inst)) {
        return -1;
    }

    const double min_value = AVS_MIN(inst->min_value, inst->curr_value);
    if (min_value != inst->min_value) {
        inst->min_value = min_value;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                              RID_MIN_MEASURED_VALUE);
    }

    const double max_value = AVS_MAX(inst->max_value, inst->curr_value);
    if (max_value != inst->max_value) {
        inst->max_value = max_value;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                              RID_MAX_MEASURED_VALUE);
    }

    return 0;
}

static int basic_sensor_resource_read(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj_ptr,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_riid_t riid,
                                      anjay_unlocked_output_ctx_t *ctx) {
    (void) anjay;

    anjay_ipso_basic_sensor_t *obj = get_obj(&obj_ptr);
    assert(obj);
    assert(iid < obj->num_instances);
    anjay_ipso_basic_sensor_instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    switch (rid) {
    case RID_MIN_MEASURED_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        (void) update_values(anjay, obj->def.oid, iid, inst);
        return _anjay_ret_double_unlocked(ctx, inst->min_value);

    case RID_MAX_MEASURED_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        (void) update_values(anjay, obj->def.oid, iid, inst);
        return _anjay_ret_double_unlocked(ctx, inst->max_value);

    case RID_SENSOR_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        (void) update_values(anjay, obj->def.oid, iid, inst);
        return _anjay_ret_double_unlocked(ctx, inst->curr_value);

    case RID_SENSOR_UNITS:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_string_unlocked(ctx, inst->impl.unit);

    case RID_MIN_RANGE_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        if (isnan(inst->impl.min_range_value)) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        } else {
            return _anjay_ret_double_unlocked(ctx, inst->impl.min_range_value);
        }

    case RID_MAX_RANGE_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        if (isnan(inst->impl.max_range_value)) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        } else {
            return _anjay_ret_double_unlocked(ctx, inst->impl.max_range_value);
        }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int
basic_sensor_resource_execute(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_unlocked_execute_ctx_t *arg_ctx) {
    (void) arg_ctx;
    (void) anjay;

    anjay_ipso_basic_sensor_t *obj = get_obj(&obj_ptr);
    assert(obj);
    assert(iid < obj->num_instances);
    anjay_ipso_basic_sensor_instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    switch (rid) {
    case RID_RESET_MIN_AND_MAX_MEASURED_VALUES:
        (void) update_curr_value(anjay, obj->def.oid, iid, inst);

        if (inst->max_value != inst->curr_value) {
            inst->max_value = inst->curr_value;
            (void) _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                                  RID_MIN_MEASURED_VALUE);
        }
        if (inst->min_value != inst->curr_value) {
            inst->min_value = inst->curr_value;
            (void) _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                                  RID_MAX_MEASURED_VALUE);
        }

        return 0;

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static anjay_ipso_basic_sensor_t *obj_from_oid(anjay_unlocked_t *anjay,
                                               anjay_oid_t oid) {
    const anjay_dm_installed_object_t *installed_obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    if (!_anjay_dm_installed_object_is_valid_unlocked(installed_obj)) {
        return NULL;
    }

    anjay_ipso_basic_sensor_t *obj = get_obj(installed_obj);

    // Checks if it is really an instance of anjay_ipso_basic_sensor_t
    if (obj->def.handlers.list_instances == basic_sensor_list_instances) {
        return obj;
    } else {
        return NULL;
    }
}

int anjay_ipso_basic_sensor_install(anjay_t *anjay_locked,
                                    anjay_oid_t oid,
                                    size_t num_instances) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    AVS_LIST(anjay_dm_installed_object_t) entry;
    anjay_ipso_basic_sensor_t *obj =
            (anjay_ipso_basic_sensor_t *) AVS_LIST_NEW_BUFFER(
                    sizeof(anjay_ipso_basic_sensor_t)
                    + num_instances
                              * sizeof(anjay_ipso_basic_sensor_instance_t));
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Out of memory"));
        err = -1;
        goto finish;
    }

    obj->def = (anjay_unlocked_dm_object_def_t) {
        .oid = oid,
        .handlers = {
            .list_instances = basic_sensor_list_instances,
            .list_resources = basic_sensor_list_resources,
            .resource_read = basic_sensor_resource_read,
            .resource_execute = basic_sensor_resource_execute
        }
    };

    obj->obj_def = &obj->def;
    obj->num_instances = num_instances;

    _anjay_dm_installed_object_init_unlocked(&obj->obj_def_ptr, &obj->obj_def);
    entry = &obj->obj_def_ptr;
    if (_anjay_register_object_unlocked(anjay, &entry)) {
        avs_free(obj);
        err = -1;
    }

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

int anjay_ipso_basic_sensor_instance_add(
        anjay_t *anjay_locked,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_ipso_basic_sensor_impl_t impl) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    anjay_ipso_basic_sensor_instance_t *inst;
    anjay_ipso_basic_sensor_t *obj = obj_from_oid(anjay, oid);
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Object") " %d" _(" not installed"), oid);
        err = -1;
        goto finish;
    }

    if (iid >= obj->num_instances) {
        _anjay_log(ipso, ERROR, _("IID too large"));
        err = -1;
        goto finish;
    }

    if (!impl.get_value) {
        _anjay_log(ipso, ERROR, _("Callback is NULL"));
        goto finish;
    }

    double value;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked_2, anjay);
    if (impl.get_value(iid, impl.user_context, &value)) {
        _anjay_log(ipso, WARNING, _("Read of") " /%d/%d" _(" failed"), oid,
                   iid);
        value = NAN;
    }
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked_2);

    inst = &obj->instances[iid];
    inst->initialized = true;
    inst->impl = impl;
    inst->curr_value = value;
    inst->min_value = value;
    inst->max_value = value;

    _anjay_notify_instances_changed_unlocked(anjay, oid);

    (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_SENSOR_VALUE);
    (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                          RID_MIN_MEASURED_VALUE);
    (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                          RID_MAX_MEASURED_VALUE);

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

int anjay_ipso_basic_sensor_instance_remove(anjay_t *anjay_locked,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    anjay_ipso_basic_sensor_t *obj = obj_from_oid(anjay, oid);
    if (!obj) {
        _anjay_log(ipso, WARNING, _("Object") " %d" _(" not installed"), oid);
        err = -1;
        goto finish;
    }

    if (iid >= obj->num_instances || !obj->instances[iid].initialized) {
        _anjay_log(ipso, ERROR, _("Object") " %d" _(" has no instance") " %d",
                   oid, iid);
        err = -1;
    } else {
        obj->instances[iid].initialized = false;
    }

    _anjay_notify_instances_changed_unlocked(anjay, oid);

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

int anjay_ipso_basic_sensor_update(anjay_t *anjay_locked,
                                   anjay_oid_t oid,
                                   anjay_iid_t iid) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    anjay_ipso_basic_sensor_instance_t *inst;
    anjay_ipso_basic_sensor_t *obj = obj_from_oid(anjay, oid);
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Object") " %d" _(" is not installed"), oid);
        err = -1;
        goto finish;
    }

    if (iid > obj->num_instances
            || !(inst = &obj->instances[iid])->initialized) {
        _anjay_log(ipso, ERROR, _("Object") " %d" _(" has no instance") " %d",
                   oid, iid);
        err = -1;
        goto finish;
    }

    if (update_values(anjay, oid, iid, inst)) {
        _anjay_log(ipso, WARNING, _("Update of") " /%d/%d" _(" failed"), oid,
                   iid);
        err = -1;
    }

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS
