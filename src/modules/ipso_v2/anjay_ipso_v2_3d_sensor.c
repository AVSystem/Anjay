/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
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
 * Min X Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The minimum measured value along the X axis.
 */
#    define RID_MIN_X_VALUE 5508

/**
 * Max X Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The maximum measured value along the X axis.
 */
#    define RID_MAX_X_VALUE 5509

/**
 * Min Y Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The minimum measured value along the Y axis.
 */
#    define RID_MIN_Y_VALUE 5510

/**
 * Max Y Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The maximum measured value along the Y axis.
 */
#    define RID_MAX_Y_VALUE 5511

/**
 * Min Z Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The minimum measured value along the Z axis.
 */
#    define RID_MIN_Z_VALUE 5512

/**
 * Max Z Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The maximum measured value along the Z axis.
 */
#    define RID_MAX_Z_VALUE 5513

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
 * Sensor Units: R, Single, Optional
 * type: string, range: N/A, unit: N/A
 * Measurement Units Definition.
 */
#    define RID_SENSOR_UNITS 5701

/**
 * X Value: R, Single, Mandatory
 * type: float, range: N/A, unit: N/A
 * The measured value along the X axis.
 */
#    define RID_X_VALUE 5702

/**
 * Y Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The measured value along the Y axis.
 */
#    define RID_Y_VALUE 5703

/**
 * Z Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The measured value along the Z axis.
 */
#    define RID_Z_VALUE 5704

typedef anjay_ipso_v2_3d_sensor_meta_t sensor_meta_t;
typedef anjay_ipso_v2_3d_sensor_value_t sensor_value_t;

typedef struct {
    bool initialized;
    sensor_meta_t meta;
    sensor_value_t curr_value;
    sensor_value_t min_value;
    sensor_value_t max_value;
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
        _anjay_dm_emit_res_unlocked(ctx, RID_MIN_X_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
        _anjay_dm_emit_res_unlocked(ctx, RID_MAX_X_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
        if (inst->meta.y_axis_present) {
            _anjay_dm_emit_res_unlocked(ctx, RID_MIN_Y_VALUE, ANJAY_DM_RES_R,
                                        ANJAY_DM_RES_PRESENT);
            _anjay_dm_emit_res_unlocked(ctx, RID_MAX_Y_VALUE, ANJAY_DM_RES_R,
                                        ANJAY_DM_RES_PRESENT);
        }
        if (inst->meta.z_axis_present) {
            _anjay_dm_emit_res_unlocked(ctx, RID_MIN_Z_VALUE, ANJAY_DM_RES_R,
                                        ANJAY_DM_RES_PRESENT);
            _anjay_dm_emit_res_unlocked(ctx, RID_MAX_Z_VALUE, ANJAY_DM_RES_R,
                                        ANJAY_DM_RES_PRESENT);
        }
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
    if (inst->meta.unit) {
        _anjay_dm_emit_res_unlocked(ctx, RID_SENSOR_UNITS, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    _anjay_dm_emit_res_unlocked(ctx, RID_X_VALUE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    if (inst->meta.y_axis_present) {
        _anjay_dm_emit_res_unlocked(ctx, RID_Y_VALUE, ANJAY_DM_RES_R,
                                    ANJAY_DM_RES_PRESENT);
    }
    if (inst->meta.z_axis_present) {
        _anjay_dm_emit_res_unlocked(ctx, RID_Z_VALUE, ANJAY_DM_RES_R,
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
    case RID_MIN_X_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->min_value.x);

    case RID_MAX_X_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->max_value.x);

    case RID_MIN_Y_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.y_axis_present);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->min_value.y);

    case RID_MAX_Y_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.y_axis_present);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->max_value.y);

    case RID_MIN_Z_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.z_axis_present);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->min_value.z);

    case RID_MAX_Z_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.z_axis_present);
        assert(inst->meta.min_max_measured_value_present);
        return _anjay_ret_double_unlocked(ctx, inst->max_value.z);

    case RID_SENSOR_UNITS:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.unit);
        return _anjay_ret_string_unlocked(ctx, inst->meta.unit);

    case RID_X_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_double_unlocked(ctx, inst->curr_value.x);

    case RID_Y_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.y_axis_present);
        return _anjay_ret_double_unlocked(ctx, inst->curr_value.y);

    case RID_Z_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        assert(inst->meta.z_axis_present);
        return _anjay_ret_double_unlocked(ctx, inst->curr_value.z);

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

        if (inst->min_value.x != inst->curr_value.x) {
            inst->min_value.x = inst->curr_value.x;
            _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                           RID_MIN_X_VALUE);
        }
        if (inst->max_value.x != inst->curr_value.x) {
            inst->max_value.x = inst->curr_value.x;
            _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                           RID_MAX_X_VALUE);
        }

        if (inst->meta.y_axis_present) {
            if (inst->min_value.y != inst->curr_value.y) {
                inst->min_value.y = inst->curr_value.y;
                _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                               RID_MIN_Y_VALUE);
            }
            if (inst->max_value.y != inst->curr_value.y) {
                inst->max_value.y = inst->curr_value.y;
                _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                               RID_MAX_Y_VALUE);
            }
        }

        if (inst->meta.z_axis_present) {
            if (inst->min_value.z != inst->curr_value.z) {
                inst->min_value.z = inst->curr_value.z;
                _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                               RID_MIN_Z_VALUE);
            }
            if (inst->max_value.y != inst->curr_value.y) {
                inst->max_value.y = inst->curr_value.y;
                _anjay_notify_changed_unlocked(anjay, obj->def.oid, iid,
                                               RID_MAX_Y_VALUE);
            }
        }

        return 0;

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static object_t *obj_from_oid(anjay_unlocked_t *anjay, anjay_oid_t oid) {
    const anjay_dm_installed_object_t *installed_obj_ptr =
            _anjay_dm_find_object_by_oid(_anjay_get_dm(anjay), oid);
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

static inline bool value_valid(const sensor_meta_t *meta,
                               const sensor_value_t *value) {
    return isfinite(value->x) && (!meta->y_axis_present || isfinite(value->y))
           && (!meta->z_axis_present || isfinite(value->z));
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
    _ANJAY_ASSERT_INSTALLED_OBJECT_IS_FIRST_FIELD(object_t, installed_obj);
    entry = &obj->installed_obj;
    if (_anjay_register_object_unlocked(anjay, &entry)) {
        avs_free(obj);
        return -1;
    }

    return 0;
}

int anjay_ipso_v2_3d_sensor_install(anjay_t *anjay_locked,
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
                                        const sensor_value_t *initial_value,
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

    if (!value_valid(meta, initial_value)) {
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
    inst->curr_value = *initial_value;
    inst->min_value = *initial_value;
    inst->max_value = *initial_value;

    _anjay_notify_instances_changed_unlocked(anjay, oid);
    return 0;
}

int anjay_ipso_v2_3d_sensor_instance_add(anjay_t *anjay_locked,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid,
                                         const sensor_value_t *initial_value,
                                         const sensor_meta_t *meta) {
    assert(anjay_locked);
    assert(initial_value);
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

int anjay_ipso_v2_3d_sensor_instance_remove(anjay_t *anjay_locked,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid) {
    assert(anjay_locked);

    int res = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    res = sensor_instance_remove_unlocked(anjay, oid, iid);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return res;
}

static void update_curr_value(anjay_unlocked_t *anjay,
                              anjay_oid_t oid,
                              anjay_iid_t iid,
                              instance_t *inst,
                              const sensor_value_t *value) {
    if (value->x != inst->curr_value.x) {
        inst->curr_value.x = value->x;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_X_VALUE);
    }
    if (inst->meta.y_axis_present && value->y != inst->curr_value.y) {
        inst->curr_value.y = value->y;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_Y_VALUE);
    }
    if (inst->meta.z_axis_present && value->z != inst->curr_value.z) {
        inst->curr_value.z = value->z;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_Z_VALUE);
    }
}

