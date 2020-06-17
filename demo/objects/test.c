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

#include "../demo.h"
#include "../demo_utils.h"
#include "../objects.h"

#include <assert.h>
#include <string.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_utils.h>
#include <avsystem/commons/avs_vector.h>

#define TEST_RES_TIMESTAMP 0
#define TEST_RES_COUNTER 1
#define TEST_RES_INCREMENT_COUNTER 2
#define TEST_RES_INT_ARRAY 3
#define TEST_RES_LAST_EXEC_ARGS_ARRAY 4
#define TEST_RES_BYTES 5
#define TEST_RES_BYTES_SIZE 6
#define TEST_RES_BYTES_BURST 7
// ID 8 was historically used for TEST_RES_EMPTY
#define TEST_RES_INIT_INT_ARRAY 9
#define TEST_RES_RAW_BYTES 10
#define TEST_RES_OPAQUE_ARRAY 11
#define TEST_RES_INT 12
#define TEST_RES_BOOL 13
#define TEST_RES_FLOAT 14
#define TEST_RES_STRING 15
#define TEST_RES_OBJLNK 16
#define TEST_RES_BYTES_ZERO_BEGIN 17
#define TEST_RES_DOUBLE 18

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
    bool bytes_zero_begin;
    void *raw_bytes;
    size_t raw_bytes_size;
    AVS_LIST(test_array_entry_t) array;
    AVS_LIST(test_exec_arg_t) last_exec_args;
    int32_t test_res_int;
    uint32_t test_res_uint;
    uint64_t test_res_ulong;
    bool test_res_bool;
    float test_res_float;
    double test_res_double;
    char test_res_string[128];
    struct {
        anjay_oid_t oid;
        anjay_iid_t iid;
    } test_res_objlnk;
} test_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(struct test_instance_struct) instances;
} test_repr_t;

static inline test_repr_t *
get_test(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, test_repr_t, def);
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

static int test_list_instances(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    test_repr_t *test = get_test(obj_ptr);
    AVS_LIST(test_instance_t) it;
    AVS_LIST_FOREACH(it, test->instances) {
        anjay_dm_emit(ctx, it->iid);
    }
    return 0;
}

static int test_instance_create(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    test_repr_t *test = get_test(obj_ptr);

    AVS_LIST(test_instance_t) created = AVS_LIST_NEW_ELEMENT(test_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    created->iid = iid;
    created->execute_counter = 0;
    created->bytes_size = 0;
    created->bytes_burst = 1000;
    created->bytes_zero_begin = true;

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
        avs_free(inst->last_exec_args->value);
    }

    avs_free(inst->raw_bytes);
    AVS_LIST_CLEAR(&inst->array);
}

static int test_instance_remove(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
    (void) anjay;
    test_repr_t *test = get_test(obj_ptr);

    AVS_LIST(test_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &test->instances) {
        if ((*it)->iid == iid) {
            release_instance(*it);
            AVS_LIST_DELETE(it);
            return 0;
        }
    }

    assert(0);
    return ANJAY_ERR_NOT_FOUND;
}

static int test_instance_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;

    test_instance_t *inst = find_instance(get_test(obj_ptr), iid);
    assert(inst);
    inst->volatile_res_present = false;
    return 0;
}

