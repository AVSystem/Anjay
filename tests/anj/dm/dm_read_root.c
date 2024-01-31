/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

/**
 * @file tests/dm_read_root.c
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

#define OID_4 4 // test object 1
#define OID_5 5 // test object 2

#define RES_INST 4

static int resource_read(dm_t *dm,
                         const dm_object_def_t *const *obj_ptr,
                         fluf_iid_t iid,
                         fluf_rid_t rid,
                         fluf_riid_t riid,
                         dm_output_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    (void) riid;
    switch (rid) {
    case 0: {
        return dm_ret_string(ctx, "read_resource_0");
    }
    case 1: {
        return dm_ret_i64(ctx, (int64_t) INT32_MAX + 1);
    }

    default: { return FLUF_COAP_CODE_METHOD_NOT_ALLOWED; }
    }
}

static int list_resources(dm_t *dm,
                          const dm_object_def_t *const *obj_ptr,
                          fluf_iid_t iid,
                          dm_resource_list_ctx_t *ctx) {
    (void) dm;
    (void) obj_ptr;
    (void) iid;
    dm_emit_res(ctx, 0, DM_RES_R, DM_RES_PRESENT);
    dm_emit_res(ctx, 1, DM_RES_R, DM_RES_PRESENT);
    return 0;
}

static const dm_object_def_t DEF_TEST_OBJ_1 = {
    .oid = OID_4,
    .handlers.resource_read = resource_read,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = dm_list_instances_SINGLE
};
static const dm_object_def_t *const DEF_TEST_OBJ_1_PTR = &DEF_TEST_OBJ_1;

static const dm_object_def_t DEF_TEST_OBJ_2 = {
    .oid = OID_5,
    .handlers.resource_read = resource_read,
    .handlers.list_resources = list_resources,
    .handlers.list_instances = dm_list_instances_SINGLE
};
static const dm_object_def_t *const DEF_TEST_OBJ_2_PTR = &DEF_TEST_OBJ_2;

// Global buffer used to store entries retrieved from data model.
static fluf_io_out_entry_t USER_BUFFER[RES_INST];
static struct user_buffer_struct {
    fluf_io_out_entry_t *buffer;
    size_t buffer_size;
    size_t count;
} USER_BUFFER_STRUCT;

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
            || out_entry->type == FLUF_DATA_TYPE_INT) {
        memcpy(user_buffer, out_entry, sizeof(*user_buffer));
        return 0;
    } else {
        printf("Unknown data type\n");
        return -1;
    }
}

static dm_output_ctx_t OUT_CTX;

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
    OUT_CTX.callback = callback_fnc;                   \
    OUT_CTX.arg = &USER_BUFFER_STRUCT;                 \
    memset(USER_BUFFER, 0x00, sizeof(USER_BUFFER))

AVS_UNIT_TEST(DataModelReadRoot, ReadRootPath) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_ROOT_PATH();
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_1_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_register_object(&dm, &DEF_TEST_OBJ_2_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_read(&dm, &uri, &OUT_CTX));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &DEF_TEST_OBJ_1_PTR));
    AVS_UNIT_ASSERT_SUCCESS(dm_unregister_object(&dm, &DEF_TEST_OBJ_2_PTR));
}

AVS_UNIT_TEST(DataModelReadRoot, ReadNotRegisteredRoot) {
    SET_UP();
    fluf_uri_path_t uri = FLUF_MAKE_ROOT_PATH();
    AVS_UNIT_ASSERT_EQUAL(dm_read(&dm, &uri, &OUT_CTX),
                          FLUF_COAP_CODE_NOT_FOUND);
}
