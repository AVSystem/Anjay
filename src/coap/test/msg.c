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

#include <alloca.h>

#include <avsystem/commons/unit/test.h>

#include "../log.h"

#define PAYLOAD_MARKER "\xFF"

#define VTTL(version, type, token_length) \
    ((((version) & 0x03) << 6) | (((type) & 0x03) << 4) | ((token_length) & 0x0f))

static void setup_msg(anjay_coap_msg_t *msg,
                      uint8_t *content,
                      size_t content_length) {
    static const anjay_coap_msg_t TEMPLATE = {
        .header = {
            .version_type_token_length = VTTL(1, ANJAY_COAP_MSG_ACKNOWLEDGEMENT, 0),
            .code = ANJAY_COAP_CODE(3, 4),
            .message_id = { 5, 6 }
        }
    };
    memset(msg, 0, sizeof(*msg) + content_length);
    memcpy(msg, &TEMPLATE, sizeof(anjay_coap_msg_t));
    assert(content || content_length == 0);
    if (content_length) {
        memcpy(msg->content, content, content_length);
    }
    msg->length = (uint32_t)(sizeof(msg->header) + content_length);
}

AVS_UNIT_TEST(coap_msg, header_memory_layout) {
    anjay_coap_msg_t msg = {
        .length = sizeof(anjay_coap_msg_header_t),
        .header = {
            .version_type_token_length = VTTL(1, ANJAY_COAP_MSG_ACKNOWLEDGEMENT, 0),
            .code = ANJAY_COAP_CODE(3, 4),
            .message_id = { 5, 6 }
        }
    };

    //      version
    //      |  type
    //      |  |  token length
    //      v  v  v     .- code .  .-- message id --.
    //      01 10 0000  011 00100  00000101  00000110
    // hex:     6    0      6   4     0   5     0   6
    AVS_UNIT_ASSERT_EQUAL_BYTES(&msg.header, "\x60\x64\x05\x06");

    msg.header.version_type_token_length = VTTL(3, ANJAY_COAP_MSG_RESET, 7);
    msg.header.code = ANJAY_COAP_CODE(7, 31);
    msg.header.message_id[0] = 255;
    msg.header.message_id[1] = 255;

    //      version
    //      |  type
    //      |  |  token length
    //      v  v  v     .- code .  .-- message id --.
    //      11 11 0111  111 11111  11111111  11111111
    // hex:     f    7      f   f     f   f     f   f
    AVS_UNIT_ASSERT_EQUAL_BYTES(&msg.header, "\xf7\xff\xff\xff");
}

AVS_UNIT_TEST(coap_msg, header_fields) {
    anjay_coap_msg_t *msg = (anjay_coap_msg_t *) alloca(sizeof(*msg));
    setup_msg(msg, NULL, 0);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_header_get_version(&msg->header), 1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_header_get_type(&msg->header), ANJAY_COAP_MSG_ACKNOWLEDGEMENT);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_header_get_token_length(&msg->header), 0);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_code_get_class(&msg->header.code), 3);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_code_get_detail(&msg->header.code), 4);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_get_id(msg), 0x0506);
}

AVS_UNIT_TEST(coap_msg, no_payload) {
    anjay_coap_msg_t *msg = (anjay_coap_msg_t *) alloca(sizeof(*msg));
    setup_msg(msg, NULL, 0);

    AVS_UNIT_ASSERT_NOT_NULL(_anjay_coap_msg_payload(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_payload_length(msg), 0);
}

AVS_UNIT_TEST(coap_msg, payload) {
    uint8_t content[] = PAYLOAD_MARKER "http://www.staggeringbeauty.com/";
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content) - 1);
    setup_msg(msg, content, sizeof(content) - 1);

    AVS_UNIT_ASSERT_NOT_NULL(_anjay_coap_msg_payload(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_payload_length(msg), sizeof(content) - 2);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(_anjay_coap_msg_payload(msg), content + 1, sizeof(content) - 2);
}

AVS_UNIT_TEST(coap_msg, options) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    uint8_t content[] = {
        [0]  = 0x00,                                                  // empty option
        [1]  = 0x10,                                                  // delta = 1
        [2]  = 0xD0, [3]         = 0x00,                              // extended delta (1b)
        [4]  = 0xE0, [5 ... 6]   = 0x00,                              // extended delta (2b)
        [7]  = 0x01, [8]         = 0x00,                              // length = 1
        [9]  = 0x0D, [10]        = 0x00, [11 ... 11+13-1]     = 0x00, // extended length (1b)
        [24] = 0x0E, [25 ... 26] = 0x00, [27 ... 27+13+256-1] = 0x00  // extended length (2b)
    };
