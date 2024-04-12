/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

/**
 * @file tests/dm_write.c
 * @brief Tests for data model Write API
 * @note Note that all strings and values written to data model in this file
 * have no special meaning, they are used only for testing purposes.
 */

#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <anj/dm.h>
#include <anj/dm_io.h>

#include "../../../src/anj/dm/dm_core.h"

#define OID_4 4 // test object

#define IID_0 0

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
#define RID_12_ABSENT 12

// number of writable resource instances in a test object
#define TOTAL_WRITABLE_RES_INST_COUNT 14

typedef struct test_object_instance_struct {
    fluf_iid_t iid;
} test_object_instance_t;

typedef struct test_object_struct {
    const dm_object_def_t *def;
} test_object_t;

#define MAX_RID_BYTES_SIZE 10
static uint8_t RESOURCE_0[MAX_RID_BYTES_SIZE];
#define MAX_RID_STR_SIZE 10
static char RESOURCE_1[MAX_RID_STR_SIZE];
static uint8_t RESOURCE_2[MAX_RID_BYTES_SIZE];
static char RESOURCE_3[MAX_RID_STR_SIZE];
static int64_t RESOURCE_4;
static double RESOURCE_5;
static bool RESOURCE_6;
static struct {
    fluf_oid_t oid;
    fluf_iid_t iid;
} RESOURCE_7;
static uint64_t RESOURCE_8;
static uint64_t RESOURCE_9;
static char RESOURCE_10[4][MAX_RID_STR_SIZE] = {};
static void clear_resources(void) {
    memset(RESOURCE_0, 0x00, sizeof(RESOURCE_0));
    memset(RESOURCE_1, 0x00, sizeof(RESOURCE_1));
    memset(RESOURCE_2, 0x00, sizeof(RESOURCE_2));
    memset(RESOURCE_3, 0x00, sizeof(RESOURCE_3));
    RESOURCE_4 = 0;
    RESOURCE_5 = 0;
    RESOURCE_6 = false;
    memset(&RESOURCE_7, 0x00, sizeof(RESOURCE_7));
    RESOURCE_8 = 0;
    RESOURCE_9 = 0;
    memset(RESOURCE_10, 0x00, sizeof(RESOURCE_10));
}

