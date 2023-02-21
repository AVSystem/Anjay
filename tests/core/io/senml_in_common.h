/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_TEST_SENML_IN_COMMON_H
#define ANJAY_IO_TEST_SENML_IN_COMMON_H

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include <anjay_modules/anjay_dm_utils.h>

#define TEST_TEARDOWN(ExpectedResult)                                       \
    do {                                                                    \
        AVS_CONCAT(ASSERT_, ExpectedResult)(_anjay_input_ctx_destroy(&in)); \
    } while (0)

static const anjay_uri_path_t TEST_RESOURCE_PATH =
        RESOURCE_PATH_INITIALIZER(13, 26, 1);

static void test_single_instance(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    // cached value
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
    ASSERT_EQ(_anjay_json_like_decoder_state(((senml_in_t *) in)->ctx),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

static void
test_single_instance_but_more_than_one(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(&path, &TEST_RESOURCE_PATH));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    // The resource is there, but the context doesn't return it because it is
    // not related to the request resource path /13/26/1. In order to actually
    // get it, we would have to do a request on an instance. Because the context
    // top-level path is restricted, obtaining next id results in error.
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_ERR_BAD_REQUEST);
}

static void test_multiple_instance(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_INSTANCE_PATH(TEST_RESOURCE_PATH.ids[ANJAY_ID_OID],
                                         TEST_RESOURCE_PATH.ids[ANJAY_ID_IID],
                                         TEST_RESOURCE_PATH.ids[ANJAY_ID_RID],
                                         4)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 42);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_INSTANCE_PATH(TEST_RESOURCE_PATH.ids[ANJAY_ID_OID],
                                         TEST_RESOURCE_PATH.ids[ANJAY_ID_IID],
                                         TEST_RESOURCE_PATH.ids[ANJAY_ID_RID],
                                         5)));

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
}

static const anjay_uri_path_t TEST_INSTANCE_PATH =
        INSTANCE_PATH_INITIALIZER(13, 26);

static void test_resource_skipping(anjay_unlocked_input_ctx_t *in) {
    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                1)));

    // we may not like this resource for some reason, let's skip its value
    ASSERT_OK(_anjay_input_next_entry(in));

    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    ASSERT_TRUE(_anjay_uri_path_equal(
            &path,
            &MAKE_RESOURCE_PATH(TEST_INSTANCE_PATH.ids[ANJAY_ID_OID],
                                TEST_INSTANCE_PATH.ids[ANJAY_ID_IID],
                                2)));

    int64_t value;
    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
}

#endif /* ANJAY_IO_TEST_SENML_IN_COMMON_H */
