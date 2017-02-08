/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <config.h>

#include <avsystem/commons/unit/memstream.h>
#include <avsystem/commons/unit/test.h>

#include "anjay/anjay.h"

#define TEST_ENV(Data) \
    avs_stream_abstract_t *stream = NULL; \
    AVS_UNIT_ASSERT_SUCCESS(avs_unit_memstream_alloc(&stream, sizeof(Data))); \
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(stream, Data, sizeof(Data) - 1)); \
    anjay_input_ctx_t *ctx; \
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_tlv_create(&ctx, &stream, true));

#define TEST_TEARDOWN \
    _anjay_input_ctx_destroy(&ctx)

AVS_UNIT_TEST(input_array, example) {
    TEST_ENV( // example from spec 6.3.3.2
            "\x08\x00\x11" // Object Instance 0
            "\xC1\x00\x03" // Resource 0 - Object ID == 3
            "\xC1\x01\x01" // Resource 1 - Instance ID == 1
            "\x86\x02" // Resource 2 - ACL array
            "\x41\x01\xE0" // [1] -> -32
            "\x41\x02\x80" // [2] -> -128
            "\xC1\x03\x01" // Resource 3 - ACL owner == 1
            "\x08\x01\x11" // Object Instance 1
            "\xC1\x00\x04" // Resource 0 - Object ID == 4
            "\xC1\x01\x02" // Resource 1 - Instance ID == 2
            "\x86\x02" // Resource 2 - ACL array
            "\x41\x01\x80" // [1] -> -128
            "\x41\x02\x80" // [2] -> -128
            "\xC1\x03\x01" // Resource 3 - ACL owner == 1
            );

    anjay_id_type_t type;
    uint16_t id;
    int32_t value;

    // check IDs for the first object
    AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(ctx, &type, &id));
    AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_IID);
    AVS_UNIT_ASSERT_EQUAL(id, 0);

    {
        anjay_input_ctx_t *obj = _anjay_input_nested_ctx(ctx);
        AVS_UNIT_ASSERT_NOT_NULL(obj);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(obj, &type, &id));
        AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RID);
        AVS_UNIT_ASSERT_EQUAL(id, 0);
        AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(obj, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 3);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(obj, &type, &id));
        AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RID);
        AVS_UNIT_ASSERT_EQUAL(id, 1);
        AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(obj, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 1);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(obj, &type, &id));
        AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RID);
        AVS_UNIT_ASSERT_EQUAL(id, 2);
        {
            anjay_input_ctx_t *array = anjay_get_array(obj);
            AVS_UNIT_ASSERT_NOT_NULL(array);

            AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(array, &type, &id));
            AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RIID);
            AVS_UNIT_ASSERT_EQUAL(id, 1);
            AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(array, &value));
            AVS_UNIT_ASSERT_EQUAL(value, -32);
            AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(array));

            AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(array, &type, &id));
            AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RIID);
            AVS_UNIT_ASSERT_EQUAL(id, 2);
            AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(array, &value));
            AVS_UNIT_ASSERT_EQUAL(value, -128);
            AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(array));

            AVS_UNIT_ASSERT_EQUAL(_anjay_input_get_id(array, &type, &id),
                                  ANJAY_GET_INDEX_END);
        }
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_get_id(obj, &type, &id));
        AVS_UNIT_ASSERT_EQUAL(type, ANJAY_ID_RID);
        AVS_UNIT_ASSERT_EQUAL(id, 3);
        AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(obj, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 1);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        AVS_UNIT_ASSERT_EQUAL(_anjay_input_get_id(obj, &type, &id),
                              ANJAY_GET_INDEX_END);
    }
    _anjay_input_next_entry(ctx);

    // do a half-assed job decoding the second one
    {
        anjay_input_ctx_t *obj = _anjay_input_nested_ctx(ctx);
        AVS_UNIT_ASSERT_NOT_NULL(obj);

        AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(obj, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 4);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(obj, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 2);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        {
            anjay_input_ctx_t *array = anjay_get_array(obj);
            AVS_UNIT_ASSERT_NOT_NULL(array);

            AVS_UNIT_ASSERT_SUCCESS(anjay_get_array_index(array, &id));
            AVS_UNIT_ASSERT_EQUAL(id, 1);
            AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(array, &value));
            AVS_UNIT_ASSERT_EQUAL(value, -128);

            AVS_UNIT_ASSERT_FAILED(anjay_get_i32(obj, &value));

            AVS_UNIT_ASSERT_SUCCESS(anjay_get_array_index(array, &id));
            AVS_UNIT_ASSERT_EQUAL(id, 2);
            AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(array, &value));
            AVS_UNIT_ASSERT_EQUAL(value, -128);
        }
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));

        AVS_UNIT_ASSERT_SUCCESS(anjay_get_i32(obj, &value));
        AVS_UNIT_ASSERT_EQUAL(value, 1);
        AVS_UNIT_ASSERT_SUCCESS(_anjay_input_next_entry(obj));
    }
    _anjay_input_next_entry(ctx);

    AVS_UNIT_ASSERT_EQUAL(_anjay_input_get_id(ctx, &type, &id),
                          ANJAY_GET_INDEX_END);

    TEST_TEARDOWN;
}
