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
        && defined(WITH_AVS_COAP_STREAMING_API)             \
        && defined(WITH_AVS_COAP_OBSERVE)

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_stream_membuf.h>

#    include <avsystem/coap/coap.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

typedef struct {
    test_env_t *env;

    avs_coap_request_header_t expected_request_header;
    const char *expected_request_data;
    size_t expected_request_data_size;

    avs_coap_response_header_t response_header;
    const char *response_data;
    size_t response_data_size;
} streaming_handle_request_args_t;

static void on_observe_cancel(avs_coap_observe_id_t id, void *env) {
    assert_observe_state_change_expected((test_env_t *) env, OBSERVE_CANCEL,
                                         id);
}

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
        char buf[4096];

        ASSERT_OK(avs_stream_read(payload_stream, &bytes_read, &finished, buf,
                                  sizeof(buf)));
        ASSERT_EQ_BYTES_SIZED(buf, args->expected_request_data + offset,
                              bytes_read);

        offset += bytes_read;
    }

    if (observe_id) {
        ASSERT_OK(avs_coap_observe_streaming_start(
                ctx, *observe_id, on_observe_cancel, args->env));
    }

    ASSERT_EQ(args->expected_request_data_size, offset);

    avs_stream_t *response_stream =
            avs_coap_streaming_setup_response(ctx, &args->response_header);
    ASSERT_NOT_NULL(response_stream);
    ASSERT_OK(avs_stream_write(response_stream, args->response_data,
                               args->response_data_size));
    return 0;
}

AVS_UNIT_TEST(udp_streaming_observe, start) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                     NO_PAYLOAD);
    // Note: Observe option values start at 0 (in a response to the initial
    // Observe) and get incremented by one with each sent notification
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")),
                     OBSERVE(0), NO_PAYLOAD);

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = request->request_header,
        .response_header = {
            .code = response->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, response);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
}

AVS_UNIT_TEST(udp_streaming_observe, notify) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                     NO_PAYLOAD);
    // Note: Observe option values start at 0 (in a response to the initial
    // Observe) and get incremented by one with each sent notification
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(NON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = request->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, request);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = request->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);

    ASSERT_OK(avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_streaming_writer,
            &test_payload));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_observe, notify_confirmable) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),
    };
    // Note: Observe option values start at 0 (in a response to the initial
    // Observe) and get incremented by one with each sent notification
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);
    expect_recv(&env, requests[1]);
    expect_timeout(&env);

    ASSERT_OK(avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_streaming_writer,
            &test_payload));

    // should be canceled by cleanup
    expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
#    undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_observe, notify_and_connection_refused) {
#    define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),

        // notify to which no response will be received
        COAP_MSG(CON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);
    avs_unit_mocksock_input_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));
    expect_observe_cancel(&env, requests[0]->msg.token);

    avs_error_t err = avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_streaming_writer,
            &test_payload);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ECONNREFUSED);

#    undef NOTIFY_PAYLOAD
}

