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

#ifndef ANJAY_COAP_TEST_UTILS_H
#define ANJAY_COAP_TEST_UTILS_H

#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>

#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/code.h>
#include <avsystem/coap/ctx.h>

#include <anjay/download.h>
#include <anjay_modules/anjay_utils_core.h>

#include "src/core/anjay_utils_private.h"

avs_coap_token_t nth_token(uint64_t k);
avs_coap_token_t current_token(void);
void reset_token_generator(void);

typedef enum {
    COAP_TEST_MSG_CONFIRMABLE,
    COAP_TEST_MSG_NON_CONFIRMABLE,
    COAP_TEST_MSG_ACKNOWLEDGEMENT,
    COAP_TEST_MSG_RESET,

    _COAP_TEST_MSG_FIRST = COAP_TEST_MSG_CONFIRMABLE,
    _COAP_TEST_MSG_LAST = COAP_TEST_MSG_RESET
} coap_test_msg_type_t;

typedef struct {
    uint16_t msg_id;
    avs_coap_token_t token;
} coap_test_msg_identity_t;

typedef struct {
    /**
     * Length of the whole message (header + content). Does not include the
     * length field itself.
     */
    uint32_t length; // whole message (header + content)

    /**
     * A FAM containing whole CoAP message: header + token + options + payload.
     */
    uint8_t content[];
} coap_test_msg_t;

/** Serialized CoAP message header. */
typedef struct coap_header {
    uint8_t version_type_token_length;
    uint8_t code;
    uint8_t message_id[2];
} coap_test_header_t;

AVS_STATIC_ASSERT(AVS_ALIGNOF(coap_test_header_t) == 1,
                  coap_header_must_always_be_properly_aligned_t);

/** @{
 * Sanity checks that ensure no padding is inserted anywhere inside
 * @ref coap_test_header_t .
 */
AVS_STATIC_ASSERT(offsetof(coap_test_header_t, version_type_token_length) == 0,
                  vttl_field_is_at_start_of_coap_test_header_t);
AVS_STATIC_ASSERT(offsetof(coap_test_header_t, code) == 1,
                  no_padding_before_code_field_of_coap_test_header_t);
AVS_STATIC_ASSERT(offsetof(coap_test_header_t, message_id) == 2,
                  no_padding_before_message_id_field_of_coap_test_header_t);
AVS_STATIC_ASSERT(sizeof(coap_test_header_t) == 4,
                  no_padding_in_coap_test_header_t);
/** @} */

#define COAP_TEST_HEADER_SIZE sizeof(coap_test_header_t)

static inline const uint8_t *
coap_test_header_end_const(const coap_test_msg_t *msg) {
    return msg->content + COAP_TEST_HEADER_SIZE;
}

static inline uint8_t *coap_test_header_end(coap_test_msg_t *msg) {
    return msg->content + COAP_TEST_HEADER_SIZE;
}

#define COAP_TEST_FIELD_GET(field, mask, shift) (((field) & (mask)) >> (shift))
#define COAP_TEST_FIELD_SET(field, mask, shift, value) \
    ((field) = (uint8_t) (((field) & ~(mask))          \
                          | (uint8_t) (((value) << (shift)) & (mask))))

static inline uint16_t extract_u16(const uint8_t *data) {
    uint16_t result;
    memcpy(&result, data, sizeof(uint16_t));
    return avs_convert_be16(result);
}

#define COAP_TEST_HEADER_VERSION_MASK 0xC0
#define COAP_TEST_HEADER_VERSION_SHIFT 6

static inline uint8_t coap_test_header_get_version(const coap_test_msg_t *msg) {
    const coap_test_header_t *hdr = (const coap_test_header_t *) msg->content;
    int val = COAP_TEST_FIELD_GET(hdr->version_type_token_length,
                                  COAP_TEST_HEADER_VERSION_MASK,
                                  COAP_TEST_HEADER_VERSION_SHIFT);
    assert(val >= 0 && val <= 3);
    return (uint8_t) val;
}