static int test_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    anjay_dm_emit_res(ctx, TEST_RES_TIMESTAMP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_COUNTER, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_INCREMENT_COUNTER, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_INT_ARRAY, ANJAY_DM_RES_RWM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_LAST_EXEC_ARGS_ARRAY, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_BYTES, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_BYTES_SIZE, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_BYTES_BURST, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_INIT_INT_ARRAY, ANJAY_DM_RES_E,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_RAW_BYTES, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_OPAQUE_ARRAY, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_INT, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_BOOL, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_FLOAT, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_STRING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_OBJLNK, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_BYTES_ZERO_BEGIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, TEST_RES_DOUBLE, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

static int test_resource_read(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_output_ctx_t *ctx) {
    (void) anjay;

    test_repr_t *test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    assert(inst);

    switch (rid) {
    case TEST_RES_TIMESTAMP:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i64(ctx, avs_time_real_now().since_real_epoch.seconds);
    case TEST_RES_COUNTER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, (int32_t) inst->execute_counter);
    case TEST_RES_INT_ARRAY: {
        test_array_entry_t *it;
        AVS_LIST_FOREACH(it, inst->array) {
            if (it->index == riid) {
                return anjay_ret_i32(ctx, it->value);
            }
        }
        return ANJAY_ERR_NOT_FOUND;
    }
    case TEST_RES_LAST_EXEC_ARGS_ARRAY: {
        test_exec_arg_t *it = NULL;
        AVS_LIST_FOREACH(it, inst->last_exec_args) {
            if ((anjay_riid_t) it->number == riid) {
                return anjay_ret_string(ctx, it->value ? it->value : "");
            }
        }
        return ANJAY_ERR_NOT_FOUND;
    }
    case TEST_RES_BYTES: {
        assert(riid == ANJAY_ID_INVALID);
        int result = 0;
        if (!inst->bytes_size && !inst->bytes_zero_begin) {
            // We used to have a bug that caused the library to segfault
            // if a resource_read handler does not call any anjay_ret_*
            // function. This case is used to check whether we do not
            // crash in such situations. See T832.
            return 0;
        }
        anjay_ret_bytes_ctx_t *bytes_ctx =
                anjay_ret_bytes_begin(ctx, (size_t) inst->bytes_size);
        if (!bytes_ctx) {
            return ANJAY_ERR_INTERNAL;
        }
        if (inst->bytes_size) {
            int32_t bytes_burst = inst->bytes_burst;
            if (bytes_burst <= 0) {
                bytes_burst = inst->bytes_size;
            }
            char *buffer = (char *) avs_malloc((size_t) bytes_burst);
            if (!buffer) {
                demo_log(ERROR, "Out of memory");
                return -1;
            }
            int32_t counter = 0;
            for (int32_t offset = 0; offset < inst->bytes_size;
                 offset += bytes_burst) {
                int32_t bytes_to_write = inst->bytes_size - offset;
                if (bytes_to_write > bytes_burst) {
                    bytes_to_write = bytes_burst;
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
            avs_free(buffer);
        }
        return result;
    }
    case TEST_RES_RAW_BYTES:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_bytes(ctx, inst->raw_bytes, inst->raw_bytes_size);
    case TEST_RES_BYTES_SIZE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, inst->bytes_size);
    case TEST_RES_BYTES_BURST:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, inst->bytes_burst);
    case TEST_RES_OPAQUE_ARRAY: {
        test_array_entry_t *it;
        AVS_LIST_FOREACH(it, inst->array) {
            if (it->index == riid) {
                break;
            }
        }
        if (!it) {
            return ANJAY_ERR_NOT_FOUND;
        }
        const uint32_t value = avs_convert_be32((uint32_t) it->value);
        return anjay_ret_bytes(ctx, &value, sizeof(value));
    }
    case TEST_RES_INT:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i32(ctx, inst->test_res_int);
    case TEST_RES_BOOL:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_bool(ctx, inst->test_res_bool);
    case TEST_RES_FLOAT:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_float(ctx, inst->test_res_float);
    case TEST_RES_DOUBLE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_double(ctx, inst->test_res_double);
    case TEST_RES_STRING:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, inst->test_res_string);
    case TEST_RES_OBJLNK:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_objlnk(ctx, inst->test_res_objlnk.oid,
                                inst->test_res_objlnk.iid);
    case TEST_RES_BYTES_ZERO_BEGIN:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_bool(ctx, inst->bytes_zero_begin);
    default:
        AVS_UNREACHABLE("Read called on unknown or non-readable resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int
test_resource_write_to_array(AVS_LIST(test_array_entry_t) *inst_array,
                             anjay_riid_t riid,
                             anjay_input_ctx_t *ctx) {
    assert(inst_array);
    assert(ctx);
    int32_t value;

    /* New element will be inserted to the array, value of existing one will get
     * overwritten. */
    if (anjay_get_i32(ctx, &value)) {
        demo_log(ERROR, "could not read integer");
        return ANJAY_ERR_INTERNAL;
    }
    bool value_updated = false;
    AVS_LIST(test_array_entry_t) *it;
    AVS_LIST_FOREACH_PTR(it, inst_array) {
        if ((*it)->index >= riid) {
            if ((*it)->index == riid) {
                (*it)->value = value;
                value_updated = true;
            }
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
        list_entry->index = riid;
        list_entry->value = value;
        AVS_LIST_INSERT(it, list_entry);
    }
    return 0;
}

static int test_resource_write(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_riid_t riid,
                               anjay_input_ctx_t *ctx) {
    (void) anjay;

    test_repr_t *test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    assert(inst);

    switch (rid) {
    case TEST_RES_COUNTER:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_i32(ctx, &inst->execute_counter);
    case TEST_RES_INT_ARRAY:
        return test_resource_write_to_array(&inst->array, riid, ctx);
    case TEST_RES_BYTES_SIZE: {
        assert(riid == ANJAY_ID_INVALID);
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
        assert(riid == ANJAY_ID_INVALID);
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
        assert(riid == ANJAY_ID_INVALID);
        return fetch_bytes(ctx, &inst->raw_bytes, &inst->raw_bytes_size);
    case TEST_RES_INT:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_i32(ctx, &inst->test_res_int);
    case TEST_RES_BOOL:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_bool(ctx, &inst->test_res_bool);
    case TEST_RES_FLOAT:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_float(ctx, &inst->test_res_float);
    case TEST_RES_DOUBLE:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_double(ctx, &inst->test_res_double);
    case TEST_RES_STRING:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_string(ctx, inst->test_res_string,
                                sizeof(inst->test_res_string));
    case TEST_RES_OBJLNK:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_objlnk(ctx, &inst->test_res_objlnk.oid,
                                &inst->test_res_objlnk.iid);
    case TEST_RES_BYTES_ZERO_BEGIN:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_get_bool(ctx, &inst->bytes_zero_begin);
    default:
        // Bootstrap Server may try to write to other resources,
        // so no AVS_UNREACHABLE() here
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int read_exec_arg_value(anjay_execute_ctx_t *arg_ctx,
                               char **out_string) {
#define VALUE_CHUNK_SIZE 256
    size_t total_bytes_read = 0;
    int result = 0;

    while (true) {
        size_t new_value_size = total_bytes_read + VALUE_CHUNK_SIZE;
        size_t bytes_read;

        char *new_string = (char *) avs_realloc(*out_string, new_value_size);
        if (!new_string) {
            demo_log(ERROR, "out of memory");
            result = ANJAY_ERR_INTERNAL;
            goto fail;
        }

        *out_string = new_string;

        result = anjay_execute_get_arg_value(arg_ctx, &bytes_read,
                                             new_string + total_bytes_read,
                                             (size_t) VALUE_CHUNK_SIZE);

        if (result < 0) {
            demo_log(ERROR, "could not read arg value: %d", (int) result);
            goto fail;
        } else if (result == 0) {
            // nothing more to read, we're done
            break;
        }

        // incomplete read; bigger buffer required
        assert(result == ANJAY_BUFFER_TOO_SHORT);
        total_bytes_read += bytes_read;
    }

    return 0;

fail:
    avs_free(*out_string);
    *out_string = NULL;
    return result;
}

static int read_exec_arg(anjay_execute_ctx_t *arg_ctx,
                         AVS_LIST(test_exec_arg_t) *list_ptr,
                         AVS_LIST(test_exec_arg_t) *out_element) {
    int arg_number;
    bool has_value;

    int result = anjay_execute_get_next_arg(arg_ctx, &arg_number, &has_value);
    if (result) {
        return result;
    }

    assert(!*out_element);
    *out_element = AVS_LIST_NEW_ELEMENT(test_exec_arg_t);
    if (!*out_element) {
        demo_log(ERROR, "out of memory");
        return ANJAY_ERR_INTERNAL;
    }

    (*out_element)->number = arg_number;

    if (has_value) {
        if ((result = read_exec_arg_value(arg_ctx, &(*out_element)->value))) {
            AVS_LIST_DELETE(out_element);
            demo_log(ERROR, "could not get read arg %d value", arg_number);
            return result;
        }
    }

    AVS_LIST(test_exec_arg_t) *insert_ptr = list_ptr;
    while (*insert_ptr && (*insert_ptr)->number < (*out_element)->number) {
        AVS_LIST_ADVANCE_PTR(&insert_ptr);
    }
    AVS_LIST_INSERT(insert_ptr, *out_element);
    return 0;
}

static int read_exec_args(anjay_execute_ctx_t *arg_ctx,
                          AVS_LIST(test_exec_arg_t) *out_args) {
    AVS_LIST_CLEAR(out_args) {
        avs_free((*out_args)->value);
    }

    int result;

    AVS_LIST(test_exec_arg_t) element = NULL;
    while (!(result = read_exec_arg(arg_ctx, out_args, &element))) {
        demo_log(DEBUG, "got arg %d", element->number);
        element = NULL;
    }

    if (result == ANJAY_EXECUTE_GET_ARG_END) {
        return 0;
    }

    AVS_LIST_CLEAR(out_args) {
        avs_free((*out_args)->value);
    }
    return result;
}

static int init_int_array_read_element(test_array_entry_t *out_entry,
                                       anjay_execute_ctx_t *arg_ctx) {
    int arg_number;
    bool has_value;

    int result = anjay_execute_get_next_arg(arg_ctx, &arg_number, &has_value);
    if (result) {
        return result;
    }

    char value_buf[16];
    if ((result = anjay_execute_get_arg_value(arg_ctx, NULL, value_buf,
                                              sizeof(value_buf)))
            < 0) {
        return result;
    }

    long value;
    if (demo_parse_long(value_buf, &value) || value < INT32_MIN
            || value > INT32_MAX) {
        demo_log(WARNING, "invalid resource %d value", arg_number);
        return ANJAY_ERR_BAD_REQUEST;
    }

    assert(0 <= arg_number && arg_number <= UINT16_MAX);
    out_entry->index = (anjay_riid_t) arg_number;
    out_entry->value = (int32_t) value;
    return 0;
}

static int init_int_array(AVS_LIST(test_array_entry_t) *out_array,
                          anjay_execute_ctx_t *arg_ctx) {
    AVS_LIST(test_array_entry_t) new_array = NULL;

    int result = 0;
    while (!result) {
        AVS_LIST(test_array_entry_t) list_entry =
                AVS_LIST_NEW_ELEMENT(test_array_entry_t);
        if (!list_entry) {
            demo_log(ERROR, "out of memory");
            result = ANJAY_ERR_INTERNAL;
        } else if ((result =
                            init_int_array_read_element(list_entry, arg_ctx))) {
            AVS_LIST_DELETE(&list_entry);
        } else {
            AVS_LIST(test_array_entry_t) *insert_ptr = &new_array;
            while (*insert_ptr && (*insert_ptr)->index < list_entry->index) {
                AVS_LIST_ADVANCE_PTR(&insert_ptr);
            }
            AVS_LIST_INSERT(insert_ptr, list_entry);
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
    (void) arg_ctx;

    test_repr_t *test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    assert(inst);

    switch (rid) {
    case TEST_RES_INCREMENT_COUNTER: {
        int result = read_exec_args(arg_ctx, &inst->last_exec_args);
        if (result) {
            demo_log(ERROR, "could not save Execute arguments");
            return result;
        }

        ++inst->execute_counter;
        anjay_notify_changed(anjay, (*obj_ptr)->oid, iid, TEST_RES_COUNTER);
        anjay_notify_changed(anjay, (*obj_ptr)->oid, iid,
                             TEST_RES_LAST_EXEC_ARGS_ARRAY);
        return 0;
    }
    case TEST_RES_INIT_INT_ARRAY: {
        int result = init_int_array(&inst->array, arg_ctx);
        if (result) {
            return result;
        }

        anjay_notify_changed(anjay, (*obj_ptr)->oid, iid, TEST_RES_INT_ARRAY);
        anjay_notify_changed(anjay, (*obj_ptr)->oid, iid,
                             TEST_RES_OPAQUE_ARRAY);
        return 0;
    }
    default:
        AVS_UNREACHABLE("Execute called on unknown or non-executable resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int test_resource_reset(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid) {
    (void) anjay;
    (void) rid;

    test_repr_t *test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    assert(inst);

    assert(rid == TEST_RES_INT_ARRAY);
    AVS_LIST_CLEAR(&inst->array);
    return 0;
}

static int
test_list_resource_instances(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    test_repr_t *test = get_test(obj_ptr);
    test_instance_t *inst = find_instance(test, iid);
    assert(inst);

    switch (rid) {
    case TEST_RES_INT_ARRAY:
    case TEST_RES_OPAQUE_ARRAY: {
        AVS_LIST(test_array_entry_t) it;
        AVS_LIST_FOREACH(it, inst->array) {
            anjay_dm_emit(ctx, it->index);
        }
        return 0;
    }
    case TEST_RES_LAST_EXEC_ARGS_ARRAY: {
        AVS_LIST(test_exec_arg_t) it;
        AVS_LIST_FOREACH(it, inst->last_exec_args) {
            anjay_dm_emit(ctx, (anjay_riid_t) it->number);
        }
        return 0;
    }
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

const anjay_dm_object_def_t TEST_OBJECT = {
    .oid = DEMO_OID_TEST,
    .handlers = {
        .list_instances = test_list_instances,
        .instance_create = test_instance_create,
        .instance_remove = test_instance_remove,
        .instance_reset = test_instance_reset,
        .list_resources = test_list_resources,
        .resource_read = test_resource_read,
        .resource_write = test_resource_write,
        .resource_execute = test_resource_execute,
        .resource_reset = test_resource_reset,
        .list_resource_instances = test_list_resource_instances,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

const anjay_dm_object_def_t **test_object_create(void) {
    test_repr_t *repr = (test_repr_t *) avs_calloc(1, sizeof(test_repr_t));
    if (!repr) {
        return NULL;
    }
    repr->def = &TEST_OBJECT;

    return &repr->def;
}

void test_object_release(const anjay_dm_object_def_t **def) {
    if (def) {
        test_repr_t *repr = get_test(def);
        AVS_LIST_CLEAR(&repr->instances) {
            release_instance(repr->instances);
        }
        avs_free(repr);
    }
}

int test_get_instances(const anjay_dm_object_def_t **def,
                       AVS_LIST(anjay_iid_t) *out) {
    test_repr_t *repr = get_test(def);
    assert(!*out);
    AVS_LIST(test_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (!(*out = AVS_LIST_NEW_ELEMENT(anjay_iid_t))) {
            demo_log(ERROR, "out of memory");
            return -1;
        }
        **out = it->iid;
        AVS_LIST_ADVANCE_PTR(&out);
    }
    return 0;
}

void test_notify_time_dependent(anjay_t *anjay,
                                const anjay_dm_object_def_t **def) {
    test_repr_t *repr = get_test(def);
    struct test_instance_struct *it;
    AVS_LIST_FOREACH(it, repr->instances) {
        anjay_notify_changed(anjay, (*def)->oid, it->iid, TEST_RES_TIMESTAMP);
    }
}
