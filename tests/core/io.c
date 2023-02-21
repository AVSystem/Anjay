/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_unit_test.h>

#include <anjay/core.h>

#define TEST_ENV(Data)                                               \
    avs_stream_inbuf_t stream = AVS_STREAM_INBUF_STATIC_INITIALIZER; \
    avs_stream_inbuf_set_buffer(&stream, Data, sizeof(Data) - 1);    \
    anjay_unlocked_input_ctx_t *ctx;                                 \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_tlv_create(                 \
            &ctx,                                                    \
            (avs_stream_t *) &stream,                                \
            &MAKE_OBJECT_PATH(ANJAY_DM_OID_ACCESS_CONTROL)));

#define TEST_TEARDOWN _anjay_input_ctx_destroy(&ctx);

AVS_UNIT_TEST(input_array, example) {
    TEST_ENV(              // example from spec 6.3.3.2
            "\x08\x00\x11" // Object Instance 0
            "\xC1\x00\x03" // Resource 0 - Object ID == 3
            "\xC1\x01\x01" // Resource 1 - Instance ID == 1
            "\x86\x02"     // Resource 2 - ACL array
            "\x41\x01\xE0" // [1] -> -32
            "\x41\x02\x80" // [2] -> -128
            "\xC1\x03\x01" // Resource 3 - ACL owner == 1
            "\x08\x01\x11" // Object Instance 1
            "\xC1\x00\x04" // Resource 0 - Object ID == 4
            "\xC1\x01\x02" // Resource 1 - Instance ID == 2
            "\x86\x02"     // Resource 2 - ACL array
            "\x41\x01\x80" // [1] -> -128
            "\x41\x02\x80" // [2] -> -128
            "\xC1\x03\x01" // Resource 3 - ACL owner == 1
    );

    anjay_uri_path_t path;
    int64_t value;

    // check paths for the first object
    {
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_path(ctx, &path, NULL));
        AVS_UNIT_ASSERT_TRUE(_anjay_uri_path_equal(
                &path, &MAKE_RESOURCE_PATH(ANJAY_DM_OID_ACCESS_CONTROL, 0, 0)));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 3);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_path(ctx, &path, NULL));
        AVS_UNIT_ASSERT_TRUE(_anjay_uri_path_equal(
                &path, &MAKE_RESOURCE_PATH(ANJAY_DM_OID_ACCESS_CONTROL, 0, 1)));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 1);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_path(ctx, &path, NULL));
        AVS_UNIT_ASSERT_TRUE(_anjay_uri_path_equal(
                &path,
                &MAKE_RESOURCE_INSTANCE_PATH(
                        ANJAY_DM_OID_ACCESS_CONTROL, 0, 2, 1)));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, -32);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_path(ctx, &path, NULL));
        AVS_UNIT_ASSERT_TRUE(_anjay_uri_path_equal(
                &path,
                &MAKE_RESOURCE_INSTANCE_PATH(
                        ANJAY_DM_OID_ACCESS_CONTROL, 0, 2, 2)));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, -128);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_path(ctx, &path, NULL));
        AVS_UNIT_ASSERT_TRUE(_anjay_uri_path_equal(
                &path, &MAKE_RESOURCE_PATH(ANJAY_DM_OID_ACCESS_CONTROL, 0, 3)));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 1);
    }
    _anjay_input_next_entry(ctx);

    // do a half-assed job decoding the second one
    {
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 4);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 2);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, -128);

        // value already consumed
        AVS_UNIT_ASSERT_FAILED(_anjay_get_i64_unlocked(ctx, &value));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, -128);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(ctx));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_get_i64_unlocked(ctx, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 1);
    }
    _anjay_input_next_entry(ctx);

    AVS_UNIT_ASSERT_EQUAL(_anjay_input_get_path(ctx, &path, NULL),
                          ANJAY_GET_PATH_END);

    TEST_TEARDOWN;
}
