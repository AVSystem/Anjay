/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf.h>

AVS_UNIT_TEST(fluf_decode, decode_read) {
    uint8_t MSG[] = "\x44"             // header v 0x01, Confirmable, tkl 4
                    "\x01\x21\x37"     // GET code 0.1, msg id 3721
                    "\x12\x34\x56\x78" // token
                    "\xB1\x33"         // uri-path_1 URI_PATH 11 /3
                    "\x01\x33"         // uri-path_2             /3
                    "\x02\x31\x31"     // uri-path_3             /11
                    "\x02\x31\x31"     // uri-path_4             /11
                    "\x62\x01\x40"     // accept ACCEPT 17 SENML_ETCH_JSON 320
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_READ);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 4);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 11);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[3], 11);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_SENML_ETCH_JSON);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x2137);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(data.coap.coap_udp.token.bytes,
                                      ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78 }),
                                      4);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 4);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 0);
}

AVS_UNIT_TEST(fluf_decode, decode_write_replace) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x03\x37\x21" // PUT code 0.1, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB1\x35"     // uri-path_1 URI_PATH 11 /5
                    "\x01\x30"     // uri-path_2             /0
                    "\x01\x31"     // uri-path_3             /1
                    "\x10"         // content_format 12 PLAINTEXT 0
                    "\xFF"         // payload marker
                    "\x33\x44\x55" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_WRITE_REPLACE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 0);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 1);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_PLAINTEXT);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 3);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x33);
    AVS_UNIT_ASSERT_EQUAL(data.binding, FLUF_BINDING_UDP);
}

AVS_UNIT_TEST(fluf_decode, decode_write_with_block) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x03\x37\x21" // PUT code 0.1, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB1\x35"     // uri-path_1 URI_PATH 11 /5
                    "\x01\x30"     // uri-path_2             /0
                    "\x01\x31"     // uri-path_3             /1
                    "\x10"         // content_format 12 PLAINTEXT 0
                    "\xd1\x02\xee" // BLOCK1 27 NUM:14 M:1 SZX:1024
                    "\xFF"         // payload marker
                    "\x33\x44\x55" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_WRITE_REPLACE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 0);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 1);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_PLAINTEXT);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 3);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x33);
    AVS_UNIT_ASSERT_EQUAL(data.block.block_type, FLUF_OPTION_BLOCK_1);
    AVS_UNIT_ASSERT_EQUAL(data.block.size, 1024);
    AVS_UNIT_ASSERT_EQUAL(data.block.more_flag, true);
    AVS_UNIT_ASSERT_EQUAL(data.block.number, 14);
}

AVS_UNIT_TEST(fluf_decode, decode_discover) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x01\x37\x21" // GET code 0.1, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB1\x35" // uri-path_1 URI_PATH 11 /5
                    "\x01\x35" // uri-path_2             /5
                    "\x61\x28" // accept ACCEPT 17 LINK_FORMAT 40
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_DISCOVER);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 2);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 5);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_LINK_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 0);
    AVS_UNIT_ASSERT_EQUAL(data.attr.discover_attr.has_depth, false);
}

AVS_UNIT_TEST(fluf_decode, decode_discover_with_depth) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x01\x37\x21" // GET code 0.1, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB1\x35" // uri-path_1 URI_PATH 11 /5
                    "\x01\x35" // uri-path_2             /5
                    "\x47\x64\x65\x70\x74\x68\x3d\x32" // URI_QUERY 15 depth=2
                    "\x21\x28" // accept ACCEPT 17 LINK_FORMAT 40
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_DISCOVER);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 2);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 5);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_LINK_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 0);
    AVS_UNIT_ASSERT_EQUAL(data.attr.discover_attr.has_depth, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.discover_attr.depth, 2);
}

AVS_UNIT_TEST(fluf_decode, decode_bootstrap_finish) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x02\x37\x21" // POST code 0.2, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB2\x62\x73" // uri-path_1 URI_PATH 11 /bs
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_BOOTSTRAP_FINISH);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 0);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 0);
}

AVS_UNIT_TEST(fluf_decode, decode_read_composite) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x05\x37\x21" // FETCH code 0.5, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xC0"                     // content_format 12 PLAINTEXT 0
                    "\x52\x2D\x17"             // accept 17 LWM2M_JSON 11543
                    "\xFF"                     // payload marker
                    "\x33\x44\x55\x33\x44\x55" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_READ_COMP);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 0);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_OMA_LWM2M_JSON);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_PLAINTEXT);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 6);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x33);
}

