/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_unit_test.h>

static const anjay_uri_path_t TEST_RESOURCE_PATH =
        RESOURCE_PATH_INITIALIZER(12, 34, 56);

#define TEST_ENV(Data, Path)                                         \
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER; \
    avs_stream_inbuf_set_buffer(&stream, Data, sizeof(Data) - 1);    \
    anjay_unlocked_input_ctx_t *in;                                  \
    ASSERT_OK(_anjay_input_cbor_create(&in, (avs_stream_t *) &stream, &(Path)));

#define TEST_TEARDOWN                             \
    do {                                          \
        ASSERT_OK(_anjay_input_ctx_destroy(&in)); \
    } while (0)

AVS_UNIT_TEST(raw_cbor_in, single_integer) {
    static const char RESOURCE[] = {
        "\x18\x2A" // unsigned(42)
    };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    int32_t value;
    ASSERT_OK(_anjay_get_i32_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, single_decimal_fraction) {
    static const char RESOURCE[] = {
        "\xC4"     // tag(4)
        "\x82"     // array(2)
        "\x20"     // negative(0)
        "\x18\x2D" // unsigned(45)
    };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    double value;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    ASSERT_EQ(value, 4.5);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, too_short_buffer_for_string) {
    static const char RESOURCE[] = {
        "\x6C#ZostanWDomu" // text(12)
    };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char too_short_buffer[8] = "SOMEDATA";
    ASSERT_EQ(_anjay_get_string_unlocked(
                      in, too_short_buffer, sizeof(too_short_buffer)),
              ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ_STR(too_short_buffer, "#Zostan");
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_FALSE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_OK);
    ASSERT_OK(_anjay_input_get_path(in, NULL, NULL));

    ASSERT_OK(_anjay_get_string_unlocked(
            in, too_short_buffer, sizeof(too_short_buffer)));
    ASSERT_EQ_STR(too_short_buffer, "WDomu");
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, empty_string) {
    static const char RESOURCE[] = {
        "\x60" // text(0)
    };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char buffer[8];
    ASSERT_OK(_anjay_get_string_unlocked(in, buffer, sizeof(buffer)));
    ASSERT_EQ_STR(buffer, "");
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}

#define CHUNK1 "test"
#define CHUNK2 "string"
#define TEST_STRING (CHUNK1 CHUNK2)

static void test_string_indefinite_impl(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char buffer[sizeof(TEST_STRING)];
    ASSERT_OK(_anjay_get_string_unlocked(in, buffer, sizeof(buffer)));
    ASSERT_EQ_STR(buffer, TEST_STRING);
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite) {
    // (_ "test", "string")
    static const char RESOURCE[] = { "\x7F\x64" CHUNK1 "\x66" CHUNK2 "\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_string_indefinite_impl(in);
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_with_empty_strings) {
    // (_ "", "test", "", "string", "")
    static const char RESOURCE[] = { "\x7F\x60\x64" CHUNK1 "\x60\x66" CHUNK2
                                     "\x60\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_string_indefinite_impl(in);
    TEST_TEARDOWN;
}

static void test_string_indefinite_empty_impl(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char buffer[1];
    ASSERT_OK(_anjay_get_string_unlocked(in, buffer, sizeof(buffer)));
    ASSERT_EQ(buffer[0], '\0');
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_empty_string) {
    // (_ "")
    static const char RESOURCE[] = { "\x7F\x60\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_string_indefinite_empty_impl(in);
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_empty_struct) {
    // (_ )
    static const char RESOURCE[] = { "\x7F\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_string_indefinite_empty_impl(in);
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_small_gets) {
    // (_ "test", "string")
    static const char RESOURCE[] = { "\x7F\x64" CHUNK1 "\x66" CHUNK2 "\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char buffer[sizeof(TEST_STRING)];
    memset(buffer, 0, sizeof(buffer));

    int result;

    do {
        size_t curr_length = strlen(buffer);
        result = _anjay_get_string_unlocked(
                in,
                buffer + curr_length,
                AVS_MIN(sizeof(buffer) - curr_length, 3));
    } while (result == ANJAY_BUFFER_TOO_SHORT);

    ASSERT_OK(result);
    ASSERT_EQ_STR(buffer, TEST_STRING);
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}

#undef TEST_STRING
#undef CHUNK1
#undef CHUNK2

#define CHUNK1 "\x00\x11\x22\x33\x44\x55"
#define CHUNK2 "\x66\x77\x88\x99"
#define TEST_BYTES (CHUNK1 CHUNK2)

static void test_bytes_indefinite_impl(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char buffer[sizeof(TEST_BYTES)];
    memset(buffer, 0, sizeof(buffer));
    size_t bytes_read = 0;
    bool message_finished = false;

    ASSERT_OK(_anjay_get_bytes_unlocked(
            in, &bytes_read, &message_finished, buffer, sizeof(buffer)));
    ASSERT_EQ(bytes_read, sizeof(TEST_BYTES) - 1);
    ASSERT_TRUE(message_finished);
    ASSERT_EQ_BYTES_SIZED(buffer, TEST_BYTES, sizeof(buffer));

    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
}

AVS_UNIT_TEST(raw_cbor_in, bytes_indefinite) {
    // (_ h'001122334455', h'66778899')
    static const char RESOURCE[] = { "\x5F\x46" CHUNK1 "\x44" CHUNK2 "\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_bytes_indefinite_impl(in);
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, bytes_indefinite_with_empty_strings) {
    // (_ h'', h'001122334455', h'', h'66778899', h'')
    static const char RESOURCE[] = { "\x5F\x40\x46" CHUNK1 "\x40\x44" CHUNK2
                                     "\x40\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_bytes_indefinite_impl(in);
    TEST_TEARDOWN;
}

AVS_UNIT_TEST(raw_cbor_in, bytes_indefinite_small_gets) {
    // (_ h'001122334455', h'66778899')
    static const char RESOURCE[] = { "\x5F\x46" CHUNK1 "\x44" CHUNK2 "\xFF" };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    char buffer[sizeof(TEST_BYTES) - 1];
    memset(buffer, 0, sizeof(buffer));

    bool message_finished = false;
    size_t total_bytes_read = 0;
    do {
        size_t bytes_read = 0;
        ASSERT_OK(_anjay_get_bytes_unlocked(
                in,
                &bytes_read,
                &message_finished,
                buffer + total_bytes_read,
                AVS_MIN(sizeof(buffer) - total_bytes_read, 3)));
        total_bytes_read += bytes_read;
    } while (!message_finished);

    ASSERT_EQ_BYTES_SIZED(buffer, TEST_BYTES, sizeof(buffer));
    cbor_in_t *cbor_input_ctx = (cbor_in_t *) in;
    ASSERT_TRUE(cbor_input_ctx->msg_finished);
    ASSERT_EQ(_anjay_json_like_decoder_state(cbor_input_ctx->cbor_decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}

#undef TEST_BYTES
#undef CHUNK1
#undef CHUNK2