static int resource_write(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          fluf_iid_t iid,
                          fluf_rid_t rid,
                          fluf_riid_t riid,
                          dm_input_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    (void) riid;
    switch (rid) {
    case RID_0_BYTES: {
        // The idea of this is to enforce of multiple calls to
        // dm_get_bytes().
        bool finished = false;
        size_t bytes_read = 0;
        size_t bytes_written = 0;
        char buf[3];
        do {
            if (dm_get_bytes(ctx, &bytes_read, &finished, buf, sizeof(buf))) {
                return -1;
            }
            if (bytes_written + bytes_read > sizeof(RESOURCE_0)) {
                return -1;
            }
            memcpy(RESOURCE_0 + bytes_written, buf, bytes_read);
            bytes_written += sizeof(buf);
        } while (!finished);
        return 0;
    }
    case RID_1_STRING: {
        // The idea of this is to enforce of multiple calls to
        // dm_get_string().
        char buff[5];
        int retval = DM_BUFFER_TOO_SHORT;
        memset(RESOURCE_1, 0x00, sizeof(RESOURCE_1));
        size_t chars_written = 0;
        do {
            retval = dm_get_string(ctx, buff, sizeof(buff));
            chars_written += strlen(buff);
            if (chars_written > sizeof(RESOURCE_1)) {
                return -1;
            }
            strcpy(RESOURCE_1 + strlen(RESOURCE_1), buff);
        } while (retval == DM_BUFFER_TOO_SHORT);
        return 0;
    }
    case RID_2_EXT_BYTES: {
        fluf_get_external_data_t *get_external_data = NULL;
        size_t len = 0;
        void *args = NULL;
        dm_get_external_bytes(ctx, &get_external_data, &args, &len);
        int result = get_external_data(RESOURCE_2, len, 0, args);
        if (result) {
            return result;
        }
        return 0;
    }
    case RID_3_EXT_STRING: {
        fluf_get_external_data_t *get_external_data = NULL;
        size_t len = 0;
        void *args = NULL;
        dm_get_external_string(ctx, &get_external_data, &args, &len);
        int result = get_external_data(RESOURCE_3, len, 0, args);
        if (result) {
            return result;
        }
        return 0;
    }
    case RID_4_INT: {
        int64_t value;
        int retval = dm_get_i64(ctx, &value);
        if (retval) {
            return retval;
        }
        RESOURCE_4 = value;
        return 0;
    }
    case RID_5_DOUBLE: {
        double value;
        int retval = dm_get_double(ctx, &value);
        if (retval) {
            return retval;
        }
        RESOURCE_5 = value;
        return 0;
    }
    case RID_6_BOOL: {
        bool value;
        int retval = dm_get_bool(ctx, &value);
        if (retval) {
            return retval;
        }
        RESOURCE_6 = value;
        return 0;
    }
    case RID_7_OBJLNK: {
        fluf_oid_t out_oid;
        fluf_iid_t out_iid;
        int retval = dm_get_objlnk(ctx, &out_oid, &out_iid);
        if (retval) {
            return retval;
        }
        RESOURCE_7.oid = out_oid;
        RESOURCE_7.iid = out_iid;
        return 0;
    }
    case RID_8_UINT: {
        uint64_t value;
        int retval = dm_get_u64(ctx, &value);
        if (retval) {
            return retval;
        }
        RESOURCE_8 = value;
        return 0;
    }
    case RID_9_TIME: {
        int64_t time;
        int retval = dm_get_time(ctx, &time);
        if (retval) {
            return retval;
        }
        RESOURCE_9 = time;
        return 0;
    }
    case RID_10_STRING_M: {
        char buff[MAX_RID_STR_SIZE];
        int retval = dm_get_string(ctx, buff, sizeof(buff));
        if (retval) {
            return retval;
        }
        if (strlen(buff) < MAX_RID_STR_SIZE) {
            strcpy(RESOURCE_10[riid], buff);
            return 0;
        }
    default:
        return -1;
    }
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
        for (fluf_riid_t i = 0; i < AVS_ARRAY_SIZE(RESOURCE_10); ++i) {
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
    dm_emit_res(ctx, RID_0_BYTES, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_1_STRING, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_2_EXT_BYTES, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_3_EXT_STRING, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_4_INT, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_5_DOUBLE, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_6_BOOL, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_7_OBJLNK, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_8_UINT, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_9_TIME, DM_RES_RW, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_10_STRING_M, DM_RES_RWM, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_11_STRING_W, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, RID_12_ABSENT, DM_RES_RW, DM_RES_ABSENT);
    return 0;
}

static int list_instances(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          dm_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    dm_emit(ctx, 0);
    return 0;
}

static const dm_object_def_t DEF = {
    .oid = OID_4,
    .handlers.resource_write = resource_write,
    .handlers.list_resource_instances = list_resource_instances,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = list_instances
};

static const test_object_t TEST_OBJECT = {
    .def = &DEF
};

// Global buffer used to store entries that should be written to data model.
static fluf_io_out_entry_t USER_BUFFER[TOTAL_WRITABLE_RES_INST_COUNT];
static struct user_buffer_struct {
    fluf_io_out_entry_t *buffer;
    size_t buffer_size;
    size_t count;
} USER_BUFFER_STRUCT;

static int callback_fnc(void *arg,
                        fluf_data_type_t expected_type,
                        fluf_io_out_entry_t *in_entry) {
    struct user_buffer_struct *user_buffer_struct =
            (struct user_buffer_struct *) arg;
    if (user_buffer_struct->count >= user_buffer_struct->buffer_size) {
        printf("Buffer overflow\n");
        return -1;
    }
    fluf_io_out_entry_t *user_buffer =
            &user_buffer_struct->buffer[user_buffer_struct->count++];

    if (user_buffer->type == expected_type) {
        *in_entry = *user_buffer;
        return 0;
    }
    return -1;
}

static dm_input_ctx_t IN_CTX;

#define OBJ_MAX 3
static dm_t dm;
static dm_installed_object_t objects[OBJ_MAX];

#define SET_UP()                                       \
    dm_initialize(&dm, objects, OBJ_MAX);              \
    clear_resources();                                 \
    USER_BUFFER_STRUCT = (struct user_buffer_struct) { \
        .buffer = USER_BUFFER,                         \
        .buffer_size = AVS_ARRAY_SIZE(USER_BUFFER),    \
        .count = 0                                     \
    };                                                 \
    memset(USER_BUFFER, 0x00, sizeof(USER_BUFFER));    \
    IN_CTX.callback = callback_fnc;                    \
    IN_CTX.arg = &USER_BUFFER_STRUCT

AVS_UNIT_TEST(DataModelWrite, WriteResourceInstance) {
    SET_UP();
    fluf_riid_t riid = 3;
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_10_STRING_M, riid);

    // prepare data to be written
    USER_BUFFER[0].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[0].value.bytes_or_string.data = "protocol";
    USER_BUFFER[0].value.bytes_or_string.chunk_length =
            strlen((const char *) USER_BUFFER[0].value.bytes_or_string.data);
    USER_BUFFER[0].value.bytes_or_string.full_length_hint =
            USER_BUFFER[0].value.bytes_or_string.chunk_length;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_write(&dm, &uri, &IN_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL_STRING(
            (const char *) USER_BUFFER[0].value.bytes_or_string.data,
            RESOURCE_10[riid]);
}

AVS_UNIT_TEST(DataModelWrite, WriteStringWithUseOfMultipleGetStringCalls) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_1_STRING);

    // prepare data to be written
    USER_BUFFER[0].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[0].value.bytes_or_string.data = "123456789";
    USER_BUFFER[0].value.bytes_or_string.chunk_length =
            strlen((const char *) USER_BUFFER[0].value.bytes_or_string.data);
    USER_BUFFER[0].value.bytes_or_string.full_length_hint =
            USER_BUFFER[0].value.bytes_or_string.chunk_length;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_write(&dm, &uri, &IN_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL_STRING(
            (const char *) USER_BUFFER[0].value.bytes_or_string.data,
            RESOURCE_1);
}

AVS_UNIT_TEST(DataModelWrite, WriteSingleInstanceResource) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_4_INT);

    // prepare data to be written
    USER_BUFFER[0].type = FLUF_DATA_TYPE_INT;
    USER_BUFFER[0].value.int_value = 2137;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_write(&dm, &uri, &IN_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(2137, RESOURCE_4);
}

