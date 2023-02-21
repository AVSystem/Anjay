/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_TCP_TEST_UTILS_H
#define AVS_COAP_SRC_TCP_TEST_UTILS_H

#define AVS_UNIT_ENABLE_SHORT_ASSERTS

#include <avsystem/coap/coap.h>
#include <avsystem/commons/avs_unit_test.h>

#include "tcp/avs_coap_tcp_msg.h"
#include "tcp/avs_coap_tcp_signaling.h"

#include "tests/utils.h"

typedef struct {
    avs_coap_tcp_cached_msg_t msg;
    avs_coap_request_header_t request_header;
    avs_coap_response_header_t response_header;
    size_t payload_offset;
    size_t options_offset;
    size_t token_offset;
    size_t size;
    uint8_t data[];
} test_msg_t;

typedef struct test_exchange_struct {
    const test_msg_t *request;
    const test_msg_t *response;
} test_exchange_t;

/* Custody option for Ping and Pong messages. */
#define CUSTODY .custody_opt = true

#define MAX_MESSAGE_SIZE(Size) .max_msg_size = Size

#define BLOCK_WISE_TRANSFER_CAPABLE .block_wise_transfer_capable = true

#define PAYLOAD_INCOMPLETE .payload_partial = true

struct coap_msg_args {
    uint8_t code;
    avs_coap_token_t token;

    const void *payload;
    size_t payload_size;
    bool payload_partial;

    // 15 = arbitrary limit on path segments
    const char *uri_path[16];
    const uint16_t *accept;
    const uint16_t *duplicated_accept;
    const uint32_t *observe;

    const char uri_host[64];

    size_t max_msg_size;
    bool block_wise_transfer_capable;

    bool custody_opt;

#ifdef WITH_AVS_COAP_BLOCK
    const avs_coap_option_block_t block1;
    const avs_coap_option_block_t block2;
#endif // WITH_AVS_COAP_BLOCK
};

static inline void add_string_opts(avs_coap_options_t *opts,
                                   uint16_t opt_num,
                                   const char *const *const strings) {
    for (size_t i = 0; strings[i]; ++i) {
        ASSERT_OK(avs_coap_options_add_string(opts, opt_num, strings[i]));
    }
}

