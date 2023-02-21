/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_stream_outbuf.h>
#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include "src/core/io/cbor/anjay_json_like_cbor_decoder.h"
#include "tests/utils/utils.h"

#define SCOPED_TEST_ENV(Data, Size)                                            \
    SCOPED_PTR(avs_stream_t, avs_stream_cleanup)                               \
    STREAM = avs_stream_membuf_create();                                       \
    ASSERT_NOT_NULL(STREAM);                                                   \
    ASSERT_OK(avs_stream_write(STREAM, (Data), (Size)));                       \
    SCOPED_PTR(anjay_json_like_decoder_t, _anjay_json_like_decoder_delete)     \
    DECODER = _anjay_cbor_decoder_new(STREAM, MAX_SENML_CBOR_NEST_STACK_SIZE); \
    ASSERT_NOT_NULL(DECODER);

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

#define TEST_DATA_INITIALIZER(Data) \
    {                               \
        .data = Data,               \
        .size = sizeof(Data) - 1    \
    }

/**
 * Convenience macro used with test_decode_uint(), e.g.:
 * > test_decode_uint(MAKE_TEST_DATA("\x00"), 0);
 */
#define MAKE_TEST_DATA(Data) ((test_data_t) TEST_DATA_INITIALIZER(Data))

AVS_UNIT_TEST(cbor_decoder, tags_are_ignored) {
    static const test_data_t inputs[] = {
        // tag with 1 byte extended length, with one byte of follow up
        TEST_DATA_INITIALIZER("\xD8\x01\x0F"),
        // tag with 2 bytes extended length, with one byte of follow up
        TEST_DATA_INITIALIZER("\xD9\x01\x02\x0F"),
        // tag with 4 bytes extended length, with one byte of follow up
        TEST_DATA_INITIALIZER("\xDA\x01\x02\x03\x04\x0F"),
        // tag with 8 bytes extended length, with one byte of follow up
        TEST_DATA_INITIALIZER("\xDB\x01\x02\x03\x04\x05\x06\x07\x08\x0F"),
    };
    for (size_t i = 0; i < AVS_ARRAY_SIZE(inputs); ++i) {
        SCOPED_TEST_ENV(inputs[i].data, inputs[i].size);
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_OK);
    }
}