AVS_UNIT_TEST(DataModelWrite, WriteMultiInstanceResource) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_10_STRING_M);

    // prepare data to be written
    USER_BUFFER[0].value.bytes_or_string.data = "HTTP";
    USER_BUFFER[1].value.bytes_or_string.data = "UDP";
    USER_BUFFER[2].value.bytes_or_string.data = "TCP";
    USER_BUFFER[3].value.bytes_or_string.data = "IP";

    for (size_t i = 0; i < 4; ++i) {
        USER_BUFFER[i].type = FLUF_DATA_TYPE_STRING;
        USER_BUFFER[i].value.bytes_or_string.chunk_length = strlen(
                (const char *) USER_BUFFER[i].value.bytes_or_string.data);
        USER_BUFFER[i].value.bytes_or_string.full_length_hint =
                USER_BUFFER[i].value.bytes_or_string.chunk_length;
    }

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_write(&dm, &uri, &IN_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));

    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[0], "HTTP");
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[1], "UDP");
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[2], "TCP");
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[3], "IP");
}

static int get_external_data_clb(void *buffer,
                                 size_t bytes_to_copy,
                                 size_t offset,
                                 void *user_args) {
    uint8_t *data = (uint8_t *) user_args;
    memcpy(buffer, data + offset, bytes_to_copy);
    return 0;
}

