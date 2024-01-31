/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include "../../src/fluf/fluf_coap_udp_header.h"
#include "../../src/fluf/fluf_coap_udp_msg.h"
#include "../../src/fluf/fluf_options.h"

AVS_UNIT_TEST(coap_udp_msg, base_msg_serialize) {
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 2);
    char *payload = "xxx";
    fluf_coap_udp_msg_t msg = {
        .header = _fluf_coap_udp_header_init(FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE,
                                             4, FLUF_COAP_CODE_VALID, 0x2137),
        .options = &opts,
        .payload = payload,
        .payload_size = strlen(payload),
        .token = {
            .size = 4,
            .bytes = { 0x12, 0x34, 0x56, 0x78 }
        },
    };

    uint8_t msg_buff[100] = { 0 };
    size_t out_bytes_written;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_header_serialize(&msg, msg_buff, sizeof(msg_buff)));

    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_data(&opts, 5, "0", 1));
    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_string(&opts, 10, "123"));

    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_udp_msg_serialize(
            &msg, msg_buff, sizeof(msg_buff), &out_bytes_written));
    AVS_UNIT_ASSERT_EQUAL(out_bytes_written, 18);

    const uint8_t EXPECTED[] = "\x54" // header v 0x01, Non-confirmable, tkl 4
                               "\x43\x21\x37"     // code 2.3, msg id 2137
                               "\x12\x34\x56\x78" // token
                               "\x51\x30"         // opt 1
                               "\x53\x31\x32\x33" // opt2
                               "\xFF"             // payload marker
                               "\x78\x78\x78"     // payload
            ;
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg_buff, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_udp_msg, no_payload_serialize) {
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 3);
    fluf_coap_udp_msg_t msg = {
        .header = _fluf_coap_udp_header_init(FLUF_COAP_UDP_TYPE_CONFIRMABLE, 5,
                                             FLUF_COAP_CODE_GET, 0x2137),
        .options = &opts,
        .payload = NULL,
        .payload_size = 0,
        .token = {
            .size = 5,
            .bytes = "\x12\x34\x56\x78\x90"
        },
    };

    uint8_t msg_buff[100] = { 0 };
    size_t out_bytes_written;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_header_serialize(&msg, msg_buff, sizeof(msg_buff)));

    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_data(&opts, 5, "0", 1));
    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_string(&opts, 10, "123"));
    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_string(&opts, 10, "123"));

    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_udp_msg_serialize(
            &msg, msg_buff, sizeof(msg_buff), &out_bytes_written));
    AVS_UNIT_ASSERT_EQUAL(out_bytes_written, 19);

    uint8_t EXPECTED[] = "\x45"         // header v 0x01, Non-confirmable, tkl 5
                         "\x01\x21\x37" // code 0.1, msg id 2137
                         "\x12\x34\x56\x78\x90" // token
                         "\x51\x30"             // opt 1
                         "\x53\x31\x32\x33"     // opt2
                         "\x03\x31\x32\x33"     // opt3
            ;
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg_buff, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_udp_msg, zero_len_options_serialize) {
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 3);
    char *payload = "xxxxx";
    fluf_coap_udp_msg_t msg = {
        .header = _fluf_coap_udp_header_init(FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE,
                                             4, FLUF_COAP_CODE_VALID, 0x2137),
        .options = &opts,
        .payload = payload,
        .payload_size = strlen(payload),
        .token = {
            .size = 4,
            .bytes = { 0x12, 0x34, 0x56, 0x78 }
        },
    };

    uint8_t msg_buff[100] = { 0 };
    size_t out_bytes_written;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_header_serialize(&msg, msg_buff, sizeof(msg_buff)));
    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_udp_msg_serialize(
            &msg, msg_buff, sizeof(msg_buff), &out_bytes_written));
    AVS_UNIT_ASSERT_EQUAL(out_bytes_written, 14);

    const uint8_t EXPECTED[] = "\x54" // header v 0x01, Non-confirmable, tkl 4
                               "\x43\x21\x37"         // code 2.3, msg id 2137
                               "\x12\x34\x56\x78"     // token
                               "\xFF"                 // payload marker
                               "\x78\x78\x78\x78\x78" // payload
            ;
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg_buff, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_udp_msg, no_options_serialize) {
    char *payload = "xxxxx";
    fluf_coap_udp_msg_t msg = {
        .header = _fluf_coap_udp_header_init(FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE,
                                             4, FLUF_COAP_CODE_VALID, 0x2137),
        .options = NULL,
        .payload = payload,
        .payload_size = strlen(payload),
        .token = {
            .size = 4,
            .bytes = { 0x12, 0x34, 0x56, 0x78 }
        },
    };

    uint8_t msg_buff[100] = { 0 };
    size_t out_bytes_written;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_header_serialize(&msg, msg_buff, sizeof(msg_buff)));
    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_udp_msg_serialize(
            &msg, msg_buff, sizeof(msg_buff), &out_bytes_written));
    AVS_UNIT_ASSERT_EQUAL(out_bytes_written, 14);

    const uint8_t EXPECTED[] = "\x54" // header v 0x01, Non-confirmable, tkl 4
                               "\x43\x21\x37"         // code 2.3, msg id 2137
                               "\x12\x34\x56\x78"     // token
                               "\xFF"                 // payload marker
                               "\x78\x78\x78\x78\x78" // payload
            ;
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg_buff, EXPECTED, sizeof(EXPECTED) - 1);
}

