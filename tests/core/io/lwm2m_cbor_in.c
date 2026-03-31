/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_vector.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    include <anjay/lwm2m_gateway.h>
#    include <string.h>
#endif // ANJAY_WITH_LWM2M_GATEWAY

#define TEST_ENV(Data, Path)                                                \
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER;        \
    avs_stream_inbuf_set_buffer(&stream, Data, sizeof(Data) - 1);           \
    anjay_unlocked_input_ctx_t *in;                                         \
    ASSERT_OK(_anjay_input_lwm2m_cbor_create(&in, (avs_stream_t *) &stream, \
                                             &(Path)));

#define TEST_TEARDOWN(ExpectedResult)                                       \
    do {                                                                    \
        AVS_CONCAT(ASSERT_, ExpectedResult)(_anjay_input_ctx_destroy(&in)); \
    } while (0)

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    define PATHS_EQUAL(Path1, Path2)        \
        (_anjay_uri_path_equal(Path1, Path2) \
         && _anjay_uri_path_prefix_equal(Path1, Path2))
#else // ANJAY_WITH_LWM2M_GATEWAY
#    define PATHS_EQUAL(Path1, Path2) _anjay_uri_path_equal(Path1, Path2)
#endif // ANJAY_WITH_LWM2M_GATEWAY

static const anjay_uri_path_t TEST_INSTANCE_PATH = MAKE_INSTANCE_PATH(13, 26);

static const anjay_uri_path_t TEST_RESOURCE_PATH =
        MAKE_RESOURCE_PATH(13, 26, 1);

static const anjay_uri_path_t TEST_RESOURCE2_PATH =
        MAKE_RESOURCE_PATH(13, 26, 2);

static const anjay_uri_path_t TEST_RESOURCE_INSTANCE_PATH =
        MAKE_RESOURCE_INSTANCE_PATH(13, 26, 1, 2);

static const anjay_uri_path_t TEST_RESOURCE_INSTANCE2_PATH =
        MAKE_RESOURCE_INSTANCE_PATH(13, 26, 1, 3);

static const anjay_uri_path_t TEST_OTHER_OBJ_RESOURCE_PATH =
        MAKE_RESOURCE_PATH(14, 27, 2);

static void expect_path(anjay_unlocked_input_ctx_t *in,
                        const anjay_uri_path_t *expected_path) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(PATHS_EQUAL(&path, expected_path));

    // cached value
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(PATHS_EQUAL(&path, expected_path));
}

static void expect_path_retrieval_failure(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_EQ(_anjay_input_get_path(in, &path, NULL), ANJAY_ERR_BAD_REQUEST);
}

static void pull_i64_value(anjay_unlocked_input_ctx_t *in, int64_t expected) {
    int64_t value = -1;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, expected);
    ASSERT_OK(_anjay_input_next_entry(in));
}

static void pull_double_value(anjay_unlocked_input_ctx_t *in, double expected) {
    double value = -1;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    ASSERT_EQ(value, expected);
    ASSERT_OK(_anjay_input_next_entry(in));
}

static void pull_bool_true(anjay_unlocked_input_ctx_t *in) {
    bool value = false;
    ASSERT_OK(_anjay_get_bool_unlocked(in, &value));
    ASSERT_TRUE(value);
    ASSERT_OK(_anjay_input_next_entry(in));
}

static void pull_bytes_value(anjay_unlocked_input_ctx_t *in,
                             const void *expected,
                             size_t expected_size) {
    void *buf = calloc(1, expected_size);
    ASSERT_NOT_NULL(buf);
    size_t bytes_read = 0;
    bool message_finished = false;
    ASSERT_OK(_anjay_get_bytes_unlocked(in, &bytes_read, &message_finished, buf,
                                        expected_size));
    ASSERT_TRUE(message_finished);
    ASSERT_EQ(bytes_read, expected_size);
    ASSERT_EQ_BYTES_SIZED(buf, expected, expected_size);
    ASSERT_OK(_anjay_input_next_entry(in));
    free(buf);
}