static inline void coap_test_header_set_version(coap_test_msg_t *msg,
                                                uint8_t version) {
    assert(version <= 3);
    coap_test_header_t *hdr = (coap_test_header_t *) msg->content;
    COAP_TEST_FIELD_SET(hdr->version_type_token_length,
                        COAP_TEST_HEADER_VERSION_MASK,
                        COAP_TEST_HEADER_VERSION_SHIFT, version);
}

#define COAP_TEST_HEADER_TOKEN_LENGTH_MASK 0x0F
#define COAP_TEST_HEADER_TOKEN_LENGTH_SHIFT 0

static inline uint8_t
coap_test_header_get_token_length(const coap_test_msg_t *msg) {
    const coap_test_header_t *hdr = (const coap_test_header_t *) msg->content;
    int val = COAP_TEST_FIELD_GET(hdr->version_type_token_length,
                                  COAP_TEST_HEADER_TOKEN_LENGTH_MASK,
                                  COAP_TEST_HEADER_TOKEN_LENGTH_SHIFT);
    assert(val >= 0 && val <= COAP_TEST_HEADER_TOKEN_LENGTH_MASK);
    return (uint8_t) val;
}

static inline void coap_test_header_set_token_length(coap_test_msg_t *msg,
                                                     uint8_t token_length) {
    assert(token_length <= AVS_COAP_MAX_TOKEN_LENGTH);
    coap_test_header_t *hdr = (coap_test_header_t *) msg->content;
    COAP_TEST_FIELD_SET(hdr->version_type_token_length,
                        COAP_TEST_HEADER_TOKEN_LENGTH_MASK,
                        COAP_TEST_HEADER_TOKEN_LENGTH_SHIFT, token_length);
}

/** @{
 * Used for retrieving CoAP message type from @ref coap_msg_header_t .
 */
#define COAP_TEST_HEADER_TYPE_MASK 0x30
#define COAP_TEST_HEADER_TYPE_SHIFT 4
/** @} */

static inline coap_test_msg_type_t
coap_test_header_get_type(const coap_test_msg_t *msg) {
    const coap_test_header_t *hdr = (const coap_test_header_t *) msg->content;
    int val = COAP_TEST_FIELD_GET(hdr->version_type_token_length,
                                  COAP_TEST_HEADER_TYPE_MASK,
                                  COAP_TEST_HEADER_TYPE_SHIFT);
    assert(val >= _COAP_TEST_MSG_FIRST && val <= _COAP_TEST_MSG_LAST);
    return (coap_test_msg_type_t) val;
}

static inline void coap_test_header_set_type(coap_test_msg_t *msg,
                                             coap_test_msg_type_t type) {
    coap_test_header_t *hdr = (coap_test_header_t *) msg->content;
    COAP_TEST_FIELD_SET(hdr->version_type_token_length,
                        COAP_TEST_HEADER_TYPE_MASK, COAP_TEST_HEADER_TYPE_SHIFT,
                        type);
}

static inline uint8_t coap_test_header_get_code(const coap_test_msg_t *msg) {
    return msg->content[offsetof(coap_test_header_t, code)];
}

static inline void coap_test_header_set_code(coap_test_msg_t *msg,
                                             uint8_t code) {
    msg->content[offsetof(coap_test_header_t, code)] = code;
}

static inline uint16_t coap_test_header_get_id(const coap_test_msg_t *msg) {
    return extract_u16(&msg->content[offsetof(coap_test_header_t, message_id)]);
}

static inline void coap_test_header_set_id(coap_test_msg_t *msg,
                                           uint16_t msg_id) {
    uint16_t msg_id_nbo = avs_convert_be16(msg_id);
    memcpy(&msg->content[offsetof(coap_test_header_t, message_id)], &msg_id_nbo,
           sizeof(msg_id_nbo));
}

