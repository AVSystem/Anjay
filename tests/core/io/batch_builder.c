/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