static void expect_whole_input_consumed(anjay_unlocked_input_ctx_t *in) {
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
    ASSERT_EQ(_anjay_json_like_decoder_state(((lwm2m_cbor_in_t *) in)->ctx),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

static void test_single_i64_resource(anjay_unlocked_input_ctx_t *in,
                                     const anjay_uri_path_t *expected_path,
                                     int64_t expected_value) {
    expect_path(in, expected_path);
    pull_i64_value(in, expected_value);
    expect_whole_input_consumed(in);
}

static void test_single_double_resource(anjay_unlocked_input_ctx_t *in,
                                        const anjay_uri_path_t *expected_path,
                                        double expected_value) {
    expect_path(in, expected_path);
    pull_double_value(in, expected_value);
    expect_whole_input_consumed(in);
}

static void test_single_bytes_resource(anjay_unlocked_input_ctx_t *in,
                                       const anjay_uri_path_t *expected_path,
                                       const void *expected_value,
                                       size_t expected_size) {
    expect_path(in, expected_path);
    pull_bytes_value(in, expected_value, expected_size);
    expect_whole_input_consumed(in);
}

static void test_two_i64_resources(anjay_unlocked_input_ctx_t *in,
                                   const anjay_uri_path_t *first_expected_path,
                                   int64_t first_expected_value,
                                   const anjay_uri_path_t *second_expected_path,
                                   int64_t second_expected_value) {
    expect_path(in, first_expected_path);
    pull_i64_value(in, first_expected_value);
    expect_path(in, second_expected_path);
    pull_i64_value(in, second_expected_value);
    expect_whole_input_consumed(in);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, single_resource) {
    // {[13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, single_resource_bool) {
    // {[13, 26, 1]: true}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\xF5" };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);
    pull_bool_true(in);
    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, single_resource_indefinite) {
    // {_ [_ 13, 26, 1]: 42}
    static const char DATA[] = { "\xBF\x9F\x0D\x18\x1A\x01\xFF\x18\x2A\xFF" };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, single_resource_nested) {
    // {13: {26: {1: 42}}}
    static const char DATA[] = { "\xA1\x0D\xA1\x18\x1A\xA1\x01\x18\x2A" };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, single_resource_nested_arrays) {
    // {[13]: {[26]: {[1]: 42}}}
    static const char DATA[] = {
        "\xA1\x81\x0D\xA1\x81\x18\x1A\xA1\x81\x01\x18\x2A"
    };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource,
              single_resource_nested_arrays_all_indefinite) {
    // {_ [_ 13]: {_ [_ 26]: {_ [_ 1]: 42}}}
    static const char DATA[] = { "\xBF\x9F\x0D\xFF\xBF\x9F\x18\x1A\xFF\xBF\x9F"
                                 "\x01\xFF\x18\x2A\xFF\xFF\xFF" };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource_instance, max_possible_nesting) {
    // Uses decimal fraction
    // {[13]: {[26]: {[1]: {[2]: 4([-1, 45])}}}}
    static const char DATA[] = { "\xA1\x81\x0D\xA1\x81\x18\x1A\xA1\x81\x01"
                                 "\xA1\x81\x02\xC4\x82\x20\x18\x2D" };
    TEST_ENV(DATA, TEST_RESOURCE_INSTANCE_PATH);
    test_single_double_resource(in, &TEST_RESOURCE_INSTANCE_PATH, 4.5);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, two_resources_1) {
    // {[13, 26]: {1: 42, 2: 21}}
    static const char DATA[] = {
        "\xA1\x82\x0D\x18\x1A\xA2\x01\x18\x2A\x02\x15"
    };
    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 42, &TEST_RESOURCE2_PATH,
                           21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, two_resources_1_repeated_next_entry) {
    // {[13, 26]: {1: 42, 2: 21}}
    static const char DATA[] = {
        "\xA1\x82\x0D\x18\x1A\xA2\x01\x18\x2A\x02\x15"
    };
    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);
    pull_i64_value(in, 42);
    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_next_entry(in));
    expect_path(in, &TEST_RESOURCE2_PATH);
    pull_i64_value(in, 21);
    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, two_resources_2) {
    // {[13, 26, 1]: 42, [13, 26, 2]: 21}
    static const char DATA[] = {
        "\xA2\x83\x0D\x18\x1A\x01\x18\x2A\x83\x0D\x18\x1A\x02\x15"
    };
    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 42, &TEST_RESOURCE2_PATH,
                           21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, two_resources_3) {
    // {[13, 26]: {1: 42}, [13, 26, 2]: 21}
    static const char DATA[] = {
        "\xA2\x82\x0D\x18\x1A\xA1\x01\x18\x2A\x83\x0D\x18\x1A\x02\x15"
    };
    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 42, &TEST_RESOURCE2_PATH,
                           21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, two_resources_4) {
    // {[13, 26, 1]: 42, [13, 26]: {[2]: 21}}
    static const char DATA[] = {
        "\xA2\x83\x0D\x18\x1A\x01\x18\x2A\x82\x0D\x18\x1A\xA1\x81\x02\x15"
    };
    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 42, &TEST_RESOURCE2_PATH,
                           21);
    TEST_TEARDOWN(OK);
}