#    ifdef WITH_AVS_COAP_BLOCK
AVS_UNIT_TEST(udp_streaming_observe, notify_block) {
#        define NOTIFY_PAYLOAD DATA_1KB "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),

        // request for second block of Notify
        COAP_MSG(CON, GET, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),

        // BLOCK Notify
        COAP_MSG(NON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 BLOCK2_RES(0, 1024, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(ACK, CONTENT, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[2]);
    expect_timeout(&env);

    ASSERT_OK(avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_streaming_writer,
            &test_payload));

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
#        undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_observe, notify_block_send_fail) {
#        define NOTIFY_PAYLOAD DATA_1KB DATA_1KB DATA_1KB "Notifaj"
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.nstart = 999;
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup(&tx_params, 4096, 1200, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),

        // request for more blocks of Notify
        COAP_MSG(CON, GET, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 1024)),
        COAP_MSG(CON, GET, ID(102), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(2, 1024)),
        COAP_MSG(CON, GET, ID(103), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(2, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),

        // BLOCK Notify
        COAP_MSG(NON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 BLOCK2_RES(0, 1024, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(ACK, CONTENT, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(102), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(2, 1024, NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[2]);
    expect_recv(&env, requests[2]);
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ENODEV));

    avs_error_t err = avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_streaming_writer,
            &test_payload);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ENODEV);

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
#        undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_observe, increasing_block_size) {
#        define NOTIFY_PAYLOAD DATA_1KB "Notifaj"
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.nstart = 999;
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup(&tx_params, 32, 4096, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 BLOCK2_REQ(0, 16)),

        // request for further blocks of Notify
        COAP_MSG(CON, GET, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 16)),
        COAP_MSG(CON, GET, ID(102), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 32)),
        COAP_MSG(CON, GET, ID(103), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 64)),
        COAP_MSG(CON, GET, ID(104), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 128)),
        COAP_MSG(CON, GET, ID(105), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 256)),
        COAP_MSG(CON, GET, ID(106), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 512)),
        COAP_MSG(CON, GET, ID(107), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 1024)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 BLOCK2_RES(0, 16, "")),

        // BLOCK Notify
        COAP_MSG(NON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 BLOCK2_RES(0, 16, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(ACK, CONTENT, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 16, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(102), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 32, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(103), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 64, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(104), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 128, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(105), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 256, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(106), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 512, NOTIFY_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(107), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    for (size_t i = 1; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_send(&env, responses[i]);
        expect_recv(&env, requests[i]);
    }
    expect_send(&env, responses[AVS_ARRAY_SIZE(requests)]);
    expect_timeout(&env);

    ASSERT_OK(avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_streaming_writer,
            &test_payload));

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
#        undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_observe, notify_block_confirmable) {
#        define NOTIFY_PAYLOAD DATA_1KB "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),

        // request for second block of Notify
        COAP_MSG(CON, GET, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 1024)),
        // separate response ack
        COAP_MSG(ACK, EMPTY, ID(1), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),

        // BLOCK Notify
        COAP_MSG(CON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 BLOCK2_RES(0, 1024, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(CON, CONTENT, ID(1), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);
    expect_recv(&env, requests[1]);
    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    expect_recv(&env, requests[3]);
    expect_timeout(&env);

    ASSERT_OK(avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_streaming_writer,
            &test_payload));

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
#        undef NOTIFY_PAYLOAD
}

AVS_UNIT_TEST(udp_streaming_observe, increasing_block_size_confirmable) {
#        define NOTIFY_PAYLOAD DATA_1KB "Notifaj"
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.nstart = 999;
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup(&tx_params, 32, 4096, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 BLOCK2_REQ(0, 16)),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),

        // requests and separate response ACKs for further blocks of Notify
        COAP_MSG(CON, GET, ID(101), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 16)),
        COAP_MSG(ACK, EMPTY, ID(1), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(102), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 32)),
        COAP_MSG(ACK, EMPTY, ID(2), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(103), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 64)),
        COAP_MSG(ACK, EMPTY, ID(3), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(104), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 128)),
        COAP_MSG(ACK, EMPTY, ID(4), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(105), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 256)),
        COAP_MSG(ACK, EMPTY, ID(5), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(106), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 512)),
        COAP_MSG(ACK, EMPTY, ID(6), NO_PAYLOAD),
        COAP_MSG(CON, GET, ID(107), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_REQ(1, 1024)),
        COAP_MSG(ACK, EMPTY, ID(7), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(100), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 BLOCK2_RES(0, 16, "")),

        // BLOCK Notify
        COAP_MSG(CON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 BLOCK2_RES(0, 16, NOTIFY_PAYLOAD)),
        // Note: further blocks should not contain the Observe option
        // see RFC 7959, Figure 12: "Observe Sequence with Block-Wise Response"
        COAP_MSG(CON, CONTENT, ID(1), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 16, NOTIFY_PAYLOAD)),
        COAP_MSG(CON, CONTENT, ID(2), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 32, NOTIFY_PAYLOAD)),
        COAP_MSG(CON, CONTENT, ID(3), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 64, NOTIFY_PAYLOAD)),
        COAP_MSG(CON, CONTENT, ID(4), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 128, NOTIFY_PAYLOAD)),
        COAP_MSG(CON, CONTENT, ID(5), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 256, NOTIFY_PAYLOAD)),
        COAP_MSG(CON, CONTENT, ID(6), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 512, NOTIFY_PAYLOAD)),
        COAP_MSG(CON, CONTENT, ID(7), TOKEN(MAKE_TOKEN("Notifaj")),
                 BLOCK2_RES(1, 1024, NOTIFY_PAYLOAD)),
    };

    streaming_handle_request_args_t args = {
        .env = &env,
        .expected_request_header = requests[0]->request_header,
        .response_header = {
            .code = responses[0]->response_header.code
        }
    };

    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);

    ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
            env.coap_ctx, streaming_handle_request, &args));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };
    test_streaming_payload_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[1]);
    expect_recv(&env, requests[1]);
    for (size_t i = 2; i < AVS_ARRAY_SIZE(responses); ++i) {
        expect_recv(&env, requests[2 * i - 2]);
        expect_send(&env, responses[i]);
        expect_recv(&env, requests[2 * i - 1]);
    }
    expect_timeout(&env);

    ASSERT_OK(avs_coap_notify_streaming(
            env.coap_ctx, observe_id,
            &(avs_coap_response_header_t) {
                .code = responses[1]->response_header.code
            },
            AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_streaming_writer,
            &test_payload));

    // should be canceled by cleanup
    expect_observe_cancel(&env, requests[0]->msg.token);
