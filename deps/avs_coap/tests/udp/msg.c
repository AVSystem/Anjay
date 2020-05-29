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

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)

#    include <avsystem/commons/avs_memory.h>

#    define AVS_UNIT_ENABLE_SHORT_ASSERTS
#    include <avsystem/commons/avs_unit_test.h>

#    include <avsystem/coap/code.h>

#    include "options/avs_coap_option.h"
#    include "udp/avs_coap_udp_msg.h"

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

static void free_ptr(void **p) {
    avs_free(*p);
}

AVS_UNIT_TEST(coap_udp_serialize, header) {
    static const size_t buf_size = sizeof(avs_coap_udp_header_t);
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ 0,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506)
    };

    size_t written;
    ASSERT_OK(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
    ASSERT_EQ(buf_size, written);

    //      version
    //      |  type
    //      |  |  token length
    //      v  v  v     .- code .  .-- message id --.
    //      01 10 0000  011 00100  00000101  00000110
    // hex:     6    0      6   4     0   5     0   6
    ASSERT_EQ_BYTES(buf, "\x60\x64\x05\x06");
}

AVS_UNIT_TEST(coap_udp_serialize, header_and_token) {
#    define TOKEN_BYTES "\x01\x02\x03\x04\x05\x06\x07"
    static const avs_coap_token_t token = {
        .size = 7,
        .bytes = TOKEN_BYTES
    };
    static const size_t buf_size =
            sizeof(avs_coap_udp_header_t) + sizeof(TOKEN_BYTES) - 1;
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_RESET, token.size,
                                            AVS_COAP_CODE(7, 31),
                                            /* msg_id = */ 0xffff),
        .token = token
    };

    size_t written;
    ASSERT_OK(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
    ASSERT_EQ(buf_size, written);

    //      version
    //      |  type
    //      |  |  token length
    //      v  v  v     .- code .  .-- message id --.
    //      01 11 0111  111 11111  11111111  11111111
    // hex:     7    7      f   f     f   f     f   f
    ASSERT_EQ_BYTES(buf, "\x77\xff\xff\xff" TOKEN_BYTES);
#    undef TOKEN_BYTES
}

AVS_UNIT_TEST(coap_udp_serialize, header_and_payload) {
#    define CONTENT "http://www.staggeringbeauty.com/"
    uint8_t content[] = CONTENT;

    const size_t buf_size =
            (sizeof(avs_coap_udp_header_t) + sizeof(AVS_COAP_PAYLOAD_MARKER)
             + sizeof(content) - 1);
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ 0,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506),
        .payload = content,
        .payload_size = sizeof(content) - 1
    };

    size_t written;
    ASSERT_OK(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));

    ASSERT_EQ(written, buf_size);
    ASSERT_EQ_BYTES(buf, "\x60\x64\x05\x06"
                         "\xff" CONTENT);
#    undef CONTENT
}

AVS_UNIT_TEST(coap_msg_serialize, buffer_too_small_for_header) {
    const size_t buf_size = sizeof(avs_coap_udp_header_t) - 1;
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ 0,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506)
    };

    size_t written;
    ASSERT_FAIL(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
}

AVS_UNIT_TEST(coap_msg_serialize, buffer_too_small_for_token) {
#    define TOKEN_BYTES "\x01\x02\x03\x04\x05\x06\x07"
    static const avs_coap_token_t token = {
        .size = 7,
        .bytes = TOKEN_BYTES
    };
    static const size_t buf_size =
            sizeof(avs_coap_udp_header_t) + sizeof(TOKEN_BYTES) - 2;
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ token.size,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506),
        .token = token
    };

    size_t written;
    ASSERT_FAIL(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
#    undef TOKEN_BYTES
}

AVS_UNIT_TEST(coap_msg_serialize, buffer_too_small_for_options) {
    static const size_t buf_size = sizeof(avs_coap_udp_header_t);
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    uint8_t opts_buf[128];
    avs_coap_options_t opts =
            avs_coap_options_create_empty(opts_buf, sizeof(opts_buf));
    ASSERT_OK(avs_coap_options_add_empty(&opts, 0));

    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ 0,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506),
        .options = opts
    };

    size_t written;
    ASSERT_FAIL(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
}

AVS_UNIT_TEST(coap_msg_serialize, buffer_too_small_for_payload_marker) {
    static const size_t buf_size = sizeof(avs_coap_udp_header_t);
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ 0,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506),
        .payload = "such pay, very load",
        .payload_size = sizeof("such pay, very load")
    };

    size_t written;
    ASSERT_FAIL(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
}