AVS_UNIT_TEST(cbor_decoder, tags_without_following_bytes_are_invalid) {
    static const char data[] = "\xC0";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(cbor_decoder,
              tag_followed_by_tag_without_following_bytes_are_invalid) {
    static const char data[] = "\xC0\xC0";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

static const uint64_t DECODE_UINT_FAILURE = UINT64_MAX;

static int test_decode_uint(test_data_t test_data, uint64_t expected_value) {
    SCOPED_TEST_ENV(test_data.data, test_data.size);
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(DECODER, &type)
            || type != ANJAY_JSON_LIKE_VALUE_UINT) {
        return -1;
    }

    anjay_json_like_number_t decoded_number;
    if (_anjay_json_like_decoder_number(DECODER, &decoded_number)) {
        return -1;
    }
    ASSERT_EQ(decoded_number.type, ANJAY_JSON_LIKE_VALUE_UINT);
    if (expected_value == DECODE_UINT_FAILURE) {
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    } else {
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
        ASSERT_EQ(decoded_number.value.u64, expected_value);
    }
    return 0;
}

AVS_UNIT_TEST(cbor_decoder, uint_small) {
    for (unsigned small_value = 0; small_value < 24; ++small_value) {
        const uint8_t data =
                (uint8_t) ((CBOR_MAJOR_TYPE_UINT << 5) | small_value);
        ASSERT_OK(test_decode_uint((test_data_t) { &data, sizeof(data) },
                                   small_value));
    }
}

AVS_UNIT_TEST(cbor_decoder, uint_extended_length_of_1_byte) {
    ASSERT_OK(test_decode_uint(MAKE_TEST_DATA("\x18\xFF"), 0xFF));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x18"), DECODE_UINT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, uint_extended_length_of_2_byte) {
    ASSERT_OK(test_decode_uint(MAKE_TEST_DATA("\x19\xAA\xBB"), 0xAABB));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x19"), DECODE_UINT_FAILURE));
    ASSERT_FAIL(
            test_decode_uint(MAKE_TEST_DATA("\x19\xAA"), DECODE_UINT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, uint_extended_length_of_4_byte) {
    ASSERT_OK(test_decode_uint(MAKE_TEST_DATA("\x1A\xAA\xBB\xCC\xDD"),
                               0xAABBCCDD));
    ASSERT_FAIL(
            test_decode_uint(MAKE_TEST_DATA("\x1A\xAA"), DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1A\xAA\xBB"),
                                 DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1A\xAA\xBB\xCC"),
                                 DECODE_UINT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, uint_extended_length_of_8_byte) {
    ASSERT_OK(test_decode_uint(MAKE_TEST_DATA(
                                       "\x1B\xAA\xBB\xCC\xDD\x00\x11\x22\x33"),
                               0xAABBCCDD00112233ULL));
    ASSERT_FAIL(
            test_decode_uint(MAKE_TEST_DATA("\x1B\xAA\xBB\xCC\xDD\x00\x11\x22"),
                             DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1B\xAA\xBB\xCC\xDD\x00\x11"),
                                 DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1B\xAA\xBB\xCC\xDD\x00"),
                                 DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1B\xAA\xBB\xCC\xDD"),
                                 DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1B\xAA\xBB\xCC"),
                                 DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1B\xAA\xBB"),
                                 DECODE_UINT_FAILURE));
    ASSERT_FAIL(
            test_decode_uint(MAKE_TEST_DATA("\x1B\xAA"), DECODE_UINT_FAILURE));
    ASSERT_FAIL(test_decode_uint(MAKE_TEST_DATA("\x1B"), DECODE_UINT_FAILURE));
}

static const int64_t DECODE_NEGATIVE_INT_FAILURE = INT64_MAX;

static int test_decode_negative_int(test_data_t test_data,
                                    int64_t expected_value) {
    SCOPED_TEST_ENV(test_data.data, test_data.size);
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(DECODER, &type)
            || type != ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT) {
        return -1;
    }

    anjay_json_like_number_t decoded_number;
    if (_anjay_json_like_decoder_number(DECODER, &decoded_number)) {
        return -1;
    }
    ASSERT_EQ(ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT, decoded_number.type);
    if (expected_value == DECODE_NEGATIVE_INT_FAILURE) {
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    } else {
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
        ASSERT_EQ(decoded_number.value.i64, expected_value);
    }
    return 0;
}

AVS_UNIT_TEST(cbor_decoder, neg_int_small) {
    for (int small_value = 0; small_value < 24; ++small_value) {
        const uint8_t data =
                (uint8_t) ((CBOR_MAJOR_TYPE_NEGATIVE_INT << 5) | small_value);
        ASSERT_OK(test_decode_negative_int(
                (test_data_t) { &data, sizeof(data) }, -small_value - 1));
    }
    const uint8_t data = (uint8_t) ((CBOR_MAJOR_TYPE_NEGATIVE_INT << 5) | 24);
    ASSERT_FAIL(test_decode_negative_int((test_data_t) { &data, sizeof(data) },
                                         DECODE_NEGATIVE_INT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, neg_int_extended_length_of_1_byte) {
    ASSERT_OK(test_decode_negative_int(MAKE_TEST_DATA("\x38\xFF"), -256));
    ASSERT_FAIL(test_decode_negative_int(MAKE_TEST_DATA("\x38"),
                                         DECODE_NEGATIVE_INT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, neg_int_extended_length_of_2_byte) {
    ASSERT_OK(test_decode_negative_int(MAKE_TEST_DATA("\x39\x00\x01"), -2));
    ASSERT_FAIL(test_decode_negative_int(MAKE_TEST_DATA("\x39\x00"),
                                         DECODE_NEGATIVE_INT_FAILURE));
    ASSERT_FAIL(test_decode_negative_int(MAKE_TEST_DATA("\x39"),
                                         DECODE_NEGATIVE_INT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, neg_int_boundary) {
    ASSERT_OK(test_decode_negative_int(
            MAKE_TEST_DATA("\x3B\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF"), INT64_MIN));
    // Overflow.
    ASSERT_FAIL(test_decode_negative_int(
            MAKE_TEST_DATA("\x3B\x80\x00\x00\x00\x00\x00\x00\x00"),
            DECODE_NEGATIVE_INT_FAILURE));
}

AVS_UNIT_TEST(cbor_decoder, bytes_short) {
    // - 1st byte: code,
    // - rest: maximum 23 bytes of payload.
    uint8_t input_bytes[1 + 23];
    uint8_t output_bytes[sizeof(input_bytes) - 1];

    for (size_t short_len = 0; short_len < 24; ++short_len) {
        input_bytes[0] =
                (uint8_t) ((CBOR_MAJOR_TYPE_BYTE_STRING << 5) | short_len);
        for (size_t i = 0; i < short_len; ++i) {
            input_bytes[i + 1] = (uint8_t) rand();
        }
        SCOPED_TEST_ENV(input_bytes, short_len + 1);
        avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
        avs_stream_outbuf_set_buffer(
                &stream, output_bytes, sizeof(output_bytes));
        // consume the bytes
        ASSERT_OK(_anjay_json_like_decoder_bytes(DECODER,
                                                 (avs_stream_t *) &stream));
        ASSERT_EQ(avs_stream_outbuf_offset(&stream), short_len);
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
        ASSERT_EQ_BYTES_SIZED(output_bytes, &input_bytes[1], short_len);
    }
}

AVS_UNIT_TEST(cbor_decoder, bytes_indefinite) {
    // (_ h'AABBCCDD', h'EEFF99')
    uint8_t input_bytes[] = { 0x5F, 0x44, 0xAA, 0xBB, 0xCC, 0xDD,
                              0x43, 0xEE, 0xFF, 0x99, 0xFF };
#define TEST_BYTES "\xAA\xBB\xCC\xDD\xEE\xFF\x99"
    uint8_t output_bytes[sizeof(TEST_BYTES) - 1];

    SCOPED_TEST_ENV(input_bytes, sizeof(input_bytes));
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, output_bytes, sizeof(output_bytes));
    // consume the bytes
    ASSERT_OK(
            _anjay_json_like_decoder_bytes(DECODER, (avs_stream_t *) &stream));
    ASSERT_EQ(avs_stream_outbuf_offset(&stream), sizeof(output_bytes));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    ASSERT_EQ_BYTES_SIZED(output_bytes, TEST_BYTES, sizeof(output_bytes));
#undef TEST_BYTES
}

AVS_UNIT_TEST(cbor_decoder, bytes_indefinite_empty) {
    // (_ )
    uint8_t input_bytes[] = { 0x5F, 0xFF };
    uint8_t output_bytes[1];

    SCOPED_TEST_ENV(input_bytes, sizeof(input_bytes));
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, output_bytes, sizeof(output_bytes));
    // consume the bytes
    ASSERT_OK(
            _anjay_json_like_decoder_bytes(DECODER, (avs_stream_t *) &stream));
    ASSERT_EQ(avs_stream_outbuf_offset(&stream), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(cbor_decoder, bytes_indefinite_invalid_integer_inside) {
    // (_ 21 )
    uint8_t input_bytes[] = { 0x5F, 0x15, 0xFF };
    uint8_t output_bytes[1];

    SCOPED_TEST_ENV(input_bytes, sizeof(input_bytes));
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, output_bytes, sizeof(output_bytes));
    ASSERT_FAIL(
            _anjay_json_like_decoder_bytes(DECODER, (avs_stream_t *) &stream));
    ASSERT_EQ(avs_stream_outbuf_offset(&stream), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(cbor_decoder, bytes_indefinite_invalid_map_inside) {
    // (_ {2: 5} )
    uint8_t input_bytes[] = { 0x5F, 0xA1, 0x02, 0x05, 0xFF };
    uint8_t output_bytes[1];

    SCOPED_TEST_ENV(input_bytes, sizeof(input_bytes));
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, output_bytes, sizeof(output_bytes));
    ASSERT_FAIL(
            _anjay_json_like_decoder_bytes(DECODER, (avs_stream_t *) &stream));
    ASSERT_EQ(avs_stream_outbuf_offset(&stream), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(cbor_decoder, bytes_indefinite_invalid_bytes_and_map_inside) {
    // (_ h'001122', {2: 5} )
    uint8_t input_bytes[] = { 0x5F, 0x43, 0x00, 0x11, 0x22,
                              0xA1, 0x02, 0x05, 0xFF };
    uint8_t output_bytes[4];

    SCOPED_TEST_ENV(input_bytes, sizeof(input_bytes));
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&stream, output_bytes, sizeof(output_bytes));
    ASSERT_FAIL(
            _anjay_json_like_decoder_bytes(DECODER, (avs_stream_t *) &stream));
    ASSERT_EQ(avs_stream_outbuf_offset(&stream), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(cbor_decoder, bytes_long) {
    // - 1st byte: code,
    // - 2nd byte: extended length high byte,
    // - 3rd byte: extended length low byte,
    // - rest: 256 bytes of payload.
    enum { PAYLOAD_LEN = 256 };
    uint8_t input_bytes[3 + PAYLOAD_LEN];

    input_bytes[0] = 0x59; // major-type=bytes, extended-length=2bytes
    input_bytes[1] = PAYLOAD_LEN >> 8;
    input_bytes[2] = PAYLOAD_LEN & 0xFF;
    for (size_t i = 3; i < sizeof(input_bytes); ++i) {
        input_bytes[i] = (uint8_t) rand();
    }

    SCOPED_TEST_ENV(input_bytes, sizeof(input_bytes));

    avs_stream_t *membuf = avs_stream_membuf_create();
    ASSERT_NOT_NULL(membuf);
    ASSERT_OK(_anjay_json_like_decoder_bytes(DECODER, membuf));
    void *output_bytes = NULL;
    size_t buffer_size;
    ASSERT_OK(avs_stream_membuf_take_ownership(
            membuf, &output_bytes, &buffer_size));
    avs_stream_cleanup(&membuf);
    ASSERT_NOT_NULL(output_bytes);
    ASSERT_EQ(buffer_size, PAYLOAD_LEN);
    ASSERT_EQ_BYTES_SIZED(output_bytes, &input_bytes[3], PAYLOAD_LEN);
    avs_free(output_bytes);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(cbor_decoder, flat_array) {
    // array [1u, 2u, 3u]
    static const char data[] = "\x83\x01\x02\x03";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(ANJAY_JSON_LIKE_VALUE_ARRAY, type);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

    anjay_json_like_number_t value;
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
    ASSERT_EQ(value.value.u64, 1);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
    ASSERT_EQ(value.value.u64, 2);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
    ASSERT_EQ(value.value.u64, 3);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    ASSERT_FAIL(_anjay_json_like_decoder_number(DECODER, &value));
}

AVS_UNIT_TEST(cbor_decoder, flat_empty_array) {
    SCOPED_TEST_ENV("\x80", 1);
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(cbor_decoder, flat_empty_array_with_uint_afterwards) {
    SCOPED_TEST_ENV("\x80\x01", 2);
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);

    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_UINT);

    anjay_json_like_number_t value;
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
    ASSERT_EQ(value.value.u64, 1);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(cbor_decoder, nested_array) {
    // array [[1u,2u,3u], 4]
    {
        static const char data_array_first[] = "\x82\x83\x01\x02\x03\x04";
        SCOPED_TEST_ENV(data_array_first, sizeof(data_array_first) - 1);
        anjay_json_like_value_type_t type;
        ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
        ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
        ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
        ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

        anjay_json_like_number_t value;
        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 1);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 2);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 3);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 4);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    }

    {
        // array [1u, [2u, 3u, 4u]]
        static const char data_array_last[] = "\x82\x01\x83\x02\x03\x04";
        SCOPED_TEST_ENV(data_array_last, sizeof(data_array_last) - 1);
        anjay_json_like_value_type_t type;
        ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
        ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
        ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
        anjay_json_like_number_t value;
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 1);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
        ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 2);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 3);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
        ASSERT_EQ(value.value.u64, 4);

        ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    }
}

AVS_UNIT_TEST(cbor_decoder, array_too_many_nest_levels) {
    // array [[[[[]]]]]
    static const char data[] = "\x81\x81\x81\x81\x80";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 3);
    ASSERT_FAIL(_anjay_json_like_decoder_enter_array(DECODER));

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(cbor_decoder, flat_map) {
    // map { 42: 300 }
    static const char data[] = "\xA1\x18\x2A\x19\x01\x2C";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);

    anjay_json_like_number_t key;
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &key));
    ASSERT_EQ(key.type, ANJAY_JSON_LIKE_VALUE_UINT);
    ASSERT_EQ(key.value.u64, 42);

    anjay_json_like_number_t value;
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_UINT);
    ASSERT_EQ(value.value.u64, 300);
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(cbor_decoder, empty_map) {
    static const char data[] = "\xA0";
    SCOPED_TEST_ENV(data, 1);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    // We enter the map, and then we immediately exit it, because it is empty.
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

#define TEST_HALF(Name, Value, Expected)                             \
    AVS_UNIT_TEST(cbor_decoder, Name) {                              \
        static const char data[] = "\xF9" Value;                     \
        SCOPED_TEST_ENV(data, sizeof(data) - 1);                     \
        anjay_json_like_number_t value;                              \
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value)); \
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_FLOAT);          \
        ASSERT_EQ(value.value.f32, Expected);                        \
    }

TEST_HALF(half_float_value, "\x50\x00", 32.0f);
TEST_HALF(half_float_nan, "\x7E\x00", NAN);
TEST_HALF(half_float_inf, "\x7C\x00", INFINITY);

AVS_UNIT_TEST(cbor_decoder, boolean_true_and_false) {
    {
        static const char data[] = "\xF5";
        SCOPED_TEST_ENV(data, 1);
        bool value;
        ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
        ASSERT_EQ(value, true);
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    }
    {
        static const char data[] = "\xF4";
        SCOPED_TEST_ENV(data, 1);
        bool value;
        ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
        ASSERT_EQ(value, false);
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
    }
}

AVS_UNIT_TEST(cbor_decoder, boolean_integers_are_not_real_booleans) {
    {
        static const char data[] = "\x00";
        SCOPED_TEST_ENV(data, 1);
        bool value;
        ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &value));
    }

    {
        static const char data[] = "\x01";
        SCOPED_TEST_ENV(data, 1);
        bool value;
        ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &value));
    }
}

