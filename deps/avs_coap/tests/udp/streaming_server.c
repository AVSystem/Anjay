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

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP) \
        && defined(WITH_AVS_COAP_STREAMING_API)

#    include <avsystem/coap/coap.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

typedef struct {
    avs_coap_request_header_t expected_request_header;
    const char *expected_request_data;
    size_t expected_request_data_size;
    bool ignore_overlong_request;
    bool expect_failure;
    bool use_peek;

    avs_coap_response_header_t response_header;
    const char *response_data;
    size_t response_data_size;
} streaming_handle_request_args_t;

static int streaming_handle_request(avs_coap_streaming_request_ctx_t *ctx,
                                    const avs_coap_request_header_t *request,
                                    avs_stream_t *payload_stream,
                                    const avs_coap_observe_id_t *observe_id,
                                    void *args_) {
    streaming_handle_request_args_t *args =
            (streaming_handle_request_args_t *) args_;

    (void) observe_id;

    ASSERT_NOT_NULL(ctx);
    ASSERT_NOT_NULL(request);
    ASSERT_NOT_NULL(payload_stream);

    ASSERT_EQ(request->code, args->expected_request_header.code);
    ASSERT_EQ(request->options.size,
              args->expected_request_header.options.size);
    ASSERT_EQ_BYTES_SIZED(request->options.begin,
                          args->expected_request_header.options.begin,
                          request->options.size);

    size_t offset = 0;
    bool finished = false;
    while (!finished) {
        size_t bytes_read;
        unsigned char buf[4096];
        size_t buf_size = sizeof(buf);
        if (args->ignore_overlong_request) {
            buf_size = AVS_MIN(buf_size,
                               args->expected_request_data_size - offset);
            if (!buf_size) {
                break;
            }
        }

        char ch;
        avs_error_t peek_err;
        if (args->use_peek) {
            peek_err = avs_stream_peek(payload_stream, 0, &ch);
        }
        avs_error_t err = avs_stream_read(payload_stream, &bytes_read,
                                          &finished, buf, buf_size);
        if (!args->expect_failure) {
            ASSERT_OK(err);
        } else if (avs_is_err(err)) {
            if (args->use_peek) {
                ASSERT_FAIL(peek_err);
                ASSERT_FALSE(avs_is_eof(peek_err));
            }
            return -1;
        }

        ASSERT_EQ_BYTES_SIZED(buf, args->expected_request_data + offset,
                              bytes_read);
        if (args->use_peek) {
            ASSERT_EQ(ch, bytes_read ? buf[0] : EOF);
            if (ch == EOF) {
                ASSERT_TRUE(finished);
            }
        }

        offset += bytes_read;
    }
    ASSERT_FALSE(args->expect_failure);

    ASSERT_EQ(args->expected_request_data_size, offset);

    avs_stream_t *response_stream =
            avs_coap_streaming_setup_response(ctx, &args->response_header);
    if (avs_coap_code_is_response(args->response_header.code)) {
        ASSERT_NOT_NULL(response_stream);
        if (args->response_data_size) {
            ASSERT_OK(avs_stream_write(response_stream, args->response_data,
                                       args->response_data_size));
        }
        return 0;
    } else {
        ASSERT_NULL(response_stream);
        return -1;
    }
}

AVS_UNIT_TEST(udp_streaming_server, no_payload) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));

    streaming_handle_request_args_t args = {
        .expected_request_header = request->request_header,
        .response_header = {
            .code = response->response_header.code
        },
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
}