AVS_UNIT_TEST(fluf_decode, decode_observe_with_pmin_pmax) {
    uint8_t MSG[] =
            "\x48"         // header v 0x01, Confirmable, tkl 8
            "\x01\x37\x21" // GET code 0.1, msg id 2137
            "\x12\x34\x56\x78\x11\x11\x11\x11"     // token
            "\x61\x00"                             // observe 6 = 0
            "\x51\x35"                             // uri-path_1 URI_PATH 11 /5
            "\x01\x35"                             // uri-path_2             /5
            "\x01\x31"                             // uri-path_3             /1
            "\x48\x70\x6d\x69\x6e\x3d\x32\x30\x30" // URI_QUERY 15 pmin=200
            "\x09\x70\x6d\x61\x78\x3d\x34\x32\x30\x30" // URI_QUERY 15 pmax=4200
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_INF_OBSERVE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 1);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 0);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_con, false);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_min_period, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_max_period, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.min_period, 200);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.max_period, 4200);
}

AVS_UNIT_TEST(fluf_decode, decode_observe_composite_with_params) {
    uint8_t MSG[] =
            "\x48"         // header v 0x01, Confirmable, tkl 8
            "\x05\x37\x21" // FETCH code 0.5, msg id 2137
            "\x12\x34\x56\x78\x11\x11\x11\x11"         // token
            "\x61\x00"                                 // observe 6 = 0
            "\x97\x70\x6d\x69\x6e\x3d\x32\x30"         // URI_QUERY 15 pmin=20
            "\x07\x65\x70\x6d\x69\x6e\x3d\x31"         // URI_QUERY 15 epmin=1
            "\x07\x65\x70\x6d\x61\x78\x3d\x32"         // URI_QUERY 15 epmax=2
            "\x05\x63\x6f\x6e\x3d\x31"                 // URI_QUERY 15 con=1
            "\x09\x70\x6d\x61\x78\x3d\x31\x32\x30\x30" // URI_QUERY 15 pmax=1200
            "\xFF"                                     // payload marker
            "\x77\x44\x55\x33\x44\x55\x33\x33\x33\x33" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_INF_OBSERVE_COMP);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 0);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 10);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x77);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_con, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_min_period, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_min_eval_period,
                          true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_max_period, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_max_eval_period,
                          true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.min_period, 20);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.max_period, 1200);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.min_eval_period, 1);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.max_eval_period, 2);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.con, 1);
}

AVS_UNIT_TEST(fluf_decode, decode_cancel_observation) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x01\x37\x21" // GET code 0.1, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\x61\x01"                         // observe 6 = 1
                    "\x51\x35" // uri-path_1 URI_PATH 11 /5
                    "\x01\x35" // uri-path_2             /5
                    "\x01\x31" // uri-path_3             /1
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_INF_CANCEL_OBSERVE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 5);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 1);
}

AVS_UNIT_TEST(fluf_decode, decode_cancel_composite) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x05\x37\x21" // FETCH code 0.5, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11"         // token
                    "\x61\x01"                                 // observe 6 = 1
                    "\xFF"                                     // payload marker
                    "\x77\x44\x55\x33\x44\x55\x33\x33\x33\x33" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_INF_CANCEL_OBSERVE_COMP);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 0);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 10);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x77);
}

AVS_UNIT_TEST(fluf_decode, decode_write_partial) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x02\x37\x21" // POST code 0.2, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB2\x31\x35" // uri-path_1 URI_PATH 11 /15
                    "\x01\x32"     // uri-path_2             /2
                    "\x10"         // content_format 12 PLAINTEXT 0
                    "\xFF"         // payload marker
                    "\x33\x44\x55" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_WRITE_PARTIAL_UPDATE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 2);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 15);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 2);
    AVS_UNIT_ASSERT_EQUAL(data.accept, FLUF_COAP_FORMAT_NOT_DEFINED);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_PLAINTEXT);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 3);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x33);
}

