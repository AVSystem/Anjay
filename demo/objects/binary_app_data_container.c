/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include "../objects.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>

#include <anjay/anjay.h>
#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_memory.h>

/**
 * Data: RW, Multiple, Mandatory
 * type: opaque, range: N/A, unit: N/A
 * Indicates the application data content.
 */
#define RID_DATA 0

/**
 * Data Priority: RW, Single, Optional
 * type: integer, range: 1 bytes, unit: N/A
 * Indicates the Application data priority: 0:Immediate 1:BestEffort
 * 2:Latest 3-100: Reserved for future use. 101-254: Proprietary mode.
 */
#define RID_DATA_PRIORITY 1

/**
 * Data Creation Time: RW, Single, Optional
 * type: time, range: N/A, unit: N/A
 * Indicates the Data instance creation timestamp.
 */
#define RID_DATA_CREATION_TIME 2

/**
 * Data Description: RW, Single, Optional
 * type: string, range: 32 bytes, unit: N/A
 * Indicates the data description. e.g. "meter reading".
 */
#define RID_DATA_DESCRIPTION 3

/**
 * Data Format: RW, Single, Optional
 * type: string, range: 32 bytes, unit: N/A
 * Indicates the format of the Application Data. e.g. YG-Meter-Water-
 * Reading UTF8-string
 */
#define RID_DATA_FORMAT 4

/**
 * App ID: RW, Single, Optional
 * type: integer, range: 2 bytes, unit: N/A
 * Indicates the destination Application ID.
 */
#define RID_APP_ID 5

#define MAX_BINARY_DATA_SIZE 1024

typedef struct {
    anjay_riid_t riid;
    uint8_t data[MAX_BINARY_DATA_SIZE];
    size_t data_length;
} data_resource_instance_t;

typedef struct binary_app_data_container_instance_struct {
    anjay_iid_t iid;
    AVS_LIST(data_resource_instance_t) data_list;
} binary_app_data_container_instance_t;

typedef struct binary_app_data_container_struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(binary_app_data_container_instance_t) instances;
    AVS_LIST(binary_app_data_container_instance_t) saved_instances;
} binary_app_data_container_t;

static inline binary_app_data_container_t *
get_obj(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, binary_app_data_container_t, def);
}

static binary_app_data_container_instance_t *
find_instance(const binary_app_data_container_t *obj, anjay_iid_t iid) {
    AVS_LIST(binary_app_data_container_instance_t) it;
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

    AVS_LIST(binary_app_data_container_instance_t) it;
    AVS_LIST_FOREACH(it, get_obj(obj_ptr)->instances) {
        anjay_dm_emit(ctx, it->iid);
    }

    return 0;
}

static int init_instance(binary_app_data_container_instance_t *inst,
                         anjay_iid_t iid) {
    assert(iid != ANJAY_ID_INVALID);

    inst->iid = iid;
    return 0;
}

static void release_instance(binary_app_data_container_instance_t *inst) {
    AVS_LIST_CLEAR(&inst->data_list);
}

static int instance_create(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid) {
    (void) anjay;
    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);

    AVS_LIST(binary_app_data_container_instance_t) created =
            AVS_LIST_NEW_ELEMENT(binary_app_data_container_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    int result = init_instance(created, iid);
    if (result) {
        AVS_LIST_CLEAR(&created);
        return result;
    }

    AVS_LIST(binary_app_data_container_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &obj->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return 0;
}

static int instance_remove(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid) {
    (void) anjay;
    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);

    AVS_LIST(binary_app_data_container_instance_t) *it;
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

    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);
    binary_app_data_container_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    AVS_LIST_CLEAR(&inst->data_list);
    return 0;
}

static int list_resources(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, RID_DATA, ANJAY_DM_RES_RWM, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void) anjay;

    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);
    binary_app_data_container_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_DATA: {
        data_resource_instance_t *it;
        AVS_LIST_FOREACH(it, inst->data_list) {
            if (it->riid == riid) {
                break;
            }
        }
        if (!it) {
            return ANJAY_ERR_NOT_FOUND;
        }
        return anjay_ret_bytes(ctx, it->data, it->data_length);
    }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static AVS_LIST(data_resource_instance_t) *
