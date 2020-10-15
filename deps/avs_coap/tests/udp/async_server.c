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

#    include <avsystem/commons/avs_errno.h>

#    include <avsystem/coap/coap.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

AVS_UNIT_TEST(udp_async_server, coap_ping) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *ping = COAP_MSG(CON, EMPTY, ID(0), NO_PAYLOAD);
    const test_msg_t *pong = COAP_MSG(RST, EMPTY, ID(0), NO_PAYLOAD);

    // the library should handle CoAP ping internally
    expect_recv(&env, ping);
    expect_send(&env, pong);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_server, non_request_non_response_non_empty_is_ignored) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

#    define AVS_COAP_CODE_WTF AVS_COAP_CODE(6, 6)
    const test_msg_t *unknown = COAP_MSG(CON, WTF, ID(0), NO_PAYLOAD);
#    undef AVS_COAP_CODE_WTF

    // the library should ignore such request
    expect_recv(&env, unknown);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

static int
failing_nonblock_request_handler(avs_coap_server_ctx_t *ctx,
                                 const avs_coap_request_header_t *request,
                                 void *result) {
    (void) ctx;
    (void) request;

    return *(int *) result;
}

AVS_UNIT_TEST(udp_async_server, incoming_request_error_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("A token"));
    const test_msg_t *response =
            COAP_MSG(ACK, NOT_FOUND, ID(0), MAKE_TOKEN("A token"));

    int result_to_return = AVS_COAP_CODE_NOT_FOUND;

    expect_recv(&env, request);
    expect_send(&env, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, failing_nonblock_request_handler, &result_to_return));
}

AVS_UNIT_TEST(udp_async_server, incoming_request_content_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

#    define PAYLOAD_CONTENT "It's dangerous to go alone, take this"

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("A token"), NO_PAYLOAD);
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), MAKE_TOKEN("A token"),
                     PAYLOAD(PAYLOAD_CONTENT));

    test_payload_writer_args_t response_payload = {
        .payload = PAYLOAD_CONTENT,
        .payload_size = sizeof(PAYLOAD_CONTENT) - 1
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                &response_payload);
    expect_send(&env, response);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

#    undef PAYLOAD_CONTENT
}

static void payload_writer_fail_case(bool cancel_exchange) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("A token"), NO_PAYLOAD);
    const test_msg_t *response = COAP_MSG(ACK, INTERNAL_SERVER_ERROR, ID(0),
                                          MAKE_TOKEN("A token"), NO_PAYLOAD);

    test_payload_writer_args_t response_payload = {
        .coap_ctx = env.coap_ctx,
        .messages_until_fail = 1,
        .cancel_exchange = cancel_exchange
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                &response_payload);
    expect_send(&env, response);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    avs_error_t err = avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env);
    ASSERT_OK(err);
}

AVS_UNIT_TEST(udp_async_server, incoming_request_payload_writer_fail) {
    payload_writer_fail_case(false);
}

AVS_UNIT_TEST(udp_async_server,
              incoming_request_payload_writer_fail_and_cancel_exchange) {
    payload_writer_fail_case(true);
}

AVS_UNIT_TEST(udp_async_server, send_request_in_request_handler) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *incoming_request =
            COAP_MSG(CON, GET, ID(123), MAKE_TOKEN("A token"), NO_PAYLOAD);
    const test_msg_t *outgoing_response =
            COAP_MSG(ACK, CONTENT, ID(123), MAKE_TOKEN("A token"), NO_PAYLOAD);

    const test_msg_t *outgoing_request =
            COAP_MSG(NON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);
    const test_msg_t *incoming_response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);

    expect_recv(&env, incoming_request);
    expect_request_handler_call_and_force_sending_request(
            &env, AVS_COAP_SERVER_REQUEST_RECEIVED, incoming_request,
            &(avs_coap_response_header_t) {
                .code = outgoing_response->response_header.code
            },
            NULL);
    expect_send(&env, outgoing_request);
    expect_send(&env, outgoing_response);

    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, incoming_response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_server, incoming_request_echo_content) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

#    define PAYLOAD_CONTENT "It's dangerous to go alone, take this"

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("A token"),
                                         PAYLOAD(PAYLOAD_CONTENT));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), MAKE_TOKEN("A token"),
                     PAYLOAD(PAYLOAD_CONTENT));

    test_payload_writer_args_t response_payload = {
        .payload = PAYLOAD_CONTENT,
        .payload_size = sizeof(PAYLOAD_CONTENT) - 1
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                &response_payload);
    expect_send(&env, response);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

#    undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_async_server, cached_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_cache(1024);

