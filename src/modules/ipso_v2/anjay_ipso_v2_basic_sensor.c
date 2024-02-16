/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_IPSO_OBJECTS_V2

#    include <assert.h>
#    include <math.h>
#    include <stdbool.h>

#    include <anjay/anjay.h>
#    include <anjay/dm.h>
#    include <anjay/ipso_objects_v2.h>

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

typedef anjay_ipso_v2_basic_sensor_meta_t sensor_meta_t;

typedef struct {
    bool initialized;
    sensor_meta_t meta;
    double curr_value;
    double min_value;
    double max_value;
} instance_t;

typedef struct {
    anjay_dm_installed_object_t installed_obj;
    anjay_unlocked_dm_object_def_t def;
    const anjay_unlocked_dm_object_def_t *def_ptr;

    size_t instance_count;
    instance_t instances[];
} object_t;

static void log_invalid_parameters_error(void) {
    _anjay_log(ipso, ERROR, _("Invalid parameters"));
}

#    define log_invalid_parameters(...)           \
        do {                                      \
            log_invalid_parameters_error();       \
            _anjay_log(ipso, DEBUG, __VA_ARGS__); \
        } while (0)

static object_t *get_obj(const anjay_dm_installed_object_t *installed_obj_ptr) {
    assert(installed_obj_ptr);
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(
                                    installed_obj_ptr),
                            object_t, def_ptr);
}

static int list_instances(anjay_unlocked_t *anjay,
                          const anjay_dm_installed_object_t installed_obj,
                          anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    object_t *obj = get_obj(&installed_obj);

    for (anjay_iid_t iid = 0; iid < obj->instance_count; iid++) {
        if (obj->instances[iid].initialized) {
            _anjay_dm_emit_unlocked(ctx, iid);
        }
    }

    return 0;
}