resource_instance_create(anjay_t *anjay,
                         binary_app_data_container_instance_t *inst,
                         anjay_riid_t riid) {
    (void) anjay;

    AVS_LIST(data_resource_instance_t) created =
            AVS_LIST_NEW_ELEMENT(data_resource_instance_t);
    if (!created) {
        return NULL;
    }

    created->riid = riid;

    AVS_LIST(data_resource_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &inst->data_list) {
        if ((*ptr)->riid > created->riid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return ptr;
}

static int resource_write(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void) anjay;

    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);
    binary_app_data_container_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_DATA: {
        AVS_LIST(data_resource_instance_t) *it;
        AVS_LIST_FOREACH_PTR(it, &inst->data_list) {
            if ((*it)->riid >= riid) {
                break;
            }
        }
        bool created = false;
        if (!(*it) || (*it)->riid != riid) {
            if (!(it = resource_instance_create(anjay, inst, riid))) {
                return ANJAY_ERR_INTERNAL;
            }
            created = true;
        }

        bool finished;
        int retval = anjay_get_bytes(ctx, &(*it)->data_length, &finished,
                                     (*it)->data, sizeof((*it)->data));

        if (!retval && !finished) {
            retval = ANJAY_ERR_INTERNAL;
        }

        if (retval) {
            if (created) {
                AVS_LIST_DELETE(it);
            } else {
                (*it)->data_length = 0;
            }
        }
        return retval;
    }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_reset(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid) {
    (void) anjay;

    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);
    binary_app_data_container_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_DATA:
        AVS_LIST_CLEAR(&inst->data_list);
        return 0;

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int list_resource_instances(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) ctx;

    binary_app_data_container_t *obj = get_obj(obj_ptr);
    assert(obj);
    binary_app_data_container_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_DATA: {
        data_resource_instance_t *it;
        AVS_LIST_FOREACH(it, inst->data_list) {
            anjay_dm_emit(ctx, it->riid);
        }
        return 0;
    }

    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int transaction_begin(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    binary_app_data_container_t *repr = get_obj(obj_ptr);
    if (!repr->instances) {
        return 0;
    }

    AVS_LIST(binary_app_data_container_instance_t) *saved_instances_tail =
            &repr->saved_instances;
    AVS_LIST(binary_app_data_container_instance_t) instance;
    bool failed = false;
    AVS_LIST_FOREACH(instance, repr->instances) {
        AVS_LIST(binary_app_data_container_instance_t) saved_instance =
                AVS_LIST_APPEND_NEW(binary_app_data_container_instance_t,
                                    saved_instances_tail);
        if (!saved_instance) {
            demo_log(ERROR, "cannot allocate a new instance");
            failed = true;
            break;
        }
        if (instance->data_list) {
            saved_instance->data_list =
                    AVS_LIST_SIMPLE_CLONE(instance->data_list);
            if (!saved_instance->data_list) {
                demo_log(ERROR, "cannot clone resource instances list");
                failed = true;
                break;
            }
        }
        saved_instance->iid = instance->iid;
        AVS_LIST_ADVANCE_PTR(&saved_instances_tail);
    }

    if (failed) {
        AVS_LIST_CLEAR(&repr->saved_instances) {
            AVS_LIST_CLEAR(&repr->saved_instances->data_list);
        }
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static int transaction_commit(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    binary_app_data_container_t *repr = get_obj(obj_ptr);
    AVS_LIST_CLEAR(&repr->saved_instances) {
        AVS_LIST_CLEAR(&repr->saved_instances->data_list);
    }
    return 0;
}

static int transaction_rollback(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    binary_app_data_container_t *repr = get_obj(obj_ptr);
    AVS_LIST_CLEAR(&repr->instances) {
        AVS_LIST_CLEAR(&repr->instances->data_list);
    }
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    return 0;
}

static const anjay_dm_object_def_t OBJ_DEF = {
    .oid = 19,
    .handlers = {
        .list_instances = list_instances,
        .instance_create = instance_create,
        .instance_remove = instance_remove,
        .instance_reset = instance_reset,

        .list_resources = list_resources,
        .resource_read = resource_read,
        .resource_write = resource_write,
        .resource_reset = resource_reset,
        .list_resource_instances = list_resource_instances,

        .transaction_begin = transaction_begin,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = transaction_commit,
        .transaction_rollback = transaction_rollback
    }
};

const anjay_dm_object_def_t **binary_app_data_container_object_create(void) {
    binary_app_data_container_t *obj =
            (binary_app_data_container_t *) avs_calloc(
                    1, sizeof(binary_app_data_container_t));
    if (!obj) {
        return NULL;
    }
    obj->def = &OBJ_DEF;

    return &obj->def;
}

void binary_app_data_container_object_release(
        const anjay_dm_object_def_t **def) {
    if (def) {
        binary_app_data_container_t *obj = get_obj(def);
        AVS_LIST_CLEAR(&obj->instances) {
            release_instance(obj->instances);
        }

        avs_free(obj);
    }
}

int binary_app_data_container_get_instances(const anjay_dm_object_def_t **def,
                                            AVS_LIST(anjay_iid_t) *out) {
    binary_app_data_container_t *obj = get_obj(def);
    assert(!*out);
    AVS_LIST(binary_app_data_container_instance_t) it;
    AVS_LIST_FOREACH(it, obj->instances) {
        if (!(*out = AVS_LIST_NEW_ELEMENT(anjay_iid_t))) {
            demo_log(ERROR, "out of memory");
            return -1;
        }
        **out = it->iid;
        AVS_LIST_ADVANCE_PTR(&out);
    }
    return 0;
}

int binary_app_data_container_write(anjay_t *anjay,
                                    const anjay_dm_object_def_t **def,
                                    anjay_iid_t iid,
                                    anjay_riid_t riid,
                                    const char *value) {
    size_t length = strlen(value);
    if (length > MAX_BINARY_DATA_SIZE) {
        demo_log(ERROR, "Value too long: %s", value);
        return -1;
    }

    binary_app_data_container_t *obj = get_obj(def);
    assert(obj);
    binary_app_data_container_instance_t *inst = find_instance(obj, iid);
    if (!inst) {
        demo_log(ERROR, "No such instance: %" PRIu16, iid);
        return -1;
    }
    AVS_LIST(data_resource_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &inst->data_list) {
        if ((*it)->riid >= riid) {
            break;
        }
    }
    if (!(*it) || (*it)->riid != riid) {
        if (!(it = resource_instance_create(anjay, inst, riid))) {
            return -1;
        }
    }

    memcpy((*it)->data, value, length);
    (*it)->data_length = length;
    anjay_notify_changed(anjay, (*def)->oid, iid, RID_DATA);
    return 0;
}