AVS_UNIT_TEST(fluf_decode, decode_write_attributes) {
    uint8_t MSG[] =
            "\x48"         // header v 0x01, Confirmable, tkl 8
            "\x03\x37\x21" // PUT code 0.3, msg id 2137
            "\x12\x34\x56\x78\x11\x11\x11\x11" // token
            "\xB2\x31\x35"                     // uri-path_1 URI_PATH 11 /15
            "\x01\x32"                         // uri-path_2             /2
            "\x02\x31\x32"                     // uri-path_3             /12
            "\x47\x70\x6d\x69\x6e\x3d\x32\x30" // URI_QUERY 15 pmin=20
            "\x07\x65\x70\x6d\x69\x6e\x3d\x31" // URI_QUERY 15 epmin=1
            "\x07\x65\x70\x6d\x61\x78\x3d\x32" // URI_QUERY 15 epmax=2
            "\x05\x63\x6f\x6e\x3d\x31"         // URI_QUERY 15 con=1
            "\x07\x67\x74\x3d\x32\x2e\x38\x35" // URI_QUERY 15 gt=2.85
            "\x09\x6c\x74\x3d\x33\x33\x33\x33\x2e\x38" // URI_QUERY 15 lt=3333.8
            "\x07\x73\x74\x3d\x2D\x30\x2e\x38"         // URI_QUERY 15 st=-0.8
            "\x06\x65\x64\x67\x65\x3d\x30"             // URI_QUERY 15 edge=0
            "\x0A\x68\x71\x6d\x61\x78\x3d\x37\x37\x37\x37" // URI_QUERY 15
                                                           // hqmax=7777
            "\x09\x70\x6d\x61\x78\x3d\x31\x32\x30\x30" // URI_QUERY 15 pmax=1200
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_WRITE_ATTR);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 15);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 2);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 12);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 0);

    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_min_period, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_max_period, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_greater_than, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_less_than, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_step, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_min_eval_period,
                          true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_max_eval_period,
                          true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_edge, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_con, true);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.has_hqmax, true);

    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.min_period, 20);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.max_period, 1200);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.min_eval_period, 1);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.max_eval_period, 2);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.edge, 0);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.con, 1);
    AVS_UNIT_ASSERT_EQUAL(data.attr.notification_attr.hqmax, 7777);

    AVS_UNIT_ASSERT_EQUAL(
            (int) (100 * data.attr.notification_attr.greater_than), 285);
    AVS_UNIT_ASSERT_EQUAL((int) (100 * data.attr.notification_attr.less_than),
                          333380);
    AVS_UNIT_ASSERT_EQUAL((int) (100 * data.attr.notification_attr.step), -80);
}

AVS_UNIT_TEST(fluf_decode, decode_write_composite) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x07\x37\x21" // IPATCH code 0.7, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xC1\x3C" // content_format 12 FORMAT_CBOR 60
                    "\xFF"     // payload marker
                    "\x77\x44\x55\x33\x44\x55\x33\x33\x33\x33" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_WRITE_COMP);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 0);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_CBOR);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 10);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x77);
}

AVS_UNIT_TEST(fluf_decode, decode_execute) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x02\x37\x21" // POST code 0.2, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB2\x31\x35" // uri-path_1 URI_PATH 11 /15
                    "\x01\x32"     // uri-path_2             /2
                    "\x02\x31\x32" // uri-path_3             /12
                    "\x11\x3C"     // content_format 12 FORMAT_CBOR 60
                    "\xFF"         // payload marker
                    "\x77\x44\x55\x33\x44\x55\x33\x33\x33\x33" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_EXECUTE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 3);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 15);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 2);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[2], 12);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_CBOR);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 10);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x77);
}

AVS_UNIT_TEST(fluf_decode, decode_create) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x02\x37\x21" // POST code 0.2, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB5\x33\x33\x36\x33\x39" // uri-path_1 URI_PATH 11 /33639
                    "\x11\x3C" // content_format 12 FORMAT_CBOR 60
                    "\xFF"     // payload marker
                    "\x76\x44\x55\x33\x44\x55\x33\x33" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_CREATE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 1);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 33639);
    AVS_UNIT_ASSERT_EQUAL(data.content_format, FLUF_COAP_FORMAT_CBOR);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 8);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x76);
}

AVS_UNIT_TEST(fluf_decode, decode_delete) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Confirmable, tkl 8
                    "\x04\x37\x21" // DELETE code 0.4, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\xB5\x33\x33\x36\x33\x39" // uri-path_1 URI_PATH 11 /33639
                    "\x01\x31"                 // uri-path_1 URI_PATH 11 /1
                    "\xFF"                     // payload marker
                    "\x76\x44\x55\x33\x44\x55\x33\x33" // payload
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_DM_DELETE);
    AVS_UNIT_ASSERT_EQUAL(data.uri.uri_len, 2);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[0], 33639);
    AVS_UNIT_ASSERT_EQUAL(data.uri.ids[1], 1);
    AVS_UNIT_ASSERT_EQUAL(data.payload_size, 8);
    AVS_UNIT_ASSERT_EQUAL(((const uint8_t *) data.payload)[0], 0x76);
}

AVS_UNIT_TEST(fluf_decode, decode_response) {
    uint8_t MSG[] = "\x68"         // header v 0x01, Ack, tkl 8
                    "\x44\x37\x21" // CHANGED code 2.4, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.type,
                          FLUF_COAP_UDP_TYPE_ACKNOWLEDGEMENT);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_CHANGED);
}