AVS_UNIT_TEST(udp_streaming_server, small_payload) {
#    define REQUEST_PAYLOAD "Actually,"
#    define RESPONSE_PAYLOAD "fish"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                         PAYLOAD(REQUEST_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                     PAYLOAD(RESPONSE_PAYLOAD));

    streaming_handle_request_args_t args = {
        .expected_request_header = request->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .response_header = {
            .code = response->response_header.code
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#    undef REQUEST_PAYLOAD
#    undef RESPONSE_PAYLOAD
}

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_streaming_server, large_payload) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_AND_2_RES(1, 1024, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .response_header = {
            .code = responses[1]->response_header.code
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, weird_block_sizes) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(2, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(3, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_REQ_AND_2_RES(2, 512, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(4), TOKEN(nth_token(4)), BLOCK2_REQ(2, 256)),
        COAP_MSG(CON, PUT, ID(5), TOKEN(nth_token(5)), BLOCK2_REQ(3, 256)),
        COAP_MSG(CON, PUT, ID(6), TOKEN(nth_token(6)), BLOCK2_REQ(2, 512)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 512, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(2, 256, true)),
        COAP_MSG(ACK, CONTINUE, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(3, 256, true)),
        COAP_MSG(ACK, CONTENT, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_AND_2_RES(2, 512, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(4), TOKEN(nth_token(4)),
                 BLOCK2_RES(2, 256, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(5), TOKEN(nth_token(5)),
                 BLOCK2_RES(3, 256, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(6), TOKEN(nth_token(6)),
                 BLOCK2_RES(2, 512, RESPONSE_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .response_header = {
            .code = responses[3]->response_header.code
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, weird_block_sizes_peek) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(2, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(3, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_REQ_AND_2_RES(2, 512, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(4), TOKEN(nth_token(4)), BLOCK2_REQ(2, 256)),
        COAP_MSG(CON, PUT, ID(5), TOKEN(nth_token(5)), BLOCK2_REQ(3, 256)),
        COAP_MSG(CON, PUT, ID(6), TOKEN(nth_token(6)), BLOCK2_REQ(2, 512)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 512, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(2, 256, true)),
        COAP_MSG(ACK, CONTINUE, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(3, 256, true)),
        COAP_MSG(ACK, CONTENT, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_AND_2_RES(2, 512, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(4), TOKEN(nth_token(4)),
                 BLOCK2_RES(2, 256, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(5), TOKEN(nth_token(5)),
                 BLOCK2_RES(3, 256, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(6), TOKEN(nth_token(6)),
                 BLOCK2_RES(2, 512, RESPONSE_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .use_peek = true,
        .response_header = {
            .code = responses[3]->response_header.code
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, increasing_block2_size) {
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.nstart = 999;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 16, 4096, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(0, 16)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 16)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 32)),
        COAP_MSG(CON, GET, ID(3), TOKEN(nth_token(3)), BLOCK2_REQ(1, 64)),
        COAP_MSG(CON, GET, ID(4), TOKEN(nth_token(4)), BLOCK2_REQ(1, 128)),
        COAP_MSG(CON, GET, ID(5), TOKEN(nth_token(5)), BLOCK2_REQ(1, 256)),
        COAP_MSG(CON, GET, ID(6), TOKEN(nth_token(6)), BLOCK2_REQ(1, 512)),
        COAP_MSG(CON, GET, ID(7), TOKEN(nth_token(7)), BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 16, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 16, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 32, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(3), TOKEN(nth_token(3)),
                 BLOCK2_RES(1, 64, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(4), TOKEN(nth_token(4)),
                 BLOCK2_RES(1, 128, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(5), TOKEN(nth_token(5)),
                 BLOCK2_RES(1, 256, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(6), TOKEN(nth_token(6)),
                 BLOCK2_RES(1, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(7), TOKEN(nth_token(7)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, setup_response_error) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD))
    };
    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTINUE, ID(0),
                                               TOKEN(nth_token(0)),
                                               BLOCK1_RES(0, 1024, true)),
                                      COAP_MSG(ACK, INTERNAL_SERVER_ERROR,
                                               ID(1), TOKEN(nth_token(1)),
                                               BLOCK1_RES(1, 1024, false)) };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, large_payload_ignored) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_AND_2_RES(0, 1024, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = 100,
        .ignore_overlong_request = true,
        .response_header = {
            .code = responses[1]->response_header.code
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, incorrect_block2_in_block1_request) {
#        define REQUEST_PAYLOAD DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ_AND_2_RES(1, 256, 32, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(1, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_REQ(2, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(4), TOKEN(nth_token(4)),
                 BLOCK1_REQ(3, 256, REQUEST_PAYLOAD))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 256, true)),
        COAP_MSG(ACK, BAD_OPTION, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(ACK, CONTINUE, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(1, 256, true)),
        COAP_MSG(ACK, CONTINUE, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_RES(2, 256, true)),
        COAP_MSG(ACK, CHANGED, ID(4), TOKEN(nth_token(4)),
                 BLOCK1_RES(3, 256, false))
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = requests[0]->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .response_header = {
            .code = responses[4]->request_header.code
        },
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_request_response_count);
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_recv(&env, requests[i]);
        expect_send(&env, responses[i]);
    }

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

#        undef REQUEST_PAYLOAD
}

static void advance_mockclock(avs_net_socket_t *socket, void *timeout) {
    (void) socket;
    _avs_mock_clock_advance(*(const avs_time_duration_t *) timeout);
}

AVS_UNIT_TEST(udp_streaming_server, block1_receive_timed_out) {
#        define REQUEST_PAYLOAD DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_deterministic();

    const test_msg_t *request = { COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                           BLOCK1_REQ(0, 16,
                                                      REQUEST_PAYLOAD)) };
    const test_msg_t *response = { COAP_MSG(ACK, CONTINUE, ID(0),
                                            TOKEN(nth_token(0)),
                                            BLOCK1_RES(0, 16, true)) };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = request->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .expect_failure = true,
        .response_header = {
            .code = AVS_COAP_CODE_CREATED
        },
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);
    avs_unit_mocksock_input_fail(env.mocksock, avs_errno(AVS_ETIMEDOUT),
                                 .and_then = advance_mockclock,
                                 .and_then_arg = &(avs_time_duration_t) {
                                     .seconds = 300
                                 });

    ASSERT_FAIL(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, block2_receive_timed_out) {
#        define RESPONSE_PAYLOAD DATA_1KB "?"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_deterministic();

    const test_msg_t *request = { COAP_MSG(CON, GET, ID(0),
                                           TOKEN(nth_token(0))) };
    const test_msg_t *response = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD))
    };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = request->request_header,
        .response_header = {
            .code = AVS_COAP_CODE_CONTENT
        },
        .response_data = RESPONSE_PAYLOAD,
        .response_data_size = sizeof(RESPONSE_PAYLOAD) - 1,
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);
    avs_unit_mocksock_input_fail(env.mocksock, avs_errno(AVS_ETIMEDOUT),
                                 .and_then = advance_mockclock,
                                 .and_then_arg = &(avs_time_duration_t) {
                                     .seconds = 300
                                 });

    ASSERT_FAIL(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, connection_closed) {
#        define REQUEST_PAYLOAD DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_deterministic();

    const test_msg_t *request = { COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                           BLOCK1_REQ(0, 16,
                                                      REQUEST_PAYLOAD)) };
    const test_msg_t *response = { COAP_MSG(ACK, CONTINUE, ID(0),
                                            TOKEN(nth_token(0)),
                                            BLOCK1_RES(0, 16, true)) };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = request->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .expect_failure = true,
        .response_header = {
            .code = AVS_COAP_CODE_CREATED
        },
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);
    avs_unit_mocksock_input_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));

    ASSERT_FAIL(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server, connection_closed_peek) {
#        define REQUEST_PAYLOAD DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_deterministic();

    const test_msg_t *request = { COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                           BLOCK1_REQ(0, 16,
                                                      REQUEST_PAYLOAD)) };
    const test_msg_t *response = { COAP_MSG(ACK, CONTINUE, ID(0),
                                            TOKEN(nth_token(0)),
                                            BLOCK1_RES(0, 16, true)) };

    streaming_handle_request_args_t args = {
        // NOTE: user handler is given the first BLOCK1 request header
        .expected_request_header = request->request_header,
        .expected_request_data = REQUEST_PAYLOAD,
        .expected_request_data_size = sizeof(REQUEST_PAYLOAD) - 1,
        .expect_failure = true,
        .use_peek = true,
        .response_header = {
            .code = AVS_COAP_CODE_CREATED
        },
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);
    avs_unit_mocksock_input_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));

    ASSERT_FAIL(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

#        undef REQUEST_PAYLOAD
}

static int broken_handle_request(avs_coap_streaming_request_ctx_t *ctx,
                                 const avs_coap_request_header_t *request,
                                 avs_stream_t *payload_stream,
                                 const avs_coap_observe_id_t *observe_id,
                                 void *args_) {
    (void) ctx;
    (void) request;
    (void) observe_id;
    (void) args_;
    bool finished = false;
    avs_error_t err = AVS_OK;
    while (!finished && avs_is_ok(err)) {
        size_t bytes_read;
        unsigned char buf[4096];
        err = avs_stream_read(payload_stream, &bytes_read, &finished, buf,
                              sizeof(buf));
    }
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ENODEV);
    // Intentionally return success even though we're not really that successful
    return 0;
}

