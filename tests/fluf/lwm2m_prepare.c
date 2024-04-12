/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf.h>

AVS_UNIT_TEST(fluf_prepare, prepare_register) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_REGISTER;
    data.content_format = FLUF_COAP_FORMAT_LINK_FORMAT;
    data.payload = "<1/1>";
    data.payload_size = 5;

    data.attr.register_attr.has_endpoint = true;
    data.attr.register_attr.has_lifetime = true;
    data.attr.register_attr.has_lwm2m_ver = true;
    data.attr.register_attr.has_Q = true;
    data.attr.register_attr.endpoint = "name";
    data.attr.register_attr.lifetime = 120;
    data.attr.register_attr.lwm2m_ver = "1.2";

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] =
            "\x48"         // Confirmable, tkl 8
            "\x02\x00\x01" // POST 0x02, msg id 0001 because fluf_init is not
                           // called
            "\x00\x00\x00\x00\x00\x00\x00\x00" // token
            "\xb2\x72\x64"                     // uri path /rd
            "\x11\x28" // content_format: application/link-format
            "\x37\x65\x70\x3d\x6e\x61\x6d\x65"         // uri-query ep=name
            "\x06\x6c\x74\x3d\x31\x32\x30"             // uri-query lt=120
            "\x09\x6c\x77\x6d\x32\x6d\x3d\x31\x2e\x32" // uri-query lwm2m=1.2
            "\x01\x51"                                 // uri-query Q
            "\xFF"
            "\x3c\x31\x2f\x31\x3e";
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 50);
}

AVS_UNIT_TEST(fluf_prepare, prepare_update) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_UPDATE;
    data.location_path.location[0] = "name";
    data.location_path.location_len[0] = 4;
    data.location_path.location_count = 1;

    data.attr.register_attr.has_sms_number = true;
    data.attr.register_attr.has_binding = true;
    data.attr.register_attr.binding = "U";

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x48"         // Confirmable, tkl 8
                         "\x02\x00\x02" // POST 0x02, msg id 0002
                         "\x00\x00\x00\x00\x00\x00\x00\x00" // token
                         "\xb2\x72\x64"                     // uri path /rd
                         "\x04\x6e\x61\x6d\x65"             // uri path /name
                         "\x43\x62\x3d\x55"                 // uri-query b=U
                         "\x03\x73\x6d\x73"                 // uri-query sms
            ;
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 28);
}

AVS_UNIT_TEST(fluf_prepare, prepare_deregister) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_DEREGISTER;
    data.location_path.location[0] = "name";
    data.location_path.location_len[0] = 4;
    data.location_path.location_count = 1;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x48"         // Confirmable, tkl 8
                         "\x04\x00\x03" // DELETE 0x04, msg id 0003
                         "\x00\x00\x00\x00\x00\x00\x00\x00" // token
                         "\xb2\x72\x64"                     // uri path /rd
                         "\x04\x6e\x61\x6d\x65"             // uri path /name
            ;
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 20);
}

AVS_UNIT_TEST(fluf_prepare, prepare_bootstrap_request) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_BOOTSTRAP_REQ;

    data.attr.bootstrap_attr.has_endpoint = true;
    data.attr.bootstrap_attr.has_pct = true;
    data.attr.bootstrap_attr.endpoint = "name";
    data.attr.bootstrap_attr.pct = 60;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x48"         // Confirmable, tkl 8
                         "\x02\x00\x04" // POST 0x02, msg id 0004
                         "\x00\x00\x00\x00\x00\x00\x00\x00" // token
                         "\xb2\x62\x73"                     // uri path /bs
                         "\x47\x65\x70\x3d\x6e\x61\x6d\x65" // uri-query ep=name
                         "\x06\x70\x63\x74\x3d\x36\x30"     // uri-query pct=60
            ;
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 30);
}

AVS_UNIT_TEST(fluf_prepare, prepare_bootstrap_pack_request) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_BOOTSTRAP_PACK_REQ;
    data.accept = FLUF_COAP_FORMAT_SENML_ETCH_JSON;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x48"         // Confirmable, tkl 8
                         "\x01\x00\x05" // GET 0x01, msg id 0005
                         "\x00\x00\x00\x00\x00\x00\x00\x00" // token
                         "\xb6\x62\x73\x70\x61\x63\x6b"     // uri path /bspack
                         "\x62\x01\x40"                     // accept /ETCH_JSON

            ;
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 22);
}

