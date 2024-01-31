/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

/**
 * @file tests/dm_read.c
 * @brief Tests for data model Read API
 * @note Note that all strings and values read from data model in this file have
 * no special meaning, they are used only for testing purposes.
 */

#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <anj/dm.h>
#include <anj/dm_io.h>

#include "../../../src/anj/dm_core.h"

#define OID_4 4 // test object

#define IID_0 0
#define IID_1 1

#define RID_0_BYTES 0
#define RID_1_STRING 1
#define RID_2_EXT_BYTES 2
#define RID_3_EXT_STRING 3
#define RID_4_INT 4
#define RID_5_DOUBLE 5
#define RID_6_BOOL 6
#define RID_7_OBJLNK 7
#define RID_8_UINT 8
#define RID_9_TIME 9
#define RID_10_STRING_M 10 // string (multiple - 4 instances)
#define RID_11_STRING_W 11 // string (write only)

// number of readable resource instances in a test object
#define TOTAL_READABLE_RES_INST_COUNT 14
#define OBJECT_INSTANCES 2

typedef struct test_object_instance_struct {
    fluf_iid_t iid;
} test_object_instance_t;

typedef struct test_object_struct {
    const dm_object_def_t *def;
    test_object_instance_t instances[OBJECT_INSTANCES];
} test_object_t;

static char *RESOURCE_INSTANCES_STRINGS[] = { "coap", "coaps", "tcp", "tls" };
static uint8_t BYTES[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0xFE, 0xFF };
static int read_external_data(void *buffer,
                              size_t bytes_to_copy,
                              size_t offset,
                              void *user_args) {
    (void) user_args;
    memcpy(buffer, BYTES + offset, bytes_to_copy);
    return 0;
}
static char REALLY_LONG_STRING[] = "really_long_string";
static int read_external_string(void *buffer,
                                size_t bytes_to_copy,
                                size_t offset,
                                void *user_args) {
    (void) user_args;
    memcpy(buffer, REALLY_LONG_STRING + offset, bytes_to_copy);
    return 0;
}
static int resource_read(dm_t *dm,
                         const dm_object_def_t *const *obj_ptr,
                         fluf_iid_t iid,
                         fluf_rid_t rid,
                         fluf_riid_t riid,
                         dm_output_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    switch (rid) {
    case RID_0_BYTES: {
        return dm_ret_bytes(ctx, BYTES, sizeof(BYTES));
    }
    case RID_1_STRING: {
        return dm_ret_string(ctx, "read_resource_0");
    }
    case RID_2_EXT_BYTES: {
        return dm_ret_external_bytes(
                ctx, read_external_data, NULL, sizeof(BYTES));
    }
    case RID_3_EXT_STRING: {
        return dm_ret_external_string(
                ctx, read_external_string, NULL, sizeof(REALLY_LONG_STRING));
    }
    case RID_4_INT: {
        return dm_ret_i64(ctx, (int64_t) INT32_MAX + 1);
    }
    case RID_5_DOUBLE: {
        return dm_ret_double(ctx, 3.14);
    }
    case RID_6_BOOL: {
        return dm_ret_bool(ctx, true);
    }
    case RID_7_OBJLNK: {
        return dm_ret_objlnk(ctx, OID_4, IID_0);
    }
    case RID_8_UINT: {
        return dm_ret_u64(ctx, UINT64_MAX);
    }
    case RID_9_TIME: {
        return dm_ret_time(ctx, 1112470620000);
    }
    case RID_10_STRING_M: {
        return dm_ret_string(ctx, RESOURCE_INSTANCES_STRINGS[riid]);
    }

    default: { return FLUF_COAP_CODE_METHOD_NOT_ALLOWED; }
    }
}

static int list_resource_instances(dm_t *dm,
                                   const dm_object_def_t *const *obj_ptr,
                                   fluf_iid_t iid,
                                   fluf_rid_t rid,
                                   dm_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case RID_10_STRING_M: {
        for (fluf_riid_t i = 0; i < AVS_ARRAY_SIZE(RESOURCE_INSTANCES_STRINGS);
             ++i) {
            dm_emit(ctx, i);
        }
        return 0;
    }
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
    }
}

static int list_resources(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          fluf_iid_t iid,
                          dm_resource_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    dm_emit_res(ctx, RID_0_BYTES, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_1_STRING, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_2_EXT_BYTES, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_3_EXT_STRING, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_4_INT, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_5_DOUBLE, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_6_BOOL, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_7_OBJLNK, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_8_UINT, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_9_TIME, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_10_STRING_M, DM_RES_RM, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_11_STRING_W, DM_RES_W, DM_RES_PRESENT);

    return 0;
}