AVS_UNIT_TEST(udp_streaming_server, connection_closed_send_broken_handler) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                         BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD));

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ENODEV));

    avs_error_t err = avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, broken_handle_request, NULL);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ENODEV);
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server,
              connection_closed_block_send_broken_handler) {
#        define REQUEST_PAYLOAD DATA_1KB DATA_1KB "?"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD))
    };
    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTINUE, ID(0),
                                               TOKEN(nth_token(0)),
                                               BLOCK1_RES(0, 1024, true)) };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_recv(&env, requests[1]);
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ENODEV));

    avs_error_t err = avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, broken_handle_request, NULL);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ENODEV);
#        undef REQUEST_PAYLOAD
}

static int broken_write_handle_request(avs_coap_streaming_request_ctx_t *ctx,
                                       const avs_coap_request_header_t *request,
                                       avs_stream_t *payload_stream,
                                       const avs_coap_observe_id_t *observe_id,
                                       void *payload) {
    (void) ctx;
    (void) request;
    (void) observe_id;
    bool finished = false;
    while (!finished) {
        size_t bytes_read;
        unsigned char buf[4096];
        ASSERT_OK(avs_stream_read(payload_stream, &bytes_read, &finished, buf,
                                  sizeof(buf)));
    }
    avs_stream_t *response_stream = avs_coap_streaming_setup_response(
            ctx, &(const avs_coap_response_header_t) {
                     .code = AVS_COAP_CODE_CONTENT
                 });
    ASSERT_NOT_NULL(response_stream);
    avs_stream_write(response_stream, payload, strlen((const char *) payload));
    // Intentionally ignore return value of the above
    return 0;
}

AVS_UNIT_TEST(udp_streaming_server,
              connection_closed_block_send_broken_write_handler) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_AND_2_RES(1, 1024, 1024, RESPONSE_PAYLOAD))
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_recv(&env, requests[2]);
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ENODEV));

    avs_error_t err = avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, broken_write_handle_request, RESPONSE_PAYLOAD);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ENODEV);
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_server,
              connection_closed_block_send_broken_bigger_write_handler) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, PUT, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 1024)),
        COAP_MSG(CON, PUT, ID(3), TOKEN(nth_token(3)), BLOCK2_REQ(2, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_AND_2_RES(1, 1024, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD)),
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    expect_recv(&env, requests[3]);
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ENODEV));

    avs_error_t err = avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, broken_write_handle_request, RESPONSE_PAYLOAD);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ENODEV);
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

#    endif // WITH_AVS_COAP_BLOCK

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP) &&
       // defined(WITH_AVS_COAP_STREAMING_API)
