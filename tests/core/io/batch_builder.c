/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include "src/core/coap/anjay_content_format.h"

#include <string.h>

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

#define MAKE_TEST_STRING(Data) \
    (test_data_t) {            \
        .data = Data,          \
        .size = sizeof(Data)   \
    }

#define MAKE_TEST_DATA(Data)     \
    (test_data_t) {              \
        .data = Data,            \
        .size = sizeof(Data) - 1 \
    }

static void builder_teardown(anjay_batch_builder_t *builder) {
    _anjay_batch_builder_cleanup(&builder);
    AVS_UNIT_ASSERT_NULL(builder);
}

static anjay_batch_builder_t *builder_setup(void) {
    anjay_batch_builder_t *builder = _anjay_batch_builder_new();
    AVS_UNIT_ASSERT_NOT_NULL(builder);
    return builder;
}

AVS_UNIT_TEST(batch_builder, empty) {
    anjay_batch_builder_t *builder = builder_setup();
    builder_teardown(builder);
}

AVS_UNIT_TEST(batch_builder, single_int_entry) {
    anjay_batch_builder_t *builder = builder_setup();

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
            AVS_TIME_REAL_INVALID, 0));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);

    builder_teardown(builder);
}

AVS_UNIT_TEST(batch_builder, two_entries) {
    anjay_batch_builder_t *builder = builder_setup();

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
            AVS_TIME_REAL_INVALID, 0));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
            AVS_TIME_REAL_INVALID, 0));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 2);

    builder_teardown(builder);
}

AVS_UNIT_TEST(batch_builder, string_copy) {
    anjay_batch_builder_t *builder = builder_setup();

    test_data_t test_string = MAKE_TEST_STRING("raz dwa trzy");

    char *str = (char *) avs_malloc(test_string.size);
    AVS_UNIT_ASSERT_NOT_NULL(str);
    memcpy(str, test_string.data, test_string.size);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_string(
            builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
            AVS_TIME_REAL_INVALID, str));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);

    // Passed string shouldn't be required anymore.
    avs_free(str);

    AVS_LIST(anjay_batch_entry_t) entry = AVS_LIST_TAIL(builder->list);
    AVS_UNIT_ASSERT_EQUAL_STRING(entry->data.value.string, test_string.data);

    builder_teardown(builder);
}

#ifdef ANJAY_WITH_LWM2M11
AVS_UNIT_TEST(batch_builder, bytes_copy) {
    anjay_batch_builder_t *builder = builder_setup();

    test_data_t test_bytes = MAKE_TEST_DATA("\x01\x02\x03\x04\x05");

    char *bytes = (char *) avs_malloc(test_bytes.size);
    AVS_UNIT_ASSERT_NOT_NULL(bytes);
    memcpy(bytes, test_bytes.data, test_bytes.size);

    _anjay_batch_add_bytes(builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
                           AVS_TIME_REAL_INVALID, bytes, test_bytes.size);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);

    // Passed bytes shouldn't be required anymore.
    avs_free(bytes);

    AVS_LIST(anjay_batch_entry_t) entry = AVS_LIST_TAIL(builder->list);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(entry->data.value.bytes.data,
                                      test_bytes.data, test_bytes.size);

    builder_teardown(builder);
}

AVS_UNIT_TEST(batch_builder, empty_bytes) {
    anjay_batch_builder_t *builder = builder_setup();

    _anjay_batch_add_bytes(builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
                           AVS_TIME_REAL_INVALID, NULL, 0);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);

    AVS_LIST(anjay_batch_entry_t) entry = AVS_LIST_TAIL(builder->list);
    AVS_UNIT_ASSERT_NULL(entry->data.value.bytes.data);
    AVS_UNIT_ASSERT_EQUAL(entry->data.value.bytes.length, 0);

    builder_teardown(builder);
}
#endif // ANJAY_WITH_LWM2M11

AVS_UNIT_TEST(batch_builder, compile) {
    anjay_batch_builder_t *builder = builder_setup();

    AVS_UNIT_ASSERT_SUCCESS(_anjay_batch_add_int(
            builder, &MAKE_RESOURCE_INSTANCE_PATH(0, 0, 0, 0),
            AVS_TIME_REAL_INVALID, 0));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(builder->list), 1);

    anjay_batch_t *batch = _anjay_batch_builder_compile(&builder);
    AVS_UNIT_ASSERT_NULL(builder);

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(batch->list), 1);
    AVS_UNIT_ASSERT_EQUAL(batch->ref_count, 1);

    _anjay_batch_release(&batch);
    AVS_UNIT_ASSERT_NULL(batch);
}
