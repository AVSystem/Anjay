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
#    include <stdbool.h>
#    include <string.h>

#    include <anjay/anjay.h>
#    include <anjay/dm.h>
#    include <anjay/ipso_objects.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_utils_core.h>

#    include <avsystem/commons/avs_defs.h>
#    include <avsystem/commons/avs_log.h>
#    include <avsystem/commons/avs_memory.h>

VISIBILITY_SOURCE_BEGIN

#    define PUSH_BUTTON_OID 3347
#    define PUSH_BUTTON_APPLICATION_TYPE_STR_LEN 40

/**
 * Digital Input State: R, Single, Mandatory
 * type: boolean, range: N/A, unit: N/A
 * The current state of a digital input.
 */
#    define RID_DIGITAL_INPUT_STATE 5500

/**
 * Digital Input Counter: R, Single, Optional
 * type: integer, range: N/A, unit: N/A
 * The cumulative value of active state detected.
 */
#    define RID_DIGITAL_INPUT_COUNTER 5501

/**
 * Application type: RW, Single, Optional
 * type: string, range: N/A, unit: N/A
 * The application type of the sensor or actuator
 * as a string depending on the use case.
 */
#    define RID_APPLICATION_TYPE 5750

typedef struct {
    bool initialized;

    bool pressed;
    uint16_t counter;
    char application_type[PUSH_BUTTON_APPLICATION_TYPE_STR_LEN];
    char application_type_backup[PUSH_BUTTON_APPLICATION_TYPE_STR_LEN];
} anjay_ipso_button_instance_t;

typedef struct {
    anjay_dm_installed_object_t obj_def_ptr;
    const anjay_unlocked_dm_object_def_t *obj_def;

    size_t num_instances;
    anjay_ipso_button_instance_t instances[];
} anjay_ipso_button_t;

static anjay_ipso_button_t *
get_obj(const anjay_dm_installed_object_t *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(obj_ptr),
                            anjay_ipso_button_t, obj_def);
}

static int ipso_button_list_instances(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj_ptr,
                                      anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    anjay_ipso_button_t *obj = get_obj(&obj_ptr);
    assert(obj);

    for (anjay_iid_t iid = 0; iid < obj->num_instances; iid++) {
        if (obj->instances[iid].initialized) {
            _anjay_dm_emit_unlocked(ctx, iid);
        }
    }

    return 0;
}

static int
ipso_button_list_resources(anjay_unlocked_t *anjay,
                           const anjay_dm_installed_object_t obj_ptr,
                           anjay_iid_t iid,
                           anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    _anjay_dm_emit_res_unlocked(ctx, RID_DIGITAL_INPUT_STATE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_DIGITAL_INPUT_COUNTER, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_APPLICATION_TYPE, ANJAY_DM_RES_RW,
                                ANJAY_DM_RES_PRESENT);

    return 0;
}

