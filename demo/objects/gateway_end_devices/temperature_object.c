/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */
#include <assert.h>
#include <stdbool.h>

#include <anjay/anjay.h>
#include <anjay/lwm2m_gateway.h>
#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_memory.h>

#include "temperature_object.h"

/**
 * Min Measured Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The minimum value measured by the sensor since power ON or reset.
 */
#define RID_MIN_MEASURED_VALUE 5601

/**
 * Max Measured Value: R, Single, Optional
 * type: float, range: N/A, unit: N/A
 * The maximum value measured by the sensor since power ON or reset.
 */
#define RID_MAX_MEASURED_VALUE 5602

/**
 * Reset Min and Max Measured Values: E, Single, Optional
 * type: N/A, range: N/A, unit: N/A
 * Reset the Min and Max Measured Values to Current Value.
 */
#define RID_RESET_MIN_AND_MAX_MEASURED_VALUES 5605

/**
 * Sensor Value: R, Single, Mandatory
 * type: float, range: N/A, unit: N/A
 * Last or Current Measured Value from the Sensor.
 */
#define RID_SENSOR_VALUE 5700

/**
 * Application Type: RW, Single, Optional
 * type: string, range: N/A, unit: N/A
 * The application type of the sensor or actuator as a string depending
 * on the use case.
 */
#define RID_APPLICATION_TYPE 5750

typedef struct temperature_instance_struct {
    anjay_iid_t iid;

    double value;
    double min_measured;
    double max_measured;
    char application_type[64];
    char application_type_backup[64];
} temperature_instance_t;

typedef struct temperature_object_struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(temperature_instance_t) instances;
    anjay_iid_t end_device_iid;
} temperature_object_t;

static inline temperature_object_t *
get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, temperature_object_t, def);
}

static temperature_instance_t *find_instance(const temperature_object_t *obj,
                                             anjay_iid_t iid) {
    AVS_LIST(temperature_instance_t) it;
    AVS_LIST_FOREACH(it, obj->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }

    return NULL;
}

static int list_instances(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    AVS_LIST(temperature_instance_t) it;
    AVS_LIST_FOREACH(it, get_obj(obj_ptr)->instances) {
        anjay_dm_emit(ctx, it->iid);
    }

    return 0;
}

static int init_instance(temperature_instance_t *inst, anjay_iid_t iid) {
    assert(iid != ANJAY_ID_INVALID);

    inst->iid = iid;
    return 0;
}

static void release_instance(temperature_instance_t *inst) {
    (void) inst;
}

static temperature_instance_t *add_instance(temperature_object_t *obj,
                                            anjay_iid_t iid) {
    assert(find_instance(obj, iid) == NULL);

    AVS_LIST(temperature_instance_t) created =
            AVS_LIST_NEW_ELEMENT(temperature_instance_t);
    if (!created) {
        return NULL;
    }

    int result = init_instance(created, iid);
    if (result) {
        AVS_LIST_CLEAR(&created);
        return NULL;
    }

    AVS_LIST(temperature_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &obj->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return created;
}

static int instance_create(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid) {
    (void) anjay;
    temperature_object_t *obj = get_obj(obj_ptr);

    return add_instance(obj, iid) ? 0 : ANJAY_ERR_INTERNAL;
}

static int instance_remove(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid) {
    (void) anjay;
    temperature_object_t *obj = get_obj(obj_ptr);

    AVS_LIST(temperature_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &obj->instances) {
        if ((*it)->iid == iid) {
            release_instance(*it);
            AVS_LIST_DELETE(it);
            return 0;
        } else if ((*it)->iid > iid) {
            break;
        }
    }

    assert(0);
    return ANJAY_ERR_NOT_FOUND;
}

static int instance_reset(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid) {
    (void) anjay;

    temperature_object_t *obj = get_obj(obj_ptr);
    temperature_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    inst->application_type[0] = '\0';
    return 0;
}

static int list_resources(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, RID_MIN_MEASURED_VALUE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_MAX_MEASURED_VALUE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_RESET_MIN_AND_MAX_MEASURED_VALUES,
                      ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_SENSOR_VALUE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_APPLICATION_TYPE, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay;

    temperature_object_t *obj = get_obj(obj_ptr);
    temperature_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_MIN_MEASURED_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_double(ctx, inst->min_measured);

    case RID_MAX_MEASURED_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_double(ctx, inst->max_measured);

    case RID_SENSOR_VALUE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_double(ctx, inst->value);

    case RID_APPLICATION_TYPE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, inst->application_type);

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_write(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void) anjay;

    temperature_object_t *obj = get_obj(obj_ptr);
    temperature_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_APPLICATION_TYPE: {
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_string(ctx, inst->application_type,
                                sizeof(inst->application_type));
    }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_execute(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_execute_ctx_t *arg_ctx) {
    (void) anjay;
    (void) arg_ctx;

    temperature_object_t *obj = get_obj(obj_ptr);
    temperature_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_RESET_MIN_AND_MAX_MEASURED_VALUES:
        inst->min_measured = NAN;
        inst->max_measured = NAN;
        return 0;

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int transaction_begin(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    temperature_object_t *obj = get_obj(obj_ptr);

    temperature_instance_t *element;
    AVS_LIST_FOREACH(element, obj->instances) {
        strcpy(element->application_type_backup, element->application_type);
    }
    return 0;
}

static int transaction_rollback(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    temperature_object_t *obj = get_obj(obj_ptr);

    temperature_instance_t *element;
    AVS_LIST_FOREACH(element, obj->instances) {
        strcpy(element->application_type, element->application_type_backup);
    }
    return 0;
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 3303,
    .version = "1.1",
    .handlers = {
        .list_instances = list_instances,
        .instance_create = instance_create,
        .instance_remove = instance_remove,
        .instance_reset = instance_reset,

        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
        .resource_execute = resource_execute,

        .transaction_begin = transaction_begin,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = transaction_rollback
    }
};

const anjay_dm_object_def_t **temperature_object_create(anjay_iid_t id) {
    temperature_object_t *obj =
            (temperature_object_t *) avs_calloc(1,
                                                sizeof(temperature_object_t));
    if (!obj) {
        return NULL;
    }
    obj->def = &OBJ_DEF;
    obj->end_device_iid = id;

    temperature_instance_t *inst = add_instance(obj, 0);
    sprintf(inst->application_type, "Sensor %d", id);

    return &obj->def;
}

void temperature_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        temperature_object_t *obj = get_obj(def);
        AVS_LIST_CLEAR(&obj->instances) {
            release_instance(obj->instances);
        }
        avs_free(obj);
    }
}

void temperature_object_update_value(anjay_t *anjay,
                                     const anjay_dm_object_def_t **def) {
    assert(anjay);
    temperature_object_t *obj = get_obj(def);

    AVS_LIST(temperature_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &obj->instances) {
        double new_value = ((double) (rand() % 12600) / 100.0) - 40.0;

        (*ptr)->value = new_value;

        if (isnan((*ptr)->min_measured) || new_value < (*ptr)->min_measured) {
            (*ptr)->min_measured = new_value;
        }
        if (isnan((*ptr)->max_measured) || new_value > (*ptr)->max_measured) {
            (*ptr)->max_measured = new_value;
        }
        anjay_lwm2m_gateway_notify_changed(anjay, obj->end_device_iid,
                                           (*def)->oid, (*ptr)->iid,
                                           RID_SENSOR_VALUE);
    }
}
