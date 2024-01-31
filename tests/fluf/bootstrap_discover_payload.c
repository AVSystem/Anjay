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

AVS_UNIT_TEST(bootstrap_discover_payload, object_0_call) {
    fluf_io_bootstrap_discover_ctx_t ctx;
    char out_buff[200] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;
    uint16_t ssid;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_init(
            &ctx, &FLUF_MAKE_OBJECT_PATH(0)));
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 0), NULL, &ssid,
            "coaps://server_1.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    ssid = 102;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 2), NULL, &ssid,
            "coaps://server_2.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
#ifdef FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.2,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,"
                   "</0/2>;ssid=102;uri=\"coaps://server_2.example.com\"",
                   out_buff, msg_len);
#else  // FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.1,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,"
                   "</0/2>;ssid=102;uri=\"coaps://server_2.example.com\"",
                   out_buff, msg_len);
#endif // FLUF_WITH_LWM2M12
}

AVS_UNIT_TEST(bootstrap_discover_payload, object_root_call) {
    fluf_io_bootstrap_discover_ctx_t ctx;
    char out_buff[200] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;
    uint16_t ssid;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_bootstrap_discover_ctx_init(&ctx, &FLUF_MAKE_ROOT_PATH()));
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 0), NULL, &ssid,
            "coaps://server_1.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    ssid = 102;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 2), NULL, &ssid,
            "coaps://server_2.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;

#ifdef FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.2,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,"
                   "</0/2>;ssid=102;uri=\"coaps://server_2.example.com\"",
                   out_buff, msg_len);
#else  // FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.1,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,"
                   "</0/2>;ssid=102;uri=\"coaps://server_2.example.com\"",
                   out_buff, msg_len);
#endif // FLUF_WITH_LWM2M12
}

AVS_UNIT_TEST(bootstrap_discover_payload, more_object_call) {
    fluf_io_bootstrap_discover_ctx_t ctx;
    char out_buff[200] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;
    uint16_t ssid;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_bootstrap_discover_ctx_init(&ctx, &FLUF_MAKE_ROOT_PATH()));
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 0), NULL, &ssid,
            "coaps://server_1.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL, &ssid, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(4), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(5), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;

#ifdef FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.2,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,</1/0>;ssid=101,"
                   "</3/0>,</4>,</5>",
                   out_buff, msg_len);
#else  // FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.1,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,</1/0>;ssid=101,"
                   "</3/0>,</4>,</5>",
                   out_buff, msg_len);
#endif // FLUF_WITH_LWM2M12
}

AVS_UNIT_TEST(bootstrap_discover_payload, oscore) {
    fluf_io_bootstrap_discover_ctx_t ctx;
    char out_buff[200] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;
    fluf_uri_path_t base_path = FLUF_MAKE_ROOT_PATH();
    uint16_t ssid;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_bootstrap_discover_ctx_init(&ctx, &base_path));
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 0), NULL, &ssid,
            "coaps://server_1.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    ssid = 102;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 2), NULL, &ssid,
            "coap://server_1.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(21, 0), NULL, &ssid, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    ssid = 102;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(21, 1), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(21, 2), NULL, &ssid, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
#ifdef FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.2,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,"
                   "</0/2>;ssid=102;uri=\"coap://server_1.example.com\",</21/"
                   "0>;ssid=101,</21/1>,</21/2>;ssid=102",
                   out_buff, msg_len);
#else  // FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.1,</0/0>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</0/1>,"
                   "</0/2>;ssid=102;uri=\"coap://server_1.example.com\",</21/"
                   "0>;ssid=101,</21/1>,</21/2>;ssid=102",
                   out_buff, msg_len);
#endif // FLUF_WITH_LWM2M12
}

AVS_UNIT_TEST(bootstrap_discover_payload, version) {
    fluf_io_bootstrap_discover_ctx_t ctx;
    char out_buff[200] = { 0 };
    size_t copied_bytes = 0;
    size_t msg_len = 0;
    fluf_uri_path_t base_path = FLUF_MAKE_ROOT_PATH();
    uint16_t ssid;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_bootstrap_discover_ctx_init(&ctx, &base_path));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 1), NULL, &ssid,
            "coaps://server_1.example.com"));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL, &ssid, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(3, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(4, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(5), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(55), "1.9", NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(55, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_get_payload(
            &ctx, &out_buff[msg_len], 200, &copied_bytes));
    msg_len += copied_bytes;

#ifdef FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.2,</0/0>,</0/1>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</1/0>;ssid=101,</3/0>,</4/"
                   "0>,</5>,</55>;ver=1.9,</55/0>",
                   out_buff, msg_len);
#else  // FLUF_WITH_LWM2M12
    VERIFY_PAYLOAD("</>;lwm2m=1.1,</0/0>,</0/1>;ssid=101;uri=\"coaps://"
                   "server_1.example.com\",</1/0>;ssid=101,</3/0>,</4/"
                   "0>,</5>,</55>;ver=1.9,</55/0>",
                   out_buff, msg_len);
#endif // FLUF_WITH_LWM2M12
}

AVS_UNIT_TEST(bootstrap_discover_payload, errors) {
    fluf_io_bootstrap_discover_ctx_t ctx;
    fluf_uri_path_t base_path = FLUF_MAKE_OBJECT_PATH(1);
    uint16_t ssid;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_bootstrap_discover_ctx_init(&ctx, &base_path));

    AVS_UNIT_ASSERT_FAILED(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_FAILED(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_OBJECT_PATH(1), ".", NULL, NULL));

    AVS_UNIT_ASSERT_FAILED(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 0), NULL, NULL, NULL));
    AVS_UNIT_ASSERT_FAILED(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL, NULL, NULL));
    ssid = 101;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
            &ctx, &FLUF_MAKE_INSTANCE_PATH(1, 0), NULL, &ssid, NULL));
}

AVS_UNIT_TEST(bootstrap_discover_payload, block_transfer) {
    for (size_t i = 5; i < 75; i++) {
        fluf_io_bootstrap_discover_ctx_t ctx;
        char out_buff[200] = { 0 };
        uint16_t ssid = 65534;
        const char *uri = "coaps://server_1.example.com";
        fluf_io_bootstrap_discover_ctx_init(&ctx, &FLUF_MAKE_ROOT_PATH());
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_bootstrap_discover_ctx_new_entry(
                &ctx, &FLUF_MAKE_INSTANCE_PATH(0, 65534), NULL, &ssid, uri));

        int res = -1;
        size_t copied_bytes = 0;
        size_t msg_len = 0;
        while (res) {
            res = fluf_io_bootstrap_discover_ctx_get_payload(
                    &ctx, &out_buff[msg_len], i, &copied_bytes);
            msg_len += copied_bytes;
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
        }
#ifdef FLUF_WITH_LWM2M12
        VERIFY_PAYLOAD("</>;lwm2m=1.2,</0/65534>;ssid=65534;uri=\"coaps://"
                       "server_1.example.com\"",
                       out_buff, msg_len);
#else  // FLUF_WITH_LWM2M12
        VERIFY_PAYLOAD("</>;lwm2m=1.1,</0/65534>;ssid=65534;uri=\"coaps://"
                       "server_1.example.com\"",
                       out_buff, msg_len);
#endif // FLUF_WITH_LWM2M12
    }
}