#    define PAYLOAD_CONTENT                                                \
        "Krzysztofie, motyla noga, to jest glin o czystosci technicznej. " \
        "Smiem watpic, abys zdolal go pomalowac."

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("4m3l1num"),
                     PAYLOAD(PAYLOAD_CONTENT));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), MAKE_TOKEN("4m3l1num"),
                     PAYLOAD(PAYLOAD_CONTENT));

    test_payload_writer_args_t response_payload = {
        .payload = PAYLOAD_CONTENT,
        .payload_size = sizeof(PAYLOAD_CONTENT) - 1
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                &response_payload);
    expect_send(&env, response);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    // duplicate request is supposed to be handled internally by repeating
    // cached response
    expect_recv(&env, request);
    expect_send(&env, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    avs_coap_stats_t stats = avs_coap_get_stats(env.coap_ctx);
    ASSERT_EQ(stats.incoming_retransmissions_count, 1);
    ASSERT_EQ(stats.outgoing_retransmissions_count, 0);

    // another duplicated request
    expect_recv(&env, request);
    expect_send(&env, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    stats = avs_coap_get_stats(env.coap_ctx);
    ASSERT_EQ(stats.incoming_retransmissions_count, 2);
    ASSERT_EQ(stats.outgoing_retransmissions_count, 0);

#    undef PAYLOAD_CONTENT
}

AVS_UNIT_TEST(udp_async_server, truncated_request_full_token) {
    // 8 bytes for input buffer: enough for CoAP/UDP header and 4-byte token
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(NULL, 8, 1024, NULL);

    // messages with full tokens should get Request Entity Too Large response
    const test_msg_t *full_token_req =
            COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("AAA"), PAYLOAD("a"));
    const test_msg_t *full_token_res =
            COAP_MSG(ACK, REQUEST_ENTITY_TOO_LARGE, ID(0), MAKE_TOKEN("AAA"),
                     NO_PAYLOAD);

    expect_recv(&env, full_token_req);

    expect_send(&env, full_token_res);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, &env));
}

AVS_UNIT_TEST(udp_async_server, truncated_request_incomplete_token) {
    // 8 bytes for input buffer: enough for CoAP/UDP header and 4-byte token
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(NULL, 8, 1024, NULL);

    // messages with incomplete tokens should be ignored
    const test_msg_t *truncated_token_req =
            COAP_MSG(CON, GET, ID(0), MAKE_TOKEN("BBBBB"), NO_PAYLOAD);

    expect_recv(&env, truncated_token_req);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, &env));
}

AVS_UNIT_TEST(udp_async_server, truncated_response_full_token) {
    // 12 bytes for input buffer: enough for CoAP/UDP header and 8-byte token
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(NULL, 12, 1024, NULL);

    // messages with full tokens should get Request Entity Too Large response
    const test_msg_t *full_token_req =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *full_token_res =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)), PAYLOAD("a"));

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &full_token_req->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, full_token_req);
    avs_sched_run(env.sched);

    // receiving response should make the context call handler
    expect_recv(&env, full_token_res);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_server, truncated_response_incomplete_token) {
    // 11 bytes for input buffer: enough for CoAP/UDP header and 7-byte token
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup(NULL, 11, 1024, NULL);

    // messages with incomplete tokens should be ignored
    const test_msg_t *truncated_token_req =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);
    const test_msg_t *truncated_token_res =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);

    avs_coap_exchange_id_t id;

    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &truncated_token_req->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, truncated_token_req);
    avs_sched_run(env.sched);

    expect_recv(&env, truncated_token_res);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, &env));

    // this needs to be cleaned up during teardown
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
}

AVS_UNIT_TEST(udp_async_server, repeated_non_repeatable_critical_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    // From RFC7252:
    // 5.4.5:
    // "If a message includes an option with more occurrences than the option
    //  is defined for, each supernumerary option occurrence that appears
    //  subsequently in the message MUST be treated like an unrecognized option
    //  (see Section 5.4.1)."
    //
    // 5.4.1:
    // "Unrecognized options of class "critical" that occur in a Confirmable
    //  request MUST cause the return of a 4.02 (Bad Option) response. This
    //  response SHOULD include a diagnostic payload describing the unrecognized
    //  option(s) (see Section 5.5.2)."
    const test_msg_t *request = COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                         ACCEPT(1), DUPLICATED_ACCEPT(2));
    const test_msg_t *response =
            COAP_MSG(ACK, BAD_OPTION, ID(0), TOKEN(nth_token(0)));
    expect_recv(&env, request);
    expect_send(&env, response);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_server, nonempty_empty_messages) {
    const test_msg_t *requests[] = {
        COAP_MSG(ACK, EMPTY, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(ACK, EMPTY, ID(0), CONTENT_FORMAT_VALUE(1)),
        COAP_MSG(ACK, EMPTY, ID(0), PAYLOAD("zadowolony")),
        COAP_MSG(CON, EMPTY, ID(0), TOKEN(nth_token(0)))
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); i++) {
        test_env_t env = test_setup_default();
        expect_recv(&env, requests[i]);
        expect_timeout(&env);
        ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL,
                                                        NULL));
        test_teardown(&env);
    }
}

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_async_server, incoming_request_block_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