#        undef NOTIFY_PAYLOAD
}
#    endif // WITH_AVS_COAP_BLOCK

#    ifdef WITH_AVS_COAP_OBSERVE_PERSISTENCE
AVS_UNIT_TEST(observe_persistence, simple) {
#        define NOTIFY_PAYLOAD "Notifaj"
    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(0),
                 NO_PAYLOAD),
        COAP_MSG(CON, CONTENT, ID(0), TOKEN(MAKE_TOKEN("Obserw")), OBSERVE(1),
                 PAYLOAD(NOTIFY_PAYLOAD))
    };
    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.token
    };

    avs_stream_t *stream = avs_stream_membuf_create();
    ASSERT_NOT_NULL(stream);
    {
        test_env_t env
                __attribute__((cleanup(test_teardown_late_expects_check))) =
                        test_setup_default();

        streaming_handle_request_args_t args = {
            .env = &env,
            .expected_request_header = requests[0]->request_header,
            .response_header = responses[0]->response_header
        };

        avs_unit_mocksock_enable_recv_timeout_getsetopt(
                env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

        expect_recv(&env, requests[0]);
        expect_send(&env, responses[0]);

        ASSERT_OK(avs_coap_streaming_handle_incoming_packet(
                env.coap_ctx, streaming_handle_request, &args));

        avs_persistence_context_t persistence =
                avs_persistence_store_context_create(stream);
        ASSERT_OK(avs_coap_observe_persist(env.coap_ctx, observe_id,
                                           &persistence));

        // Canceled by cleanup.
        expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
    }

    {
        test_env_t env
                __attribute__((cleanup(test_teardown_late_expects_check))) =
                        test_setup_without_socket(NULL, 1024, 1024, NULL);

        avs_persistence_context_t persistence =
                avs_persistence_restore_context_create(stream);
        ASSERT_OK(avs_coap_observe_restore(env.coap_ctx, on_observe_cancel,
                                           &env, &persistence));

        avs_net_socket_t *socket = NULL;
        avs_unit_mocksock_create_datagram(&socket);
        avs_unit_mocksock_enable_inner_mtu_getopt(socket, 1500);

        avs_unit_mocksock_expect_connect(socket, NULL, NULL);
        avs_net_socket_connect(socket, NULL, NULL);

        ASSERT_OK(avs_coap_ctx_set_socket(env.coap_ctx, socket));
        env.mocksock = socket;

        test_streaming_payload_t test_payload = {
            .data = NOTIFY_PAYLOAD,
            .size = sizeof(NOTIFY_PAYLOAD) - 1
        };

        avs_unit_mocksock_enable_recv_timeout_getsetopt(
                env.mocksock, avs_time_duration_from_scalar(1, AVS_TIME_S));

        expect_send(&env, responses[1]);
        expect_recv(&env, requests[1]);
        expect_timeout(&env);
        ASSERT_OK(avs_coap_notify_streaming(
                env.coap_ctx, observe_id, &responses[1]->response_header,
                AVS_COAP_NOTIFY_PREFER_CONFIRMABLE, test_streaming_writer,
                &test_payload));
        // Canceled by cleanup.
        expect_observe_cancel(&env, MAKE_TOKEN("Obserw"));
    }
    avs_stream_cleanup(&stream);
#        undef NOTIFY_PAYLOAD
}
#    endif // WITH_AVS_COAP_OBSERVE_PERSISTENCE

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP) &&
       // defined(WITH_AVS_COAP_STREAMING_API) && defined(WITH_AVS_COAP_OBSERVE)