static void update_x_min_max(anjay_unlocked_t *anjay,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             instance_t *inst,
                             const sensor_value_t *value) {
    if (value->x < inst->min_value.x) {
        inst->min_value.x = value->x;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_MIN_X_VALUE);
    }
    if (value->x > inst->max_value.x) {
        inst->max_value.x = value->x;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_MAX_X_VALUE);
    }
}

static void update_y_min_max(anjay_unlocked_t *anjay,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             instance_t *inst,
                             const sensor_value_t *value) {
    if (value->y < inst->min_value.y) {
        inst->min_value.y = value->y;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_MIN_Y_VALUE);
    }
    if (value->y > inst->max_value.y) {
        inst->max_value.y = value->y;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_MAX_Y_VALUE);
    }
}

static void update_z_min_max(anjay_unlocked_t *anjay,
                             anjay_oid_t oid,
                             anjay_iid_t iid,
                             instance_t *inst,
                             const sensor_value_t *value) {
    if (value->z < inst->min_value.z) {
        inst->min_value.z = value->z;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_MIN_Z_VALUE);
    }
    if (value->z > inst->max_value.z) {
        inst->max_value.z = value->z;
        (void) _anjay_notify_changed_unlocked(anjay, oid, iid, RID_MAX_Z_VALUE);
    }
}

static int sensor_value_update_unlocked(anjay_unlocked_t *anjay,
                                        anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        const sensor_value_t *value) {
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

    if (!value_valid(&inst->meta, value)) {
        log_invalid_parameters(_("Update of") " /%d/%d" _(" failed"), oid, iid);
        return -1;
    }

    update_curr_value(anjay, oid, iid, inst, value);
    if (inst->meta.min_max_measured_value_present) {
        update_x_min_max(anjay, oid, iid, inst, value);
        if (inst->meta.y_axis_present) {
            update_y_min_max(anjay, oid, iid, inst, value);
        }
        if (inst->meta.z_axis_present) {
            update_z_min_max(anjay, oid, iid, inst, value);
        }
    }

    return 0;
}

int anjay_ipso_v2_3d_sensor_value_update(anjay_t *anjay_locked,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid,
                                         const sensor_value_t *value) {
    assert(anjay_locked);
    assert(value);

    int res = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    res = sensor_value_update_unlocked(anjay, oid, iid, value);
    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return res;
}

#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS_V2