AVS_UNIT_TEST(coap_msg_serialize, buffer_too_small_for_payload_content) {
    static const size_t buf_size =
            sizeof(avs_coap_udp_header_t) + sizeof(AVS_COAP_PAYLOAD_MARKER);
    void *buf __attribute__((cleanup(free_ptr))) = avs_calloc(1, buf_size);

    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            /* token length = */ 0,
                                            AVS_COAP_CODE(3, 4),
                                            /* msg_id = */ 0x0506),
        .payload = "such pay, very load",
        .payload_size = sizeof("such pay, very load")
    };

    size_t written;
    ASSERT_FAIL(_avs_coap_udp_msg_serialize(&msg, buf, buf_size, &written));
}

static inline void assert_header_eq(const avs_coap_udp_header_t *a,
                                    const avs_coap_udp_header_t *b) {
    ASSERT_EQ(_avs_coap_udp_header_get_version(a),
              _avs_coap_udp_header_get_version(b));
    ASSERT_EQ(_avs_coap_udp_header_get_type(a),
              _avs_coap_udp_header_get_type(b));
    ASSERT_EQ(_avs_coap_udp_header_get_token_length(a),
              _avs_coap_udp_header_get_token_length(b));
    ASSERT_EQ(_avs_coap_udp_header_get_id(a), _avs_coap_udp_header_get_id(b));
}

AVS_UNIT_TEST(coap_udp_parse, header_valid) {
    const uint8_t MSG[] = "\x60\x64\x05\x06";

    avs_coap_udp_msg_t msg;
    ASSERT_OK(_avs_coap_udp_msg_parse(&msg, MSG, sizeof(MSG) - 1));

    const avs_coap_udp_header_t expected_hdr =
            _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                      /* token length = */ 0,
                                      AVS_COAP_CODE(3, 4),
                                      /* msg_id = */ 0x0506);

    assert_header_eq(&msg.header, &expected_hdr);
    ASSERT_EQ(msg.token.size, 0);
    ASSERT_EQ(msg.options.size, 0);
    ASSERT_EQ(msg.payload_size, 0);
}

AVS_UNIT_TEST(coap_udp_parse, header_invalid_version) {
    avs_coap_udp_msg_t msg;

    const uint8_t MSG_V0[] = "\x20\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_V0, sizeof(MSG_V0) - 1));

    const uint8_t MSG_V2[] = "\xa0\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_V2, sizeof(MSG_V2) - 1));

    const uint8_t MSG_V3[] = "\xc0\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_V3, sizeof(MSG_V3) - 1));
}

AVS_UNIT_TEST(coap_udp_parse, header_invalid_token_length) {
    avs_coap_udp_msg_t msg;

    const uint8_t MSG_9B_TOKEN[32] = "\x69\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_9B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 9));

    const uint8_t MSG_10B_TOKEN[32] = "\x6a\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_10B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 10));

    const uint8_t MSG_11B_TOKEN[32] = "\x6b\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_11B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 11));

    const uint8_t MSG_12B_TOKEN[32] = "\x6c\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_12B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 12));

    const uint8_t MSG_13B_TOKEN[32] = "\x6d\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_13B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 13));

    const uint8_t MSG_14B_TOKEN[32] = "\x6e\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_14B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 14));

    const uint8_t MSG_15B_TOKEN[32] = "\x6f\x64\x05\x06";
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, MSG_15B_TOKEN,
                                        sizeof(avs_coap_udp_header_t) + 15));
}

AVS_UNIT_TEST(coap_msg_parse, request_code_on_ack) {
    const test_msg_t *test = COAP_MSG(ACK, GET, NO_PAYLOAD);

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, test->data, test->size));
}

AVS_UNIT_TEST(coap_msg_parse, reset_non_empty) {
    const test_msg_t *test = COAP_MSG(RST, GET, NO_PAYLOAD);

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, test->data, test->size));
}

AVS_UNIT_TEST(coap_msg_parse, reset_empty) {
    const test_msg_t *test = COAP_MSG(RST, EMPTY, NO_PAYLOAD);

    avs_coap_udp_msg_t msg;
    ASSERT_OK(_avs_coap_udp_msg_parse(&msg, test->data, test->size));

    const avs_coap_udp_header_t expected_hdr =
            _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_RESET,
                                      /* token length = */ 0,
                                      AVS_COAP_CODE_EMPTY,
                                      /* msg_id = */ 0);
    assert_header_eq(&msg.header, &expected_hdr);
}

AVS_UNIT_TEST(coap_msg_parse, empty) {
    const test_msg_t *test = COAP_MSG(CON, EMPTY, NO_PAYLOAD);

    avs_coap_udp_msg_t msg;
    ASSERT_OK(_avs_coap_udp_msg_parse(&msg, test->data, test->size));
}

AVS_UNIT_TEST(coap_msg_parse, empty_with_token) {
    const test_msg_t *test = COAP_MSG(CON, EMPTY, TOKEN(MAKE_TOKEN("A token")));

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, test->data, test->size));
}

