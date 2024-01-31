/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#define VERIFY_PAYLOAD(Payload, Buff, Len)                     \
    do {                                                       \
        AVS_UNIT_ASSERT_EQUAL(Len, strlen(Buff));              \
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(Payload, Buff, Len); \
    } while (0)

AVS_UNIT_TEST(discover_payload, first_example_from_specification) {
    fluf_io_discover_ctx_t ctx;
    char out_buff[300] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_uri_path_t base_path = FLUF_MAKE_OBJECT_PATH(3);
    fluf_attr_notification_t obj_attr = { 0 };
    obj_attr.has_min_period = true;
    obj_attr.min_period = 10;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_init(&ctx, &base_path, NULL));

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &base_path, &obj_attr, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    fluf_attr_notification_t obj_inst_attr = { 0 };
    obj_inst_attr.has_max_period = true;
    obj_inst_attr.max_period = 60;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 0), &obj_inst_attr, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 2), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 3), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 4), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    uint16_t dim = 2;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 6), NULL, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    fluf_attr_notification_t res_attr = { 0 };
    res_attr.has_greater_than = true;
    res_attr.has_less_than = true;
    res_attr.greater_than = 50;
    res_attr.less_than = 42.2;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 7), &res_attr, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 8), NULL, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 11), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 16), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</3>;pmin=10,</3/0>;pmax=60,</3/0/1>,</3/0/2>,</3/0/3>,</3/"
                   "0/4>,</3/0/6>;dim=2,</3/0/7>;dim=2;gt=50;lt=42.2,</3/0/"
                   "8>;dim=2,</3/0/11>,</3/0/16>",
                   out_buff, msg_len);
}

AVS_UNIT_TEST(discover_payload, second_example_from_specification) {
    fluf_io_discover_ctx_t ctx;
    char out_buff[300] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_uri_path_t base_path = FLUF_MAKE_OBJECT_PATH(1);
    uint8_t depth = 1;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_discover_ctx_init(&ctx, &base_path, &depth));
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_discover_ctx_new_entry(&ctx, &base_path, NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    fluf_attr_notification_t obj_inst_attr = { 0 };
    obj_inst_attr.has_max_period = true;
    obj_inst_attr.max_period = 300;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 4), &obj_inst_attr, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</1>,</1/0>,</1/4>;pmax=300", out_buff, msg_len);
}

AVS_UNIT_TEST(discover_payload, third_example_from_specification) {
    fluf_io_discover_ctx_t ctx;
    char out_buff[300] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_uri_path_t base_path = FLUF_MAKE_INSTANCE_PATH(3, 0);
    uint8_t depth = 3;
    fluf_attr_notification_t obj_inst_attr = { 0 };
    obj_inst_attr.has_min_period = true;
    obj_inst_attr.min_period = 10;
    obj_inst_attr.has_max_period = true;
    obj_inst_attr.max_period = 60;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_discover_ctx_init(&ctx, &base_path, &depth));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &base_path, &obj_inst_attr, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 2), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 3), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 4), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    uint16_t dim = 2;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 6), NULL, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 6, 0), NULL, NULL,
            NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 6, 3), NULL, NULL,
            NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    fluf_attr_notification_t res_attr = { 0 };
    res_attr.has_greater_than = true;
    res_attr.has_less_than = true;
    res_attr.greater_than = 50;
    res_attr.less_than = 42.2;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 7), &res_attr, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 7, 0), NULL, NULL,
            NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    fluf_attr_notification_t res_inst_attr = { 0 };
    res_inst_attr.has_less_than = true;
    res_inst_attr.less_than = 45.0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 7, 1), &res_inst_attr,
            NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 8), NULL, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 8, 1), NULL, NULL,
            NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 8, 2), NULL, NULL,
            NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 11), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_PATH(3, 0, 16), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</3/0>;pmin=10;pmax=60,</3/0/1>,</3/0/2>,</3/0/3>,</3/0/"
                   "4>,</3/0/6>;dim=2,</3/0/6/0>,</3/0/6/3>,</3/0/"
                   "7>;dim=2;gt=50;lt=42.2,</3/0/7/0>,</3/0/7/1>;lt=45,</3/"
                   "0/8>;dim=2,</3/0/8/1>,</3/0/8/2>,</3/0/11>,</3/0/16>",
                   out_buff, msg_len);
}

