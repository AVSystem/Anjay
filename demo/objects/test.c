/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <string.h>
#include <assert.h>

#include "../objects.h"
#include "../utils.h"

#include <avsystem/commons/vector.h>

#define TEST_RES_TIMESTAMP            0
#define TEST_RES_COUNTER              1
#define TEST_RES_INCREMENT_COUNTER    2
#define TEST_RES_INT_ARRAY            3
#define TEST_RES_LAST_EXEC_ARGS_ARRAY 4
#define TEST_RES_BYTES                5
#define TEST_RES_BYTES_SIZE           6
#define TEST_RES_BYTES_BURST          7
#define TEST_RES_EMPTY                8
#define TEST_RES_INIT_INT_ARRAY       9
#define TEST_RES_RAW_BYTES            10

#define _TEST_RES_COUNT               11

typedef struct test_array_entry_struct {
    anjay_riid_t index;
    int32_t value;
} test_array_entry_t;

typedef struct {
    int number;
    char *value;
} test_exec_arg_t;

typedef struct test_instance_struct {
    anjay_iid_t iid;
    int32_t execute_counter;
    bool volatile_res_present;
    int32_t volatile_res_value;
    int32_t bytes_size;
    int32_t bytes_burst;
    void *raw_bytes;
    size_t raw_bytes_size;
    AVS_LIST(test_array_entry_t) array;
    AVS_LIST(test_exec_arg_t) last_exec_args;
} test_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(struct test_instance_struct) instances;
} test_repr_t;

static inline test_repr_t *
get_test(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, test_repr_t, def);
}

static test_instance_t *find_instance(const test_repr_t *repr,
                                      anjay_iid_t iid) {
    AVS_LIST(test_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        }
    }

    return NULL;
}

static int test_instance_present(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid) {
    (void)anjay;
    return find_instance(get_test(obj_ptr), iid) != NULL;
}

static int test_instance_it(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t *out,
                            void **cookie) {
    (void)anjay;

    test_repr_t* test = get_test(obj_ptr);
    AVS_LIST(test_instance_t) curr = (AVS_LIST(test_instance_t))*cookie;

    if (!curr) {
        curr = test->instances;
    } else {
        curr = AVS_LIST_NEXT(curr);
    }

    *out = curr ? curr->iid : ANJAY_IID_INVALID;
    *cookie = curr;
    return 0;
}