AVS_UNIT_TEST(coap_msg_parse, empty_with_options) {
    const test_msg_t *test = COAP_MSG(CON, EMPTY, CONTENT_FORMAT_VALUE(1));

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, test->data, test->size));
}

AVS_UNIT_TEST(coap_msg_parse, empty_with_payload) {
    const test_msg_t *test = COAP_MSG(CON, EMPTY, PAYLOAD("http://doger.io"));

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, test->data, test->size));
}

AVS_UNIT_TEST(coap_msg_parse, token) {
    const test_msg_t *test = COAP_MSG(CON, GET, TOKEN(MAKE_TOKEN("A token")));

    avs_coap_udp_msg_t msg;
    ASSERT_OK(_avs_coap_udp_msg_parse(&msg, test->data, test->size));

    ASSERT_EQ(msg.token.size, sizeof("A token") - 1);
    ASSERT_EQ_BYTES(msg.token.bytes, "A token");

    ASSERT_EQ(msg.options.size, 0);
    ASSERT_EQ(msg.payload_size, 0);
}

#    define CON_GET_ID_0_EMPTY_TOKEN "\x40\x01\x00\x00"
#    define CON_GET_ID_0_8B_TOKEN "\x48\x01\x00\x00"

AVS_UNIT_TEST(coap_msg_parse, opt_length_overflow) {
    // CoAP options are limited to 16-bit unsigned integers
    // 2-byte extended option delta + 0xffff = 13 + 256 + 0xffff > 0xffff
    uint8_t packet[] = CON_GET_ID_0_EMPTY_TOKEN "\xe0\xff\xff";

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg_parse, payload_marker_but_no_payload) {
    // Payload marker MUST be omitted in case of empty packets
    uint8_t packet[] = CON_GET_ID_0_EMPTY_TOKEN "\xff";

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg_parse, token_options_payload) {
    uint8_t packet[] =
            CON_GET_ID_0_8B_TOKEN "\x01\x02\x03\x04\x05\x06\x07\x08" // token
                                                                     // options
                                  "\x00"         // num delta 0, length 0
                                  "\xd0\x00"     // num delta 13, length 0
                                  "\xe0\x00\x00" // num delta 13+256, length 0
                                  "\xff"         // payload marker
                                  "foo bar baz"; // payload

    avs_coap_udp_msg_t msg;
    ASSERT_OK(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));

    ASSERT_EQ(msg.token.size, 8);
    ASSERT_EQ_BYTES(msg.token.bytes, "\x01\x02\x03\x04\x05\x06\x07\x08");

    uint32_t value;
    ASSERT_OK(avs_coap_options_get_u32(&msg.options, 0, &value));
    ASSERT_EQ(value, 0);
    value = 0xDEADBEEF;
    ASSERT_OK(avs_coap_options_get_u32(&msg.options, 13, &value));
    ASSERT_EQ(value, 0);
    value = 0xDEADBEEF;
    ASSERT_OK(avs_coap_options_get_u32(&msg.options, 13 + 13 + 256, &value));
    ASSERT_EQ(value, 0);

    ASSERT_EQ(msg.payload_size, sizeof("foo bar baz") - 1);
    ASSERT_EQ_BYTES(msg.payload, "foo bar baz");
}

AVS_UNIT_TEST(coap_msg_parse, max_valid_option_number) {
    uint8_t packet[] = CON_GET_ID_0_EMPTY_TOKEN
            "\xe0\xfe\xf2"; // num 13 + 256 + 65266 = 65535

    avs_coap_udp_msg_t msg;
    ASSERT_OK(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg_parse, invalid_option_number) {
    uint8_t packet[] = CON_GET_ID_0_EMPTY_TOKEN
            "\xe0\xfe\xf3"; // num 13 + 256 + 65267 = 65536

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg_parse, invalid_option_number_sum) {
    uint8_t packet[] = CON_GET_ID_0_EMPTY_TOKEN
            "\xe0\xfe\xf2" // num 13 + 256 + 65266 = 65535
            "\x10";        // num 65536 (+1)

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg, fuzz_1_missing_token) {
    uint8_t packet[] = "\x68\x64\x05\x06\x0a";

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg, fuzz_2_missing_option_ext_length) {
    uint8_t packet[] = "\x60\x64\x05\x06\xfa";

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

AVS_UNIT_TEST(coap_msg, fuzz_3_token_and_options) {
    uint8_t packet[] = "\x64\x2d\x8d\x20" // header
                       "\x50\x16\xf8\x5b" // token
                       "\x73\x77\x4c\x4f\x03\xe8\x0a";

    avs_coap_udp_msg_t msg;
    ASSERT_FAIL(_avs_coap_udp_msg_parse(&msg, packet, sizeof(packet) - 1));
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