#define CHUNK1 "\x00\x11\x22\x33\x44\x55\x66\x77"
#define CHUNK2 "\x88\x99\xAA\xBB\xCC\xDD\xEE\xFF"
#define TEST_BYTES CHUNK1 CHUNK2

AVS_UNIT_TEST(lwm2m_cbor_in_resource, bytes) {
    // {[13, 26, 1]: h'00112233445566778899AABBCCDDEEFF'}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x50" TEST_BYTES };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_bytes_resource(in, &TEST_RESOURCE_PATH, TEST_BYTES,
                               sizeof(TEST_BYTES) - 1);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, bytes_indefinite) {
    // {[13, 26, 1]: (_ h'0011223344556677', h'8899AABBCCDDEEFF')}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x5F\x48" CHUNK1
                                 "\x48" CHUNK2 "\xFF" };

    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    test_single_bytes_resource(in, &TEST_RESOURCE_PATH, TEST_BYTES,
                               sizeof(TEST_BYTES) - 1);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, bytes_indefinite_and_int) {
    // {[13, 26]: {1: (_ h'0011223344556677', h'8899AABBCCDDEEFF'), 2: 7}}
    static const char DATA[] = { "\xA1\x82\x0D\x18\x1A\xA2\x01\x5F\x48" CHUNK1
                                 "\x48" CHUNK2 "\xFF\x02\x07" };

    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);
    pull_bytes_value(in, TEST_BYTES, sizeof(TEST_BYTES) - 1);
    expect_path(in, &TEST_RESOURCE2_PATH);
    pull_i64_value(in, 7);
    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, bytes_indefinite_small_gets) {
    // {[13, 26, 1]: (_ h'0011223344556677', h'8899AABBCCDDEEFF')}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x5F\x48" CHUNK1
                                 "\x48" CHUNK2 "\xFF" };

    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);

    size_t bytes_to_read = sizeof(TEST_BYTES) - 1;
    void *buf = calloc(1, bytes_to_read);
    ASSERT_NOT_NULL(buf);

    bool message_finished = false;
    size_t total_bytes_read = 0;

    while (!message_finished) {
        size_t bytes_read;
        ASSERT_OK(_anjay_get_bytes_unlocked(
                in,
                &bytes_read,
                &message_finished,
                buf + total_bytes_read,
                AVS_MIN(bytes_to_read - total_bytes_read, 3)));
        total_bytes_read += bytes_read;
    }

    ASSERT_EQ_BYTES_SIZED(buf, TEST_BYTES, bytes_to_read);

    ASSERT_OK(_anjay_input_next_entry(in));
    free(buf);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, bytes_split) {
    // {[13, 26, 1]: h'00112233445566778899AABBCCDDEEFF'}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x50" TEST_BYTES };

    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);

    const size_t bytes_to_read = sizeof(TEST_BYTES) - 1;
    char *buf = calloc(1, bytes_to_read);
    ASSERT_NOT_NULL(buf);

    size_t bytes_read = 0;
    bool message_finished = true;
    ASSERT_OK(_anjay_get_bytes_unlocked(in, &bytes_read, &message_finished, buf,
                                        bytes_to_read / 2));
    ASSERT_FALSE(message_finished);
    ASSERT_EQ(bytes_read, bytes_to_read / 2);

    ASSERT_OK(_anjay_get_bytes_unlocked(in,
                                        &bytes_read,
                                        &message_finished,
                                        buf + bytes_read,
                                        bytes_to_read / 2));
    ASSERT_TRUE(message_finished);
    ASSERT_EQ(bytes_read, bytes_to_read / 2);
    ASSERT_EQ_BYTES_SIZED(buf, TEST_BYTES, bytes_to_read);

    ASSERT_OK(_anjay_input_next_entry(in));
    free(buf);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

#undef TEST_BYTES
#undef CHUNK1
#undef CHUNK2

#define TEST_STRING_PART1 "c--cossi"
#define TEST_STRING_PART2 "ezepsulo"
#define TEST_STRING TEST_STRING_PART1 TEST_STRING_PART2