static anjay_iid_t get_new_iid(AVS_LIST(test_instance_t) instances) {
    anjay_iid_t iid = 1;
    AVS_LIST(test_instance_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static int test_instance_create(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t *inout_iid,
                                anjay_ssid_t ssid) {
    (void) anjay; (void) ssid;
    test_repr_t* test = get_test(obj_ptr);

    AVS_LIST(test_instance_t) created = AVS_LIST_NEW_ELEMENT(test_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    if (*inout_iid == ANJAY_IID_INVALID) {
        *inout_iid = get_new_iid(test->instances);
        if (*inout_iid == ANJAY_IID_INVALID) {
            AVS_LIST_CLEAR(&created);
            return ANJAY_ERR_INTERNAL;
        }
    }

    created->iid = *inout_iid;
    created->execute_counter = 0;
    created->bytes_size = 0;
    created->bytes_burst = 1000;

    AVS_LIST(test_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &test->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return 0;
}

static void release_instance(test_instance_t *inst) {
    AVS_LIST_CLEAR(&inst->last_exec_args) {
        free(inst->last_exec_args->value);
    }

    free(inst->raw_bytes);
    AVS_LIST_CLEAR(&inst->array);
}

static int test_instance_remove(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void)anjay;
    test_repr_t* test = get_test(obj_ptr);

    AVS_LIST(test_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &test->instances) {
        if ((*it)->iid == iid) {
            release_instance(*it);
            AVS_LIST_DELETE(it);
            return 0;
        }
    }

    return ANJAY_ERR_NOT_FOUND;
}

static int test_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    test_instance_t *inst = find_instance(get_test(obj_ptr), iid);
    inst->volatile_res_present = false;
    return 0;
}

static int test_resource_supported(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_rid_t rid) {
    (void) anjay;
    (void) obj_ptr;
    switch (rid) {
    case TEST_RES_TIMESTAMP:
    case TEST_RES_COUNTER:
    case TEST_RES_INCREMENT_COUNTER:
    case TEST_RES_INT_ARRAY:
    case TEST_RES_LAST_EXEC_ARGS_ARRAY:
    case TEST_RES_BYTES:
    case TEST_RES_BYTES_SIZE:
    case TEST_RES_BYTES_BURST:
    case TEST_RES_EMPTY:
    case TEST_RES_INIT_INT_ARRAY:
    case TEST_RES_RAW_BYTES:
        return 1;
    default:
        return 0;
    }

}

static int test_resource_present(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid) {
    (void) anjay;
    (void) iid;
    return test_resource_supported(anjay, obj_ptr, rid);
}

static int test_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_output_ctx_t *ctx) {
    (void)anjay;

    test_repr_t* test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case TEST_RES_TIMESTAMP:
        return anjay_ret_i32(ctx, (int32_t) time(NULL));
    case TEST_RES_COUNTER:
        return anjay_ret_i32(ctx, (int32_t) inst->execute_counter);
    case TEST_RES_INT_ARRAY: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array) {
            return ANJAY_ERR_INTERNAL;
        }

        if (inst->array) {
            test_array_entry_t *it;
            AVS_LIST_FOREACH(it, inst->array) {
                if (anjay_ret_array_index(array, it->index)
                    || anjay_ret_i32(array, it->value)) {
                    return ANJAY_ERR_INTERNAL;
                }
            }
        }
        return anjay_ret_array_finish(array);
    }
    case TEST_RES_LAST_EXEC_ARGS_ARRAY: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array) {
            return ANJAY_ERR_INTERNAL;
        }

        if (inst->last_exec_args) {
            test_exec_arg_t *it;
            AVS_LIST_FOREACH(it, inst->last_exec_args) {
                if (anjay_ret_array_index(array, (anjay_riid_t)it->number)
                    || anjay_ret_string(array, it->value ? it->value : "")) {
                    return ANJAY_ERR_INTERNAL;
                }
            }
        }
        return anjay_ret_array_finish(array);
    }
    case TEST_RES_BYTES: {
        if (!inst->bytes_size) {
            return 0;
        }
        anjay_ret_bytes_ctx_t *bytes_ctx =
                anjay_ret_bytes_begin(ctx, (size_t) inst->bytes_size);
        char buffer[inst->bytes_burst];
        int result = 0;
        int32_t counter = 0;
        for (int32_t offset = 0; offset < inst->bytes_size; offset += inst->bytes_burst) {
            int32_t bytes_to_write = inst->bytes_size - offset;
            if (bytes_to_write > inst->bytes_burst) {
                bytes_to_write = inst->bytes_burst;
            }
            for (int32_t i = 0; i < bytes_to_write; ++i) {
                buffer[i] = (char) (counter++ % 128);
            }
            result = anjay_ret_bytes_append(bytes_ctx, buffer,
                                            (size_t) bytes_to_write);
            if (result) {
                break;
            }
        }
        return result;
    }
    case TEST_RES_RAW_BYTES:
        if (!inst->raw_bytes_size) {
            return 0;
        }
        anjay_ret_bytes_ctx_t *bytes_ctx =
                anjay_ret_bytes_begin(ctx, inst->raw_bytes_size);
        if (!bytes_ctx) {
            return -1;
        }
        return anjay_ret_bytes_append(bytes_ctx, inst->raw_bytes,
                                      inst->raw_bytes_size);
    case TEST_RES_BYTES_SIZE:
        return anjay_ret_i32(ctx, inst->bytes_size);
    case TEST_RES_BYTES_BURST:
        return anjay_ret_i32(ctx, inst->bytes_burst);
    case TEST_RES_EMPTY:
        return 0; // trololo, see T832
    case TEST_RES_INCREMENT_COUNTER:
    case TEST_RES_INIT_INT_ARRAY:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int test_resource_write_to_array(AVS_LIST(test_array_entry_t) *inst_array,
                                        anjay_input_ctx_t *input_array) {
    assert(inst_array);
    assert(input_array);
    int result;
    test_array_entry_t entry;

    /* New element will be inserted to the array, value of existing one will get
     * overwritten. */
    while ((result = anjay_get_array_index(input_array, &entry.index)) == 0) {
        if (anjay_get_i32(input_array, &entry.value)) {
            demo_log(ERROR, "could not read integer");
            return ANJAY_ERR_INTERNAL;
        }
        bool value_updated = false;
        test_array_entry_t *it;
        AVS_LIST_FOREACH(it, *inst_array) {
            if (it->index == entry.index) {
                it->value = entry.value;
                value_updated = true;
                break;
            }
        }

        if (!value_updated) {
            AVS_LIST(test_array_entry_t) list_entry =
                AVS_LIST_NEW_ELEMENT(test_array_entry_t);
            if (!list_entry) {
                demo_log(ERROR, "out of memory");
                return ANJAY_ERR_INTERNAL;
            }
            *list_entry = entry;
            AVS_LIST_APPEND(inst_array, list_entry);
        }
    }

    if (result && result != ANJAY_GET_INDEX_END) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int test_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_input_ctx_t *ctx) {
    (void)anjay;

    test_repr_t* test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case TEST_RES_COUNTER:
        return anjay_get_i32(ctx, &inst->execute_counter);
    case TEST_RES_INT_ARRAY: {
        anjay_input_ctx_t *input_array = anjay_get_array(ctx);
        if (!input_array) {
            return ANJAY_ERR_INTERNAL;
        }

        AVS_LIST_CLEAR(&inst->array);
        return test_resource_write_to_array(&inst->array, input_array);
    }
    case TEST_RES_BYTES_SIZE: {
        int32_t value;
        int result = anjay_get_i32(ctx, &value);
        if (result) {
            return result;
        }
        if (value < 0) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->bytes_size = value;
        return 0;
    }
    case TEST_RES_BYTES_BURST: {
        int32_t value;
        int result = anjay_get_i32(ctx, &value);
        if (result) {
            return result;
        }
        /* Prevent infinite loop while bursting data. */
        if (value <= 0) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->bytes_burst = value;
        return 0;
    }
    case TEST_RES_RAW_BYTES:
        return fetch_bytes(ctx, &inst->raw_bytes, &inst->raw_bytes_size);
    case TEST_RES_BYTES:
    case TEST_RES_TIMESTAMP:
    case TEST_RES_INCREMENT_COUNTER:
    case TEST_RES_LAST_EXEC_ARGS_ARRAY:
    case TEST_RES_INIT_INT_ARRAY:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int read_exec_arg_value(anjay_execute_ctx_t *arg_ctx,
                               char **out_string) {
#define VALUE_CHUNK_SIZE 256
    size_t bytes_read = 0;
    ssize_t result = 0;

    while (true) {
        size_t new_value_size = bytes_read + VALUE_CHUNK_SIZE;

        char *new_string = (char*)realloc(*out_string, new_value_size);
        if (!new_string) {
            demo_log(ERROR, "out of memory");
            result = ANJAY_ERR_INTERNAL;
            goto fail;
        }

        *out_string = new_string;

        result = anjay_execute_get_arg_value(arg_ctx, new_string + bytes_read,
                                             (ssize_t)VALUE_CHUNK_SIZE);

        if (result < 0) {
            demo_log(ERROR, "could not read arg value: %d", (int)result);
            goto fail;
        } else if ((size_t)result != VALUE_CHUNK_SIZE - 1) {
            // nothing more to read, we're done
            break;
        }

        // incomplete read; bigger buffer required
        bytes_read += (size_t)result;
    }

    return 0;

fail:
    free(*out_string);
    *out_string = NULL;
    return (int)result;
}

static int read_exec_arg(anjay_execute_ctx_t *arg_ctx,
                         AVS_LIST(test_exec_arg_t) *insert_ptr) {
    int arg_number;
    bool has_value;

    int result = anjay_execute_get_next_arg(arg_ctx, &arg_number, &has_value);
    if (result) {
        return result;
    }

    AVS_LIST(test_exec_arg_t) arg = AVS_LIST_NEW_ELEMENT(test_exec_arg_t);
    if (!arg) {
        demo_log(ERROR, "out of memory");
        return ANJAY_ERR_INTERNAL;
    }

    arg->number = arg_number;

    if (has_value) {
        int result = read_exec_arg_value(arg_ctx, &arg->value);
        if (result) {
            AVS_LIST_DELETE(&arg);
            demo_log(ERROR, "could not get read arg %d value", arg_number);
            return result;
        }
    }

    AVS_LIST_INSERT(insert_ptr, arg);
    return 0;
}

static int read_exec_args(anjay_execute_ctx_t *arg_ctx,
                          AVS_LIST(test_exec_arg_t) *out_args) {
    AVS_LIST_CLEAR(out_args) {
        free((*out_args)->value);
    }

    int result;
    AVS_LIST(test_exec_arg_t) *tail = out_args;

    while ((result = read_exec_arg(arg_ctx, tail)) == 0) {
        demo_log(DEBUG, "got arg %d", (*tail)->number);
        tail = AVS_LIST_NEXT_PTR(tail);
    }

    if (result == ANJAY_EXECUTE_GET_ARG_END) {
        return 0;
    }

    AVS_LIST_CLEAR(out_args) {
        free((*out_args)->value);
    }
    return result;
}

static int init_int_array_read_element(test_array_entry_t *out_entry,
                                       anjay_execute_ctx_t *arg_ctx) {
    int arg_number;
    bool has_value;

    ssize_t result = anjay_execute_get_next_arg(arg_ctx, &arg_number, &has_value);
    if (result) {
        return (int)result;
    }

    char value_buf[16];
    result = anjay_execute_get_arg_value(arg_ctx, value_buf, sizeof(value_buf));
    if (result < 0) {
        return (int)result;
    }

    long value;
    if (demo_parse_long(value_buf, &value)
            || value < INT32_MIN
            || value > INT32_MAX) {
        demo_log(WARNING, "invalid resource %d value", arg_number);
        return ANJAY_ERR_BAD_REQUEST;
    }

    assert(0 <= arg_number && arg_number <= UINT16_MAX);
    out_entry->index = (anjay_riid_t)arg_number;
    out_entry->value = (int32_t)value;
    return 0;
}

static int init_int_array(AVS_LIST(test_array_entry_t) *out_array,
                          anjay_execute_ctx_t *arg_ctx) {
    AVS_LIST(test_array_entry_t) new_array = NULL;
    AVS_LIST(test_array_entry_t) *end = &new_array;

    int result = 0;
    while (!result) {
        test_array_entry_t entry;
        result = init_int_array_read_element(&entry, arg_ctx);

        if (!result) {
            AVS_LIST(test_array_entry_t) list_entry =
                    AVS_LIST_INSERT_NEW(test_array_entry_t, end);

            if (!list_entry) {
                demo_log(ERROR, "out of memory");
                result = ANJAY_ERR_INTERNAL;
            } else {
                *list_entry = entry;
            }
        }
    }

    if (result != ANJAY_EXECUTE_GET_ARG_END) {
        AVS_LIST_CLEAR(&new_array);
        return result;
    }

    AVS_LIST_CLEAR(out_array);
    *out_array = new_array;
    return 0;
}

static int test_resource_execute(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_execute_ctx_t *arg_ctx) {
    (void)arg_ctx;

    test_repr_t* test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case TEST_RES_TIMESTAMP:
    case TEST_RES_COUNTER:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    case TEST_RES_INCREMENT_COUNTER:
        {
            int result = read_exec_args(arg_ctx, &inst->last_exec_args);
            if (result) {
                demo_log(ERROR, "could not save Execute arguments");
                return result;
            }

            ++inst->execute_counter;
            anjay_notify_changed(anjay, (*obj_ptr)->oid, iid, TEST_RES_COUNTER);
            return 0;
        }
    case TEST_RES_INIT_INT_ARRAY:
        return init_int_array(&inst->array, arg_ctx);
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int test_resource_dim(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid) {
    (void) anjay;

    test_repr_t *test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case TEST_RES_INT_ARRAY:
        return (int) AVS_LIST_SIZE(inst->array);
    case TEST_RES_LAST_EXEC_ARGS_ARRAY:
        return (int) AVS_LIST_SIZE(inst->last_exec_args);
    default:
        return ANJAY_DM_DIM_INVALID;
    }
}

const anjay_dm_object_def_t TEST_OBJECT = {
    .oid = 1337,
    .rid_bound = _TEST_RES_COUNT,

    .instance_it = test_instance_it,
    .instance_present = test_instance_present,
    .instance_create = test_instance_create,
    .instance_remove = test_instance_remove,
    .instance_reset = test_instance_reset,
    .resource_supported = test_resource_supported,
    .resource_present = test_resource_present,
    .resource_read = test_resource_read,
    .resource_write = test_resource_write,
    .resource_execute = test_resource_execute,
    .resource_dim = test_resource_dim,
    .transaction_begin = anjay_dm_transaction_NOOP,
    .transaction_validate = anjay_dm_transaction_NOOP,
    .transaction_commit = anjay_dm_transaction_NOOP,
    .transaction_rollback = anjay_dm_transaction_NOOP
};

const anjay_dm_object_def_t **test_object_create(void) {
    test_repr_t *repr = (test_repr_t*)calloc(1, sizeof(test_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &TEST_OBJECT;

    return &repr->def;
}

void test_object_release(const anjay_dm_object_def_t **def) {
    test_repr_t *repr = get_test(def);
    if (repr) {
        AVS_LIST_CLEAR(&repr->instances) {
            release_instance(repr->instances);
        }
        free(repr);
    }
}

void test_notify_time_dependent(anjay_t *anjay,
                                const anjay_dm_object_def_t **def) {
    test_repr_t *repr = get_test(def);
    struct test_instance_struct *it;
    AVS_LIST_FOREACH(it, repr->instances) {
        anjay_notify_changed(anjay, (*def)->oid, it->iid, TEST_RES_TIMESTAMP);
    }
}
