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

#include "src/core/io/json/anjay_json_decoder.h"
#include "tests/utils/utils.h"

#define SCOPED_TEST_ENV(Data, Size)                                        \
    SCOPED_PTR(avs_stream_t, avs_stream_cleanup)                           \
    STREAM = avs_stream_membuf_create();                                   \
    ASSERT_NOT_NULL(STREAM);                                               \
    ASSERT_OK(avs_stream_write(STREAM, (Data), (Size)));                   \
    SCOPED_PTR(anjay_json_like_decoder_t, _anjay_json_like_decoder_delete) \
    DECODER = _anjay_json_decoder_new(STREAM);                             \
    ASSERT_NOT_NULL(DECODER);

#define TEST_NUMBER(Name, Value)                                     \
    AVS_UNIT_TEST(json_decoder, number_##Name) {                     \
        static const char data[] = #Value;                           \
        SCOPED_TEST_ENV(data, strlen(data));                         \
        anjay_json_like_number_t value;                              \
        ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value)); \
        ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);         \
        ASSERT_EQ(value.value.f64, (double) (Value));                \
        ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),           \
                  ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);           \
    }

TEST_NUMBER(zero, 0);
TEST_NUMBER(positive_integer, 1234);
TEST_NUMBER(negative_integer, -1234);
TEST_NUMBER(positive_fraction, 1.234);
TEST_NUMBER(negative_fraction, -1.234);
TEST_NUMBER(positive_integer_with_unsigned_lowercase_exponent, 1234e56);
TEST_NUMBER(positive_integer_with_unsigned_uppercase_exponent, 1234E56);
TEST_NUMBER(positive_integer_with_positive_lowercase_exponent, 1234e+56);
TEST_NUMBER(positive_integer_with_positive_uppercase_exponent, 1234E+56);
TEST_NUMBER(positive_integer_with_negative_lowercase_exponent, 1234e-56);
TEST_NUMBER(positive_integer_with_negative_uppercase_exponent, 1234E-56);
TEST_NUMBER(negative_integer_with_unsigned_lowercase_exponent, -1234e56);
TEST_NUMBER(negative_integer_with_unsigned_uppercase_exponent, -1234E56);
TEST_NUMBER(negative_integer_with_positive_lowercase_exponent, -1234e+56);
TEST_NUMBER(negative_integer_with_positive_uppercase_exponent, -1234E+56);
TEST_NUMBER(negative_integer_with_negative_lowercase_exponent, -1234e-56);
TEST_NUMBER(negative_integer_with_negative_uppercase_exponent, -1234E-56);
TEST_NUMBER(positive_fraction_with_unsigned_lowercase_exponent, 1.234e56);
TEST_NUMBER(positive_fraction_with_unsigned_uppercase_exponent, 1.234E56);
TEST_NUMBER(positive_fraction_with_positive_lowercase_exponent, 1.234e+56);
TEST_NUMBER(positive_fraction_with_positive_uppercase_exponent, 1.234E+56);
TEST_NUMBER(positive_fraction_with_negative_lowercase_exponent, 1.234e-56);
TEST_NUMBER(positive_fraction_with_negative_uppercase_exponent, 1.234E-56);
TEST_NUMBER(negative_fraction_with_unsigned_lowercase_exponent, -1.234e56);
TEST_NUMBER(negative_fraction_with_unsigned_uppercase_exponent, -1.234E56);
TEST_NUMBER(negative_fraction_with_positive_lowercase_exponent, -1.234e+56);
TEST_NUMBER(negative_fraction_with_positive_uppercase_exponent, -1.234E+56);
TEST_NUMBER(negative_fraction_with_negative_lowercase_exponent, -1.234e-56);
TEST_NUMBER(negative_fraction_with_negative_uppercase_exponent, -1.234E-56);
TEST_NUMBER(leading_zero_fraction, 0.123);
TEST_NUMBER(leading_zero_exponent, 0e123);