AVS_UNIT_TEST(coap_udp_msg, serialize_error) {
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 2);
    char *payload = "xxx";
    fluf_coap_udp_msg_t msg = {
        .header = _fluf_coap_udp_header_init(FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE,
                                             4, FLUF_COAP_CODE_VALID, 0x2137),
        .options = &opts,
        .payload = payload,
        .payload_size = strlen(payload),
        .token = {
            .size = 4,
            .bytes = { 0x12, 0x34, 0x56, 0x78 }
        },
    };

    uint8_t msg_buff[100] = { 0 };
    size_t out_bytes_written;

    AVS_UNIT_ASSERT_FAILED(_fluf_coap_udp_header_serialize(&msg, msg_buff, 4));
    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_header_serialize(&msg, msg_buff, 15));

    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_data(&opts, 5, "0", 1));
    AVS_UNIT_ASSERT_SUCCESS(_fluf_coap_options_add_string(&opts, 10, "123"));

    // msg len = 18
    msg_buff[16] = 0xFE;
    AVS_UNIT_ASSERT_FAILED(_fluf_coap_udp_msg_serialize(&msg, msg_buff, 15,
                                                        &out_bytes_written));
    AVS_UNIT_ASSERT_EQUAL(msg_buff[16], 0xFE); // buffer is not overwritten
}

AVS_UNIT_TEST(coap_udp_msg, base_msg_parse) {

    uint8_t MSG[] = "\x54"             // header v 0x01, Non-confirmable, tkl 4
                    "\x43\x21\x37"     // code 2.3, msg id 2137
                    "\x12\x34\x56\x78" // token
                    "\x51\x30"         // opt 1
                    "\x53\x31\x32\x33" // opt2
                    "\xFF"             // payload marker
                    "\x78\x78\x78"     // payload
            ;
    fluf_coap_udp_msg_t out_msg;
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 4);
    out_msg.options = &opts;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_msg_decode(&out_msg, MSG, sizeof(MSG) - 1));

    AVS_UNIT_ASSERT_EQUAL(out_msg.header.version_type_token_length, 0x54);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.header.message_id, "\x21\x37", 2);
    AVS_UNIT_ASSERT_EQUAL(out_msg.header.code, FLUF_COAP_CODE_VALID);
    AVS_UNIT_ASSERT_EQUAL(out_msg.token.size, 4);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.token.bytes, "\x12\x34\x56\x78",
                                      4);

    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options_number, 2);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[0].option_number, 5);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[0].payload_len, 1);
    AVS_UNIT_ASSERT_EQUAL(
            ((const uint8_t *) out_msg.options->options[0].payload)[0], 0x30);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[1].option_number, 10);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[1].payload_len, 3);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.options->options[1].payload,
                                      "123", 3);

    AVS_UNIT_ASSERT_EQUAL(out_msg.payload_size, 3);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.payload, "xxx",
                                      out_msg.payload_size);
}

