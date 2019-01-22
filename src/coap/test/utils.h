/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay/download.h>
#include <avsystem/commons/unit/test.h>

#include <avsystem/commons/coap/msg.h>
#include <avsystem/commons/coap/msg_builder.h>

struct coap_msg_args {
    avs_coap_msg_type_t type;
    uint8_t code;
    avs_coap_msg_identity_t id;

    // The array is needed to set token with maximum length by convenient ID
    // macro without invalid writing null-byte outside id.token.bytes[] array.
    const char token_as_string__[AVS_COAP_MAX_TOKEN_LENGTH + 1];

    const uint16_t *content_format;
    const uint16_t *accept;
    const uint32_t *observe;

    const anjay_etag_t *etag;
    const avs_coap_block_info_t block1;
    const avs_coap_block_info_t block2;

    const void *payload;
    size_t payload_size;

    AVS_LIST(const anjay_string_t) location_path;
    AVS_LIST(const anjay_string_t) uri_path;
    AVS_LIST(const anjay_string_t) uri_query;
};

static int add_string_options(avs_coap_msg_info_t *info,
                              uint16_t option_number,
                              AVS_LIST(const anjay_string_t) values) {
    AVS_LIST(const anjay_string_t) it;
    AVS_LIST_FOREACH(it, values) {
        if (avs_coap_msg_info_opt_string(info, option_number, it->c_str)) {
            return -1;
        }
    }

    return 0;
}

static inline const avs_coap_msg_t *
coap_msg__(avs_coap_aligned_msg_buffer_t *buf,
           size_t buf_size,
           const struct coap_msg_args *args) {
    avs_coap_msg_builder_t builder;
    avs_coap_msg_info_t info = {
        .type = args->type,
        .code = args->code,
        .identity = args->id,
    };

    memcpy(info.identity.token.bytes, args->token_as_string__,
           args->id.token.size);

    if (args->block1.valid) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_msg_info_opt_block(&info, &args->block1));
    }
    if (args->block2.valid) {
        AVS_UNIT_ASSERT_SUCCESS(
                avs_coap_msg_info_opt_block(&info, &args->block2));
    }
    if (args->etag) {
        AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_info_opt_opaque(
                &info, AVS_COAP_OPT_ETAG, args->etag->value, args->etag->size));
    }

    AVS_UNIT_ASSERT_SUCCESS(add_string_options(
            &info, AVS_COAP_OPT_LOCATION_PATH, args->location_path));
    AVS_UNIT_ASSERT_SUCCESS(
            add_string_options(&info, AVS_COAP_OPT_URI_PATH, args->uri_path));
    AVS_UNIT_ASSERT_SUCCESS(
            add_string_options(&info, AVS_COAP_OPT_URI_QUERY, args->uri_query));
    if (args->content_format) {
        AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_info_opt_u16(
                &info, AVS_COAP_OPT_CONTENT_FORMAT, *args->content_format));
    }
    if (args->accept) {
        AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_info_opt_u16(
                &info, AVS_COAP_OPT_ACCEPT, *args->accept));
    }
    if (args->observe) {
        AVS_UNIT_ASSERT_SUCCESS(avs_coap_msg_info_opt_u32(
                &info, AVS_COAP_OPT_OBSERVE, *args->observe));
    }

    AVS_UNIT_ASSERT_SUCCESS(
            avs_coap_msg_builder_init(&builder, buf, buf_size, &info));

    AVS_UNIT_ASSERT_EQUAL(args->payload_size,
                          avs_coap_msg_builder_payload(&builder, args->payload,
                                                       args->payload_size));

    AVS_LIST_CLEAR(
            (AVS_LIST(anjay_string_t) *) (intptr_t) &args->location_path);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &args->uri_path);
    AVS_LIST_CLEAR((AVS_LIST(anjay_string_t) *) (intptr_t) &args->uri_query);
    avs_coap_msg_info_reset(&info);
    return avs_coap_msg_builder_get_msg(&builder);
}

/* Convenience macros for use in COAP_MSG */
#define CON AVS_COAP_MSG_CONFIRMABLE
#define NON AVS_COAP_MSG_NON_CONFIRMABLE
#define ACK AVS_COAP_MSG_ACKNOWLEDGEMENT
#define RST AVS_COAP_MSG_RESET

/* Convenience macro for use in COAP_MSG, to allow skipping AVS_COAP_CODE_
 * prefix */
#define CODE__(x) AVS_COAP_CODE_##x

/* Convenience macro for use in COAP_MSG, to allow skipping ANJAY_COAP_FORMAT_
 * prefix */
#define FORMAT__(x) ANJAY_COAP_FORMAT_##x

/* Allocates a 64k buffer on the stack, constructs a message inside it and
 * returns the message pointer.
 *
 * @p Type    - one of AVS_COAP_MSG_* constants or CON, NON, ACK, RST.
 * @p Code    - suffix of one of AVS_COAP_CODE_* constants, e.g. GET
 *              or BAD_REQUEST.
 * @p Id      - message identity specified with the ID() macro.
 * @p Payload - one of NO_PAYLOAD, PAYLOAD(), BLOCK2().
 * @p Opts... - additional options, e.g. ETAG(), PATH(), QUERY().
 *
 * Example usage:
 * @code
 * const avs_coap_msg_t *msg = COAP_MSG(CON, GET, ID(0), NO_PAYLOAD);
 * const avs_coap_msg_t *msg = COAP_MSG(ACK, CONTENT, ID(0),
 *                                      BLOCK2(0, 16, "full_payload"));
 * @endcode
 */
#define COAP_MSG(Type, Code, Id, ... /* Payload, Opts... */)                  \
    coap_msg__(avs_coap_ensure_aligned_buffer(&(uint8_t[65536]){ 0 }), 65536, \
               &(struct coap_msg_args) {                                      \
                   .type = (Type),                                            \
                   .code = CODE__(Code),                                      \
                   Id,                                                        \
                   __VA_ARGS__                                                \
               })

/* Used in COAP_MSG() to define message identity. */
#define ID_TOKEN(MsgId, Token)                                                \
    .id = (avs_coap_msg_identity_t) { (uint16_t) (MsgId),                     \
                                      (avs_coap_token_t) { sizeof(Token) - 1, \
                                                           "" } },            \
    .token_as_string__ = Token

/* Used in COAP_MSG() to define message identity with empty token. */
#define ID(MsgId) ID_TOKEN((MsgId), "")

/* Used in COAP_MSG() to specify ETag option value. */
#define ETAG(Tag)                                         \
    .etag = (const anjay_etag_t *) &(anjay_coap_etag_t) { \
        .size = sizeof(Tag) - 1,                          \
        .value = Tag                                      \
    }

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
        .valid = true,                                                     \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),          \
        .size = (assert((Size) < (1 << 15)), (uint16_t) (Size)),           \
        .has_more = ((Seq + 1) * (Size) + 1 < sizeof("" __VA_ARGS__))      \
    },                                                                     \
    .payload = ((const uint8_t *) ("" __VA_ARGS__)) + (Seq) * (Size),      \
    .payload_size =                                                        \
            sizeof("" __VA_ARGS__) == sizeof("")                           \
                    ? 0                                                    \
                    : ((((Seq) + 1) * (Size) + 1 < sizeof("" __VA_ARGS__)) \
                               ? (Size)                                    \
                               : (sizeof("" __VA_ARGS__) - 1               \
                                  - (Seq) * (Size)))