AVS_UNIT_TEST(DataModelWrite, WriteObjectInstance) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_INSTANCE_PATH(OID_4, IID_0);

    // prepare data to be written
    USER_BUFFER[0].type = FLUF_DATA_TYPE_BYTES;
    uint8_t bytes[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    USER_BUFFER[0].value.bytes_or_string.data = bytes;
    USER_BUFFER[0].value.bytes_or_string.offset = 0;
    USER_BUFFER[0].value.bytes_or_string.chunk_length = sizeof(bytes);
    USER_BUFFER[0].value.bytes_or_string.full_length_hint = sizeof(bytes);
    char str[] = "AVSystem";
    USER_BUFFER[1].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[1].value.bytes_or_string.data = str;
    USER_BUFFER[1].value.bytes_or_string.offset = 0;
    USER_BUFFER[1].value.bytes_or_string.chunk_length = strlen(str);
    USER_BUFFER[1].value.bytes_or_string.full_length_hint = strlen(str);
    USER_BUFFER[2].type = FLUF_DATA_TYPE_EXTERNAL_BYTES;
    USER_BUFFER[2].value.external_data.get_external_data =
            get_external_data_clb;
    USER_BUFFER[2].value.external_data.user_args = bytes;
    USER_BUFFER[2].value.external_data.length = sizeof(bytes);
    USER_BUFFER[3].type = FLUF_DATA_TYPE_EXTERNAL_BYTES;
    USER_BUFFER[3].value.external_data.get_external_data =
            get_external_data_clb;
    USER_BUFFER[3].value.external_data.user_args = str;
    USER_BUFFER[3].value.external_data.length = sizeof(str);
    USER_BUFFER[4].type = FLUF_DATA_TYPE_INT;
    USER_BUFFER[4].value.int_value = -2137;
    USER_BUFFER[5].type = FLUF_DATA_TYPE_DOUBLE;
    USER_BUFFER[5].value.double_value = 3.14;
    USER_BUFFER[6].type = FLUF_DATA_TYPE_BOOL;
    USER_BUFFER[6].value.bool_value = true;
    USER_BUFFER[7].type = FLUF_DATA_TYPE_OBJLNK;
    USER_BUFFER[7].value.objlnk.oid = 1;
    USER_BUFFER[7].value.objlnk.iid = 2;
    USER_BUFFER[8].type = FLUF_DATA_TYPE_UINT;
    USER_BUFFER[8].value.uint_value = 2137;
    USER_BUFFER[9].type = FLUF_DATA_TYPE_TIME;
    USER_BUFFER[9].value.time_value = 1112470620000;
    USER_BUFFER[10].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[10].value.bytes_or_string.data = "HTTP";
    USER_BUFFER[10].value.bytes_or_string.offset = 0;
    USER_BUFFER[10].value.bytes_or_string.chunk_length =
            strlen((const char *) USER_BUFFER[10].value.bytes_or_string.data);
    USER_BUFFER[10].value.bytes_or_string.full_length_hint =
            USER_BUFFER[10].value.bytes_or_string.chunk_length;
    USER_BUFFER[11].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[11].value.bytes_or_string.data = "UDP";
    USER_BUFFER[11].value.bytes_or_string.offset = 0;
    USER_BUFFER[11].value.bytes_or_string.chunk_length =
            strlen((const char *) USER_BUFFER[11].value.bytes_or_string.data);
    USER_BUFFER[11].value.bytes_or_string.full_length_hint =
            USER_BUFFER[11].value.bytes_or_string.chunk_length;
    USER_BUFFER[12].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[12].value.bytes_or_string.data = "TCP";
    USER_BUFFER[12].value.bytes_or_string.offset = 0;
    USER_BUFFER[12].value.bytes_or_string.chunk_length =
            strlen((const char *) USER_BUFFER[12].value.bytes_or_string.data);
    USER_BUFFER[12].value.bytes_or_string.full_length_hint =
            USER_BUFFER[12].value.bytes_or_string.chunk_length;
    USER_BUFFER[13].type = FLUF_DATA_TYPE_STRING;
    USER_BUFFER[13].value.bytes_or_string.data = "IP";
    USER_BUFFER[13].value.bytes_or_string.offset = 0;
    USER_BUFFER[13].value.bytes_or_string.chunk_length =
            strlen((const char *) USER_BUFFER[13].value.bytes_or_string.data);
    USER_BUFFER[13].value.bytes_or_string.full_length_hint =
            USER_BUFFER[13].value.bytes_or_string.chunk_length;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_SUCCESS(dm_write(&dm, &uri, &IN_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));

    AVS_UNIT_ASSERT_TRUE(!memcmp(RESOURCE_0, bytes, sizeof(bytes)));
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_1, str);
    AVS_UNIT_ASSERT_TRUE(!memcmp(RESOURCE_2, bytes, sizeof(bytes)));
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_3, str);
    AVS_UNIT_ASSERT_EQUAL(RESOURCE_4, -2137);
    AVS_UNIT_ASSERT_EQUAL(RESOURCE_5, 3.14);
    AVS_UNIT_ASSERT_TRUE(RESOURCE_6);
    AVS_UNIT_ASSERT_EQUAL(RESOURCE_7.oid, 1);
    AVS_UNIT_ASSERT_EQUAL(RESOURCE_7.iid, 2);
    AVS_UNIT_ASSERT_EQUAL(RESOURCE_8, 2137);
    AVS_UNIT_ASSERT_EQUAL(RESOURCE_9, 1112470620000);
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[0], "HTTP");
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[1], "UDP");
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[2], "TCP");
    AVS_UNIT_ASSERT_EQUAL_STRING(RESOURCE_10[3], "IP");
}