struct coap_msg_args {
    coap_test_msg_type_t type;
    uint8_t code;
    coap_test_msg_identity_t id;

    bool has_raw_token;
    avs_coap_token_t token;

    // The array is needed to set token with maximum length by convenient ID
    // macro without invalid writing null-byte outside id.token.bytes[] array.
    const char token_as_string__[AVS_COAP_MAX_TOKEN_LENGTH + 1];

    const uint16_t *content_format;
    const uint16_t *accept;
    const uint32_t *observe;

    const avs_coap_etag_t etag;
    const bool has_etag;
    const avs_coap_option_block_t block1;
    const bool has_block1;
    const avs_coap_option_block_t block2;
    const bool has_block2;

    const void *payload;
    size_t payload_size;

    AVS_LIST(const anjay_string_t) location_path;
    AVS_LIST(const anjay_string_t) uri_path;
    AVS_LIST(const anjay_string_t) uri_query;
};

static void add_string_options(avs_coap_options_t *options,
                               uint16_t option_number,
                               AVS_LIST(const anjay_string_t) values) {
    AVS_LIST(const anjay_string_t) it;
    AVS_LIST_FOREACH(it, values) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_options_add_string(options, option_number, it->c_str));
    }
}

static uint8_t *msg_end_ptr(coap_test_msg_t *msg) {
    return &msg->content[msg->length];
}

static size_t bytes_remaining(const coap_test_msg_t *msg, size_t max_size) {
    return max_size - msg->length - offsetof(coap_test_msg_t, content);
}

static void append_data(coap_test_msg_t *msg,
                        const size_t max_size,
                        const void *data,
                        size_t data_size) {
    AVS_UNIT_ASSERT_TRUE(data_size <= bytes_remaining(msg, max_size));
    memcpy(msg_end_ptr(msg), data, data_size);
    msg->length += (uint32_t) data_size;
}

typedef struct aligned_buffer aligned_buffer_t;

static inline aligned_buffer_t *ensure_aligned_buffer(void *buffer) {
    AVS_ASSERT((uintptr_t) buffer % AVS_ALIGNOF(coap_test_msg_t) == 0,
               "Buffer alignment must be the same as of coap_test_msg_t");
    return (aligned_buffer_t *) buffer;
}

static inline const coap_test_msg_t *
coap_msg__(aligned_buffer_t *buf,
           size_t buf_size,
           const struct coap_msg_args *args) {
    coap_test_msg_t *msg = (coap_test_msg_t *) buf;

    coap_test_header_set_type(msg, args->type);
    coap_test_header_set_version(msg, 1);
    coap_test_header_set_code(msg, args->code);
    coap_test_header_set_token_length(msg, args->id.token.size);
    coap_test_header_set_id(msg, args->id.msg_id);
    msg->length = (uint32_t) COAP_TEST_HEADER_SIZE;

    avs_coap_token_t token = {
        .bytes = { 0 },
        .size = args->id.token.size
    };
    memcpy(token.bytes, args->id.token.bytes, args->id.token.size);
    if (!args->has_raw_token) {
        memcpy(token.bytes, args->token_as_string__, args->id.token.size);
    }
    append_data(msg, buf_size, token.bytes, token.size);

    uint8_t options_buffer[1024];
    avs_coap_options_t options =
            avs_coap_options_create_empty(options_buffer,
                                          sizeof(options_buffer));

    if (args->has_block1) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_options_add_block(&options, &args->block1));
    }
    if (args->has_block2) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_options_add_block(&options, &args->block2));
    }
    if (args->has_etag) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_options_add_etag(&options, &args->etag));
    }

    add_string_options(&options, AVS_COAP_OPTION_LOCATION_PATH,
                       args->location_path);
    add_string_options(&options, AVS_COAP_OPTION_URI_PATH, args->uri_path);
    add_string_options(&options, AVS_COAP_OPTION_URI_QUERY, args->uri_query);

    if (args->content_format) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_options_add_u16(&options,
                                         AVS_COAP_OPTION_CONTENT_FORMAT,
                                         *args->content_format));
    }
    if (args->accept) {
        AVS_UNIT_ASSERT_SUCCESS(avs_coap_options_add_u16(
                &options, AVS_COAP_OPTION_ACCEPT, *args->accept));
    }
    if (args->observe) {
        AVS_UNIT_ASSERT_SUCCESS(avs_coap_options_add_u32(
                &options, AVS_COAP_OPTION_OBSERVE, *args->observe));
    }

    append_data(msg, buf_size, options.begin, options.size);

    if (args->payload_size) {
        // payload marker
        append_data(msg, buf_size, &(const uint8_t) { 0xFF }, 1);
        append_data(msg, buf_size, args->payload, args->payload_size);
    }

    AVS_LIST_CLEAR(
            (AVS_LIST(anjay_string_t) *) (intptr_t) &args->location_path);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &args->uri_path);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &args->uri_query);
    return msg;
}