static inline test_object_t *get_obj(const dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, test_object_t, def);
}

static int list_instances(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          dm_list_ctx_t *ctx) {
    (void) dm;

    test_object_t *test_object = get_obj(obj_ptr);

    for (uint16_t i = 0; i < (uint16_t) AVS_ARRAY_SIZE(test_object->instances);
         ++i) {
        dm_emit(ctx, i);
    }
    return 0;
}

static const dm_object_def_t DEF = {
    .oid = OID_4,
    .handlers.resource_read = resource_read,
    .handlers.list_resource_instances = list_resource_instances,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = list_instances
};

static const test_object_t TEST_OBJECT = {
    .def = &DEF,
    .instances =
            {
                {
                    .iid = IID_0
                },
                {
                    .iid = IID_1
                }
            }
};

// Global buffer used to store entries retrieved from data model.
static fluf_io_out_entry_t
        USER_BUFFER[OBJECT_INSTANCES * TOTAL_READABLE_RES_INST_COUNT];
static struct user_buffer_struct {
    fluf_io_out_entry_t *buffer;
    size_t buffer_size;
    size_t count;
} USER_BUFFER_STRUCT;

static dm_output_ctx_t OUT_CTX;

static int callback_fnc(void *arg, fluf_io_out_entry_t *out_entry) {
    struct user_buffer_struct *user_buffer_struct =
            (struct user_buffer_struct *) arg;
    if (user_buffer_struct->count >= user_buffer_struct->buffer_size) {
        printf("Buffer overflow\n");
        return -1;
    }
    fluf_io_out_entry_t *user_buffer =
            &user_buffer_struct->buffer[user_buffer_struct->count++];

    if (out_entry->type == FLUF_DATA_TYPE_STRING
            || out_entry->type == FLUF_DATA_TYPE_INT
            || out_entry->type == FLUF_DATA_TYPE_BYTES
            || out_entry->type == FLUF_DATA_TYPE_DOUBLE
            || out_entry->type == FLUF_DATA_TYPE_BOOL
            || out_entry->type == FLUF_DATA_TYPE_OBJLNK
            || out_entry->type == FLUF_DATA_TYPE_UINT
            || out_entry->type == FLUF_DATA_TYPE_TIME
            || out_entry->type == FLUF_DATA_TYPE_EXTERNAL_BYTES
            || out_entry->type == FLUF_DATA_TYPE_EXTERNAL_STRING) {
        memcpy(user_buffer, out_entry, sizeof(fluf_io_out_entry_t));
        return 0;
    } else {
        printf("Unknown data type\n");
        return -1;
    }
}

#define OBJ_MAX 3
static dm_t dm;
static dm_installed_object_t objects[OBJ_MAX];

#define SET_UP()                                       \
    dm_initialize(&dm, objects, OBJ_MAX);              \
    USER_BUFFER_STRUCT = (struct user_buffer_struct) { \
        .buffer = USER_BUFFER,                         \
        .buffer_size = AVS_ARRAY_SIZE(USER_BUFFER),    \
        .count = 0                                     \
    };                                                 \
    memset(USER_BUFFER, 0x00, sizeof(USER_BUFFER));    \
    OUT_CTX.callback = callback_fnc;                   \
    OUT_CTX.arg = &USER_BUFFER_STRUCT

AVS_UNIT_TEST(DataModelRead, ReadResourceInstance) {
    SET_UP();
    fluf_riid_t riid = 3;
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_10_STRING_M, riid);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_read(&dm, &uri, &OUT_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[0].type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            (const char *) USER_BUFFER[0].value.bytes_or_string.data,
            RESOURCE_INSTANCES_STRINGS[riid]);
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&uri, &USER_BUFFER[0].path));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 1);
}

AVS_UNIT_TEST(DataModelRead, ReadSingleInstanceResource) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_1_STRING);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_read(&dm, &uri, &OUT_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[0].type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            (const char *) USER_BUFFER[0].value.bytes_or_string.data,
            "read_resource_0");
    AVS_UNIT_ASSERT_TRUE(fluf_uri_path_equal(&uri, &USER_BUFFER[0].path));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 1);
}