#pragma GCC diagnostic pop

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content));
    setup_msg(msg, content, sizeof(content));

    anjay_coap_opt_iterator_t it = _anjay_coap_opt_begin(msg);
    size_t expected_opt_number = 0;
    const uint8_t *expected_opt_ptr = msg->content;

    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 1;

    expected_opt_number += 1;
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 1;

    expected_opt_number += 13;
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 2;

    expected_opt_number += 13+256;
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 3;

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 1+1;

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 1+1+13;

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_opt_end(&it));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&it), expected_opt_number);
    AVS_UNIT_ASSERT_TRUE((const uint8_t*)it.curr_opt == expected_opt_ptr);
    expected_opt_ptr += 1+2+13+256;

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_next(&it) == &it);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&it));
}

AVS_UNIT_TEST(coap_msg, validate_valid) {
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg));
    setup_msg(msg, NULL, 0);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);
}

AVS_UNIT_TEST(coap_msg, validate_empty) {
    anjay_coap_msg_t *msg = (anjay_coap_msg_t *) alloca(sizeof(*msg));
    setup_msg(msg, NULL, 0);
    msg->header.code = ANJAY_COAP_CODE_EMPTY;

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, validate_empty_with_token) {
    uint8_t content[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content));
    setup_msg(msg, content, sizeof(content));
    msg->header.code = ANJAY_COAP_CODE_EMPTY;
    msg->header.version_type_token_length = VTTL(1, 0, sizeof(content));

    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, validate_empty_with_payload) {
    uint8_t content[] = PAYLOAD_MARKER "http://doger.io";
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content) - 1);
    setup_msg(msg, content, sizeof(content) - 1);
    msg->header.code = ANJAY_COAP_CODE_EMPTY;

    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, validate_unrecognized_version) {
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg));
    setup_msg(msg, NULL, 0);

    _anjay_coap_msg_header_set_version(&msg->header, 0);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);

    _anjay_coap_msg_header_set_version(&msg->header, 2);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);

    _anjay_coap_msg_header_set_version(&msg->header, 3);
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);
}

AVS_UNIT_TEST(coap_msg, validate_with_token) {
    uint8_t content[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content));
    setup_msg(msg, content, sizeof(content));
    msg->header.version_type_token_length = VTTL(1, 0, sizeof(content));

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);
}

AVS_UNIT_TEST(coap_msg, validate_invalid_token_length) {
    uint8_t content[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content));
    setup_msg(msg, content, sizeof(content));
    msg->header.version_type_token_length = VTTL(1, 0, sizeof(content));

    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, validate_opt_length_overflow) {
    uint8_t opts[] = "\xe0\xff\xff";

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(opts) - 1);
    setup_msg(msg, opts, sizeof(opts) - 1);

    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, validate_null_opt) {
    uint8_t opts[] = "\x00";

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(opts) - 1);
    setup_msg(msg, opts, sizeof(opts) - 1);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 1);

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 0);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 1);

    _anjay_coap_opt_next(&optit);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&optit));
}

AVS_UNIT_TEST(coap_msg, validate_opt_ext_delta_byte) {
    uint8_t opts[] = "\xd0\x01";

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(opts) - 1);
    setup_msg(msg, opts, sizeof(opts) - 1);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 1);

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 1 + ANJAY_COAP_EXT_U8_BASE);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 1 + ANJAY_COAP_EXT_U8_BASE);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 2);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 1 + 1);

    _anjay_coap_opt_next(&optit);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&optit));
}

AVS_UNIT_TEST(coap_msg, validate_opt_ext_delta_short) {
    uint8_t opts[] = "\xe0\x01\x00";

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(opts) - 1);
    setup_msg(msg, opts, sizeof(opts) - 1);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 1);

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 256 + 269);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 256 + 269);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 3);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 1 + 2);

    _anjay_coap_opt_next(&optit);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&optit));
}

AVS_UNIT_TEST(coap_msg, validate_opt_ext_length_byte) {
    #define OPTS_SIZE (2 + (1 + ANJAY_COAP_EXT_U8_BASE))
    uint8_t opts[OPTS_SIZE] = "\x0d\x01";
    anjay_coap_msg_t *msg = (anjay_coap_msg_t *) alloca(sizeof(*msg) + OPTS_SIZE);
    setup_msg(msg, opts, OPTS_SIZE);
    #undef OPTS_SIZE

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 1);

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 0);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 1 + ANJAY_COAP_EXT_U8_BASE);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 2);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 1 + 1 + (1 + ANJAY_COAP_EXT_U8_BASE));

    _anjay_coap_opt_next(&optit);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&optit));
}