AVS_UNIT_TEST(DataModelWrite, WriteNotPresentObjectInstance) {
    SET_UP();
    fluf_riid_t riid = 4;
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_10_STRING_M, riid);
    // deliberately missing dm_register here
    AVS_UNIT_ASSERT_EQUAL(dm_write(&dm, &uri, &IN_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
}

static int error_callback(void *arg,
                          fluf_data_type_t expected_type,
                          fluf_io_out_entry_t *entry) {
    (void) arg;
    (void) expected_type;
    (void) entry;
    return -1;
}

AVS_UNIT_TEST(DataModelWrite, WriteCheckCTXCallbackError) {
    SET_UP();
    fluf_riid_t riid = 3;
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_INSTANCE_PATH(
            OID_4, IID_0, RID_10_STRING_M, riid);
    IN_CTX.callback = error_callback;

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_write(&dm, &uri, &IN_CTX), -1);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelWrite, WriteWithUriWithNoIID) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_OBJECT_PATH(OID_4);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_write(&dm, &uri, &IN_CTX),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelWrite, WriteReadOnly) {
    SET_UP();
    fluf_uri_path_t uri =
            FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_11_STRING_W);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_write(&dm, &uri, &IN_CTX),
                          FLUF_COAP_CODE_METHOD_NOT_ALLOWED);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}

AVS_UNIT_TEST(DataModelWrite, WriteNotPresent) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_RESOURCE_PATH(OID_4, IID_0, RID_12_ABSENT);

    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &TEST_OBJECT.def));
    AVS_UNIT_ASSERT_EQUAL(dm_write(&dm, &uri, &IN_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &TEST_OBJECT.def));
}