AVS_UNIT_TEST(discover_payload, fourth_example_from_specification) {
    fluf_io_discover_ctx_t ctx;
    char out_buff[300] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_uri_path_t base_path = FLUF_MAKE_INSTANCE_PATH(3, 0);
    uint8_t depth = 0;
    fluf_attr_notification_t attributes = { 0 };
    attributes.has_max_period = true;
    attributes.has_min_period = true;
    attributes.max_period = 60;
    attributes.min_period = 10;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_discover_ctx_init(&ctx, &base_path, &depth));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &base_path, &attributes, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    VERIFY_PAYLOAD("</3/0>;pmin=10;pmax=60", out_buff, msg_len);
}

AVS_UNIT_TEST(discover_payload, fifth_example_from_specification) {
    fluf_io_discover_ctx_t ctx;
    char out_buff[300] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_uri_path_t base_path = FLUF_MAKE_RESOURCE_PATH(3, 0, 7);
    fluf_attr_notification_t attributes = { 0 };
    attributes.has_max_period = true;
    attributes.has_min_period = true;
    attributes.max_period = 60;
    attributes.min_period = 10;
    attributes.has_greater_than = true;
    attributes.has_less_than = true;
    attributes.greater_than = 50;
    attributes.less_than = 42e20;
    uint16_t dim = 2;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_init(&ctx, &base_path, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &base_path, &attributes, NULL, &dim));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 7, 0), NULL, NULL,
            NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    fluf_attr_notification_t res_inst_attr = { 0 };
    res_inst_attr.has_less_than = true;
    res_inst_attr.less_than = 45.0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 7, 1), &res_inst_attr,
            NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 300, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</3/0/7>;dim=2;pmin=10;pmax=60;gt=50;lt=4.2e21,</3/0/7/"
                   "0>,</3/0/7/1>;lt=45",
                   out_buff, msg_len);
}

AVS_UNIT_TEST(discover_payload, block_transfer) {
    for (size_t i = 5; i < 75; i++) {
        fluf_io_discover_ctx_t ctx;
        char out_buff[300] = { 0 };
        size_t copied_bytes = 0;
        size_t msg_len = 0;

        fluf_uri_path_t base_path = FLUF_MAKE_RESOURCE_PATH(3, 0, 7);
        fluf_attr_notification_t attributes = { 0 };
        attributes.has_max_period = true;
        attributes.has_min_period = true;
        attributes.max_period = 60;
        attributes.min_period = 10;
        attributes.has_greater_than = true;
        attributes.has_less_than = true;
        attributes.greater_than = 50;
        attributes.less_than = 42.2;
        uint16_t dim = 2;
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_discover_ctx_init(&ctx, &base_path, NULL));
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
                &ctx, &base_path, &attributes, NULL, &dim));

        int res = -1;
        while (res) {
            res = fluf_io_discover_ctx_get_payload(&ctx, &out_buff[msg_len], i,
                                                   &copied_bytes);
            msg_len += copied_bytes;
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
        }
        res = -1;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
                &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 7, 0), NULL, NULL,
                NULL));
        while (res) {
            res = fluf_io_discover_ctx_get_payload(&ctx, &out_buff[msg_len], i,
                                                   &copied_bytes);
            msg_len += copied_bytes;
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
        }
        res = -1;
        fluf_attr_notification_t res_inst_attr = { 0 };
        res_inst_attr.has_less_than = true;
        res_inst_attr.less_than = 45.0;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_discover_ctx_new_entry(
                &ctx, &FLUF_MAKE_RESOURCE_INSTANCE_PATH(3, 0, 7, 1),
                &res_inst_attr, NULL, NULL));
        while (res) {
            res = fluf_io_discover_ctx_get_payload(&ctx, &out_buff[msg_len], i,
                                                   &copied_bytes);
            msg_len += copied_bytes;
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
        }

        VERIFY_PAYLOAD("</3/0/7>;dim=2;pmin=10;pmax=60;gt=50;lt=42.2,</3/0/7/"
                       "0>,</3/0/7/1>;lt=45",
                       out_buff, msg_len);
    }
}
