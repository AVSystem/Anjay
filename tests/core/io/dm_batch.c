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

#include <anjay/lwm2m_send.h>

#include <avsystem/commons/avs_base64.h>
#include <avsystem/commons/avs_unit_test.h>

#include <inttypes.h>

#include "tests/utils/mock_clock.h"

#define TEST_OID 1234

#define BYTES_RID 0
#define STRING_RID 1
#define INT_RID 2
#define UINT_RID 3
#define DOUBLE_RID 4
#define BOOL_RID 5
#define OBJLNK_RID 6
#define INT_ARRAY_RID 7
#define ILLEGAL_IMPL_RID 8

const char test_bytes[] =
        "cfqgldupfjwxzxtmlzdouyimtewybqzmninterrjmrpvfsfyixtnvaqygtfiueme";
#define TEST_BYTES_SIZE (sizeof(test_bytes) - 1)
#define STRING_VALUE "test"
#define INT_VALUE 122333221
#define UINT_VALUE UINT64_MAX
#define DOUBLE_VALUE 1.1
#define BOOL_VALUE true
#define OBJLNK_OID 1
#define OBJLNK_IID 2
const int int_array[4] = { 10, 20, 30, 40 };
const size_t int_array_size = sizeof(int_array) / sizeof(int_array[0]);

#define MOCK_CLOCK_START_RELATIVE 1000
#define MOCK_CLOCK_START_ABSOLUTE \
    (SENML_TIME_SECONDS_THRESHOLD + MOCK_CLOCK_START_RELATIVE)

static int test_list_resources(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    anjay_dm_emit_res(ctx, BYTES_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, STRING_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, INT_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, UINT_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, DOUBLE_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, BOOL_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, OBJLNK_RID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, INT_ARRAY_RID, ANJAY_DM_RES_RM,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, ILLEGAL_IMPL_RID, ANJAY_DM_RES_R,
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
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case BYTES_RID: {
        assert(riid == ANJAY_ID_INVALID);
        anjay_ret_bytes_ctx_t *bytes_ctx;
        int retval = -1;
        if ((bytes_ctx = anjay_ret_bytes_begin(ctx, TEST_BYTES_SIZE))) {
            const size_t append_size = TEST_BYTES_SIZE / 4;
            for (size_t i = 0; i < TEST_BYTES_SIZE; i += append_size) {
                retval = anjay_ret_bytes_append(bytes_ctx, test_bytes + i,
                                                append_size);
                if (retval) {
                    break;
                }
            }
        }
        return retval;
    }
    case STRING_RID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_string(ctx, STRING_VALUE);
    case INT_RID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_i64(ctx, INT_VALUE);
    case UINT_RID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_u64(ctx, UINT_VALUE);
    case DOUBLE_RID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_double(ctx, DOUBLE_VALUE);
    case BOOL_RID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_bool(ctx, BOOL_VALUE);
    case OBJLNK_RID:
        assert(riid == ANJAY_ID_INVALID);
        return anjay_ret_objlnk(ctx, OBJLNK_OID, OBJLNK_IID);
    case INT_ARRAY_RID:
        assert(riid < int_array_size);
        return anjay_ret_i32(ctx, int_array[riid]);
    case ILLEGAL_IMPL_RID:
        assert(riid == ANJAY_ID_INVALID);
        // Invalid case, it shouldn't be possible to call anjay_ret_* twice.
        return anjay_ret_i64(ctx, 0) || anjay_ret_i64(ctx, 1);
    default:
        return -1;
    }
}

