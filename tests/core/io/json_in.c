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
    ASSERT_OK(_anjay_input_json_create(&in, (avs_stream_t *) &stream, &(Path)));

AVS_UNIT_TEST(json_in_resource, single_instance) {
    static const char RESOURCE[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 } ]";
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_single_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource_permuted, single_instance) {
    static const char RESOURCE[] = "[ { \"v\": 42, \"n\": \"/13/26/1\" } ]";
    TEST_ENV(RESOURCE, TEST_RESOURCE_PATH);
    test_single_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource, single_instance_with_trailing_comma) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, ]";
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    ASSERT_FAIL(_anjay_input_get_path(in, &(anjay_uri_path_t) { 0 }, NULL));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in_resource, single_instance_with_invalid_data) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", manure } ]";
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    ASSERT_FAIL(_anjay_input_get_path(in, &(anjay_uri_path_t) { 0 }, NULL));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in_resource, single_instance_with_invalid_data_later) {
    static const char RESOURCES[] =
            "[ { \"n\": \"/13/26/1\", \"v\": 42 }, manure ]";
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    ASSERT_FAIL(_anjay_input_get_path(in, &(anjay_uri_path_t) { 0 }, NULL));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in_resource, single_instance_but_more_than_one) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2\", \"v\": 43 } ]";
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    test_single_instance_but_more_than_one(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource,
              single_instance_but_more_than_one_without_last_get_path) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2\", \"v\": 43 } ]";
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

