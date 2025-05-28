/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
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

#ifdef ANJAY_WITH_LWM2M_GATEWAY
#    define URI_EQUAL(path, expected_path)                       \
        ASSERT_TRUE(_anjay_uri_path_equal(path, expected_path)); \
        ASSERT_TRUE(_anjay_uri_path_prefix_equal(path, expected_path));
#else // ANJAY_WITH_LWM2M_GATEWAY
#    define URI_EQUAL(path, expected_path) \
        ASSERT_TRUE(_anjay_uri_path_equal(path, expected_path));
#endif // ANJAY_WITH_LWM2M_GATEWAY

static const anjay_uri_path_t TEST_RESOURCE_PATH =
        RESOURCE_PATH_INITIALIZER(13, 26, 1);

static const anjay_uri_path_t TEST_INSTANCE_PATH =
        INSTANCE_PATH_INITIALIZER(13, 26);

#ifdef ANJAY_WITH_LWM2M_GATEWAY
static const anjay_uri_path_t TEST_RESOURCE_PATH_WITH_PREFIX =
        RESOURCE_PATH_INITIALIZER_WITH_PREFIX("0aapud0", 13, 26, 1);

static const anjay_uri_path_t TEST_INSTANCE_PATH_WITH_PREFIX =
        INSTANCE_PATH_INITIALIZER_WITH_PREFIX("0aapud0", 13, 26);
#endif // ANJAY_WITH_LWM2M_GATEWAY

static void check_path(anjay_unlocked_input_ctx_t *in,
                       const anjay_uri_path_t *expected_path,
                       int64_t expected_value) {
    anjay_uri_path_t path;
    int64_t value;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    URI_EQUAL(&path, expected_path);

    // cached value
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    URI_EQUAL(&path, expected_path);

    ASSERT_OK(_anjay_get_i64_unlocked(in, &value));
    ASSERT_EQ(value, expected_value);
}

static void check_paths(anjay_unlocked_input_ctx_t *in,
                        const anjay_uri_path_t *expected_paths,
                        const size_t paths_count) {
    for (size_t i = 0; i < paths_count; i++) {
        check_path(in, &expected_paths[i], 42 + (int) i);
        ASSERT_OK(_anjay_input_next_entry(in));
    }
    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
    ASSERT_EQ(_anjay_json_like_decoder_state(((senml_in_t *) in)->ctx),
              ANJAY_JSON_LIKE_DECODER_STATE_FINISHED);
}

static void
test_single_instance_but_more_than_one(anjay_unlocked_input_ctx_t *in,
                                       const anjay_uri_path_t *expected_path) {
    check_path(in, expected_path, 42);
    ASSERT_OK(_anjay_input_next_entry(in));
    // The resource is there, but the context doesn't return it because it
    // is not related to the request resource path /13/26/1. In order to
    // actually get it, we would have to do a request on an instance.
    // Because the context top-level path is restricted, obtaining next id
    // results in error.
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_ERR_BAD_REQUEST);
}

static void test_skipping(anjay_unlocked_input_ctx_t *in,
                          const anjay_uri_path_t *expected_paths,
                          size_t paths_count) {
    ASSERT_EQ(paths_count, 2);

    anjay_uri_path_t path;
    ASSERT_OK(_anjay_input_get_path(in, &path, NULL));
    URI_EQUAL(&path, &expected_paths[0]);

    // we may not like this resource for some reason, let's skip its value
    ASSERT_OK(_anjay_input_next_entry(in));

    check_path(in, &expected_paths[1], 43);

    ASSERT_OK(_anjay_input_next_entry(in));
    ASSERT_EQ(_anjay_input_get_path(in, NULL, NULL), ANJAY_GET_PATH_END);
}

#endif /* ANJAY_IO_TEST_SENML_IN_COMMON_H */