static inline const test_msg_t *
coap_msg__(uint8_t *buf, size_t buf_size, const struct coap_msg_args *args) {
    char opts_buf[4096];
    avs_coap_options_t opts =
            avs_coap_options_create_empty(opts_buf, sizeof(opts_buf));

    if (strlen(args->uri_host)) {
        ASSERT_OK(avs_coap_options_add_string(&opts, AVS_COAP_OPTION_URI_HOST,
                                              args->uri_host));
    }

#ifdef WITH_AVS_COAP_BLOCK
    if (args->block1.size > 0) {
        ASSERT_OK(avs_coap_options_add_block(&opts, &args->block1));
    }
    if (args->block2.size > 0) {
        ASSERT_OK(avs_coap_options_add_block(&opts, &args->block2));
    }
#endif // WITH_AVS_COAP_BLOCK

    if (args->code == AVS_COAP_CODE_CSM) {
        if (args->max_msg_size) {
            ASSERT_OK(
                    avs_coap_options_add_u32(&opts,
                                             _AVS_COAP_OPTION_MAX_MESSAGE_SIZE,
                                             (uint32_t) args->max_msg_size));
        }
        if (args->block_wise_transfer_capable) {
            ASSERT_OK(avs_coap_options_add_empty(
                    &opts, _AVS_COAP_OPTION_BLOCK_WISE_TRANSFER_CAPABILITY));
        }
    }

    add_string_opts(&opts, AVS_COAP_OPTION_URI_PATH, args->uri_path);

    if (args->accept) {
        ASSERT_OK(avs_coap_options_add_u16(&opts, AVS_COAP_OPTION_ACCEPT,
                                           *args->accept));
    }

    if (args->duplicated_accept) {
        ASSERT_OK(avs_coap_options_add_u16(&opts, AVS_COAP_OPTION_ACCEPT,
                                           *args->duplicated_accept));
    }
    if (args->observe) {
        ASSERT_OK(avs_coap_options_add_u32(&opts, AVS_COAP_OPTION_OBSERVE,
                                           *args->observe));
    }

    if (args->custody_opt) {
        ASSERT_OK(avs_coap_options_add_empty(&opts, _AVS_COAP_OPTION_CUSTODY));
    }

    ASSERT_EQ((uintptr_t) buf % AVS_ALIGNOF(size_t), 0);
    test_msg_t *test_msg = (test_msg_t *) buf;

    test_msg->msg = (avs_coap_tcp_cached_msg_t) {
        .content = (avs_coap_borrowed_msg_t) {
            .code = args->code,
            .token = args->token,
            .options = opts,
            .payload = args->payload,
            .payload_size = args->payload_size
        },
        // Hack for test purposes:
        // There may be more than one byte remaining. We don't care, because
        // it's used only for setting a boolean flag passed to request handler.
        .remaining_bytes = (size_t) args->payload_partial
    };

    uint8_t header_buf[_AVS_COAP_TCP_MAX_HEADER_LENGTH];
    avs_coap_tcp_header_t header =
            _avs_coap_tcp_header_init(args->payload_size, opts.size,
                                      args->token.size, args->code);
    size_t header_size = _avs_coap_tcp_header_serialize(&header, header_buf,
                                                        sizeof(header_buf));
    test_msg->token_offset = header_size;

    size_t initial_size = buf_size - sizeof(test_msg_t);
    bytes_appender_t appender = {
        .write_ptr = test_msg->data,
        .bytes_left = initial_size
    };

    ASSERT_OK(_avs_coap_bytes_append(&appender, header_buf, header_size));
    ASSERT_OK(_avs_coap_bytes_append(&appender,
                                     test_msg->msg.content.token.bytes,
                                     test_msg->msg.content.token.size));
    test_msg->options_offset = initial_size - appender.bytes_left;
    ASSERT_OK(_avs_coap_bytes_append(&appender,
                                     test_msg->msg.content.options.begin,
                                     test_msg->msg.content.options.size));
    if (test_msg->msg.content.payload_size) {
        ASSERT_OK(
                _avs_coap_bytes_append(&appender, &AVS_COAP_PAYLOAD_MARKER, 1));
    }
    test_msg->payload_offset = initial_size - appender.bytes_left;
    ASSERT_OK(_avs_coap_bytes_append(&appender, test_msg->msg.content.payload,
                                     test_msg->msg.content.payload_size));
    test_msg->size = initial_size - appender.bytes_left;

    // Adjust options field to point to test_msg and not to the stack-allocated
    // buffer. We could use _avs_coap_tcp_msg_parse, but this function is also
    // used to construct invalid messages, which makes parse fail.
    test_msg->msg.content.options = (avs_coap_options_t) {
        .begin = &test_msg->data[test_msg->options_offset],
        .size = opts.size,
        .capacity = opts.size
    };

    test_msg->request_header = (avs_coap_request_header_t) {
        .code = test_msg->msg.content.code,
        .options = test_msg->msg.content.options
    };
    test_msg->response_header = (avs_coap_response_header_t) {
        .code = test_msg->msg.content.code,
        .options = test_msg->msg.content.options
    };

    return test_msg;
}

/* Allocates a 64k buffer on the stack, constructs a message inside it and
 * returns the message pointer.
 *
 * @p Code    - suffix of one of AVS_COAP_CODE_* constants, e.g. GET
 *              or BAD_REQUEST.
 * @p Opts... - additional options, e.g. ETAG(), PATH(), QUERY().
 *
 * Example usage:
 * @code
 * const avs_coap_msg_t *msg = COAP_MSG(GET, ID(0), NO_PAYLOAD);
 * @endcode
 */
#define COAP_MSG(Code, ... /* Payload, Opts... */)                           \
    coap_msg__(                                                              \
            (uint8_t *) (size_t[(65535 + sizeof(size_t)) / sizeof(size_t)]){ \
                    0 },                                                     \
            65536,                                                           \
            &(struct coap_msg_args) {                                        \
                .code = CODE__(Code), __VA_ARGS__                            \
            })

#endif // AVS_COAP_SRC_TCP_TEST_UTILS_H