AVS_UNIT_TEST(json_decoder, number_too_long) {
    char data[2 * ANJAY_MAX_DOUBLE_STRING_SIZE];
    for (size_t i = 0; i < sizeof(data) - 1; ++i) {
        data[i] = '1';
    }
    data[sizeof(data) - 1] = '\0';
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_number_t value;
    ASSERT_FAIL(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, number_invalid_chars) {
    static const char data[] = "123false";
    SCOPED_TEST_ENV(data, strlen(data));

    // unexpected character will be treated as end-of-token, so the number
    // itself will parse just fine...
    anjay_json_like_number_t value;
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(value.value.f64, 123.0);

    // ...but the "next token" will be unexpected at the top level
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(
            DECODER, &(anjay_json_like_value_type_t) { 0 }));
}

AVS_UNIT_TEST(json_decoder, number_unparsable_value) {
    static const char data[] = "1e2e3";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_number(
            DECODER, &(anjay_json_like_number_t) { 0 }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, number_leading_dot) {
    static const char data[] = ".1";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_number(
            DECODER, &(anjay_json_like_number_t) { 0 }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, number_minus_leading_dot) {
    static const char data[] = "-.1";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_number(
            DECODER, &(anjay_json_like_number_t) { 0 }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

static const char *read_short_string(anjay_json_like_decoder_t *ctx) {
    static char short_string[128];
    avs_stream_outbuf_t stream = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(
            &stream, short_string, sizeof(short_string) - 1);
    int result = _anjay_json_like_decoder_bytes(ctx, (avs_stream_t *) &stream);
    if (result) {
        return NULL;
    }
    short_string[avs_stream_outbuf_offset(&stream)] = '\0';
    return short_string;
}

AVS_UNIT_TEST(json_decoder, string_simple) {
    static const char data[] = "\"Hello, world!\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_EQ_STR(read_short_string(DECODER), "Hello, world!");
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, string_unicode) {
    static const char data[] = "\"お前はもうライトウェイト\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_EQ_STR(read_short_string(DECODER), "お前はもうライトウェイト");
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, string_simple_escapes) {
    static const char data[] = "\"Ve\\ry use\\ful se\\t of \\\"\\bi\\ngo\\\" "
                               "characters \\\\o\\/\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_EQ_STR(read_short_string(DECODER),
                  "Ve\ry use\ful se\t of \"\bi\ngo\" characters \\o/");
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, string_unicode_escapes) {
    static const char data[] =
            "\"\\u304a\\u524D\\u306f\\u3082\\u3046\\u004c\\u0077\\u004D\\u0032"
            "\\u004d\\u0020\\u0028\\u00B0\\u0414\\u00b0\\u0029\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_EQ_STR(read_short_string(DECODER), "お前はもうLwM2M (°Д°)");
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, string_invalid_characters) {
    static const char data[] = "\"Hello,\nworld!\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_NULL(read_short_string(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, string_invalid_escape) {
    static const char data[] = "\"Hello, \\world!\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_NULL(read_short_string(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, string_too_short_unicode_escape) {
    static const char data[] = "\"Hello, world\\u21\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_NULL(read_short_string(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, string_invalid_unicode_escape) {
    static const char data[] = "\"Hello, world\\uGHIJ\"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_NULL(read_short_string(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, string_null_unicode_escape) {
    static const char data[] = "\"Hello, world\\u\0\0\0\0\"";
    SCOPED_TEST_ENV(data, sizeof(data) - 1);
    ASSERT_NULL(read_short_string(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, string_space_unicode_escape) {
    static const char data[] = "\"Hello, world\\u    \"";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_NULL(read_short_string(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, boolean_true) {
    static const char data[] = "true";
    SCOPED_TEST_ENV(data, strlen(data));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, true);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, boolean_false) {
    static const char data[] = "false";
    SCOPED_TEST_ENV(data, strlen(data));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, false);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, boolean_true_too_short) {
    static const char data[] = "tru";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &(bool) { false }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, boolean_false_too_short) {
    static const char data[] = "fals";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &(bool) { false }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, boolean_true_wrong) {
    static const char data[] = "tRue";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &(bool) { false }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, boolean_false_wrong) {
    static const char data[] = "falSe";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &(bool) { false }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, null) {
    static const char data[] = "null";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_NULL);
}

AVS_UNIT_TEST(json_decoder, only_one_value) {
    static const char data[] = "true false";
    SCOPED_TEST_ENV(data, strlen(data));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, true);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_bool(DECODER, &value));
}

AVS_UNIT_TEST(json_decoder, flat_array) {
    static const char data[] = "[1, 2, 3]";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(ANJAY_JSON_LIKE_VALUE_ARRAY, type);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

    anjay_json_like_number_t value;
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(value.value.f64, 1.0);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(value.value.f64, 2.0);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &value));
    ASSERT_EQ(value.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(value.value.f64, 3.0);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    ASSERT_FAIL(_anjay_json_like_decoder_number(DECODER, &value));
}

AVS_UNIT_TEST(json_decoder, empty_array) {
    static const char data[] = "[]";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(ANJAY_JSON_LIKE_VALUE_ARRAY, type);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);

    ASSERT_FAIL(_anjay_json_like_decoder_number(
            DECODER, &(anjay_json_like_number_t) { 0 }));
}