AVS_UNIT_TEST(DataModelRead, ReadMultiInstanceResource) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_10_STRING_M);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_read(&dm, &uri, &OUT_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    for (uint16_t i = 0; i < AVS_ARRAY_SIZE(RESOURCE_INSTANCES_STRINGS); ++i) {
        AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[i].type, FLUF_DATA_TYPE_STRING);
        AVS_UNIT_ASSERT_EQUAL_STRING(
                (const char *) USER_BUFFER[i].value.bytes_or_string.data,
                RESOURCE_INSTANCES_STRINGS[i]);
        uri.ids[FLUF_ID_RIID] = i;
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_uri_path_equal(&uri, &USER_BUFFER[i].path));
    }
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count,
                          AVS_ARRAY_SIZE(RESOURCE_INSTANCES_STRINGS));
}

static size_t test_OID_4(fluf_iid_t iid, size_t iterator) {
    // /OID_4/iid/RID_0_BYTES
    fluf_uri_path_t expected_uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_0_BYTES);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_NOT_NULL(USER_BUFFER[iterator].value.bytes_or_string.data);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.bytes_or_string.offset,
                          0);
    AVS_UNIT_ASSERT_EQUAL(
            USER_BUFFER[iterator].value.bytes_or_string.chunk_length,
            sizeof(BYTES));
    AVS_UNIT_ASSERT_EQUAL(
            USER_BUFFER[iterator].value.bytes_or_string.full_length_hint,
            sizeof(BYTES));
    AVS_UNIT_ASSERT_SUCCESS(
            memcmp(USER_BUFFER[iterator].value.bytes_or_string.data,
                   BYTES,
                   USER_BUFFER[iterator].value.bytes_or_string.chunk_length));
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_1_STRING
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_1_STRING);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            (const char *) USER_BUFFER[iterator].value.bytes_or_string.data,
            "read_resource_0");
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_2_EXT_BYTES
    uint8_t read_buff[sizeof(REALLY_LONG_STRING)];
    memset(read_buff, 0xFF, sizeof(read_buff));
    const size_t chunk_size = 2;
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_2_EXT_BYTES);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type,
                          FLUF_DATA_TYPE_EXTERNAL_BYTES);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.external_data.length,
                          sizeof(BYTES));
    AVS_UNIT_ASSERT_NOT_NULL(
            USER_BUFFER[iterator].value.external_data.get_external_data);
    size_t bytes_to_copy = USER_BUFFER[iterator].value.external_data.length;
    size_t remaining_bytes = USER_BUFFER[iterator].value.external_data.length;
    while (remaining_bytes >= chunk_size) {
        AVS_UNIT_ASSERT_SUCCESS(
                USER_BUFFER[iterator].value.external_data.get_external_data(
                        read_buff + bytes_to_copy - remaining_bytes,
                        chunk_size,
                        bytes_to_copy - remaining_bytes,
                        NULL));
        remaining_bytes -= chunk_size;
    }
    if (remaining_bytes) {
        AVS_UNIT_ASSERT_SUCCESS(
                USER_BUFFER[iterator].value.external_data.get_external_data(
                        read_buff + bytes_to_copy - remaining_bytes,
                        remaining_bytes,
                        bytes_to_copy - remaining_bytes,
                        NULL));
    }
    AVS_UNIT_ASSERT_SUCCESS(memcmp(read_buff, BYTES, sizeof(BYTES)));
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_3_EXT_STRING
    memset(read_buff, 0xFF, sizeof(read_buff));
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_3_EXT_STRING);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type,
                          FLUF_DATA_TYPE_EXTERNAL_STRING);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.external_data.length,
                          sizeof(REALLY_LONG_STRING));
    AVS_UNIT_ASSERT_NOT_NULL(
            USER_BUFFER[iterator].value.external_data.get_external_data);
    bytes_to_copy = USER_BUFFER[iterator].value.external_data.length;
    remaining_bytes = USER_BUFFER[iterator].value.external_data.length;
    while (remaining_bytes >= chunk_size) {
        AVS_UNIT_ASSERT_SUCCESS(
                USER_BUFFER[iterator].value.external_data.get_external_data(
                        read_buff + bytes_to_copy - remaining_bytes,
                        chunk_size,
                        bytes_to_copy - remaining_bytes,
                        NULL));
        remaining_bytes -= chunk_size;
    }
    if (remaining_bytes) {
        AVS_UNIT_ASSERT_SUCCESS(
                USER_BUFFER[iterator].value.external_data.get_external_data(
                        read_buff + bytes_to_copy - remaining_bytes,
                        remaining_bytes,
                        bytes_to_copy - remaining_bytes,
                        NULL));
    }
    AVS_UNIT_ASSERT_SUCCESS(
            memcmp(read_buff, REALLY_LONG_STRING, sizeof(REALLY_LONG_STRING)));
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_4_INT
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_4_INT);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_INT);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.int_value,
                          (int64_t) INT32_MAX + 1);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_5_DOUBLE
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_5_DOUBLE);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_DOUBLE);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.double_value, 3.14);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_6_BOOL
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_6_BOOL);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_BOOL);
    AVS_UNIT_ASSERT_TRUE(USER_BUFFER[iterator].value.bool_value);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_7_OBJLNK
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_7_OBJLNK);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.objlnk.oid, OID_4);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.objlnk.iid, IID_0);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_8_UINT
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_8_UINT);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_UINT);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.int_value, UINT64_MAX);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_9_TIME
    expected_uri = FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_9_TIME);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type, FLUF_DATA_TYPE_TIME);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].value.time_value,
                          1112470620000);
    AVS_UNIT_ASSERT_TRUE(
            fluf_uri_path_equal(&expected_uri, &USER_BUFFER[iterator++].path));
    // /OID_4/iid/RID_10_STRING_M
    fluf_uri_path_t expected_path_2 =
            FLUF_MAKE_RESOURCE_PATH(OID_4, iid, RID_10_STRING_M);
    for (uint16_t i = 0; i < AVS_ARRAY_SIZE(RESOURCE_INSTANCES_STRINGS); ++i) {
        AVS_UNIT_ASSERT_EQUAL(USER_BUFFER[iterator].type,
                              FLUF_DATA_TYPE_STRING);
        AVS_UNIT_ASSERT_EQUAL_STRING(
                (const char *) USER_BUFFER[iterator].value.bytes_or_string.data,
                RESOURCE_INSTANCES_STRINGS[i]);
        expected_path_2.ids[3] = i;
        for (uint8_t idx = 0; idx < expected_path_2.uri_len; idx++) {
            AVS_UNIT_ASSERT_EQUAL(expected_path_2.ids[idx],
                                  USER_BUFFER[iterator].path.ids[idx]);
        }
        iterator++;
    }
    return iterator;
}