AVS_UNIT_TEST(fluf_prepare, prepare_non_con_notify) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_INF_NON_CON_NOTIFY;
    data.coap.coap_udp.token.size = 2;
    data.coap.coap_udp.token.bytes[0] = 0x44;
    data.coap.coap_udp.token.bytes[1] = 0x44;
    data.content_format = 0;
    data.observe_number = 0x2233;
    data.payload_size = 3;
    data.payload = "211";

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x52"         // NonConfirmable, tkl 2
                         "\x45\x00\x06" // CONTENT 2.5 msg id 0006
                         "\x44\x44"     // token
                         "\x62\x22\x33" // observe 0x2233
                         "\x60"         // content-format 0
                         "\xFF"
                         "\x32\x31\x31"

            ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 14);
}

AVS_UNIT_TEST(fluf_prepare, prepare_send) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_INF_CON_SEND;
    data.content_format = FLUF_COAP_FORMAT_OPAQUE_STREAM;
    data.payload = "<1/1>";
    data.payload_size = 5;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x48"         // Confirmable, tkl 8
                         "\x02\x00\x07" // POST 0x02, msg id 0007
                         "\x00\x00\x00\x00\x00\x00\x00\x00" // token
                         "\xb2\x64\x70"                     // uri path /dp
                         "\x11\x2A" // content_format: octet-stream
                         "\xFF"
                         "\x3c\x31\x2f\x31\x3e";
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 23);
}

AVS_UNIT_TEST(fluf_prepare, prepare_non_con_send) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_INF_NON_CON_SEND;
    data.content_format = FLUF_COAP_FORMAT_OPAQUE_STREAM;
    data.payload = "<1/1>";
    data.payload_size = 5;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x58"         // NonConfirmable, tkl 8
                         "\x02\x00\x08" // POST 0x02, msg id 0008
                         "\x00\x00\x00\x00\x00\x00\x00\x00" // token
                         "\xb2\x64\x70"                     // uri path /dp
                         "\x11\x2A" // content_format: octet-stream
                         "\xFF"
                         "\x3c\x31\x2f\x31\x3e";
    memcpy(&EXPECTED[4], data.coap.coap_udp.token.bytes, 8); // copy token

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 23);
}

AVS_UNIT_TEST(fluf_prepare, prepare_con_notify) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_INF_CON_NOTIFY;
    data.coap.coap_udp.token.size = 2;
    data.coap.coap_udp.token.bytes[0] = 0x44;
    data.coap.coap_udp.token.bytes[1] = 0x44;
    data.content_format = 0;
    data.observe_number = 0x2233;
    data.payload_size = 3;
    data.payload = "211";

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x42"         // Confirmable, tkl 2
                         "\x45\x00\x09" // CONTENT 2.5 msg id 0009
                         "\x44\x44"     // token
                         "\x62\x22\x33" // observe 0x2233
                         "\x60"         // content-format 0
                         "\xFF"
                         "\x32\x31\x31"

            ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 14);
}

AVS_UNIT_TEST(fluf_prepare, prepare_response) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_RESPONSE;
    data.msg_code = FLUF_COAP_CODE_CREATED;
    // msd_id and token are normally taken from request
    data.coap.coap_udp.message_id = 0x2222;
    data.coap.coap_udp.token.size = 3;
    data.coap.coap_udp.token.bytes[0] = 0x11;
    data.coap.coap_udp.token.bytes[1] = 0x22;
    data.coap.coap_udp.token.bytes[2] = 0x33;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x63"         // ACK, tkl 3
                         "\x41\x22\x22" // CREATED 0x41
                         "\x11\x22\x33" // token
            ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 7);
}

