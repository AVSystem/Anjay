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
#include <avsystem/commons/avs_memory.h>

#include "push_button_object.h"

/**
 * Digital Input State: R, Single, Mandatory
 * type: boolean, range: N/A, unit: N/A
 * The current state of a digital input.
 */
#define RID_DIGITAL_INPUT_STATE 5500

/**
 * Digital Input Counter: R, Single, Optional
 * type: integer, range: N/A, unit: N/A
 * The cumulative value of active state detected.
 */
#define RID_DIGITAL_INPUT_COUNTER 5501

/**
 * Application Type: RW, Single, Optional
 * type: string, range: N/A, unit: N/A
 * The application type of the sensor or actuator as a string depending
 * on the use case.
 */
#define RID_APPLICATION_TYPE 5750

typedef struct push_button_instance_struct {
    bool digital_input_state;
    int32_t digital_input_counter;
    char application_type[64];
    char application_type_backup[64];
} push_button_instance_t;

typedef struct push_button_object_struct {
    const anjay_dm_object_def_t *def;
    push_button_instance_t instances[1];
    anjay_iid_t end_device_iid;
} push_button_object_t;

static inline push_button_object_t *
get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, push_button_object_t, def);
}

static int list_instances(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    push_button_object_t *obj = get_obj(obj_ptr);
    for (anjay_iid_t iid = 0; iid < AVS_ARRAY_SIZE(obj->instances); iid++) {
        anjay_dm_emit(ctx, iid);
    }

    return 0;
}

static int instance_reset(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid) {
    (void) anjay;

    push_button_object_t *obj = get_obj(obj_ptr);
    assert(iid < AVS_ARRAY_SIZE(obj->instances));
    push_button_instance_t *inst = &obj->instances[iid];

    inst->digital_input_state = false;
    inst->digital_input_counter = 0;
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

    anjay_dm_emit_res(ctx, RID_DIGITAL_INPUT_STATE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DIGITAL_INPUT_COUNTER, ANJAY_DM_RES_R,
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

    push_button_object_t *obj = get_obj(obj_ptr);
    assert(iid < AVS_ARRAY_SIZE(obj->instances));
    push_button_instance_t *inst = &obj->instances[iid];

    switch (rid) {
    case RID_DIGITAL_INPUT_STATE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_bool(ctx, inst->digital_input_state);

    case RID_DIGITAL_INPUT_COUNTER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, inst->digital_input_counter);

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

    push_button_object_t *obj = get_obj(obj_ptr);
    assert(iid < AVS_ARRAY_SIZE(obj->instances));
    push_button_instance_t *inst = &obj->instances[iid];

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

static int transaction_begin(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    push_button_object_t *obj = get_obj(obj_ptr);
    push_button_instance_t *instance = &obj->instances[0];
    strcpy(instance->application_type_backup, instance->application_type);

    return 0;
}

static int transaction_rollback(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;

    push_button_object_t *obj = get_obj(obj_ptr);
    push_button_instance_t *instance = &obj->instances[0];
    strcpy(instance->application_type, instance->application_type_backup);

    return 0;
}

const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 3347,
    .handlers = {
        .list_instances = list_instances,
        .instance_reset = instance_reset,
        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
        .transaction_begin = transaction_begin,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = transaction_rollback
    }
};

const anjay_dm_object_def_t **push_button_object_create(anjay_iid_t id) {
    push_button_object_t *obj =
            (push_button_object_t *) avs_calloc(1,
                                                sizeof(push_button_object_t));
    if (!obj) {
        return NULL;
    }
    obj->def = &OBJ_DEF;
    obj->instances[0].digital_input_state = false;
    obj->instances[0].digital_input_counter = 0;
    sprintf(obj->instances[0].application_type, "Button %d", id);
    obj->end_device_iid = id;

    return &obj->def;
}

void push_button_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        push_button_object_t *obj = get_obj(def);
        avs_free(obj);
    }
}

void push_button_press(anjay_t *anjay, const anjay_dm_object_def_t **def) {
    push_button_object_t *obj = get_obj(def);
    if (!obj->instances[0].digital_input_state) {
        obj->instances[0].digital_input_counter++;
        (void) anjay_lwm2m_gateway_notify_changed(
                anjay, obj->end_device_iid, 3347, 0, RID_DIGITAL_INPUT_COUNTER);
    }
    obj->instances[0].digital_input_state = true;
    (void) anjay_lwm2m_gateway_notify_changed(anjay, obj->end_device_iid, 3347,
                                              0, RID_DIGITAL_INPUT_STATE);
}

void push_button_release(anjay_t *anjay, const anjay_dm_object_def_t **def) {
    push_button_object_t *obj = get_obj(def);
    obj->instances[0].digital_input_state = false;
    (void) anjay_lwm2m_gateway_notify_changed(anjay, obj->end_device_iid, 3347,
                                              0, RID_DIGITAL_INPUT_STATE);
}