static int ipso_button_resource_read(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t obj_ptr,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     anjay_riid_t riid,
                                     anjay_unlocked_output_ctx_t *ctx) {
    (void) anjay;

    anjay_ipso_button_t *obj = get_obj(&obj_ptr);
    assert(obj);
    assert(iid < obj->num_instances);
    anjay_ipso_button_instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    switch (rid) {
    case RID_DIGITAL_INPUT_STATE:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_bool_unlocked(ctx, inst->pressed);

    case RID_DIGITAL_INPUT_COUNTER:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_i64_unlocked(ctx, inst->counter);

    case RID_APPLICATION_TYPE:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_ret_string_unlocked(ctx, inst->application_type);

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int ipso_button_resource_write(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj_ptr,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_riid_t riid,
                                      anjay_unlocked_input_ctx_t *ctx) {
    (void) anjay;

    anjay_ipso_button_t *obj = get_obj(&obj_ptr);
    assert(obj);
    assert(iid < obj->num_instances);
    anjay_ipso_button_instance_t *inst = &obj->instances[iid];
    assert(inst->initialized);

    switch (rid) {

    case RID_APPLICATION_TYPE:
        assert(riid == ANJAY_ID_INVALID);
        return _anjay_get_string_unlocked(ctx, inst->application_type,
                                          PUSH_BUTTON_APPLICATION_TYPE_STR_LEN);

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int ipso_button_transaction_begin(anjay_unlocked_t *anjay,
                                         anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;

    anjay_ipso_button_t *obj = get_obj(&obj_ptr);

    for (anjay_iid_t iid = 0; iid < obj->num_instances; iid++) {
        if (obj->instances[iid].initialized) {
            strcpy(obj->instances[iid].application_type_backup,
                   obj->instances[iid].application_type);
        }
    }

    return 0;
}

static int
ipso_button_transaction_rollback(anjay_unlocked_t *anjay,
                                 anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;

    anjay_ipso_button_t *obj = get_obj(&obj_ptr);

    for (anjay_iid_t iid = 0; iid < obj->num_instances; iid++) {
        if (obj->instances[iid].initialized) {
            strcpy(obj->instances[iid].application_type,
                   obj->instances[iid].application_type_backup);
        }
    }

    return 0;
}

static int ipso_button_transaction_NOOP(anjay_unlocked_t *anjay,
                                        anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    (void) obj_ptr;
    return 0;
}

static const anjay_unlocked_dm_object_def_t OBJECT_DEF = {
    .oid = PUSH_BUTTON_OID,
    .handlers = {
        .list_instances = ipso_button_list_instances,
        .list_resources = ipso_button_list_resources,
        .resource_read = ipso_button_resource_read,
        .resource_write = ipso_button_resource_write,

        .transaction_begin = ipso_button_transaction_begin,
        .transaction_validate = ipso_button_transaction_NOOP,
        .transaction_commit = ipso_button_transaction_NOOP,
        .transaction_rollback = ipso_button_transaction_rollback
    }
};

static anjay_ipso_button_t *obj_from_anjay(anjay_unlocked_t *anjay) {
    anjay_ipso_button_t *obj =
            get_obj(_anjay_dm_find_object_by_oid(anjay, PUSH_BUTTON_OID));

    // Checks if it is really an instance of anjay_ipso_button_t
    if (obj->obj_def == &OBJECT_DEF) {
        return obj;
    } else {
        return NULL;
    }
}

int anjay_ipso_button_install(anjay_t *anjay_locked, size_t num_instances) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    AVS_LIST(anjay_dm_installed_object_t) entry;
    anjay_ipso_button_t *obj = (anjay_ipso_button_t *) AVS_LIST_NEW_BUFFER(
            sizeof(anjay_ipso_button_t)
            + num_instances * sizeof(anjay_ipso_button_instance_t));
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Out of memory"));
        err = -1;
        goto finish;
    }

    obj->obj_def = &OBJECT_DEF;
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

int anjay_ipso_button_instance_add(anjay_t *anjay_locked,
                                   anjay_iid_t iid,
                                   const char *application_type) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    anjay_ipso_button_instance_t *inst;
    anjay_ipso_button_t *obj = obj_from_anjay(anjay);
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Push Button Object not installed"));
        err = -1;
        goto finish;
    }

    if (iid >= obj->num_instances) {
        _anjay_log(ipso, ERROR, _("IID too large"));
        err = -1;
        goto finish;
    }

    if (strlen(application_type) >= PUSH_BUTTON_APPLICATION_TYPE_STR_LEN) {
        _anjay_log(ipso, ERROR, _("Application Type is too long"));
        err = -1;
        goto finish;
    }

    inst = &obj->instances[iid];
    inst->initialized = true;
    inst->counter = 0;
    inst->pressed = false;

    (void) strcpy(inst->application_type, application_type);

    _anjay_notify_instances_changed_unlocked(anjay, PUSH_BUTTON_OID);

    (void) _anjay_notify_changed_unlocked(anjay, PUSH_BUTTON_OID, iid,
                                          RID_DIGITAL_INPUT_COUNTER);
    (void) _anjay_notify_changed_unlocked(anjay, PUSH_BUTTON_OID, iid,
                                          RID_DIGITAL_INPUT_STATE);
    (void) _anjay_notify_changed_unlocked(anjay, PUSH_BUTTON_OID, iid,
                                          RID_APPLICATION_TYPE);

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

int anjay_ipso_button_instance_remove(anjay_t *anjay_locked, anjay_iid_t iid) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    anjay_ipso_button_t *obj = obj_from_anjay(anjay);
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Push Button Object not installed"));
        err = -1;
        goto finish;
    }

    if (iid >= obj->num_instances || !obj->instances[iid].initialized) {
        _anjay_log(ipso, ERROR, _("Push Button Object has no instance") " %d",
                   iid);
        err = -1;
    } else {
        obj->instances[iid].initialized = false;
    }

    _anjay_notify_instances_changed_unlocked(anjay, PUSH_BUTTON_OID);

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

int anjay_ipso_button_update(anjay_t *anjay_locked,
                             anjay_iid_t iid,
                             bool pressed) {
    if (!anjay_locked) {
        _anjay_log(ipso, ERROR, _("Anjay pointer is NULL"));
        return -1;
    }

    int err = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    anjay_ipso_button_t *obj = obj_from_anjay(anjay);
    if (!obj) {
        _anjay_log(ipso, ERROR, _("Push Button Object not installed"));
        err = -1;
        goto finish;
    }

    anjay_ipso_button_instance_t *inst;
    if (iid > obj->num_instances
            || !(inst = &obj->instances[iid])->initialized) {
        _anjay_log(ipso, ERROR, _("Push Button Object has no instance") " %d",
                   iid);
        err = -1;
        goto finish;
    }

    if (inst->pressed != pressed) {
        inst->pressed = pressed;
        (void) _anjay_notify_changed_unlocked(anjay, PUSH_BUTTON_OID, iid,
                                              RID_DIGITAL_INPUT_STATE);

        if (inst->pressed) {
            inst->counter++;
            (void) _anjay_notify_changed_unlocked(anjay, PUSH_BUTTON_OID, iid,
                                                  RID_DIGITAL_INPUT_COUNTER);
        }
    }

finish:;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS
