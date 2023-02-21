/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_vector.h>

#include "senml_in_common.h"

#define TEST_ENV(Data, Path)                                         \
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER; \
    avs_stream_inbuf_set_buffer(&stream, Data, sizeof(Data) - 1);    \
    anjay_unlocked_input_ctx_t *in;                                  \
    ASSERT_OK(_anjay_input_senml_cbor_create(                        \
            &in, (avs_stream_t *) &stream, &(Path)));

AVS_UNIT_TEST(cbor_in_resource, single_instance) {
    static const char RESOURCE[] = {
        "\x81"         // array(1)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
    };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_single_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_resource_permuted, single_instance) {
    static const char RESOURCE[] = {
        "\x81"         // array(1)
        "\xA2"         // map(2)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
    };
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_single_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_resource, single_instance_but_more_than_one) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
    };
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    test_single_instance_but_more_than_one(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_resource,
              single_instance_but_more_than_one_without_last_get_path) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
    };
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    // Context is restirected to /13/26/1, but it has more data to obtain,
    // which means the request is broken.
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in_resource, single_instance_with_first_resource_unrelated) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
    };
    // NOTE: Request is on /13/26/1 but the first resource in the payload is
    // /13/26/2.
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);

    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_ERR_BAD_REQUEST);

    // Basically nothing was extracted from the context, because it was broken
    // from the very beginning.
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in_resource_permuted, single_instance_but_more_than_one) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
                       // ,
        "\xA2"         // map(2)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
    };
    TEST_ENV(RESOURCES, MAKE_RESOURCE_PATH(13, 26, 1));
    test_single_instance_but_more_than_one(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_resource, multiple_instance) {
    static const char RESOURCES[] = {
        "\x82"           // array(2)
        "\xA2"           // map(2)
        "\x00"           // unsigned(0) => SenML Name
        "\x6A/13/26/1/4" // text(10)
        "\x02"           // unsigned(2) => SenML Value
        "\x18\x2A"       // unsigned(42)
                         // ,
        "\xA2"           // map(2)
        "\x00"           // unsigned(0) => SenML Name
        "\x6A/13/26/1/5" // text(10)
        "\x02"           // unsigned(2) => SenML Value
        "\x18\x2B"       // unsigned(43)
    };
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    test_multiple_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_resource_permuted, multiple_instance) {
    static const char RESOURCES[] = {
        "\x82"           // array(2)
        "\xA2"           // map(2)
        "\x02"           // unsigned(2) => SenML Value
        "\x18\x2A"       // unsigned(42)
        "\x00"           // unsigned(0) => SenML Name
        "\x6A/13/26/1/4" // text(10)
                         // ,
        "\xA2"           // map(2)
        "\x02"           // unsigned(2) => SenML Value
        "\x18\x2B"       // unsigned(43)
        "\x00"           // unsigned(0) => SenML Name
        "\x6A/13/26/1/5" // text(10)
    };
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    test_multiple_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_instance, with_simple_resource) {
    static const char RESOURCE[] = {
        "\x81"         // array(1)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
    };
    TEST_ENV(RESOURCE, TEST_INSTANCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                1)));

    // cached value
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                1)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_instance, with_more_than_one_resource) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
    };
    TEST_ENV(RESOURCES, TEST_INSTANCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                1)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                2)));

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_instance, resource_skipping) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
    };
    TEST_ENV(RESOURCES, TEST_INSTANCE_PATH);
    test_resource_skipping(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_instance_permuted, resource_skipping) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
                       // ,
        "\xA2"         // map(2)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
    };
    TEST_ENV(RESOURCES, TEST_INSTANCE_PATH);
    test_resource_skipping(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_instance, multiple_resource_skipping) {
    static const char RESOURCES[] = {
        "\x82"           // array(2)
        "\xA2"           // map(2)
        "\x00"           // unsigned(0) => SenML Name
        "\x6A/13/26/1/4" // text(10)
        "\x02"           // unsigned(2) => SenML Value
        "\x18\x2A"       // unsigned(42)
                         // ,
        "\xA2"           // map(2)
        "\x00"           // unsigned(0) => SenML Name
        "\x6A/13/26/2/5" // text(10)
        "\x02"           // unsigned(2) => SenML Value
        "\x18\x2B"       // unsigned(43)
    };
    TEST_ENV(RESOURCES, TEST_INSTANCE_PATH);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_INSTANCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                         TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                         1,
                                         4)));

    // we may not like this resource for some reason, let's skip its value
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_INSTANCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                         TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                         2,
                                         5)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_object, with_single_instance_and_some_resources) {
    static const char RESOURCES[] = {
        "\x82"         // array(2)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
    };
    TEST_ENV(RESOURCES, MAKE_OBJECT_PATH(13));

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 26, 1)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 26, 2)));

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_object, with_some_instances_and_some_resources) {
    static const char RESOURCES[] = {
        "\x84"         // array(4)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2A"     // unsigned(42)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/26/2" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2B"     // unsigned(43)
                       //
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/27/3" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2C"     // unsigned(44)
                       // ,
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/13/27/4" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x18\x2D"     // unsigned(45)

    };
    TEST_ENV(RESOURCES, MAKE_OBJECT_PATH(13));

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 26, 1)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 26, 2)));

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 27, 3)));

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 44);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 27, 4)));

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 45);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);

    TEST_TEARDOWN(OK);
}

#define TEST_VALUE_ENV(TypeAndValue)                                           \
    static const char RESOURCE[] = { "\x81" /* array(1) */                     \
                                     "\xA2" /* map(2) */                       \
                                     "\x00" /* unsigned(0) => SenML Name */    \
                                     "\x68/13/26/1" /* text(8) */              \
                                     TypeAndValue };                           \
    TEST_ENV(RESOURCE, MAKE_RESOURCE_PATH(13, 26, 1));                         \
    {                                                                          \
        anjay_uri_path_t path;                                                 \
        ASSERT_OK(_anjay_input_get_path(in, &path, NULL));                     \
        ASSERT_TRUE(                                                           \
                _anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 26, 1))); \
    }

AVS_UNIT_TEST(cbor_in_value, string_with_zero_length_buffer) {
    // unsigned(3) => SenML String & string(foobar)
    TEST_VALUE_ENV("\x03\x66"
                   "foobar");

    char buf[16] = "nothing";
    ASSERT_EQ(_anjay_get_string_unlocked(in, buf, 0), ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ(buf[0], 'n');

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, bytes_with_too_short_buffer) {
    // unsigned(8) => SenML Data & bytes(foobar)
    TEST_VALUE_ENV("\x08\x46"
                   "foobar");

    char buf[16] = "nothing";
    size_t bytes_read;
    bool message_finished;
    ASSERT_OK(_anjay_get_bytes_unlocked(
            in, &bytes_read, &message_finished, buf, 0));
    ASSERT_EQ(bytes_read, 0);
    ASSERT_EQ(message_finished, false);
    ASSERT_EQ(buf[0], 'n');

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, u64_as_double_within_range) {
    // unsigned(2) => SenML Value & unsigned(9007199254740992)
    TEST_VALUE_ENV("\x02\x1B\x00\x20\x00\x00\x00\x00\x00\x00");

    double value;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    ASSERT_EQ(value, UINT64_C(9007199254740992));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, u64_as_double_out_of_range) {
    // unsigned(2) => SenML Value & unsigned(9007199254740993)
    TEST_VALUE_ENV("\x02\x1B\x00\x20\x00\x00\x00\x00\x00\x01");

    double value;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    // precision is lost, but we don't care
    ASSERT_EQ(value, 9007199254740992.0);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, i64_as_double_within_range) {
    // unsigned(2) => SenML Value & negative(9007199254740991)
    TEST_VALUE_ENV("\x02\x3B\x00\x1F\xFF\xFF\xFF\xFF\xFF\xFF");

    double value;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    ASSERT_EQ(value, -INT64_C(9007199254740992));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, i64_as_double_out_of_range) {
    // unsigned(2) => SenML Value & negative(9007199254740993)
    TEST_VALUE_ENV("\x02\x3B\x00\x20\x00\x00\x00\x00\x00\x00");

    double value;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    // precision is lost, but we don't care
    ASSERT_EQ(value, -9007199254740992.0);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, float_as_i64_when_convertible) {
    // unsigned(2) => SenML Value & simple_f32(3.0)
    TEST_VALUE_ENV("\x02\xFA\x40\x40\x00\x00");

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 3);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, float_as_i64_when_not_convertible) {
    // unsigned(2) => SenML Value & simple_f32(3.1415926535)
    TEST_VALUE_ENV("\x02\xFA\x40\x49\x0f\xdb");

    int64_t value;
    ASSERT_FAIL(_anjay_get_i64_unlocked(in, &value));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, double_as_i64_when_convertible) {
    // unsigned(2) => SenML Value & simple_f64(3)
    TEST_VALUE_ENV("\x02\xFB\x40\x08\x00\x00\x00\x00\x00\x00");

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 3);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, double_as_u64_when_convertible) {
    // unsigned(2) => SenML Value & simple_f64(3)
    TEST_VALUE_ENV("\x02\xFB\x40\x08\x00\x00\x00\x00\x00\x00");

    uint64_t value;
    ASSERT_OK(_anjay_get_u64_unlocked(in, &value));
    ASSERT_EQ(value, 3);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, double_as_huge_u64_when_convertible) {
    // unsigned(2) => SenML Value & simple_f64(1.844674407370955e19)
    TEST_VALUE_ENV("\x02\xFB\x43\xEF\xFF\xFF\xFF\xFF\xFF\xFF");

    uint64_t value;
    ASSERT_OK(_anjay_get_u64_unlocked(in, &value));
    ASSERT_EQ(value, UINT64_MAX - 2047);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, double_as_i64_not_convertible) {
    // unsigned(2) => SenML Value & simple_f64(3.1415926535)
    TEST_VALUE_ENV("\x02\xFB\x40\x09\x21\xfb\x54\x41\x17\x44");

    int64_t value;
    ASSERT_FAIL(_anjay_get_i64_unlocked(in, &value));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, half_read_as_double) {
    // unsigned(2) => SenML Value & simple_f16(32)
    TEST_VALUE_ENV("\x02\xF9\x50\x00");
    double value;
    ASSERT_OK(_anjay_get_double_unlocked(in, &value));
    ASSERT_EQ(value, 32.0);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, objlnk_valid) {
    // text(3) => "vlo" & string(32:42532)
    TEST_VALUE_ENV("\x63"
                   "vlo"
                   "\x68"
                   "32:42532");

    anjay_oid_t oid;
    anjay_iid_t iid;
    ASSERT_OK(_anjay_get_objlnk_unlocked(in, &oid, &iid));
    ASSERT_EQ(oid, 32);
    ASSERT_EQ(iid, 42532);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, objlnk_with_trash_at_the_end) {
    // text(3) => "vlo" & string(32:42foo)
    TEST_VALUE_ENV("\x63"
                   "vlo"
                   "\x68"
                   "32:42foo");

    anjay_oid_t oid;
    anjay_iid_t iid;
    ASSERT_FAIL(_anjay_get_objlnk_unlocked(in, &oid, &iid));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in_value, objlnk_with_overflow) {
    // text(3) => "vlo" & string(1:423444)
    TEST_VALUE_ENV("\x63"
                   "vlo"
                   "\x68"
                   "1:423444");

    anjay_oid_t oid;
    anjay_iid_t iid;
    ASSERT_FAIL(_anjay_get_objlnk_unlocked(in, &oid, &iid));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(cbor_in, valid_paths) {
    anjay_uri_path_t path;
    ASSERT_OK(parse_absolute_path(&path, "/"));
    ASSERT_OK(parse_absolute_path(&path, "/1"));
    ASSERT_OK(parse_absolute_path(&path, "/1/2"));
    ASSERT_OK(parse_absolute_path(&path, "/1/2/3"));
    ASSERT_OK(parse_absolute_path(&path, "/1/2/3/4"));
    ASSERT_OK(parse_absolute_path(&path, "/1/2/3/65534"));
    ASSERT_OK(parse_absolute_path(&path, "/65534/65534/65534/65534"));
}

AVS_UNIT_TEST(cbor_in, invalid_paths) {
    anjay_uri_path_t path;
    ASSERT_FAIL(parse_absolute_path(&path, ""));
    ASSERT_FAIL(parse_absolute_path(&path, "//"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/2/"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/2/3/"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/2/3/4/"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/2/3/65535"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/2/3/65536"));
    ASSERT_FAIL(parse_absolute_path(&path, "/1/2//3"));
    ASSERT_FAIL(parse_absolute_path(&path, "/-1/2/3"));
}
#undef TEST_VALUE_ENV

#define TEST_VALUE_ENV(TypeAndValue)                                        \
    static const char RESOURCE[] = { "\x81" /* array(1) */                  \
                                     "\xA2" /* map(2) */                    \
                                     "\x00" /* unsigned(0) => SenML Name */ \
                                     "\x68/13/26/1" /* text(8) */           \
                                     TypeAndValue };                        \
    TEST_ENV(RESOURCE, MAKE_RESOURCE_PATH(13, 26, 1));

AVS_UNIT_TEST(cbor_in, get_integer_before_get_id) {
    // SenML Value -> unsigned(42)
    TEST_VALUE_ENV("\x02\x18\x2A");
    ASSERT_FAIL(_anjay_get_i64_unlocked(in, &(int64_t) { 0 }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in, get_float_before_get_id) {
    // unsigned(2) => SenML Value & simple_f32(3.0)
    TEST_VALUE_ENV("\x02\xFA\x40\x40\x00\x00");
    ASSERT_FAIL(_anjay_get_double_unlocked(in, &(double) { 0 }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in, get_bytes_before_get_id) {
    // unsigned(8) => SenML Data & bytes(foobar)
    TEST_VALUE_ENV("\x08\x46"
                   "foobar");
    ASSERT_FAIL(_anjay_get_bytes_unlocked(
            in, &(size_t) { 0 }, &(bool) { false }, (char[32]){}, 32));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in, get_string_before_get_id) {
    // unsigned(3) => SenML String & string(foobar)
    TEST_VALUE_ENV("\x03\x66"
                   "foobar");
    ASSERT_FAIL(_anjay_get_string_unlocked(in, (char[32]){}, 32));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in, get_bool_before_get_id) {
    // unsigned(4) => SenML Boolean Value & false
    TEST_VALUE_ENV("\x04\xF4");
    ASSERT_FAIL(_anjay_get_bool_unlocked(in, &(bool) { false }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in, get_objlnk_before_get_id) {
    // text(3) => "vlo" & string(32:42532)
    TEST_VALUE_ENV("\x63"
                   "vlo"
                   "\x68"
                   "32:42532");
    ASSERT_FAIL(_anjay_get_objlnk_unlocked(
            in, &(anjay_oid_t) { 0 }, &(anjay_iid_t) { 0 }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(cbor_in, get_path_for_resource_instance_path) {
    static const char RESOURCE_INSTANCE_PATH[] = {
        "\x81"         // array(1)
        "\xA1"         // map(1)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/3/0/0/1" // text(8)
    };
    TEST_ENV(RESOURCE_INSTANCE_PATH, MAKE_RESOURCE_INSTANCE_PATH(3, 0, 0, 1));
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path, &MAKE_RESOURCE_INSTANCE_PATH(3, 0, 0, 1)));
    TEST_TEARDOWN(OK);
}

#define COMPOSITE_TEST_ENV(Data, Path)                               \
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER; \
    avs_stream_inbuf_set_buffer(&stream, Data, sizeof(Data) - 1);    \
    anjay_unlocked_input_ctx_t *in;                                  \
    ASSERT_OK(_anjay_input_senml_cbor_composite_read_create(         \
            &in, (avs_stream_t *) &stream, &(Path)));

AVS_UNIT_TEST(cbor_in_composite, composite_read_mode_additional_payload) {
    static const char RESOURCE_INSTANCE_WITH_PAYLOAD[] = {
        "\x81"         // array(1)
        "\xA2"         // map(2)
        "\x00"         // unsigned(0) => SenML Name
        "\x68/3/0/0/1" // text(8)
        "\x02"         // unsigned(2) => SenML Value
        "\x63"         // text(3)
        "foo"
    };
    COMPOSITE_TEST_ENV(RESOURCE_INSTANCE_WITH_PAYLOAD, MAKE_ROOT_PATH());
    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));
    TEST_TEARDOWN(FAIL);
}

#undef COMPOSITE_TEST_ENV
#undef TEST_VALUE_ENV
#undef TEST_ENV