static int list_resources(anjay_unlocked_t *anjay,
                          const anjay_dm_installed_object_t installed_obj,
                          anjay_iid_t iid,
                          anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;

    object_t *obj = get_obj(&installed_obj);
    assert(iid < obj->instance_count);
    instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    if (inst->meta.min_max_measured_value_present) {
        _anjay_dm_emit_res_unlocked(ctx, RID_MIN_MEASURED_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    if (inst->meta.min_max_measured_value_present) {
        _anjay_dm_emit_res_unlocked(ctx, RID_MAX_MEASURED_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    if (!isnan(inst->meta.min_range_value)) {
        _anjay_dm_emit_res_unlocked(ctx, RID_MIN_RANGE_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    if (!isnan(inst->meta.max_range_value)) {
        _anjay_dm_emit_res_unlocked(ctx, RID_MAX_RANGE_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    if (inst->meta.min_max_measured_value_present) {
        _anjay_dm_emit_res_unlocked(ctx, RID_RESET_MIN_AND_MAX_MEASURED_VALUES,
                                    ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    }
    _anjay_dm_emit_res_unlocked(ctx, RID_SENSOR_VALUE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    if (inst->meta.unit) {
        _anjay_dm_emit_res_unlocked(ctx, RID_SENSOR_UNITS, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }

    return 0;
}

static int resource_read(anjay_unlocked_t *anjay,
                         const anjay_dm_installed_object_t installed_obj,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_unlocked_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;

    object_t *obj = get_obj(&installed_obj);
    assert(iid < obj->instance_count);
    instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    switch (rid) {
    case RID_MIN_MEASURED_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->min_value);

    case RID_MAX_MEASURED_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->max_value);

    case RID_SENSOR_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_double_unlocked(ctx, inst->curr_value);

    case RID_SENSOR_UNITS:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.unit);
        return _anjay_ret_string_unlocked(ctx, inst->meta.unit);

    case RID_MIN_RANGE_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(!isnan(inst->meta.min_range_value));
        return _anjay_ret_double_unlocked(ctx, inst->meta.min_range_value);

    case RID_MAX_RANGE_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(!isnan(inst->meta.max_range_value));
        return _anjay_ret_double_unlocked(ctx, inst->meta.max_range_value);

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_execute(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t installed_obj,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_unlocked_execute_ctx_t *arg_ctx) {
    (void) arg_ctx;
    (void) anjay;

    object_t *obj = get_obj(&installed_obj);
    assert(iid < obj->instance_count);
    instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    switch (rid) {
    case RID_RESET_MIN_AND_MAX_MEASURED_VALUES:
        assert(inst->meta.min_max_measured_value_present);

        if (inst->min_value != inst->curr_value) {
            inst->min_value = inst->curr_value;
            (void) _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                                  RID_MIN_MEASURED_VALUE);
        }
        if (inst->max_value != inst->curr_value) {
            inst->max_value = inst->curr_value;
            (void) _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                                  RID_MAX_MEASURED_VALUE);
        }

        return 0;

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static object_t *obj_from_oid(anjay_unlocked_t *anjay, anjay_oid_t oid) {
    const anjay_dm_installed_object_t *installed_obj_ptr =
            _anjay_dm_find_object_by_oid(anjay, oid);
    if (!_anjay_dm_installed_object_is_valid_unlocked(installed_obj_ptr)) {
        return NULL;
    }

    // Checks if it is really an instance of object_t
    return (*_anjay_dm_installed_object_get_unlocked(installed_obj_ptr))
                                   ->handlers.list_instances
                           == list_instances
                   ? get_obj(installed_obj_ptr)
                   : NULL;
}

static int sensor_install_unlocked(anjay_unlocked_t *anjay,
                                   anjay_oid_t oid,
                                   const char *version,
                                   size_t instance_count) {
    if (instance_count == 0 || instance_count >= ANJAY_ID_INVALID) {
        log_invalid_parameters(_("Instance count of out range"));
        return -1;
    }
    AVS_LIST(anjay_dm_installed_object_t) entry;
    object_t *obj = (object_t *) AVS_LIST_NEW_BUFFER(
            sizeof(object_t) + instance_count * sizeof(instance_t));
    if (!obj) {
        _anjay_log_oom();
        return -1;
    }

    obj->def = (anjay_unlocked_dm_object_def_t) {
        .oid = oid,
        .version = version,
        .handlers = {
            .list_instances = list_instances,
            .list_resources = list_resources,
            .resource_read = resource_read,
            .resource_execute = resource_execute
        }
    };

    obj->def_ptr = &obj->def;
    obj->instance_count = instance_count;

    _anjay_dm_installed_object_init_unlocked(&obj->installed_obj,
                                             &obj->def_ptr);
    entry = &obj->installed_obj;
    if (_anjay_register_object_unlocked(anjay, &entry)) {
        avs_free(obj);
        return -1;
    }

    return 0;
}

int anjay_ipso_v2_basic_sensor_install(anjay_t *anjay_locked,
                                       anjay_oid_t oid,
                                       const char *version,
                                       size_t instance_count) {
    assert(anjay_locked);

    int res = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    res = sensor_install_unlocked(anjay, oid, version, instance_count);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return res;
}

static int sensor_instance_add_unlocked(anjay_unlocked_t *anjay,
                                        anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        double initial_value,
                                        const sensor_meta_t *meta) {
    object_t *obj = obj_from_oid(anjay, oid);
    if (!obj) {
        log_invalid_parameters(_("Object") " %d" _(" not installed"), oid);
        return -1;
    }

    if (iid >= obj->instance_count) {
        log_invalid_parameters(_("IID too large"));
        return -1;
    }

    if ((!isnan(meta->min_range_value) && !isfinite(meta->min_range_value))
            || (!isnan(meta->max_range_value)
                && !isfinite(meta->max_range_value))) {
        log_invalid_parameters(_("Min/max range values not finite"));
        return -1;
    }

    if (!isnan(meta->min_range_value) && !isnan(meta->max_range_value)
            && meta->min_range_value > meta->max_range_value) {
        log_invalid_parameters(_("Min range larger than max range value"));
        return -1;
    }

    if (!isfinite(initial_value)) {
        log_invalid_parameters(_("Initial value invalid"));
        return -1;
    }

    instance_t *inst = &obj->instances[iid];
    if (inst->initialized) {
        log_invalid_parameters(_("Instance already initialized"));
        return -1;
    }

    inst->initialized = true;
    inst->meta = *meta;
    inst->curr_value = initial_value;
    inst->min_value = initial_value;
    inst->max_value = initial_value;

    _anjay_notify_instances_changed_unlocked(anjay, oid);
    return 0;
}

int anjay_ipso_v2_basic_sensor_instance_add(
        anjay_t *anjay_locked,
        anjay_oid_t oid,
        anjay_iid_t iid,
        double initial_value,
        const anjay_ipso_v2_basic_sensor_meta_t *meta) {
    assert(anjay_locked);
    assert(meta);

    int res = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    res = sensor_instance_add_unlocked(anjay, oid, iid, initial_value, meta);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return res;
}

static int sensor_instance_remove_unlocked(anjay_unlocked_t *anjay,
                                           anjay_oid_t oid,
                                           anjay_iid_t iid) {
    object_t *obj = obj_from_oid(anjay, oid);
    if (!obj) {
        log_invalid_parameters(_("Object") " %d" _(" not installed"), oid);
        return -1;
    }

    if (iid >= obj->instance_count || !obj->instances[iid].initialized) {
        log_invalid_parameters(_("Object") " %d" _(" has no instance") " %d",
                               oid, iid);
        return -1;
    }

    obj->instances[iid].initialized = false;
    _anjay_notify_instances_changed_unlocked(anjay, oid);

    return 0;
}

int anjay_ipso_v2_basic_sensor_instance_remove(anjay_t *anjay_locked,
                                               anjay_oid_t oid,
                                               anjay_iid_t iid) {
    assert(anjay_locked);

    int res = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    res = sensor_instance_remove_unlocked(anjay, oid, iid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return res;
}

static int sensor_value_update_unlocked(anjay_unlocked_t *anjay,
                                        anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        double value) {
    object_t *obj = obj_from_oid(anjay, oid);
    if (!obj) {
        log_invalid_parameters(_("Object") " %d" _(" not installed"), oid);
        return -1;
    }

    instance_t *inst;
    if (iid > obj->instance_count
            || !(inst = &obj->instances[iid])->initialized) {
        log_invalid_parameters(_("Object") " %d" _(" has no instance") " %d",
                               oid, iid);
        return -1;
    }

    if (!isfinite(value)) {
        log_invalid_parameters(_("Update of") " /%d/%d" _(" failed"), oid, iid);
        return -1;
    }

    if (value != inst->curr_value) {
        inst->curr_value = value;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                              RID_SENSOR_VALUE);
    }
    if (inst->meta.min_max_measured_value_present) {
        if (value < inst->min_value) {
            inst->min_value = value;
            (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                                  RID_MIN_MEASURED_VALUE);
        }
        if (value > inst->max_value) {
            inst->max_value = value;
            (void) _anjay_notify_changed_unlocked(anjay, oid, iid,
                                                  RID_MAX_MEASURED_VALUE);
        }
    }

    return 0;
}

int anjay_ipso_v2_basic_sensor_value_update(anjay_t *anjay_locked,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid,
                                            double value) {
    assert(anjay_locked);

    int res = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    res = sensor_value_update_unlocked(anjay, oid, iid, value);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return res;
}

#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS_V2