AVS_UNIT_TEST(coap_udp_msg, no_options_parse) {

    uint8_t MSG[] = "\x54"             // header v 0x01, Non-confirmable, tkl 4
                    "\x43\x21\x37"     // code 2.3, msg id 2137
                    "\x12\x34\x56\x78" // token
                    "\xFF"             // payload marker
                    "\x78\x78\x78\x78\x78" // payload
            ;
    fluf_coap_udp_msg_t out_msg;
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 1);
    out_msg.options = &opts;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_msg_decode(&out_msg, MSG, sizeof(MSG) - 1));

    AVS_UNIT_ASSERT_EQUAL(out_msg.header.version_type_token_length, 0x54);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.header.message_id, "\x21\x37", 2);
    AVS_UNIT_ASSERT_EQUAL(out_msg.header.code, FLUF_COAP_CODE_VALID);
    AVS_UNIT_ASSERT_EQUAL(out_msg.token.size, 4);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.token.bytes, "\x12\x34\x56\x78",
                                      4);

    AVS_UNIT_ASSERT_EQUAL(out_msg.payload_size, 5);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.payload, "xxxxx",
                                      out_msg.payload_size);
}

AVS_UNIT_TEST(coap_udp_msg, no_payload_parse) {

    uint8_t MSG[] = "\x45"         // header v 0x01, Non-confirmable, tkl 5
                    "\x01\x21\x37" // code 0.1, msg id 2137
                    "\x12\x34\x56\x78\x90" // token
                    "\x51\x30"             // opt 1
                    "\x53\x31\x32\x33"     // opt2
                    "\x03\x31\x32\x33"     // opt3
            ;
    fluf_coap_udp_msg_t out_msg;
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 4);
    out_msg.options = &opts;

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_msg_decode(&out_msg, MSG, sizeof(MSG) - 1));

    AVS_UNIT_ASSERT_EQUAL(out_msg.header.version_type_token_length, 0x45);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.header.message_id, "\x21\x37", 2);
    AVS_UNIT_ASSERT_EQUAL(out_msg.header.code, FLUF_COAP_CODE_GET);
    AVS_UNIT_ASSERT_EQUAL(out_msg.token.size, 5);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.token.bytes,
                                      "\x12\x34\x56\x78\x90", 5);

    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options_number, 3);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[0].option_number, 5);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[0].payload_len, 1);
    AVS_UNIT_ASSERT_EQUAL(
            ((const uint8_t *) out_msg.options->options[0].payload)[0], 0x30);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[1].option_number, 10);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[1].payload_len, 3);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.options->options[1].payload,
                                      "123", 3);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[2].option_number, 10);
    AVS_UNIT_ASSERT_EQUAL(out_msg.options->options[2].payload_len, 3);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(out_msg.options->options[2].payload,
                                      "123", 3);

    AVS_UNIT_ASSERT_EQUAL(out_msg.payload_size, 0);
}

AVS_UNIT_TEST(coap_udp_msg, parse_error) {

    uint8_t MSG[] = "\x54"             // header v 0x01, Non-confirmable, tkl 4
                    "\x43\x21\x37"     // code 2.3, msg id 2137
                    "\x12\x34\x56\x78" // token
                    "\x51\x30"         // opt 1
                    "\x53\x31\x32\x33" // opt2
                    "\xFF"             // payload marker
                    "\x78\x78\x78"     // payload
            ;
    fluf_coap_udp_msg_t out_msg;
    _FLUF_COAP_OPTIONS_INIT_EMPTY(opts, 2);
    out_msg.options = &opts;

    // incorrect version number
    uint8_t err1[sizeof(MSG) - 1];
    memcpy(err1, MSG, sizeof(err1));
    err1[0] = 0xD4;
    AVS_UNIT_ASSERT_FAILED(
            _fluf_coap_udp_msg_decode(&out_msg, err1, sizeof(err1)));

    // not enough space for options
    opts.options_size = 1;
    AVS_UNIT_ASSERT_FAILED(
            _fluf_coap_udp_msg_decode(&out_msg, MSG, sizeof(MSG) - 1));
    opts.options_size = 2;

    // no payload marker
    uint8_t err2[sizeof(MSG) - 1];
    memcpy(err2, MSG, sizeof(err2));
    err2[14] = 0x11;
    AVS_UNIT_ASSERT_FAILED(
            _fluf_coap_udp_msg_decode(&out_msg, err2, sizeof(err2)));

    // incorrect token length
    uint8_t err3[sizeof(MSG) - 1];
    memcpy(err3, MSG, sizeof(err3));
    err3[0] = 0x52;
    AVS_UNIT_ASSERT_FAILED(
            _fluf_coap_udp_msg_decode(&out_msg, err3, sizeof(err3)));

    AVS_UNIT_ASSERT_SUCCESS(
            _fluf_coap_udp_msg_decode(&out_msg, MSG, sizeof(MSG) - 1));
}