static int
test_list_resource_instances(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case INT_ARRAY_RID:
        for (anjay_riid_t riid = 0; riid < int_array_size; ++riid) {
            anjay_dm_emit(ctx, riid);
        }
        return 0;
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

static const anjay_dm_object_def_t OBJECT_DEF = {
    .oid = TEST_OID,
    .handlers = {
        .list_instances = anjay_dm_list_instances_SINGLE,
        .list_resources = test_list_resources,
        .resource_read = test_resource_read,
        .list_resource_instances = test_list_resource_instances
    }
};

#define TEST_SETUP(TimeStart)                                       \
    static const anjay_configuration_t CONFIG = {                   \
        .endpoint_name = "test"                                     \
    };                                                              \
                                                                    \
    anjay_t *anjay = anjay_new(&CONFIG);                            \
    AVS_UNIT_ASSERT_NOT_NULL(anjay);                                \
                                                                    \
    const anjay_dm_object_def_t *test_object_def_ptr = &OBJECT_DEF; \
    AVS_UNIT_ASSERT_SUCCESS(                                        \
            anjay_register_object(anjay, &test_object_def_ptr));    \
    anjay_batch_builder_t *builder = _anjay_batch_builder_new();    \
    AVS_UNIT_ASSERT_NOT_NULL(builder);                              \
                                                                    \
    _anjay_mock_clock_start(                                        \
            avs_time_monotonic_from_scalar((TimeStart), AVS_TIME_S));

#define TEST_TEARDOWN()                     \
    _anjay_batch_builder_cleanup(&builder); \
    anjay_delete(anjay);                    \
    _anjay_mock_clock_finish();

static inline bool is_data_valid(anjay_batch_data_t first,
                                 anjay_batch_data_t second) {
    bool retval = (first.type == second.type);
    if (!retval) {
        return retval;
    }

    switch (first.type) {
    case ANJAY_BATCH_DATA_BYTES:
        retval = (first.value.bytes.length == second.value.bytes.length)
                 && !memcmp(first.value.bytes.data,
                            second.value.bytes.data,
                            first.value.bytes.length);
        break;
    case ANJAY_BATCH_DATA_STRING:
        retval = !strcmp(first.value.string, second.value.string);
        break;
    case ANJAY_BATCH_DATA_INT:
        retval = (first.value.int_value == second.value.int_value);
        break;
    case ANJAY_BATCH_DATA_UINT:
        retval = (first.value.uint_value == second.value.uint_value);
        break;
    case ANJAY_BATCH_DATA_DOUBLE:
        retval = (first.value.double_value == second.value.double_value);
        break;
    case ANJAY_BATCH_DATA_BOOL:
        retval = (first.value.bool_value == second.value.bool_value);
        break;
    case ANJAY_BATCH_DATA_OBJLNK:
        retval = (first.value.objlnk.oid == second.value.objlnk.oid)
                 && (first.value.objlnk.iid == second.value.objlnk.iid);
        break;
    case ANJAY_BATCH_DATA_START_AGGREGATE:
        retval = true;
        break;
    default:
        AVS_UNREACHABLE("fix tests");
        retval = false;
    }

    return retval;
}

static inline bool is_time_almost_equal(avs_time_real_t older,
                                        avs_time_real_t newer) {
    return avs_time_duration_less(avs_time_real_diff(newer, older),
                                  avs_time_duration_from_scalar(10,
                                                                AVS_TIME_MS));
}

static bool is_entry_valid(anjay_batch_entry_t *entry,
                           anjay_rid_t rid,
                           anjay_riid_t riid,
                           anjay_batch_data_t data) {
    if (entry->path.ids[ANJAY_ID_OID] != TEST_OID
            || entry->path.ids[ANJAY_ID_IID] != 0
            || entry->path.ids[ANJAY_ID_RID] != rid
            || entry->path.ids[ANJAY_ID_RIID] != riid
            || !is_data_valid(entry->data, data)) {
        return false;
    }
    if (data.type == ANJAY_BATCH_DATA_START_AGGREGATE) {
        return !avs_time_real_valid(entry->timestamp);
    } else {
        return is_time_almost_equal(entry->timestamp, avs_time_real_now());
    }
}

static inline int
add_current(anjay_batch_builder_t *builder, anjay_t *anjay, anjay_rid_t rid) {
    return anjay_send_batch_data_add_current(
            (anjay_send_batch_builder_t *) builder, anjay, TEST_OID, 0, rid);
}

AVS_UNIT_TEST(dm_batch, single_bytes) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, BYTES_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        BYTES_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_BYTES,
                                            .value.bytes = {
                                                .data = test_bytes,
                                                .length = TEST_BYTES_SIZE
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, single_string) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, STRING_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        STRING_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_STRING,
                                            .value = {
                                                .string = STRING_VALUE
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, single_int) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, INT_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        INT_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_INT,
                                            .value = {
                                                .int_value = INT_VALUE
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, single_uint) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, UINT_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        UINT_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_UINT,
                                            .value = {
                                                .uint_value = UINT_VALUE
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, single_double) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, DOUBLE_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        DOUBLE_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_DOUBLE,
                                            .value = {
                                                .double_value = DOUBLE_VALUE
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, single_bool) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, BOOL_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        BOOL_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_BOOL,
                                            .value = {
                                                .bool_value = BOOL_VALUE
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, single_objlnk) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, OBJLNK_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        OBJLNK_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_OBJLNK,
                                            .value.objlnk = {
                                                .oid = OBJLNK_OID,
                                                .iid = OBJLNK_IID
                                            }
                                        }));
    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, two_resources) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, INT_RID));
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        INT_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_INT,
                                            .value = {
                                                .int_value = INT_VALUE
                                            }
                                        }));

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, DOUBLE_RID));
    AVS_UNIT_ASSERT_TRUE(is_entry_valid(AVS_LIST_TAIL(builder->list),
                                        DOUBLE_RID,
                                        ANJAY_ID_INVALID,
                                        (anjay_batch_data_t) {
                                            .type = ANJAY_BATCH_DATA_DOUBLE,
                                            .value = {
                                                .double_value = DOUBLE_VALUE
                                            }
                                        }));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 2);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, multiple_instance_resource) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(add_current(builder, anjay, INT_ARRAY_RID));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), int_array_size + 1);

    AVS_UNIT_ASSERT_TRUE(
            is_entry_valid(builder->list, INT_ARRAY_RID, ANJAY_ID_INVALID,
                           (anjay_batch_data_t) {
                               .type = ANJAY_BATCH_DATA_START_AGGREGATE
                           }));

    anjay_batch_entry_t *entry;
    uint16_t riid = 0;
    AVS_LIST_FOREACH(entry, AVS_LIST_NEXT(builder->list)) {
        AVS_UNIT_ASSERT_TRUE(is_entry_valid(entry,
                                            INT_ARRAY_RID,
                                            riid,
                                            (anjay_batch_data_t) {
                                                .type = ANJAY_BATCH_DATA_INT,
                                                .value = {
                                                    .int_value = int_array[riid]
                                                }
                                            }));
        riid++;
    }

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, illegal_op) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_FAILED(add_current(builder, anjay, ILLEGAL_IMPL_RID));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_empty) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    const char expected[] = "[]";
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream),
                          sizeof(expected) - 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_bytes) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    char encoded_test_bytes[100] = { 0 };
    AVS_UNIT_ASSERT_SUCCESS(avs_base64_encode_custom(
            encoded_test_bytes, sizeof(encoded_test_bytes),
            (const uint8_t *) test_bytes, TEST_BYTES_SIZE,
            (avs_base64_config_t) {
                .alphabet = AVS_BASE64_URL_SAFE_CHARS,
                .padding_char = '\0'
            }));

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_bytes(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, BYTES_RID),
            AVS_TIME_REAL_INVALID, test_bytes, TEST_BYTES_SIZE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"vd\":\"%s\"}]",
                                TEST_OID, 0, BYTES_RID, encoded_test_bytes);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_one_resource) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            AVS_TIME_REAL_INVALID, INT_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_RID, (int64_t) INT_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_one_resource_with_absolute_timestamp) {
    TEST_SETUP(MOCK_CLOCK_START_ABSOLUTE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            avs_time_real_from_scalar(MOCK_CLOCK_START_ABSOLUTE - 123,
                                      AVS_TIME_S),
            INT_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"t\":%.17g,\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_RID,
                                MOCK_CLOCK_START_ABSOLUTE - 123.,
                                (int64_t) INT_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_two_resources) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            AVS_TIME_REAL_INVALID, INT_VALUE));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_string(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, STRING_RID),
            AVS_TIME_REAL_INVALID, STRING_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"v\":%" PRIi64 "},{\"n\":\"/%" PRIu16
                                "/%" PRIu16 "/%" PRIu16 "\",\"vs\":\"%s\"}]",
                                TEST_OID, 0, INT_RID, (int64_t) INT_VALUE,
                                TEST_OID, 0, STRING_RID, STRING_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_two_resources_with_relative_timestamp) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            avs_time_real_from_scalar(1, AVS_TIME_MIN), INT_VALUE));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_string(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, STRING_RID),
            avs_time_real_from_scalar(2, AVS_TIME_MIN), STRING_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    // Because _anjay_batch_builder_compile() calls avs_time_real_now() it
    // advances MOCK_CLOCK by 1 nanosecond and we want to disable this effect
    // here
    _anjay_mock_clock_reset(avs_time_monotonic_from_scalar(
            MOCK_CLOCK_START_RELATIVE, AVS_TIME_S));

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size = avs_simple_snprintf(
            expected, sizeof(expected),
            "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
            "\",\"t\":%.17g,\"v\":%" PRIi64 "},{\"n\":\"/%" PRIu16 "/%" PRIu16
            "/%" PRIu16 "\",\"t\":%.17g,\"vs\":\"%s\"}]",
            TEST_OID, 0, INT_RID, 60. - MOCK_CLOCK_START_RELATIVE,
            (int64_t) INT_VALUE, TEST_OID, 0, STRING_RID,
            120. - MOCK_CLOCK_START_RELATIVE, STRING_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, serialize_resource_instance) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder,
            &MAKE_RESOURCE_INSTANCE_PATH(TEST_OID, 0, INT_ARRAY_RID, 0),
            AVS_TIME_REAL_INVALID, int_array[0]));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "/%" PRIu16 "\",\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_ARRAY_RID, 0,
                                (int64_t) int_array[0]);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, absolute_timestamp_higher_than_serialization_time) {
    TEST_SETUP(MOCK_CLOCK_START_ABSOLUTE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            avs_time_real_from_scalar(MOCK_CLOCK_START_ABSOLUTE + 123,
                                      AVS_TIME_S),
            INT_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_RID,
                                // timestamp is omitted
                                (int64_t) INT_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, relative_timestamp_higher_than_serialization_time) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            avs_time_real_from_scalar(MOCK_CLOCK_START_RELATIVE + 123,
                                      AVS_TIME_S),
            INT_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_RID,
                                // timestamp is omitted
                                (int64_t) INT_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, relative_timestamp_absolute_serialization_time) {
    TEST_SETUP(MOCK_CLOCK_START_ABSOLUTE);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            avs_time_real_from_scalar(MOCK_CLOCK_START_RELATIVE, AVS_TIME_S),
            INT_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_RID,
                                // timestamp is omitted
                                (int64_t) INT_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}