AVS_UNIT_TEST(lwm2m_cbor_in_resource, string) {
    // {[13, 26, 1]: "c--cossiezepsulo"}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x70" TEST_STRING };

    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);

    const size_t bytes_to_read = sizeof(TEST_STRING);
    void *buf = malloc(bytes_to_read);
    ASSERT_NOT_NULL(buf);
    memset(buf, 21, bytes_to_read);

    ASSERT_OK(_anjay_get_string_unlocked(in, buf, bytes_to_read));
    ASSERT_EQ_STR(buf, TEST_STRING);

    ASSERT_OK(_anjay_input_next_entry(in));
    free(buf);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, string_split) {
    // {[13, 26, 1]: "c--cossiezepsulo"}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x70" TEST_STRING };

    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    expect_path(in, &TEST_RESOURCE_PATH);

    const size_t bytes_to_read = sizeof(TEST_STRING) / 2 + 1;
    void *buf = malloc(bytes_to_read);
    ASSERT_NOT_NULL(buf);
    memset(buf, 21, bytes_to_read);

    ASSERT_EQ(_anjay_get_string_unlocked(in, buf, bytes_to_read),
              ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ_STR(buf, TEST_STRING_PART1);
    memset(buf, 21, bytes_to_read);

    ASSERT_OK(_anjay_get_string_unlocked(in, buf, bytes_to_read));
    ASSERT_EQ_STR(buf, TEST_STRING_PART2);

    ASSERT_OK(_anjay_input_next_entry(in));
    free(buf);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

#undef TEST_STRING
#undef TEST_STRING_PART1
#undef TEST_STRING_PART2

AVS_UNIT_TEST(lwm2m_cbor_in_resource_instance, null_and_int) {
    // {[13, 26, 1]: {0: null, 1: 5}}
    static const char DATA[] = {
        "\xA1\x83\x0D\x18\x1A\x01\xA2\x02\xF6\x03\x05"
    };

    TEST_ENV(DATA, TEST_RESOURCE_PATH);

    expect_path(in, &TEST_RESOURCE_INSTANCE_PATH);
    ASSERT_OK(_anjay_input_get_null(in));
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_path(in, &TEST_RESOURCE_INSTANCE2_PATH);
    pull_i64_value(in, 5);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource_instance, try_null_then_int) {
    // {[13, 26, 1, 2]: 5}
    static const char DATA[] = { "\xA1\x84\x0D\x18\x1A\x01\x02\x05" };

    TEST_ENV(DATA, TEST_RESOURCE_INSTANCE_PATH);

    expect_path(in, &TEST_RESOURCE_INSTANCE_PATH);
    ASSERT_FAIL(_anjay_input_get_null(in));
    pull_i64_value(in, 5);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

static void expect_resource_path(anjay_unlocked_input_ctx_t *ctx,
                                 uint16_t rid) {
    expect_path(ctx, &MAKE_RESOURCE_PATH(13, 26, rid));
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, all_types) {
    // It's important to duplicate some type at the end to ensure that nesting
    // of the paths works correctly for all types.
    // {[13, 26]: {1: 1, 2: -1, 3: 2.5, 4: "test", 5: h'11223344', 6: "12:34",
    // 7: 1}}
    static const char DATA[] = {
        "\xA1\x82\x0D\x18\x1A\xA7\x01\x01\x02\x20\x03\xF9\x41\x00\x04\x64\x74"
        "\x65\x73\x74\x05\x44\x11\x22\x33\x44\x06\x65\x31\x32\x3A\x33\x34\x07"
        "\x01"
    };

    TEST_ENV(DATA, TEST_INSTANCE_PATH);

    expect_resource_path(in, 1);
    uint64_t u64 = 0;
    ASSERT_OK(_anjay_get_u64_unlocked(in, &u64));
    ASSERT_EQ(u64, 1);
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_resource_path(in, 2);
    int64_t i64 = 0;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &i64));
    ASSERT_EQ(i64, -1);
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_resource_path(in, 3);
    double f = 0;
    ASSERT_OK(_anjay_get_double_unlocked(in, &f));
    ASSERT_EQ(f, 2.5);
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_resource_path(in, 4);
    char str[sizeof("test")];
    ASSERT_OK(_anjay_get_string_unlocked(in, str, sizeof(str)));
    ASSERT_EQ_STR(str, "test");
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_resource_path(in, 5);
    char buf[sizeof("\x11\x22\x33\x44") - 1];
    size_t bytes_read = 0;
    bool message_finished = false;
    ASSERT_OK(_anjay_get_bytes_unlocked(in, &bytes_read, &message_finished, buf,
                                        sizeof(buf)));
    ASSERT_EQ_BYTES_SIZED(buf, "\x11\x22\x33\x44", sizeof(buf));
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_resource_path(in, 6);
    anjay_oid_t oid = 0;
    anjay_iid_t iid = 0;
    ASSERT_OK(_anjay_get_objlnk_unlocked(in, &oid, &iid));
    ASSERT_EQ(oid, 12);
    ASSERT_EQ(iid, 34);
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_resource_path(in, 7);
    u64 = 0;
    ASSERT_OK(_anjay_get_u64_unlocked(in, &u64));
    ASSERT_EQ(u64, 1);
    ASSERT_OK(_anjay_input_next_entry(in));

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, composite) {
    // {13: {26: {1: 1}}, 14: {27: {2: 2}}}
    static const char DATA[] = {
        "\xA2\x0D\xA1\x18\x1A\xA1\x01\x01\x0E\xA1\x18\x1B\xA1\x02\x02"
    };

    TEST_ENV(DATA, MAKE_ROOT_PATH());
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 1,
                           &TEST_OTHER_OBJ_RESOURCE_PATH, 2);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, composite_indefinite_maps) {
    // {_ 13: {_ 26: {_ 1: 1}}, 14: {_ 27: {_ 2: 2}}}
    static const char DATA[] = {
        "\xBF\x0D\xBF\x18\x1A\xBF\x01\x01\xFF\xFF\x0E\xBF\x18\x1B\xBF\x02\x02"
        "\xFF\xFF\xFF"
    };

    TEST_ENV(DATA, MAKE_ROOT_PATH());
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 1,
                           &TEST_OTHER_OBJ_RESOURCE_PATH, 2);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, composite_indefinite_maps_and_arrays) {
    // {_ [_ 13]: {_ [_ 26]: {_ [_ 1]: 1}}, [_ 14]: {_ [_ 27]: {_ [_ 2]: 2}}}
    static const char DATA[] = {
        "\xBF\x9F\x0D\xFF\xBF\x9F\x18\x1A\xFF\xBF\x9F\x01\xFF\x01\xFF\xFF\x9F"
        "\x0E\xFF\xBF\x9F\x18\x1B\xFF\xBF\x9F\x02\xFF\x02\xFF\xFF\xFF"
    };

    TEST_ENV(DATA, MAKE_ROOT_PATH());
    test_two_i64_resources(in, &TEST_RESOURCE_PATH, 1,
                           &TEST_OTHER_OBJ_RESOURCE_PATH, 2);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_id_too_big_solo) {
    // {[13, 26]: {65536: 5}}
    static const char DATA[] = {
        "\xA1\x82\x0D\x18\x1A\xA1\x1A\x00\x01\x00\x00\x05"
    };

    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_id_too_big_array) {
    // {[13, 26, 65536]: 5}
    static const char DATA[] = {
        "\xA1\x83\x0D\x18\x1A\x1A\x00\x01\x00\x00\x05"
    };

    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_too_long_1) {
    // {[13, 26, 3, 4, 5]: 5}
    static const char DATA[] = { "\xA1\x85\x0D\x18\x1A\x03\x04\x05\x05" };

    TEST_ENV(DATA, TEST_INSTANCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_too_long_2) {
    // {[13, 26, 1]: {2: 5, [3, 4]: 6}}
    static const char DATA[] = {
        "\xA1\x83\x0D\x18\x1A\x01\xA2\x02\x05\x82\x03\x04\x05"
    };

    TEST_ENV(DATA, TEST_RESOURCE_INSTANCE_PATH);

    expect_path(in, &TEST_RESOURCE_INSTANCE_PATH);
    pull_i64_value(in, 5);
    expect_path_retrieval_failure(in);

    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_too_long_3) {
    // {13: {26: {1: {2: {3: 4}}}}}
    static const char DATA[] = {
        "\xA1\x0D\xA1\x18\x1A\xA1\x01\xA1\x02\xA1\x03\x04"
    };

    TEST_ENV(DATA, TEST_RESOURCE_INSTANCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_with_empty_array_keys) {
    // {13: {[]: 26}, {[]: 1}: 42}
    static const char DATA[] = {
        "\xA2\x0D\xA1\x80\x18\x1A\xA1\x80\x01\x18\x2A"
    };

    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_outside_base_1) {
    // {[13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_OBJECT_PATH(14));
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_outside_base_2) {
    // {[13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_INSTANCE_PATH(13, 27));
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_outside_base_3) {
    // {[13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_RESOURCE_PATH(13, 26, 2));
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_outside_base_4) {
    // {[13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_RESOURCE_INSTANCE_PATH(13, 26, 1, 69));
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_unexpected_type_1) {
    // {[13, 0.26, 1]: 42}
    static const char DATA[] = {
        "\xA1\x83\x0D\xFB\x3F\xD0\xA3\xD7\x0A\x3D\x70\xA4\x01\x18\x2A"
    };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_unexpected_type_2) {
    // {0.13: {[26, 1]: 42}}
    static const char DATA[] = { "\xA1\xFB\x3F\xC0\xA3\xD7\x0A\x3D\x70\xA4\xA1"
                                 "\x82\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_unexpected_type_3) {
    // {13: {"26": {1: 42}}}
    static const char DATA[] = { "\xA1\x0D\xA1\x62\x32\x36\xA1\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, not_a_map_int) {
    // 0
    static const char DATA[] = { "\x00" };
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER;
    avs_stream_inbuf_set_buffer(&stream, DATA, sizeof(DATA) - 1);
    anjay_unlocked_input_ctx_t *in;
    ASSERT_FAIL(_anjay_input_lwm2m_cbor_create(&in, (avs_stream_t *) &stream,
                                               &(MAKE_ROOT_PATH())));
}

AVS_UNIT_TEST(lwm2m_cbor_in, not_a_map_array) {
    // [1, 2, 3, 4]
    static const char DATA[] = { "\x84\x01\x02\x03\x04" };
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER;
    avs_stream_inbuf_set_buffer(&stream, DATA, sizeof(DATA) - 1);
    anjay_unlocked_input_ctx_t *in;
    ASSERT_FAIL(_anjay_input_lwm2m_cbor_create(&in, (avs_stream_t *) &stream,
                                               &(MAKE_ROOT_PATH())));
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_lack_unexpected_array) {
    // {13: {26: [{1: 42}]}}
    static const char DATA[] = { "\xA1\x0D\xA1\x18\x1A\x81\xA1\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, path_sudden_nested_map_end) {
    // {[13, 26]: {1: 42}} - cut in the middle, see below
    static const char DATA[] = {
        "\xA1"     // map, 1 pair
        "\x82"     //   array, 2 elements
        "\x0D"     //     unsigned 13
        "\x18\x1A" //     unsigned 26
        "\xA1"     //   map, 1 pair
        // 2 elements should follow, but for the test we cut the input here
    };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(OK);
}

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    define PREFIXED_URI(Prefix, BasePathVar)                         \
        const anjay_uri_path_t AVS_CONCAT(Prefix, _, BasePathVar) = { \
            .ids = {                                                  \
                [ANJAY_ID_OID] = BasePathVar.ids[ANJAY_ID_OID],       \
                [ANJAY_ID_IID] = BasePathVar.ids[ANJAY_ID_IID],       \
                [ANJAY_ID_RID] = BasePathVar.ids[ANJAY_ID_RID],       \
                [ANJAY_ID_RIID] = BasePathVar.ids[ANJAY_ID_RIID],     \
            },                                                        \
            .prefix = #Prefix                                         \
        }

static PREFIXED_URI(dev1, TEST_INSTANCE_PATH);
static PREFIXED_URI(dev1, TEST_RESOURCE_PATH);
static PREFIXED_URI(dev1, TEST_RESOURCE2_PATH);
static PREFIXED_URI(dev1, TEST_RESOURCE_INSTANCE_PATH);
static PREFIXED_URI(dev1, TEST_OTHER_OBJ_RESOURCE_PATH);
static PREFIXED_URI(dev2, TEST_RESOURCE_PATH);

#    undef PREFIXED_URI

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_single_resource) {
    // {["dev1", 13, 26, 1]: 42}
    static const char DATA[] = {
        "\xA1\x84\x64\x64\x65\x76\x31\x0D\x18\x1A\x01\x18\x2A"
    };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &dev1_TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_single_resource_indefinite) {
    // {_ [_ "dev1", 13, 26, 1]: 42}
    static const char DATA[] = {
        "\xBF\x9F\x64\x64\x65\x76\x31\x0D\x18\x1A\x01\xFF\x18\x2A\xFF"
    };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &dev1_TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_single_resource_nested) {
    // {"dev1": {13: {26: {1: 42}}}}
    static const char DATA[] = {
        "\xA1\x64\x64\x65\x76\x31\xA1\x0D\xA1\x18\x1A\xA1\x01\x18\x2A"
    };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &dev1_TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_single_resource_nested_arrays) {
    // {["dev1"]: {[13]: {[26]: {[1]: 42}}}}
    static const char DATA[] = { "\xA1\x81\x64\x64\x65\x76\x31\xA1\x81\x0D\xA1"
                                 "\x81\x18\x1A\xA1\x81\x01\x18\x2A" };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &dev1_TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource,
              gw_single_resource_nested_arrays_all_indefinite) {
    // {_ [_ (_ "dev1")]: {_ [_ 13]: {_ [_ 26]: {_ [_ 1]: 42}}}}
    static const char DATA[] = {
        "\xBF\x9F\x7F\x64\x64\x65\x76\x31\xFF\xFF\xBF\x9F\x0D\xFF\xBF\x9F\x18"
        "\x1A\xFF\xBF\x9F\x01\xFF\x18\x2A\xFF\xFF\xFF\xFF"
    };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    test_single_i64_resource(in, &dev1_TEST_RESOURCE_PATH, 42);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource_instance, gw_max_possible_nesting) {
    // Uses decimal fraction
    // {["dev1"]: {[13]: {[26]: {[1]: {[2]: 4([-1, 45])}}}}}
    static const char DATA[] = {
        "\xA1\x81\x64\x64\x65\x76\x31\xA1\x81\x0D\xA1\x81\x18\x1A\xA1\x81\x01"
        "\xA1\x81\x02\xC4\x82\x20\x18\x2D"
    };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_INSTANCE_PATH);
    test_single_double_resource(in, &dev1_TEST_RESOURCE_INSTANCE_PATH, 4.5);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_two_resources_1) {
    // {["dev1", 13, 26]: {1: 42, 2: 21}}
    static const char DATA[] = {
        "\xA1\x83\x64\x64\x65\x76\x31\x0D\x18\x1A\xA2\x01\x18\x2A\x02\x15"
    };
    TEST_ENV(DATA, dev1_TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &dev1_TEST_RESOURCE_PATH, 42,
                           &dev1_TEST_RESOURCE2_PATH, 21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_two_resources_2) {
    // {["dev1", 13, 26, 1]: 42, ["dev1", 13, 26, 2]: 21}
    static const char DATA[] = {
        "\xA2\x84\x64\x64\x65\x76\x31\x0D\x18\x1A\x01\x18\x2A\x84\x64\x64\x65"
        "\x76\x31\x0D\x18\x1A\x02\x15"
    };
    TEST_ENV(DATA, dev1_TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &dev1_TEST_RESOURCE_PATH, 42,
                           &dev1_TEST_RESOURCE2_PATH, 21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_two_resources_3) {
    // {["dev1", 13, 26]: {1: 42}, ["dev1", 13, 26, 2]: 21}
    static const char DATA[] = {
        "\xA2\x83\x64\x64\x65\x76\x31\x0D\x18\x1A\xA1\x01\x18\x2A\x84\x64\x64"
        "\x65\x76\x31\x0D\x18\x1A\x02\x15"
    };
    TEST_ENV(DATA, dev1_TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &dev1_TEST_RESOURCE_PATH, 42,
                           &dev1_TEST_RESOURCE2_PATH, 21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_two_resources_4) {
    // {"dev1": {[13, 26, 1]: 42, [13, 26]: {[2]: 21}}}
    static const char DATA[] = {
        "\xA1\x64\x64\x65\x76\x31\xA2\x83\x0D\x18\x1A\x01\x18\x2A\x82\x0D\x18"
        "\x1A\xA1\x81\x02\x15"
    };
    TEST_ENV(DATA, dev1_TEST_INSTANCE_PATH);
    test_two_i64_resources(in, &dev1_TEST_RESOURCE_PATH, 42,
                           &dev1_TEST_RESOURCE2_PATH, 21);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_composite) {
    // {"dev1": {[13, 26, 1]: 1, [14, 27, 2]: 2}, ["dev2", 13, 26]: {1: 3}, [13,
    // 26, 1]: 4}
    static const char DATA[] = {
        "\xA3\x64\x64\x65\x76\x31\xA2\x83\x0D\x18\x1A\x01\x01\x83\x0E\x18\x1B"
        "\x02\x02\x83\x64\x64\x65\x76\x32\x0D\x18\x1A\xA1\x01\x03\x83\x0D\x18"
        "\x1A\x01\x04"
    };
    TEST_ENV(DATA, MAKE_ROOT_PATH());

    expect_path(in, &dev1_TEST_RESOURCE_PATH);
    pull_i64_value(in, 1);
    expect_path(in, &dev1_TEST_OTHER_OBJ_RESOURCE_PATH);
    pull_i64_value(in, 2);
    expect_path(in, &dev2_TEST_RESOURCE_PATH);
    pull_i64_value(in, 3);
    expect_path(in, &TEST_RESOURCE_PATH);
    pull_i64_value(in, 4);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in_resource, gw_composite_indefinite_maps_and_arrays) {
    // {_ [_ (_ "dev1"), 13, 26]: {_ 1: 1}, [_ (_ "dev2"), 13, 26]: {_ 1: 2}, [_
    // 13, 26, 1]: 3}
    static const char DATA[] = {
        "\xBF\x9F\x7F\x64\x64\x65\x76\x31\xFF\x0D\x18\x1A\xFF\xBF\x01\x01\xFF"
        "\x9F\x7F\x64\x64\x65\x76\x32\xFF\x0D\x18\x1A\xFF\xBF\x01\x02\xFF\x9F"
        "\x0D\x18\x1A\x01\xFF\x03\xFF"
    };

    TEST_ENV(DATA, MAKE_ROOT_PATH());

    expect_path(in, &dev1_TEST_RESOURCE_PATH);
    pull_i64_value(in, 1);
    expect_path(in, &dev2_TEST_RESOURCE_PATH);
    pull_i64_value(in, 2);
    expect_path(in, &TEST_RESOURCE_PATH);
    pull_i64_value(in, 3);

    expect_whole_input_consumed(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_too_long_1) {
    // {["dev1", 13, 26, 3, 4, 5]: 5}
    static const char DATA[] = {
        "\xA1\x86\x64\x64\x65\x76\x31\x0D\x18\x1A\x03\x04\x05\x05"
    };

    TEST_ENV(DATA, dev1_TEST_INSTANCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_too_long_2) {
    // {["dev1", 13, 26, 1]: {2: 5, [3, 4]: 6}}
    static const char DATA[] = { "\xA1\x84\x64\x64\x65\x76\x31\x0D\x18\x1A\x01"
                                 "\xA2\x02\x05\x82\x03\x04\x06" };

    TEST_ENV(DATA, dev1_TEST_RESOURCE_INSTANCE_PATH);

    expect_path(in, &dev1_TEST_RESOURCE_INSTANCE_PATH);
    pull_i64_value(in, 5);
    expect_path_retrieval_failure(in);

    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_too_long_3) {
    // {"dev1": {13: {26: {1: {2: {3: 4}}}}}}
    static const char DATA[] = { "\xA1\x64\x64\x65\x76\x31\xA1\x0D\xA1\x18\x1A"
                                 "\xA1\x01\xA1\x02\xA1\x03\x04" };

    TEST_ENV(DATA, dev1_TEST_RESOURCE_INSTANCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_mismatch_1) {
    // {[13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_mismatch_2) {
    // {["dev1", 13, 26, 1]: 42}
    static const char DATA[] = {
        "\xA1\x84\x64\x64\x65\x76\x31\x0D\x18\x1A\x01\x18\x2A"
    };
    TEST_ENV(DATA, TEST_RESOURCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_mismatch_3) {
    // {["dev2", 13, 26, 1]: 42}
    static const char DATA[] = {
        "\xA1\x84\x64\x64\x65\x76\x32\x0D\x18\x1A\x01\x18\x2A"
    };
    TEST_ENV(DATA, dev1_TEST_RESOURCE_PATH);
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_wrong_level_1) {
    // {[13, 26]: {"dev1": {1: 42}}}
    static const char DATA[] = {
        "\xA1\x82\x0D\x18\x1A\xA1\x64\x64\x65\x76\x31\xA1\x01\x18\x2A"
    };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_wrong_level_2) {
    // {[13, 26]: {[1, "dev1"]: 42}}
    static const char DATA[] = {
        "\xA1\x82\x0D\x18\x1A\xA1\x82\x01\x64\x64\x65\x76\x31\x18\x2A"
    };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_twice_in_array) {
    // {["dev1", 13, 26, "dev1"]: {1: 42}}
    static const char DATA[] = { "\xA1\x84\x64\x64\x65\x76\x31\x0D\x18\x1A\x64"
                                 "\x64\x65\x76\x31\xA1\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

// NOTE: if this ends up being configurable, come up with a way to encode
// the string dynamically (including its length, which may have variable
// size)
AVS_STATIC_ASSERT(ANJAY_GATEWAY_MAX_PREFIX_LEN == 9, prefix_len_is_9);

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_too_long_solo) {
    // {"123456789": {[13, 26, 1]: 42}}
    static const char DATA[] = { "\xA1\x69\x31\x32\x33\x34\x35\x36\x37\x38\x39"
                                 "\xA1\x83\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_too_long_array) {
    // {["123456789", 13, 26, 1]: 42}
    static const char DATA[] = { "\xA1\x84\x69\x31\x32\x33\x34\x35\x36\x37\x38"
                                 "\x39\x0D\x18\x1A\x01\x18\x2A" };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(lwm2m_cbor_in, gw_path_prefix_too_long_array_indefinite) {
    // {[(_ "12345678", "12345678")]: {[13, 26, 1]: 42}}
    static const char DATA[] = {
        "\xA1\x81\x7F\x68\x31\x32\x33\x34\x35\x36\x37\x38\x68\x31\x32\x33\x34"
        "\x35\x36\x37\x38\xFF\xA1\x83\x0D\x18\x1A\x01\x18\x2A"
    };
    TEST_ENV(DATA, MAKE_ROOT_PATH());
    expect_path_retrieval_failure(in);
    TEST_TEARDOWN(FAIL);
}

#endif // ANJAY_WITH_LWM2M_GATEWAY

#undef TEST_ENV