AVS_UNIT_TEST(fluf_decode, decode_empty_response) {
    uint8_t MSG[] = "\x60"         // header v 0x01, Ack, tkl 8
                    "\x00\x37\x21" // empty code 2.4, msg id 2137
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.type,
                          FLUF_COAP_UDP_TYPE_ACKNOWLEDGEMENT);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_EMPTY);
}

AVS_UNIT_TEST(fluf_decode, decode_con_response) {
    uint8_t MSG[] = "\x48"         // header v 0x01, Con, tkl 8
                    "\x44\x37\x21" // CHANGED code 2.4, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.type,
                          FLUF_COAP_UDP_TYPE_CONFIRMABLE);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_CHANGED);
}

AVS_UNIT_TEST(fluf_decode, decode_non_con_response) {
    uint8_t MSG[] = "\x58"         // header v 0x01, Con, tkl 8
                    "\x44\x37\x21" // CHANGED code 2.4, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.type,
                          FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_CHANGED);
}

AVS_UNIT_TEST(fluf_decode, decode_ping) {
    uint8_t MSG[] = "\x40"         // header v 0x01, Con, tkl 8
                    "\x00\x37\x21" // CHANGED code 2.4, msg id 2137
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_COAP_PING);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.type,
                          FLUF_COAP_UDP_TYPE_CONFIRMABLE);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_EMPTY);
}

AVS_UNIT_TEST(fluf_decode, decode_response_with_etag) {
    uint8_t MSG[] = "\x68"         // header v 0x01, Ack, tkl 8
                    "\x44\x37\x21" // CHANGED code 2.4, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\x43\x33\x33\x32"                 // etag 3 332
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(data.etag.size, 3);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED("332", data.etag.bytes, 3);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_CHANGED);
}

AVS_UNIT_TEST(fluf_decode, decode_response_with_location_path) {
    uint8_t MSG[] = "\x68"         // header v 0x01, Ack, tkl 8
                    "\x41\x37\x21" // CREATED code 2.1, msg id 2137
                    "\x12\x34\x56\x78\x11\x11\x11\x11" // token
                    "\x82\x72\x64"                     // LOCATION_PATH 8 /rd
                    "\x04\x35\x61\x33\x66"             // LOCATION_PATH 8 /5a3f
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));

    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.message_id, 0x3721);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
            data.coap.coap_udp.token.bytes,
            ((uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0x11, 0x11, 0x11, 0x11 }), 8);
    AVS_UNIT_ASSERT_EQUAL(data.coap.coap_udp.token.size, 8);
    AVS_UNIT_ASSERT_EQUAL(data.operation, FLUF_OP_RESPONSE);
    AVS_UNIT_ASSERT_EQUAL(data.msg_code, FLUF_COAP_CODE_CREATED);
    AVS_UNIT_ASSERT_EQUAL(data.location_path.location_len[0], 4);
    AVS_UNIT_ASSERT_EQUAL(data.location_path.location[0][0], '5');
    AVS_UNIT_ASSERT_EQUAL(data.location_path.location_count, 1);
}

AVS_UNIT_TEST(fluf_decode, decode_error_to_long_uri) {
    uint8_t MSG[] = "\x44"             // header v 0x01, Confirmable, tkl 4
                    "\x01\x21\x37"     // GET code 0.1, msg id 3721
                    "\x12\x34\x56\x78" // token
                    "\xB1\x33"         // uri-path_1 URI_PATH 11 /3
                    "\x01\x33"         // uri-path_2             /3
                    "\x02\x31\x31"     // uri-path_3             /11
                    "\x02\x31\x31"     // uri-path_4             /11
                    "\x02\x31\x31"     // uri-path_5             /11
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_FAILED(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));
}

AVS_UNIT_TEST(fluf_decode, decode_error_incorrect_post) {
    uint8_t MSG[] = "\x44"             // header v 0x01, Confirmable, tkl 4
                    "\x02\x21\x37"     // POST code 0.2, msg id 3721
                    "\x12\x34\x56\x78" // token
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_FAILED(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));
}

AVS_UNIT_TEST(fluf_decode, decode_error_attr) {
    uint8_t MSG[] =
            "\x48"         // header v 0x01, Confirmable, tkl 8
            "\x03\x37\x21" // PUT code 0.3, msg id 2137
            "\x12\x34\x56\x78\x11\x11\x11\x11"     // token
            "\xd7\x02\x70\x6d\x69\x6e\x3d\x6e\x30" // URI_QUERY 15 pmin=n0
            ;

    fluf_data_t data;
    AVS_UNIT_ASSERT_FAILED(
            fluf_msg_decode(MSG, sizeof(MSG) - 1, FLUF_BINDING_UDP, &data));
}