AVS_UNIT_TEST(dm_batch, negative_timestamp) {
    TEST_SETUP(MOCK_CLOCK_START_RELATIVE);

    const int negative_timestamp = -MOCK_CLOCK_START_RELATIVE;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_PATH(TEST_OID, 0, INT_RID),
            avs_time_real_from_scalar(negative_timestamp, AVS_TIME_S),
            INT_VALUE));
    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
    AVS_UNIT_ASSERT_NOT_NULL(batch);

    // Because _anjay_batch_builder_compile() calls avs_time_real_now() it
    // advances MOCK_CLOCK by 1 nanosecond and we want to disable this effect
    // here
    _anjay_mock_clock_reset(avs_time_monotonic_from_scalar(
            MOCK_CLOCK_START_RELATIVE, AVS_TIME_S));

    char buffer[200] = { 0 };
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, buffer, sizeof(buffer));
    anjay_output_ctx_t *out_ctx =
            _anjay_output_senml_like_create((avs_stream_t *) &stream,
                                            &MAKE_ROOT_PATH(),
                                            AVS_COAP_FORMAT_SENML_JSON);
    AVS_UNIT_ASSERT_NOT_NULL(out_ctx);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_data_output(anjay, batch, 1, out_ctx));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_output_ctx_destroy(&out_ctx));
    char expected[200];
    int expected_size =
            avs_simple_snprintf(expected, sizeof(expected),
                                "[{\"n\":\"/%" PRIu16 "/%" PRIu16 "/%" PRIu16
                                "\",\"t\":%d,\"v\":%" PRIi64 "}]",
                                TEST_OID, 0, INT_RID,
                                negative_timestamp - MOCK_CLOCK_START_RELATIVE,
                                (int64_t) INT_VALUE);
    AVS_UNIT_ASSERT_EQUAL(avs_stream_outbuf_offset(&stream), expected_size);
    AVS_UNIT_ASSERT_EQUAL_STRING(buffer, expected);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);

    TEST_TEARDOWN();
}