/* Convenience macros for use in COAP_MSG */
#define CON COAP_TEST_MSG_CONFIRMABLE
#define NON COAP_TEST_MSG_NON_CONFIRMABLE
#define ACK COAP_TEST_MSG_ACKNOWLEDGEMENT
#define RST COAP_TEST_MSG_RESET

/* Convenience macro for use in COAP_MSG, to allow skipping AVS_COAP_CODE_
 * prefix */
#define CODE__(x) AVS_COAP_CODE_##x

/* Convenience macro for use in COAP_MSG, to allow skipping
 * AVS_COAP_FORMAT_ prefix */
#define FORMAT__(x) AVS_COAP_FORMAT_##x

/* Allocates a 64k buffer on the stack, constructs a message inside it and
 * returns the message pointer.
 *
 * @p Type    - one of COAP_TEST_MSG_* constants or CON, NON, ACK, RST.
 * @p Code    - suffix of one of AVS_COAP_CODE_* constants, e.g. GET
 *              or BAD_REQUEST.
 * @p Id      - message identity specified with the ID() macro.
 * @p Payload - one of NO_PAYLOAD, PAYLOAD(), BLOCK2().
 * @p Opts... - additional options, e.g. ETAG(), PATH(), QUERY().
 *
 * Example usage:
 * @code
 * const coap_test_msg_t *msg = COAP_MSG(CON, GET, ID(0), NO_PAYLOAD);
 * const coap_test_msg_t *msg = COAP_MSG(ACK, CONTENT, ID(0),
 *                                       BLOCK2(0, 16, "full_payload"));
 * @endcode
 */
#define COAP_MSG(Type, Code, Id, ... /* Payload, Opts... */)         \
    coap_msg__(ensure_aligned_buffer(&(uint8_t[65536]){ 0 }), 65536, \
               &(struct coap_msg_args) {                             \
                   .type = (Type),                                   \
                   .code = CODE__(Code),                             \
                   Id,                                               \
                   __VA_ARGS__                                       \
               })

/* Used in COAP_MSG() to define message identity. */
#define ID_TOKEN(MsgId, Token)                                                 \
    .id = (coap_test_msg_identity_t) { (uint16_t) (MsgId),                     \
                                       (avs_coap_token_t) { sizeof(Token) - 1, \
                                                            "" } },            \
    .token_as_string__ = Token

/* Used in COAP_MSG() to define message identity with empty token. */
#define ID(MsgId) ID_TOKEN((MsgId), "")

/* Used in COAP_MSG() to pass message token. */
#define ID_TOKEN_RAW(MsgId, Token)     \
    .has_raw_token = true,             \
    .id = (coap_test_msg_identity_t) { \
        (uint16_t)(MsgId), (Token)     \
    }

/* Used in COAP_MSG() to specify ETag option value. */
#define ETAG(Tag)                \
    .etag = (avs_coap_etag_t) {  \
        .size = sizeof(Tag) - 1, \
        .bytes = Tag             \
    },                           \
    .has_etag = true