AVS_UNIT_TEST(DataModelRead, ReadObjectInstance) {
    SET_UP();
    size_t iterator = 0;
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_4, IID_0);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_read(&dm, &uri, &OUT_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    iterator = test_OID_4(IID_0, iterator);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, iterator);
}

AVS_UNIT_TEST(DataModelRead, ReadObject) {
    SET_UP();
    size_t iterator = 0;
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_4);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_read(&dm, &uri, &OUT_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    iterator = test_OID_4(IID_0, iterator);
    iterator = test_OID_4(IID_1, iterator);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, iterator);
}

AVS_UNIT_TEST(DataModelRead, HandlerNotSet) {
    // Specific setup for this test
    USER_BUFFER_STRUCT.count = 0;
    const dm_object_def_t SPECIFIC_DEF = {
        .oid = OID_4,
        .handlers.resource_read = NULL,
        .handlers.list_resources = list_resources,
        .handlers.list_resource_instances = list_resource_instances
    };
    const dm_object_def_t *SPECIFIC_DEF_PTR = &SPECIFIC_DEF;
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_4, IID_0, RID_10_STRING_M, 1);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &SPECIFIC_DEF_PTR));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &SPECIFIC_DEF_PTR));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadNotRegistered) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_4);
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadNotPresentObject) {
    SET_UP();
    fluf_riid_t riid = 4;
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_10_STRING_M, riid);
    // deliberately missing dm_register here
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadNotPresentObjectInstance) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_4, 2);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadNotPresentResource) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, 20);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadNotPresentResourceInstance) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_INSTANCE_PATH(OID_4, IID_0, RID_10_STRING_M, 20);
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

static int error_callback(void *arg, fluf_io_out_entry_t *entry) {
    (void) arg;
    (void) entry;
    return -1;
}

AVS_UNIT_TEST(DataModelRead, ReadCheckCTXCallbackErrorSingleInstanceResource) {
    SET_UP();
    fluf_riid_t riid = 3;
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_10_STRING_M, riid);
    OUT_CTX.callback = error_callback;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX), -1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadCheckCTXCallbackErrorMultiInstanceResource) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_10_STRING_M);
    OUT_CTX.callback = error_callback;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX), -1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadCheckCTXCallbackErrorObjectInstance) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_4, IID_0);
    OUT_CTX.callback = error_callback;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX), -1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadCheckCTXCallbackErrorObject) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_4);
    OUT_CTX.callback = error_callback;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX), -1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadCheckCTXCallbackErrorRoot) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_ROOT_PATH();
    OUT_CTX.callback = error_callback;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX), -1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}

AVS_UNIT_TEST(DataModelRead, ReadOnlyWritable) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_11_STRING_W);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(USER_BUFFER_STRUCT.count, 0);
}
