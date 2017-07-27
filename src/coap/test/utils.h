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

#include <alloca.h>

#include <avsystem/commons/unit/test.h>
#include <anjay/download.h>

#include "../msg.h"
#include "../msg_builder.h"

#define PAYLOAD_MARKER "\xFF"

#define VTTL(version, type, token_length) \
    ((((version) & 0x03) << 6) | (((type) & 0x03) << 4) | ((token_length) & 0x0f))

static inline void setup_msg(anjay_coap_msg_t *msg,
                             const uint8_t *content,
                             size_t content_length) {
    static const anjay_coap_msg_t TEMPLATE = {
        .header = {
            .version_type_token_length = VTTL(1, ANJAY_COAP_MSG_ACKNOWLEDGEMENT, 0),
            .code = ANJAY_COAP_CODE(3, 4),
            .message_id = { 5, 6 }
        }
    };
    memset(msg, 0, sizeof(*msg) + content_length);
    memcpy(msg, &TEMPLATE, offsetof(anjay_coap_msg_t, content));
    assert(content || content_length == 0);
    if (content_length) {
        memcpy(msg->content, content, content_length);
    }
    msg->length = (uint32_t)(sizeof(msg->header) + content_length);
}

struct coap_msg_args {
    anjay_coap_msg_type_t type;
    uint8_t code;
    anjay_coap_msg_identity_t id;

    const uint16_t *accept;
    const uint32_t *observe;

    anjay_etag_t etag;
    const coap_block_info_t block1;
    const coap_block_info_t block2;

    const void *payload;
    size_t payload_size;

    AVS_LIST(const anjay_string_t) uri_path;
    AVS_LIST(const anjay_string_t) uri_query;
};

static int add_string_options(anjay_coap_msg_info_t *info,
                              uint16_t option_number,
                              AVS_LIST(const anjay_string_t) values) {
    AVS_LIST(const anjay_string_t) it;
    AVS_LIST_FOREACH(it, values) {
        if (_anjay_coap_msg_info_opt_string(info, option_number, it->c_str)) {
            return -1;
        }
    }

    return 0;
}

static inline const anjay_coap_msg_t *
coap_msg__(anjay_coap_aligned_msg_buffer_t *buf,
           size_t buf_size,
           const struct coap_msg_args *args) {
    anjay_coap_msg_builder_t builder;
    anjay_coap_msg_info_t info = {
        .type = args->type,
        .code = args->code,
        .identity = args->id,
    };

    if (args->block1.valid) {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_msg_info_opt_block(&info, &args->block1));
    }
    if (args->block2.valid) {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_msg_info_opt_block(&info, &args->block2));
    }
    if (args->etag.size > 0) {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_msg_info_opt_opaque(&info, ANJAY_COAP_OPT_ETAG,
                                                args->etag.value,
                                                args->etag.size));
    }

    AVS_UNIT_ASSERT_SUCCESS(add_string_options(&info, ANJAY_COAP_OPT_URI_PATH,
                                               args->uri_path));
    AVS_UNIT_ASSERT_SUCCESS(add_string_options(&info, ANJAY_COAP_OPT_URI_QUERY,
                                               args->uri_query));
    if (args->accept) {
        AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u16(
                &info, ANJAY_COAP_OPT_ACCEPT, *args->accept));
    }
    if (args->observe) {
        AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u32(
                &info, ANJAY_COAP_OPT_OBSERVE, *args->observe));
    }

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_builder_init(&builder, buf, buf_size, &info));

    AVS_UNIT_ASSERT_EQUAL(
            args->payload_size,
            _anjay_coap_msg_builder_payload(&builder, args->payload,
                                            args->payload_size));

    AVS_LIST_CLEAR(&args->uri_path);
    AVS_LIST_CLEAR(&args->uri_query);
    _anjay_coap_msg_info_reset(&info);
    return _anjay_coap_msg_builder_get_msg(&builder);
}

/* Convenience macros for use in COAP_MSG */
#define CON ANJAY_COAP_MSG_CONFIRMABLE
#define NON ANJAY_COAP_MSG_NON_CONFIRMABLE
#define ACK ANJAY_COAP_MSG_ACKNOWLEDGEMENT
#define RST ANJAY_COAP_MSG_RESET

/* Convenience macro for use in COAP_MSG, to allow skipping ANJAY_COAP_CODE_
 * prefix */