AVS_UNIT_TEST(coap_msg, validate_opt_ext_length_short) {
    uint8_t opts[3 + 256 + ANJAY_COAP_EXT_U16_BASE] = "\x0e\x01\x00";

    anjay_coap_msg_t *msg = (anjay_coap_msg_t *) alloca(
            sizeof(*msg) + sizeof(opts) + (256 + ANJAY_COAP_EXT_U16_BASE) - 1);
    setup_msg(msg, opts, 3 + (256 + ANJAY_COAP_EXT_U16_BASE));

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 1);

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 0);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 256 + ANJAY_COAP_EXT_U16_BASE);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 3);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 1 + 2 + (256 + ANJAY_COAP_EXT_U16_BASE));

    _anjay_coap_opt_next(&optit);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&optit));
}

AVS_UNIT_TEST(coap_msg, validate_multiple_opts) {
    uint8_t opts[] = "\x00" "\xd0\x00" "\xe0\x00\x00";

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(opts) - 1);
    setup_msg(msg, opts, sizeof(opts) - 1);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 3);

    anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);

    // opt "\x00"
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 0);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 1);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 1);

    _anjay_coap_opt_next(&optit);

    // opt "\xd0\x00"
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 13);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 13);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 2);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 2);

    _anjay_coap_opt_next(&optit);

    // opt "\xe0\x00"
    AVS_UNIT_ASSERT_TRUE(optit.msg == msg);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_number(&optit), 13 + 269);
    AVS_UNIT_ASSERT_NOT_NULL(optit.curr_opt);

    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_delta(optit.curr_opt), 269);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_content_length(optit.curr_opt), 0);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_value(optit.curr_opt) == (const uint8_t*)optit.curr_opt + 3);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_opt_sizeof(optit.curr_opt), 3);

    _anjay_coap_opt_next(&optit);
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_opt_end(&optit));
}

AVS_UNIT_TEST(coap_msg, validate_payload) {
    uint8_t content[] = PAYLOAD_MARKER "http://fuldans.se";
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content) - 1);
    setup_msg(msg, content, sizeof(content) - 1);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);
}

AVS_UNIT_TEST(coap_msg, validate_payload_marker_only) {
    uint8_t content[] = PAYLOAD_MARKER;
    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content) - 1);
    setup_msg(msg, content, sizeof(content) - 1);

    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 0);
}

AVS_UNIT_TEST(coap_msg, validate_full) {
    uint8_t content[] = "\x01\x02\x03\x04\x05\x06\x07\x08" // token
                        "\x00" "\xd0\x00" "\xe0\x00\x00" // options
                        PAYLOAD_MARKER "foo bar baz"; // content

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content));
    setup_msg(msg, content, sizeof(content));
    msg->header.version_type_token_length = VTTL(1, 0, 8);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_count_opts(msg), 3);
    AVS_UNIT_ASSERT_EQUAL(_anjay_coap_msg_payload_length(msg), sizeof("foo bar baz"));
}

AVS_UNIT_TEST(coap_msg, payload_shorter_than_4b) {
    uint8_t content[] = PAYLOAD_MARKER "kek";

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) alloca(sizeof(*msg) + sizeof(content) - 1);
    setup_msg(msg, content, sizeof(content) - 1);
    msg->header.version_type_token_length = VTTL(1, 0, 0);

    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_is_valid(msg));
    AVS_UNIT_ASSERT_TRUE(_anjay_coap_msg_payload(msg) == msg->content + 1);
}

static anjay_coap_msg_t *deserialize_msg(void *out_buffer,
                                         const char *raw_data,
                                         size_t data_size) {
    anjay_coap_msg_t *msg = (anjay_coap_msg_t*)out_buffer;
    msg->length = (uint32_t)data_size;
    memcpy(&msg->header, raw_data, data_size);
    return msg;
}
#define DESERIALIZE_MSG(Content) \
    deserialize_msg(alloca(sizeof(uint32_t) + sizeof(Content) - 1), \
                    Content, sizeof(Content) - 1)

AVS_UNIT_TEST(coap_msg, fuzz_1_missing_token) {
    anjay_coap_msg_t *msg = DESERIALIZE_MSG("\x68\x64\x05\x06\x0a");
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, fuzz_2_missing_option_ext_length) {
    anjay_coap_msg_t *msg = DESERIALIZE_MSG("\x60\x64\x05\x06\xfa");
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

AVS_UNIT_TEST(coap_msg, fuzz_3_token_and_options) {
    anjay_coap_msg_t *msg = DESERIALIZE_MSG("\x64\x2d\x8d\x20" // header
                                            "\x50\x16\xf8\x5b" // token
                                            "\x73\x77\x4c\x4f\x03\xe8\x0a");
    AVS_UNIT_ASSERT_FALSE(_anjay_coap_msg_is_valid(msg));
}

