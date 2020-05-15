#include "test_object.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <avsystem/commons/avs_list.h>

typedef struct test_instance {
    anjay_iid_t iid;

    bool has_label;
    char label[32];

    bool has_value;
    int32_t value;
} test_instance_t;

typedef struct test_object {
    // handlers
    const anjay_dm_object_def_t *obj_def;

    // object state
    AVS_LIST(test_instance_t) instances;

    AVS_LIST(test_instance_t) backup_instances;
} test_object_t;

static test_object_t *get_test_object(const anjay_dm_object_def_t *const *obj) {
    assert(obj);

    // use the container_of pattern to retrieve test_object_t pointer
    // AVS_CONTAINER_OF macro provided by avsystem/commons/defs.h
    return AVS_CONTAINER_OF(obj, test_object_t, obj_def);
}

static AVS_LIST(test_instance_t) get_instance(test_object_t *repr,
                                              anjay_iid_t iid) {
    AVS_LIST(test_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            // Since list of instances is sorted by Instance ID,
            // Instance with given iid does not exist on that list
            break;
        }
    }
    // Instance was not found.
    return NULL;
}

static int test_list_instances(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_dm_list_ctx_t *ctx) {
    (void) anjay; // unused

    // iterate over all instances and return their IDs
    AVS_LIST(test_instance_t) it;
    AVS_LIST_FOREACH(it, get_test_object(obj_ptr)->instances) {
        anjay_dm_emit(ctx, it->iid);
    }

    return 0;
}

static int test_instance_create(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay; // unused

    test_object_t *repr = get_test_object(obj_ptr);

    AVS_LIST(test_instance_t) new_instance =
            AVS_LIST_NEW_ELEMENT(test_instance_t);

    if (!new_instance) {
        // out of memory
        return ANJAY_ERR_INTERNAL;
    }

    new_instance->iid = iid;

    // find a place where instance should be inserted,
    // insert it and claim a victory
    AVS_LIST(test_instance_t) *insert_ptr;
    AVS_LIST_FOREACH_PTR(insert_ptr, &repr->instances) {
        if ((*insert_ptr)->iid > new_instance->iid) {
            break;
        }
    }
    AVS_LIST_INSERT(insert_ptr, new_instance);
    return 0;
}

static int test_instance_remove(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay; // unused
    test_object_t *repr = get_test_object(obj_ptr);

    AVS_LIST(test_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_DELETE(it);
            return 0;
        }
    }
    // should never happen as Anjay checks whether instance is present
    // prior to issuing instance_remove
    return ANJAY_ERR_INTERNAL;
}

static int test_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay; // unused

    test_instance_t *instance = get_instance(get_test_object(obj_ptr), iid);
    // mark all Resource values for Object Instance `iid` as unset
    instance->has_label = false;
    instance->has_value = false;
    return 0;
}

static int test_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;   // unused
    (void) obj_ptr; // unused
    (void) iid;     // unused

    anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    return 0;
}

static int test_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay; // unused

    const test_instance_t *current_instance =
            (const test_instance_t *) get_instance(get_test_object(obj_ptr),
                                                   iid);

    assert(riid == ANJAY_ID_INVALID);

    switch (rid) {
    case 0:
        return anjay_ret_string(ctx, current_instance->label);
    case 1:
        return anjay_ret_i32(ctx, current_instance->value);
    default:
        // control will never reach this part due to test_list_resources
        return ANJAY_ERR_INTERNAL;
    }
}

static int test_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_riid_t riid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay; // unused

    test_instance_t *current_instance =
            (test_instance_t *) get_instance(get_test_object(obj_ptr), iid);

    assert(riid == ANJAY_ID_INVALID);

    switch (rid) {
    case 0: {
        // `anjay_get_string` may return a chunk of data instead of the
        // whole value - we need to make sure the client is able to hold
        // the entire value
        char buffer[sizeof(current_instance->label)];
        int result = anjay_get_string(ctx, buffer, sizeof(buffer));

        if (result == 0) {
            // value OK - save it
            memcpy(current_instance->label, buffer, sizeof(buffer));
            current_instance->has_label = true;
        } else if (result == ANJAY_BUFFER_TOO_SHORT) {
            // the value is too long to store in the buffer
            result = ANJAY_ERR_BAD_REQUEST;
        }

        return result;
    }

    case 1: {
        // reading primitive values can be done directly - the value will only
        // be written to the output variable if everything went fine
        int result = anjay_get_i32(ctx, &current_instance->value);
        if (result == 0) {
            current_instance->has_value = true;
        }
        return result;
    }

    default:
        // control will never reach this part due to test_list_resources
        return ANJAY_ERR_INTERNAL;
    }
}

static int test_transaction_begin(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay; // unused

    test_object_t *test = get_test_object(obj_ptr);

    // store a snapshot of object state
    test->backup_instances = AVS_LIST_SIMPLE_CLONE(test->instances);

    if (test->instances && !test->backup_instances) {
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static int
test_transaction_validate(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay; // unused

    test_object_t *test = get_test_object(obj_ptr);

    AVS_LIST(test_instance_t) it;

    // ensure all Object Instances contain all Mandatory Resources
    AVS_LIST_FOREACH(it, test->instances) {
        if (!it->has_label || !it->has_value) {
            // validation failed: Object state invalid, rollback required
            return ANJAY_ERR_BAD_REQUEST;
        }
    }

    // validation successful, can commit
    return 0;
}

static int
test_transaction_commit(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;   // unused
    (void) obj_ptr; // unused
    test_object_t *test = get_test_object(obj_ptr);

    // we free backup instances now, as current instance set is valid
    AVS_LIST_CLEAR(&test->backup_instances);
    return 0;
}

static int
test_transaction_rollback(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay; // unused

    test_object_t *test = get_test_object(obj_ptr);

    // restore saved object state
    AVS_LIST_CLEAR(&test->instances);
    test->instances = test->backup_instances;
    test->backup_instances = NULL;
    return 0;
}

static const anjay_dm_object_def_t OBJECT_DEF = {
    // Object ID
    .oid = 1234,

    .handlers = {
        .list_instances = test_list_instances,
        .instance_create = test_instance_create,
        .instance_remove = test_instance_remove,
        .instance_reset = test_instance_reset,

        .list_resources = test_list_resources,
        .resource_read = test_resource_read,
        .resource_write = test_resource_write,

        .transaction_begin = test_transaction_begin,
        .transaction_validate = test_transaction_validate,
        .transaction_commit = test_transaction_commit,
        .transaction_rollback = test_transaction_rollback
    }
};

const anjay_dm_object_def_t **create_test_object(void) {
    test_object_t *repr =
            (test_object_t *) avs_calloc(1, sizeof(test_object_t));
    if (repr) {
        repr->obj_def = &OBJECT_DEF;
        return &repr->obj_def;
    }
    return NULL;
}

void delete_test_object(const anjay_dm_object_def_t **obj) {
    if (!obj) {
        return;
    }
    test_object_t *repr = get_test_object(obj);
    AVS_LIST_CLEAR(&repr->instances);
    AVS_LIST_CLEAR(&repr->backup_instances);
    avs_free(repr);
}