#define CODE__(x) ANJAY_COAP_CODE_##x

/* Allocates a 64k buffer on the stack, constructs a message inside it and
 * returns the message pointer.
 *
 * @p Type    - one of ANJAY_COAP_MSG_* constants or CON, NON, ACK, RST.
 * @p Code    - suffix of one of ANJAY_COAP_CODE_* constants, e.g. GET
 *              or BAD_REQUEST.
 * @p Id      - message identity specified with the ID() macro.
 * @p Payload - one of NO_PAYLOAD, PAYLOAD(), BLOCK2().
 * @p Opts... - additional options, e.g. ETAG(), PATH(), QUERY().
 *
 * Example usage:
 * @code
 * const anjay_coap_msg_t *msg = COAP_MSG(CON, GET, ID(0), NO_PAYLOAD);
 * const anjay_coap_msg_t *msg = COAP_MSG(ACK, CONTENT, ID(0),
 *                                        BLOCK2(0, 16, "full_payload"));
 * @endcode
 */
#define COAP_MSG(Type, Code, Id, .../* Payload, Opts... */) \
    coap_msg__(_anjay_coap_ensure_aligned_buffer(&(uint8_t[65536]){0}), 65536, \
               &(struct coap_msg_args){ \
                   .type = (Type), \
                   .code = CODE__(Code), \
                   Id, __VA_ARGS__ \
                })

/* Used in COAP_MSG() to define message identity. */
#define ID(MsgId, .../* Token */) \
    .id = (anjay_coap_msg_identity_t){ \
        (uint16_t)(MsgId), \
        (anjay_coap_token_t){__VA_ARGS__}, \
        sizeof("" __VA_ARGS__) - 1 }

/* Used in COAP_MSG() to specify ETag option value. */
#define ETAG(Tag) \
    .etag = (anjay_etag_t){ \
        .size = sizeof(Tag) - 1, \
        .value = (Tag) \
    }

/* Used in COAP_MSG() to specify a list of Uri-Path options. */
#define PATH(... /* Segments */) \
    .uri_path = _anjay_make_string_list(__VA_ARGS__, NULL)

/* Used in COAP_MSG() to specify a list of Uri-Query options. */
#define QUERY(... /* Segments */) \
    .uri_query = _anjay_make_string_list(__VA_ARGS__, NULL)

/* Used in COAP_MSG() to specify the Accept option. */
#define ACCEPT(Format) \
    .accept = (const uint16_t[1]) { (Format) }

/* Used in COAP_MSG() to specify the Observe option. */
#define OBSERVE(Value) \
    .observe = (const uint32_t[1]) { (Value) }

/* Used in COAP_MSG() to define a message with no payload or BLOCK options. */
#define NO_PAYLOAD \
    .block1 = {}, \
    .block2 = {}, \
    .payload = NULL, \
    .payload_size = 0

/* Used in COAP_MSG() to define a non-block message payload (string literal).
 * Terminating nullbyte is not considered part of the payload. */
#define PAYLOAD(Payload) \
    .block1 = {}, \
    .block2 = {}, \
    .payload = (Payload), \
    .payload_size = sizeof(Payload) - 1,

/**
 * Used in COAP_MSG to define BLOCK2 option, and optionally add block payload.
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 * @p Payload - if specified, FULL PAYLOAD OF WHOLE BLOCK-WISE TRANSFER (!),
 *              given as a string literal. The macro will extract the portion
 *              of it based on Seq and Size. Terminating nullbyte is not
 *              considered part of the payload.
 */
#define BLOCK2(Seq, Size, ... /* Payload */) \
    .block1 = {}, \
    .block2 = { \
        .type = COAP_BLOCK2, \
        .valid = true, \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t)(Seq)), \
        .size = (assert((Size) < (1 << 15)), (uint16_t)(Size)), \
        .has_more = ((Seq + 1) * (Size) + 1 < sizeof("" __VA_ARGS__)) \
    }, \
    .payload = ((const uint8_t*)("" __VA_ARGS__)) + (Seq) * (Size), \
    .payload_size = sizeof("" __VA_ARGS__) == sizeof("") \
            ? 0 \
            : ((((Seq) + 1) * (Size) + 1 < sizeof("" __VA_ARGS__)) \
                ? (Size) \
                : (sizeof("" __VA_ARGS__) - 1 - (Seq) * (Size)))

