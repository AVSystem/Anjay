/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <math.h>
#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#define VERIFY_PAYLOAD(Payload, Buff, Len)                     \
    do {                                                       \
        AVS_UNIT_ASSERT_EQUAL(Len, strlen(Buff));              \
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(Payload, Buff, Len); \
    } while (0)

AVS_UNIT_TEST(register_payload, only_objects) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(2), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(3), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(4), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(5), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    VERIFY_PAYLOAD("</1>,</2>,</3>,</4>,</5>", out_buff, msg_len);
}

AVS_UNIT_TEST(register_payload, objects_with_version) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(2), "1.2"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(3), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(4), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(5), "2.3"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</1>,</2>;ver=1.2,</3>,</4>,</5>;ver=2.3", out_buff,
                   msg_len);
}

AVS_UNIT_TEST(register_payload, objects_with_instances) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(2), "1.2"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(2, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 2), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 3), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(4), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(5), "2.3"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</1/0>,</1/1>,</2>;ver=1.2,</2/0>,</3/0>,</3/1>,</3/2>,</3/"
                   "3>,</4>,</5>;ver=2.3",
                   out_buff, msg_len);
}

AVS_UNIT_TEST(register_payload, instances_without_version) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(2, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 2), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 3), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</1/0>,</1/1>,</2/0>,</3/0>,</3/1>,</3/2>,</3/3>,</4>,</5>",
                   out_buff, msg_len);
}

AVS_UNIT_TEST(register_payload, instances_with_version) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 3), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(4), "1.1"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(5, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</1>,</3/3>,</4>;ver=1.1,</5/0>", out_buff, msg_len);
}

AVS_UNIT_TEST(register_payload, big_numbers) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(22), "1.2"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(22, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(333, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(333, 1), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(333, 2), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(333, 3333), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(4444), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(55555), "2.3"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</1/0>,</1/1>,</22>;ver=1.2,</22/0>,</333/0>,</333/1>,</"
                   "333/2>,</333/3333>,</4444>,</55555>;ver=2.3",
                   out_buff, msg_len);
}

AVS_UNIT_TEST(register_payload, errors) {
    fluf_io_register_ctx_t ctx;
    char out_buff[100] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;

    fluf_io_register_ctx_init(&ctx);

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(2), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(1), NULL));

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(2, 0), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(2, 2), NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(2, 1), NULL));

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(22), "1.2"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(23), "12"));
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(23), ".12"));
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(23), "12."));
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(23), "a.2"));
    AVS_UNIT_ASSERT_FAILED(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(23), "2.b"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(23), "1.2"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_get_payload(
            &ctx, &out_buff[msg_len], 100, &copied_bytes));
    msg_len += copied_bytes;

    VERIFY_PAYLOAD("</2>,</2/0>,</2/2>,</22>;ver=1.2,</23>;ver=1.2", out_buff,
                   msg_len);
}

AVS_UNIT_TEST(register_payload, block_transfer) {
    for (size_t i = 5; i < 25; i++) {
        fluf_io_register_ctx_t ctx;
        char out_buff[50] = { 0 };
        fluf_io_register_ctx_init(&ctx);
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_register_ctx_new_entry(
                &ctx, &FLUF_MAKE_OBJECT_PATH(65222), "9.9"));

        int res = -1;
        size_t copied_bytes = 0;
        size_t msg_len = 0;
        while (res) {
            res = fluf_io_register_ctx_get_payload(&ctx, &out_buff[msg_len], i,
                                                   &copied_bytes);
            msg_len += copied_bytes;
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
        }
        VERIFY_PAYLOAD("</65222>;ver=9.9", out_buff, msg_len);
    }
}