static uint8_t make_header(cbor_major_type_t major_type, uint8_t value) {
    return (uint8_t) ((((uint8_t) major_type) << 5) | value);
}

static void encode_int(char **out_buffer, int64_t value) {
    const uint64_t encoded =
            avs_convert_be64((uint64_t) (value < 0 ? -(value + 1) : value));
    const uint8_t major_type =
            value < 0 ? CBOR_MAJOR_TYPE_NEGATIVE_INT : CBOR_MAJOR_TYPE_UINT;
    const uint8_t header = make_header(major_type, CBOR_EXT_LENGTH_8BYTE);

    memcpy(*out_buffer, &header, 1);
    *out_buffer += 1;
    memcpy(*out_buffer, &encoded, sizeof(encoded));
    *out_buffer += sizeof(encoded);
}

#define TEST_TYPICAL_DECIMAL_FRACTION(Name, Exponent, Mantissa)               \
    AVS_UNIT_TEST(cbor_decoder, typical_decimal_##Name) {                     \
        /* Tag(4), Array [ Exponent, Mantissa ] */                            \
        char data[2 + 2 * (sizeof(uint8_t) + sizeof(uint64_t))] = "\xC4\x82"; \
        char *integers = &data[2];                                            \
        encode_int(&integers, (Exponent));                                    \
        encode_int(&integers, (Mantissa));                                    \
        SCOPED_TEST_ENV(data, sizeof(data));                                  \
        anjay_json_like_number_t value;                                       \
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));          \
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);                  \
        ASSERT_EQ(value.value.f64, (Mantissa) *pow(10.0, (Exponent)));        \
    }