AVS_UNIT_TEST(json_decoder, flat_map) {
    static const char data[] = "{ \"Fun\": true, \"Stuff\": -2 }";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Fun", sizeof("Fun"));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, true);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Stuff", sizeof("Stuff"));
    anjay_json_like_number_t number;
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, -2);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, invalid_comma_in_map) {
    static const char data[] = "{ \"Fun\", \"Stuff\" }";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Fun", sizeof("Fun"));

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(json_decoder, invalid_colon_in_map) {
    static const char data[] = "{ \"Fun\": true: \"Stuff\" }";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_EQ_BYTES_SIZED(read_short_string(DECODER), "Fun", sizeof("Fun"));
    bool value;
    ASSERT_OK(_anjay_json_like_decoder_bool(DECODER, &value));
    ASSERT_EQ(value, true);

    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(json_decoder, empty_map) {
    static const char data[] = "{}";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

static void maps_in_array_test_impl(anjay_json_like_decoder_t *decoder) {
    anjay_json_like_value_type_t type;
    anjay_json_like_number_t number;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(decoder, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(decoder));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 1);

    ASSERT_OK(_anjay_json_like_decoder_current_value_type(decoder, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_OK(_anjay_json_like_decoder_enter_map(decoder));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 1);

    ASSERT_OK(_anjay_json_like_decoder_current_value_type(decoder, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_OK(_anjay_json_like_decoder_enter_map(decoder));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 2);
    ASSERT_EQ_STR(read_short_string(decoder), "1");

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(decoder, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 2);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 1);
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(decoder, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_OK(_anjay_json_like_decoder_enter_map(decoder));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 2);
    ASSERT_EQ_STR(read_short_string(decoder), "3");

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(decoder, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 4);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 2);
    ASSERT_EQ_STR(read_short_string(decoder), "5");

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(decoder, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 6);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(decoder), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(decoder),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, maps_in_array) {
    static const char data[] = "[ {}, { \"1\": 2 }, { \"3\": 4, \"5\": 6 } ]";
    SCOPED_TEST_ENV(data, strlen(data));
    maps_in_array_test_impl(DECODER);
}

AVS_UNIT_TEST(json_decoder, maps_in_array_no_whitespace) {
    static const char data[] = "[{},{\"1\":2},{\"3\":4,\"5\":6}]";
    SCOPED_TEST_ENV(data, strlen(data));
    maps_in_array_test_impl(DECODER);
}

AVS_UNIT_TEST(json_decoder, maps_in_array_all_possible_whitespace) {
    static const char data[] =
            " [\r{\n}\t, \r{\n\t\"1\" \r\n:\t \r2\n\t }\r\n\t, \r\n\t{ \r\n\t "
            "\"3\"\r\n\t \r:\n\t \r\n4\t \r\n\t, \r\n\t \r\"5\"\n\t \r\n\t: "
            "\r\n\t \r\n6\t \r\n\t \r}\n\t \r\n\t] \r\n\t \r\n\t";
    SCOPED_TEST_ENV(data, strlen(data));
    maps_in_array_test_impl(DECODER);
}

AVS_UNIT_TEST(json_decoder, arrays_in_map) {
    static const char data[] = "{ \"0\": [], \"1\": [2], \"3\": [4, 5, 6] }";
    SCOPED_TEST_ENV(data, strlen(data));

    anjay_json_like_value_type_t type;
    anjay_json_like_number_t number;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_MAP);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);

    ASSERT_EQ_STR(read_short_string(DECODER), "0");
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);

    ASSERT_EQ_STR(read_short_string(DECODER), "1");
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 2);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 1);
    ASSERT_EQ_STR(read_short_string(DECODER), "3");
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);

    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 4);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 5);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 2);
    ASSERT_OK(_anjay_json_like_decoder_number(DECODER, &number));
    ASSERT_EQ(number.type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_EQ(number.value.f64, 6);

    ASSERT_EQ(_anjay_json_like_decoder_nesting_level(DECODER), 0);
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

AVS_UNIT_TEST(json_decoder, nesting_too_deep) {
    static const char data[] = "[ [ [ 42 ] ] ]";
    SCOPED_TEST_ENV(data, strlen(data));

    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_FAIL(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
}

AVS_UNIT_TEST(json_decoder, non_string_map_keys) {
    static const char data[] = "{ 1: 2 }";
    SCOPED_TEST_ENV(data, strlen(data));

    ASSERT_OK(_anjay_json_like_decoder_enter_map(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(
            DECODER, &(anjay_json_like_value_type_t) { 0 }));
}

AVS_UNIT_TEST(json_decoder, no_input) {
    static const char data[] = "";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(
            DECODER, &(anjay_json_like_value_type_t) { 0 }));
}

AVS_UNIT_TEST(json_decoder, invalid_input) {
    static const char data[] = "manure";
    SCOPED_TEST_ENV(data, strlen(data));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(
            DECODER, &(anjay_json_like_value_type_t) { 0 }));
}

AVS_UNIT_TEST(json_decoder, nested_unexpected_eof) {
    static const char data[] = "[";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(json_decoder, nested_invalid_entry) {
    static const char data[] = "[manure]";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(json_decoder, nested_next_unexpected_eof) {
    static const char data[] = "[42, ";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_OK(_anjay_json_like_decoder_number(
            DECODER, &(anjay_json_like_number_t) { 0 }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(json_decoder, nested_next_invalid_entry) {
    static const char data[] = "[42, manure]";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_DOUBLE);
    ASSERT_OK(_anjay_json_like_decoder_number(
            DECODER, &(anjay_json_like_number_t) { 0 }));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}

AVS_UNIT_TEST(json_decoder, invalid_whitespace) {
    static const char data[] = "[\v42\f]";
    SCOPED_TEST_ENV(data, strlen(data));
    anjay_json_like_value_type_t type;
    ASSERT_OK(_anjay_json_like_decoder_current_value_type(DECODER, &type));
    ASSERT_EQ(type, ANJAY_JSON_LIKE_VALUE_ARRAY);
    ASSERT_OK(_anjay_json_like_decoder_enter_array(DECODER));
    ASSERT_EQ(_anjay_json_like_decoder_state(DECODER),
              ANJAY_JSON_LIKE_DECODER_STATE_ERROR);
    ASSERT_FAIL(_anjay_json_like_decoder_current_value_type(DECODER, &type));
}