AVS_UNIT_TEST(fluf_prepare, prepare_response_with_etag) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_RESPONSE;
    data.msg_code = FLUF_COAP_CODE_CREATED;
    // msd_id and token are normally taken from request
    data.coap.coap_udp.message_id = 0x2222;
    data.coap.coap_udp.token.size = 3;
    data.coap.coap_udp.token.bytes[0] = 0x11;
    data.coap.coap_udp.token.bytes[1] = 0x22;
    data.coap.coap_udp.token.bytes[2] = 0x33;
    data.etag.bytes[0] = '3';
    data.etag.bytes[1] = '3';
    data.etag.bytes[2] = '2';
    data.etag.size = 3;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x63"             // ACK, tkl 3
                         "\x41\x22\x22"     // CREATED 0x41
                         "\x11\x22\x33"     // token
                         "\x43\x33\x33\x32" // etag 3 332
            ;

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 11);
}

AVS_UNIT_TEST(fluf_prepare, prepare_response_with_payload) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_RESPONSE;
    data.msg_code = FLUF_COAP_CODE_CONTENT;
    data.content_format = FLUF_COAP_FORMAT_CBOR;
    data.payload_size = 5;
    data.payload = "00000";

    data.coap.coap_udp.message_id = 0x2222;
    data.coap.coap_udp.token.size = 3;
    data.coap.coap_udp.token.bytes[0] = 0x11;
    data.coap.coap_udp.token.bytes[1] = 0x22;
    data.coap.coap_udp.token.bytes[2] = 0x33;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x63"         // ACK, tkl 3
                         "\x45\x22\x22" // CONTENT 0x45
                         "\x11\x22\x33" // token
                         "\xC1\x3C"     // content_format: cbor
                         "\xFF"
                         "\x30\x30\x30\x30\x30";

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 15);
}

AVS_UNIT_TEST(fluf_prepare, prepare_response_with_block) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_RESPONSE;
    data.msg_code = FLUF_COAP_CODE_CONTENT;
    data.payload_size = 5;
    data.payload = "00000";

    data.block.block_type = FLUF_OPTION_BLOCK_2;
    data.block.size = 512;
    data.block.number = 132;
    data.block.more_flag = true;

    data.coap.coap_udp.message_id = 0x2222;
    data.coap.coap_udp.token.size = 3;
    data.coap.coap_udp.token.bytes[0] = 0x11;
    data.coap.coap_udp.token.bytes[1] = 0x22;
    data.coap.coap_udp.token.bytes[2] = 0x33;

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_msg_prepare(&data, buff, sizeof(buff), &out_msg_size));

    uint8_t EXPECTED[] = "\x63"         // ACK, tkl 3
                         "\x45\x22\x22" // CONTENT 0x45
                         "\x11\x22\x33" // token
                         "\xC0"         // CONTENT_FORMAT 12
                         "\xb2\x08\x4D" // block2 23
                         "\xFF"
                         "\x30\x30\x30\x30\x30";

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(buff, EXPECTED, sizeof(EXPECTED) - 1);
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 17);
}

AVS_UNIT_TEST(fluf_prepare, prepare_error_buff_size) {
    fluf_data_t data;
    memset(&data, 0, sizeof(fluf_data_t));
    uint8_t buff[100];
    size_t out_msg_size;

    data.binding = FLUF_BINDING_UDP;
    data.operation = FLUF_OP_REGISTER;
    data.content_format = FLUF_COAP_FORMAT_LINK_FORMAT;
    data.payload = "<1/1><1/1>";
    data.payload_size = 10;
    data.attr.register_attr.has_endpoint = true;
    data.attr.register_attr.has_lifetime = true;
    data.attr.register_attr.has_lwm2m_ver = true;
    data.attr.register_attr.has_Q = true;
    data.attr.register_attr.endpoint = "name";
    data.attr.register_attr.lifetime = 120;
    data.attr.register_attr.lwm2m_ver = "1.2";

    for (size_t i = 1; i < 55; i++) {
        AVS_UNIT_ASSERT_FAILED(fluf_msg_prepare(&data, buff, i, &out_msg_size));
    }
    AVS_UNIT_ASSERT_SUCCESS(fluf_msg_prepare(&data, buff, 55, &out_msg_size));
    AVS_UNIT_ASSERT_EQUAL(out_msg_size, 55);
}