AVS_UNIT_TEST(json_in_resource, single_instance_with_first_resource_unrelated) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/2\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/1\", \"v\": 43 } ]";
    // NOTE: Request is on /13/26/1 but the first resource in the payload is
    // /13/26/2.
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);

    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_ERR_BAD_REQUEST);

    // Basically nothing was extracted from the context, because it was broken
    // from the very beginning.
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in_resource_permuted, single_instance_but_more_than_one) {
    static const char RESOURCES[] = "[ { \"v\": 42, \"n\": \"/13/26/1\" }, "
                                    "{ \"v\": 43, \"n\": \"/13/26/2\" } ]";
    TEST_ENV(RESOURCES, MAKE_RESOURCE_PATH(13, 26, 1));
    test_single_instance_but_more_than_one(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource, multiple_instance) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1/4\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/1/5\", \"v\": 43 } ]";
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    test_multiple_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource_permuted, multiple_instance) {
    static const char RESOURCES[] = "[ { \"v\": 42, \"n\": \"/13/26/1/4\" }, "
                                    "{ \"v\": 43, \"n\": \"/13/26/1/5\" } ]";
    TEST_ENV(RESOURCES, TEST_RESOURCE_PATH);
    test_multiple_instance(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_instance, with_simple_resource) {
    static const char RESOURCE[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 } ]";
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

AVS_UNIT_TEST(json_in_instance, with_more_than_one_resource) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2\", \"v\": 43 } ]";
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

AVS_UNIT_TEST(json_in_instance, resource_skipping) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2\", \"v\": 43 } ]";
    TEST_ENV(RESOURCES, TEST_INSTANCE_PATH);
    test_resource_skipping(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource, invalid_iid) {
    static const char RESOURCES[] = "[ { \"n\": \"/5/65535/1\", \"v\": 42 } ]";
    TEST_ENV(RESOURCES, MAKE_OBJECT_PATH(5));

    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource, invalid_rid) {
    static const char RESOURCES[] = "[ { \"n\": \"/5/0/65535\", \"v\": 42 } ]";
    TEST_ENV(RESOURCES, MAKE_INSTANCE_PATH(5, 0));

    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_resource, invalid_riid) {
    static const char RESOURCES[] =
            "[ { \"n\": \"/5/0/3/65535\", \"v\": 42 } ]";
    TEST_ENV(RESOURCES, MAKE_RESOURCE_PATH(5, 0, 3));

    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_instance_permuted, resource_skipping) {
    static const char RESOURCES[] = "[ { \"v\": 42, \"n\": \"/13/26/1\" }, "
                                    "{ \"v\": 43, \"n\": \"/13/26/2\" } ]";
    TEST_ENV(RESOURCES, TEST_INSTANCE_PATH);
    test_resource_skipping(in);
    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_instance, multiple_resource_skipping) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1/4\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2/5\", \"v\": 43 } ]";
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

AVS_UNIT_TEST(json_in_object, with_single_instance_and_some_resources) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2\", \"v\": 43 } ]";
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

AVS_UNIT_TEST(json_in_object, invalid_oid) {
    static const char RESOURCES[] = "[ { \"n\": \"/65535/1/1\", \"v\": 42 } ]";
    TEST_ENV(RESOURCES, MAKE_ROOT_PATH());

    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_object, with_some_instances_and_some_resources) {
    static const char RESOURCES[] = "[ { \"n\": \"/13/26/1\", \"v\": 42 }, "
                                    "{ \"n\": \"/13/26/2\", \"v\": 43 }, "
                                    "{ \"n\": \"/13/27/3\", \"v\": 44 }, "
                                    "{ \"n\": \"/13/27/4\", \"v\": 45 } ]";
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
    static const char RESOURCE[] =                                             \
            "[ { \"n\": \"/13/26/1\", " TypeAndValue " } ]";                   \
    TEST_ENV(RESOURCE, MAKE_RESOURCE_PATH(13, 26, 1));                         \
    {                                                                          \
        anjay_uri_path_t path;                                                 \
        ASSERT_OK(_anjay_input_get_path(in, &path, NULL));                     \
        ASSERT_TRUE(                                                           \
                _anjay_uri_path_equal(&path, &MAKE_RESOURCE_PATH(13, 26, 1))); \
    }

AVS_UNIT_TEST(json_in_value, string_with_zero_length_buffer) {
    TEST_VALUE_ENV("\"vs\": \"foobar\"");

    char buf[16] = "nothing";
    ASSERT_EQ(_anjay_get_string_unlocked(in, buf, 0), ANJAY_BUFFER_TOO_SHORT);
    ASSERT_EQ(buf[0], 'n');

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, bytes_with_too_short_buffer) {
    TEST_VALUE_ENV("\"vd\": \"Zm9vYmFy\""); // base64(foobar)

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

AVS_UNIT_TEST(json_in_value, double_as_i64_when_convertible) {
    TEST_VALUE_ENV("\"v\": 3");

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 3);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, double_as_u64_when_convertible) {
    TEST_VALUE_ENV("\"v\": 3");

    uint64_t value;
    ASSERT_OK(_anjay_get_u64_unlocked(in, &value));
    ASSERT_EQ(value, 3);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, double_as_huge_u64_when_convertible) {
    TEST_VALUE_ENV("\"v\": 1.844674407370955e19");

    uint64_t value;
    ASSERT_OK(_anjay_get_u64_unlocked(in, &value));
    ASSERT_EQ(value, UINT64_MAX - 2047);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, double_as_i64_not_convertible) {
    TEST_VALUE_ENV("\"v\": 3.1415926535");

    int64_t value;
    ASSERT_FAIL(_anjay_get_i64_unlocked(in, &value));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, objlnk_valid) {
    TEST_VALUE_ENV("\"vlo\": \"32:42532\"");

    anjay_oid_t oid;
    anjay_iid_t iid;
    ASSERT_OK(_anjay_get_objlnk_unlocked(in, &oid, &iid));
    ASSERT_EQ(oid, 32);
    ASSERT_EQ(iid, 42532);

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, objlnk_with_trash_at_the_end) {
    TEST_VALUE_ENV("\"vlo\": \"32:42foo\"");

    anjay_oid_t oid;
    anjay_iid_t iid;
    ASSERT_FAIL(_anjay_get_objlnk_unlocked(in, &oid, &iid));

    TEST_TEARDOWN(OK);
}

AVS_UNIT_TEST(json_in_value, objlnk_with_overflow) {
    TEST_VALUE_ENV("\"vlo\": \"1:423444\"");

    anjay_oid_t oid;
    anjay_iid_t iid;
    ASSERT_FAIL(_anjay_get_objlnk_unlocked(in, &oid, &iid));

    TEST_TEARDOWN(OK);
}
#undef TEST_VALUE_ENV

#define TEST_VALUE_ENV(TypeAndValue)                         \
    static const char RESOURCE[] =                           \
            "[ { \"n\": \"/13/26/1\", " TypeAndValue " } ]"; \
    TEST_ENV(RESOURCE, MAKE_RESOURCE_PATH(13, 26, 1));

AVS_UNIT_TEST(json_in, get_integer_before_get_id) {
    TEST_VALUE_ENV("\"v\": 42");
    ASSERT_FAIL(_anjay_get_i64_unlocked(in, &(int64_t) { 0 }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in, get_float_before_get_id) {
    TEST_VALUE_ENV("\"v\": 3.0");
    ASSERT_FAIL(_anjay_get_double_unlocked(in, &(double) { 0 }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in, get_bytes_before_get_id) {
    TEST_VALUE_ENV("\"vd\": \"Zm9vYmFy\""); // base64(foobar)
    ASSERT_FAIL(_anjay_get_bytes_unlocked(
            in, &(size_t) { 0 }, &(bool) { false }, (char[32]){}, 32));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in, get_string_before_get_id) {
    TEST_VALUE_ENV("\"vs\": \"foobar\"");
    ASSERT_FAIL(_anjay_get_string_unlocked(in, (char[32]){}, 32));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in, get_bool_before_get_id) {
    TEST_VALUE_ENV("\"vb\": false");
    ASSERT_FAIL(_anjay_get_bool_unlocked(in, &(bool) { false }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in, get_objlnk_before_get_id) {
    TEST_VALUE_ENV("\"vlo\": \"32:42532\"");
    ASSERT_FAIL(_anjay_get_objlnk_unlocked(
            in, &(anjay_oid_t) { 0 }, &(anjay_iid_t) { 0 }));
    TEST_TEARDOWN(FAIL);
}

AVS_UNIT_TEST(json_in, get_path_for_resource_instance_path) {
    static const char RESOURCE_INSTANCE_PATH[] = "[ { \"n\": \"/3/0/0/1\" } ]";
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
    ASSERT_OK(_anjay_input_json_composite_read_create(               \
            &in, (avs_stream_t *) &stream, &(Path)));

AVS_UNIT_TEST(json_in_composite, composite_read_mode_additional_payload) {
    static const char RESOURCE_INSTANCE_WITH_PAYLOAD[] =
            "[ { \"n\": \"/3/0/0/1\", \"v\": \"foo\" } ]";
    COMPOSITE_TEST_ENV(RESOURCE_INSTANCE_WITH_PAYLOAD, MAKE_ROOT_PATH());
    anjay_uri_path_t path;
    ASSERT_FAIL(_anjay_input_get_path(in, &path, NULL));
    TEST_TEARDOWN(FAIL);
}

#undef COMPOSITE_TEST_ENV
#undef TEST_VALUE_ENV
#undef TEST_ENV