#        define REQUEST_PAYLOAD DATA_1KB "?"

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_AND_2_RES(1, 1024, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 1024, REQUEST_PAYLOAD))
    };

    test_payload_writer_args_t response_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                requests[0], NULL, NULL);
    expect_send(&env, responses[0]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[1],
                                &(avs_coap_response_header_t) {
                                    .code = responses[1]->response_header.code
                                },
                                &response_payload);
    expect_send(&env, responses[1]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[2]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_send(&env, responses[2]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_server, incoming_block_request_nonblock_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD "abcd1234"

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(1, 1024, false), PAYLOAD(RESPONSE_PAYLOAD))
    };

    test_payload_writer_args_t response_payload = {
        .payload = RESPONSE_PAYLOAD,
        .payload_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                requests[0], NULL, NULL);
    expect_send(&env, responses[0]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[1],
                                &(avs_coap_response_header_t) {
                                    .code = responses[1]->response_header.code
                                },
                                &response_payload);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_send(&env, responses[1]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_server, incoming_request_block_response_weird_sizes) {
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.nstart = 999;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 4096, 800, NULL);

#        define REQUEST_PAYLOAD DATA_1KB "?"

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(2, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(3, 256, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_REQ(2, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(4), TOKEN(nth_token(4)), BLOCK2_REQ(2, 256)),
        COAP_MSG(CON, GET, ID(5), TOKEN(nth_token(5)), BLOCK2_REQ(3, 256)),
        COAP_MSG(CON, GET, ID(6), TOKEN(nth_token(6)), BLOCK2_REQ(2, 512))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 512, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(2, 256, true)),
        COAP_MSG(ACK, CONTINUE, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(3, 256, true)),
        COAP_MSG(ACK, CONTENT, ID(3), TOKEN(nth_token(3)),
                 BLOCK1_AND_2_RES(2, 512, 512, REQUEST_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(4), TOKEN(nth_token(4)),
                 BLOCK2_RES(2, 256, REQUEST_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(5), TOKEN(nth_token(5)),
                 BLOCK2_RES(3, 256, REQUEST_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(6), TOKEN(nth_token(6)),
                 BLOCK2_RES(2, 512, REQUEST_PAYLOAD))
    };

    test_payload_writer_args_t response_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                requests[0], NULL, NULL);
    expect_send(&env, responses[0]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                requests[1], NULL, NULL);
    expect_send(&env, responses[1]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[2]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                requests[2], NULL, NULL);
    expect_send(&env, responses[2]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[3]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[3],
                                &(avs_coap_response_header_t) {
                                    .code = responses[3]->response_header.code
                                },
                                &response_payload);
    expect_send(&env, responses[3]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[4]);
    expect_send(&env, responses[4]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[5]);
    expect_send(&env, responses[5]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[6]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_send(&env, responses[6]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_server, block2_request_from_the_middle) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB DATA_1KB "!"

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(2, 1024)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(3, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(2, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(3, 1024, RESPONSE_PAYLOAD))
    };

    test_payload_writer_args_t response_payload = {
        .payload = RESPONSE_PAYLOAD,
        .expected_payload_offset = 2048,
        .payload_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                &response_payload);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_send(&env, responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_server, block2_request_not_in_order) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB "!"

    /**
     * avs_coap treats requests with BLOCK2 option where blocks numbers are not
     * in order as separate requests, not a single exchange. Treating them as
     * a single exchange would break contract for
     * @ref avs_coap_payload_writer_t .
     */
    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(2, 1024)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(2, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD))
    };

    test_payload_writer_args_t response_payload = {
        .payload = RESPONSE_PAYLOAD,
        .expected_payload_offset = 2048,
        .payload_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[0],
                                &(avs_coap_response_header_t) {
                                    .code = responses[0]->response_header.code
                                },
                                &response_payload);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
    expect_send(&env, responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    response_payload.expected_payload_offset = 1024;
    expect_recv(&env, requests[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[1],
                                &(avs_coap_response_header_t) {
                                    .code = responses[1]->response_header.code
                                },
                                &response_payload);
    expect_send(&env, responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_server, request_timeout_refresh) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_AND_2_RES(1, 1024, 1024, REQUEST_PAYLOAD)),
    };

    test_payload_writer_args_t response_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    expect_recv(&env, requests[0]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                requests[0], NULL, NULL);
    expect_send(&env, responses[0]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    // a timeout job should be scheduled
    ASSERT_TRUE(avs_time_duration_valid(avs_sched_time_to_next(env.sched)));

    const avs_time_duration_t EPSILON =
            avs_time_duration_from_scalar(1, AVS_TIME_S);

    _avs_mock_clock_advance(
            avs_time_duration_diff(avs_sched_time_to_next(env.sched), EPSILON));

    // receiving another request within deadline should refresh the timeout
    expect_recv(&env, requests[1]);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED,
                                requests[1],
                                &(avs_coap_response_header_t) {
                                    .code = responses[1]->response_header.code
                                },
                                &response_payload);
    expect_send(&env, responses[1]);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    _avs_mock_clock_advance(EPSILON);
    avs_sched_run(env.sched);
    ASSERT_TRUE(avs_time_duration_valid(avs_sched_time_to_next(env.sched)));

    // timeout job should still be running
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    avs_sched_run(env.sched);
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_server, request_timeout) {
#        define REQUEST_PAYLOAD DATA_1KB "?"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)),
                                         BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                     BLOCK1_RES(0, 1024, true));

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                request, NULL, NULL);
    expect_send(&env, response);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    // a timeout job should be scheduled
    ASSERT_TRUE(avs_time_duration_valid(avs_sched_time_to_next(env.sched)));

    // the scheduler should call cancel handler
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);

    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    avs_sched_run(env.sched);

    ASSERT_FALSE(avs_time_duration_valid(avs_sched_time_to_next(env.sched)));