TEST_TYPICAL_DECIMAL_FRACTION(small, 2, 3);
TEST_TYPICAL_DECIMAL_FRACTION(small_negative_mantissa, 2, -3);
TEST_TYPICAL_DECIMAL_FRACTION(small_negative_exponent, -2, 3);
TEST_TYPICAL_DECIMAL_FRACTION(small_negative_exponent_and_mantissa, -2, -3);
TEST_TYPICAL_DECIMAL_FRACTION(big_exponent, 100, 2);
TEST_TYPICAL_DECIMAL_FRACTION(big_negative_exponent, -100, 2);
TEST_TYPICAL_DECIMAL_FRACTION(big_negative_exponent_and_mantissa, -100, -2);

AVS_UNIT_TEST(cbor_decoder, decimal_fraction_tag_after_tag) {
    static const char data[] = "\xC4\xC4\x82\x02\x03";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    anjay_json_like_number_t value;
    ASSERT_FAIL(_anjay_json_like_decoder_number(DECODER, &value));
}

AVS_UNIT_TEST(cbor_decoder, decimal_fraction_tag_but_no_data) {
    static const char data[] = "\xC4";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    anjay_json_like_value_type_t value_type;
    ASSERT_OK(
            _anjay_json_like_decoder_current_value_type(DECODER, &value_type));
    ASSERT_EQ(value_type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    anjay_json_like_number_t value;
    ASSERT_FAIL(_anjay_json_like_decoder_number(DECODER, &value));
}

static const char *read_short_string(anjay_json_like_decoder_t *ctx) {
    static char short_string[128];
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(
            &stream, short_string, sizeof(short_string) - 1);
    ASSERT_OK(_anjay_json_like_decoder_bytes(ctx, (avs_stream_t *) &stream));
    short_string[avs_stream_outbuf_offset(&stream)] = '\0';
    return short_string;
}

AVS_UNIT_TEST(cbor_decoder, indefinite_map) {
    // indefinite_map {
    //      "Fun": true,
    //      "Stuff": -2,
    // }
    static const char data[] = "\xBF\x63"
                               "Fun"
                               "\xF5\x65"
                               "Stuff"
                               "\x21\xFF";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Fun", sizeof("Fun"));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, true);

    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Stuff", sizeof("Stuff"));
    anjay_json_like_number_t number;
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT);
    ASSERT_EQ(number.value.i64, -2);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(cbor_decoder, indefinite_map_with_odd_number_of_items) {
    // indefinite_map {
    //      "Fun": true,
    //      "Stuff":
    // }
    static const char data[] = "\xBF\x63"
                               "Fun"
                               "\xF5\x65"
                               "Stuff"
                               "\xFF";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Fun", sizeof("Fun"));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, true);

    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Stuff", sizeof("Stuff"));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(cbor_decoder, indefinite_map_that_is_empty) {
    // indefinite_map {}
    static const char data[] = "\xBF\xFF";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
}