/* Used in COAP_MSG() to specify a list of Location-Path options. */
#define LOCATION_PATH(... /* Segments */) \
    .location_path = ANJAY_MAKE_STRING_LIST(__VA_ARGS__)

/* Used in COAP_MSG() to specify a list of Uri-Path options. */
#define PATH(... /* Segments */) .uri_path = ANJAY_MAKE_STRING_LIST(__VA_ARGS__)

/* Used in COAP_MSG() to specify a list of Uri-Query options. */
#define QUERY(... /* Segments */) \
    .uri_query = ANJAY_MAKE_STRING_LIST(__VA_ARGS__)

/* Used in COAP_MSG() to specify the Content-Format option even with
 * unsupported value. */
#define CONTENT_FORMAT_VALUE(Format)        \
    .content_format = (const uint16_t[1]) { \
        (Format)                            \
    }

/* Used in COAP_MSG() to specify the Content-Format option using predefined
 * constants. */
#define CONTENT_FORMAT(Format) CONTENT_FORMAT_VALUE(FORMAT__(Format))

/* Used in COAP_MSG() to specify the Accept option. */
#define ACCEPT(Format)              \
    .accept = (const uint16_t[1]) { \
        (Format)                    \
    }

/* Used in COAP_MSG() to specify the Observe option. */
#define OBSERVE(Value)               \
    .observe = (const uint32_t[1]) { \
        (Value)                      \
    }

/* Used in COAP_MSG() to define a message with no payload or BLOCK options. */
#define NO_PAYLOAD   \
    .block1 = { 0 }, \
    .block2 = { 0 }, \
    .payload = NULL, \
    .payload_size = 0

/* Used in COAP_MSG() to define a non-block message payload from external
 * variable (not only string literal). */
#define PAYLOAD_EXTERNAL(Payload, PayloadSize) \
    .block1 = { 0 },                           \
    .block2 = { 0 },                           \
    .payload = Payload,                        \
    .payload_size = PayloadSize,

/* Used in COAP_MSG() to define a non-block message payload (string literal).
 * Terminating nullbyte is not considered part of the payload. */
#define PAYLOAD(Payload) PAYLOAD_EXTERNAL(Payload, sizeof(Payload) - 1)

/**
 * Used in COAP_MSG to define BLOCK2 option, and optionally add block payload.
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 * @p Payload - if specified, FULL PAYLOAD OF WHOLE BLOCK-WISE TRANSFER (!),
 *              given as a string literal. The macro will extract the portion
 *              of it based on Seq and Size. Terminating nullbyte is not
 *              considered part of the payload.
 */
#define BLOCK2(Seq, Size, ... /* Payload */)                               \
    .block1 = { 0 },                                                       \
    .block2 = {                                                            \
        .type = AVS_COAP_BLOCK2,                                           \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),          \
        .size = (assert((Size) < (1 << 15)), (uint16_t) (Size)),           \
        .has_more = ((Seq + 1) * (Size) + 1 < sizeof("" __VA_ARGS__))      \
    },                                                                     \
    .has_block2 = true,                                                    \
    .payload = ((const uint8_t *) ("" __VA_ARGS__)) + (Seq) * (Size),      \
    .payload_size =                                                        \
            sizeof("" __VA_ARGS__) == sizeof("")                           \
                    ? 0                                                    \
                    : ((((Seq) + 1) * (Size) + 1 < sizeof("" __VA_ARGS__)) \
                               ? (Size)                                    \
                               : (sizeof("" __VA_ARGS__) - 1               \
                                  - (Seq) * (Size)))

static inline void expect_timeout(avs_net_socket_t *mocksock) {
    avs_unit_mocksock_input_fail(mocksock, avs_errno(AVS_ETIMEDOUT));
}

#endif // ANJAY_COAP_TEST_UTILS_H