#        undef REQUEST_PAYLOAD
}

#        define INVALID_BLOCK1(Seq, Size, Payload) \
            .block1 = {                            \
                .type = AVS_COAP_BLOCK1,           \
                .seq_num = Seq,                    \
                .size = Size,                      \
                .has_more = true                   \
            },                                     \
            .payload = Payload,                    \
            .payload_size =                        \
                    (assert(sizeof(Payload) - 1 < Size), sizeof(Payload) - 1)

AVS_UNIT_TEST(udp_async_server, invalid_block_opt_in_request) {
    // BLOCK1.has_more == 1 and BLOCK1.size != payload size
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                                         INVALID_BLOCK1(0, 1024, "test"));
    const test_msg_t *response =
            COAP_MSG(ACK, BAD_OPTION, ID(0), TOKEN(nth_token(0)));

    expect_recv(&env, request);
    expect_send(&env, response);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));
}

AVS_UNIT_TEST(udp_async_server, duplicated_block_requests) {
#        define RESPONSE_PAYLOAD DATA_1KB "?"
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                     BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD));
    const test_msg_t *error =
            COAP_MSG(ACK, INTERNAL_SERVER_ERROR, ID(0), TOKEN(nth_token(0)));

    test_payload_writer_args_t response_payload = {
        .payload = RESPONSE_PAYLOAD,
        .payload_size = sizeof(RESPONSE_PAYLOAD) - 1
    };

    expect_recv(&env, request);
    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_RECEIVED, request,
                                &(avs_coap_response_header_t) {
                                    .code = response->response_header.code
                                },
                                &response_payload);
    expect_send(&env, response);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(
            env.coap_ctx, test_accept_new_request, &env));

    // assert that the duplicated request will be treated as a new request, so
    // it will not call the existing request handler
    expect_recv(&env, request);
    expect_send(&env, error);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_request_handler_call(&env, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL,
                                NULL, NULL);
#        undef RESPONSE_PAYLOAD
}

#    else // WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_async_server_no_block, block1_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    // Equivalent to
    // COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
    //          BLOCK1_REQ(0, 1024, "test"))
    // but we're unable to easily construct such message if BLOCK support is
    // disabled.
    const uint8_t request[] = { 0x48, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0xD1, 0x0E,
                                0x06, 0xFF, 0x74, 0x65, 0x73, 0x74 };
    const test_msg_t *response =
            COAP_MSG(ACK, BAD_OPTION, ID(0), TOKEN(nth_token(0)));

    avs_unit_mocksock_input(env.mocksock, request, sizeof(request));
    expect_send(&env, response);

    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#    endif // WITH_AVS_COAP_BLOCK

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
