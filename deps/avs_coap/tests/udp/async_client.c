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

AVS_UNIT_TEST(udp_async_client, send_request_empty_get) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));
    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // receiving response should make the context call handler
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_non_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(NON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));
    avs_coap_exchange_id_t id;

    // a request should be sent
    expect_send(&env, request);
    ASSERT_OK(avs_coap_client_send_async_request(env.coap_ctx, &id,
                                                 &request->request_header, NULL,
                                                 NULL, NULL, NULL));

    // A response to NON request is not expected and should be ignored.
    // Because CoAP context does not associate any state with sent NON
    // requests, no ID is returned.
    ASSERT_FALSE(avs_coap_exchange_id_valid(id));

    expect_recv(&env, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_multiple_response_in_order) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, POST, ID(2), TOKEN(nth_token(2)))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(ACK, BAD_REQUEST, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(RST, EMPTY, ID(2), NO_PAYLOAD),
    };
    avs_coap_exchange_id_t ids[AVS_ARRAY_SIZE(requests)];

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        // send each request
        ASSERT_OK(avs_coap_client_send_async_request(
                env.coap_ctx, &ids[i], &requests[i]->request_header, NULL, NULL,
                test_response_handler, &env.expects_list));
        ASSERT_TRUE(avs_coap_exchange_id_valid(ids[i]));

        expect_send(&env, requests[i]);
        avs_sched_run(env.sched);
    }

    expect_recv(&env, responses[0]);
    expect_handler_call(&env, &ids[0], AVS_COAP_CLIENT_REQUEST_OK,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_OK,
                        responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_separate_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *separate_ack0 = COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD);
    const test_msg_t *response =
            COAP_MSG(CON, CONTENT, ID(1), TOKEN(nth_token(0)));
    const test_msg_t *separate_ack1 = COAP_MSG(ACK, EMPTY, ID(1), NO_PAYLOAD);

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // receiving separate ACK should not call the handler yet
    expect_recv(&env, separate_ack0);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // handler should be called after receiving the actual response
    // the library should also send separate ACK
    expect_recv(&env, response);
    expect_send(&env, separate_ack1);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_separate_response_failed_to_send) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *separate_ack0 = COAP_MSG(ACK, EMPTY, ID(0), NO_PAYLOAD);
    const test_msg_t *response =
            COAP_MSG(CON, CONTENT, ID(1), TOKEN(nth_token(0)));

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // receiving separate ACK should not call the handler yet
    expect_recv(&env, separate_ack0);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // handler should be called after receiving the actual response
    // the library should also send separate ACK, but it fails, thus
    // the exchange fails.
    expect_recv(&env, response);
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    ASSERT_FAIL(
            avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_separate_response_without_ack) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(CON, CONTENT, ID(1), TOKEN(nth_token(0)));
    const test_msg_t *separate_ack1 = COAP_MSG(ACK, EMPTY, ID(1), NO_PAYLOAD);

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // handler should be called after receiving the actual response even if
    // it's a Separate Response and separate ACK was not seen
    //
    // the library should also send separate ACK
    expect_recv(&env, response);
    expect_send(&env, separate_ack1);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_separate_non_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(NON, CONTENT, ID(1), TOKEN(nth_token(0)));

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // handler should be called after receiving the actual response even if
    // it's a Separate Response and separate ACK was not seen
    //
    // the library should NOT send ACK for NON response
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_put_with_payload) {
#    define CONTENT "shut up and take my payload"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    test_payload_writer_args_t test_payload = {
        .payload = CONTENT,
        .payload_size = sizeof(CONTENT) - 1
    };

    const test_msg_t *request =
            COAP_MSG(CON, PUT, ID(0), TOKEN(nth_token(0)), PAYLOAD(CONTENT));
    const test_msg_t *response =
            COAP_MSG(ACK, CHANGED, ID(0), TOKEN(nth_token(0)));

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, test_payload_writer,
            &test_payload, test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // receiving response should make the context call handler
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#    undef CONTENT
}

AVS_UNIT_TEST(udp_async_client, send_request_multiple_with_nstart) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_nstart(1);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, POST, ID(2), TOKEN(nth_token(2)))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(ACK, BAD_REQUEST, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(RST, EMPTY, ID(2), NO_PAYLOAD),
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t ids[AVS_ARRAY_SIZE(requests)];

    // Start all requests. Only the first one should be sent because of NSTART.
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        ASSERT_OK(avs_coap_client_send_async_request(
                env.coap_ctx, &ids[i], &requests[i]->request_header, NULL, NULL,
                test_response_handler, &env.expects_list));
        ASSERT_TRUE(avs_coap_exchange_id_valid(ids[i]));
    }

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // handlers should be called only after receiving responses
    expect_recv(&env, responses[0]);
    expect_handler_call(&env, &ids[0], AVS_COAP_CLIENT_REQUEST_OK,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_send(&env, requests[1]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_OK,
                        responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_send(&env, requests[2]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, send_request_with_retransmissions) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));
    avs_coap_exchange_id_t id;

    expect_send(&env, request);
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    // no jobs should be executed yet
    avs_sched_run(env.sched);
    avs_coap_stats_t stats = avs_coap_get_stats(env.coap_ctx);
    ASSERT_EQ(stats.outgoing_retransmissions_count, 0);

    // retransmissions should be handled by the scheduler
    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    expect_send(&env, request);
    avs_sched_run(env.sched);
    stats = avs_coap_get_stats(env.coap_ctx);
    ASSERT_EQ(stats.outgoing_retransmissions_count, 1);
    ASSERT_EQ(stats.incoming_retransmissions_count, 0);

    // the handler should only be called at this point
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, fail_if_no_response_after_retransmissions) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    avs_coap_exchange_id_t id;

    // send original request
    expect_send(&env, request);
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    // no jobs should be executed yet
    avs_sched_run(env.sched);

    // retransmissions should be handled by the scheduler
    for (size_t i = 0; i < env.tx_params.max_retransmit; ++i) {
        _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
        expect_send(&env, request);
        avs_sched_run(env.sched);
        avs_coap_stats_t stats = avs_coap_get_stats(env.coap_ctx);
        ASSERT_EQ(stats.outgoing_retransmissions_count, i + 1);
        ASSERT_EQ(stats.incoming_retransmissions_count, 0);
    }

    // At this point all retransmissions are done, and we are waiting for a
    // response to the last retransmission. After this time, scheduler should
    // call user-defined handler indicating failure.
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    avs_sched_run(env.sched);
}

AVS_UNIT_TEST(udp_async_client, cancel_single) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    avs_coap_exchange_id_t id;

    // send original request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // a retransmission job should be scheduled
    ASSERT_TRUE(avs_time_monotonic_valid(avs_sched_time_of_next(env.sched)));

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    avs_coap_exchange_cancel(env.coap_ctx, id);
}

AVS_UNIT_TEST(udp_async_client, invalid_cancel) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_coap_exchange_id_t id = {
        .value = 42
    };
    avs_coap_exchange_cancel(env.coap_ctx, id);
}

AVS_UNIT_TEST(udp_async_client, invalid_send) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    avs_coap_exchange_id_t id;
    const test_msg_t *response =
            COAP_MSG(CON, CONTENT, ID(0), TOKEN(nth_token(0)));
    ASSERT_FAIL(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &response->request_header, NULL, NULL,
            test_response_handler, NULL));
}

AVS_UNIT_TEST(udp_async_client, malformed_packets_are_ignored) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // garbage should be ignored
    avs_unit_mocksock_input(env.mocksock, "\x00", 1);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    avs_unit_mocksock_input(env.mocksock, "\x40\x00\x00\x00\x00", 5);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // receiving response should make the context call handler
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, cancels_all_exchanges_on_cleanup) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_deterministic();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, PUT, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, POST, ID(2), TOKEN(nth_token(2)))
    };
    avs_coap_exchange_id_t ids[AVS_ARRAY_SIZE(requests)];

    // only the first one should be sent; others are suspended because of
    // NSTART = 1
    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        ASSERT_OK(avs_coap_client_send_async_request(
                env.coap_ctx, &ids[i], &requests[i]->request_header, NULL, NULL,
                test_response_handler, &env.expects_list));
        ASSERT_TRUE(avs_coap_exchange_id_valid(ids[i]));
    }

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_handler_call(&env, &ids[0], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    // test_teardown will call avs_coap_ctx_cleanup() that fullfills expected
    // handler calls. If it does not, this test will fail on ASSERT_NULL()
    // in test_teardown.
}

AVS_UNIT_TEST(udp_async_client,
              send_request_piggybacked_response_matched_by_id_and_token) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *res_bad_id =
            COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(0)));
    const test_msg_t *res_bad_token =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(1)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));

    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // Piggybacked Response with mismatched message ID or token should be
    // ignored as invalid
    expect_recv(&env, res_bad_id);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, res_bad_token);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // No response received yet, we should see a retransmission
    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    expect_send(&env, request);
    avs_sched_run(env.sched);

    // handler should be called after receiving the actual response
    // the library should also send separate ACK
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client,
              repeated_non_repeatable_critical_option_in_piggybacked_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    // Accept option in response only for test purposes.
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)), ACCEPT(1),
                     DUPLICATED_ACCEPT(2));

    avs_coap_exchange_id_t id;
    avs_coap_client_send_async_request(env.coap_ctx, &id,
                                       &(avs_coap_request_header_t) {
                                           .code = request->msg.header.code
                                       },
                                       NULL, NULL, test_response_handler,
                                       &env.expects_list);

    expect_send(&env, request);
    avs_sched_run(env.sched);

    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client,
              repeated_non_repeatable_critical_option_in_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    // Accept option in response only for test purposes.
    const test_msg_t *ack = COAP_MSG(ACK, EMPTY, ID(0));
    const test_msg_t *response =
            COAP_MSG(CON, CONTENT, ID(0), TOKEN(nth_token(0)), ACCEPT(1),
                     DUPLICATED_ACCEPT(2));
    const test_msg_t *reset = COAP_MSG(RST, EMPTY, ID(0));

    avs_coap_exchange_id_t id;
    avs_coap_client_send_async_request(env.coap_ctx, &id,
                                       &(avs_coap_request_header_t) {
                                           .code = request->msg.header.code
                                       },
                                       NULL, NULL, test_response_handler,
                                       &env.expects_list);

    expect_send(&env, request);
    avs_sched_run(env.sched);

    expect_recv(&env, ack);
    expect_recv(&env, response);
    expect_send(&env, reset);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, invalid_ack_should_be_ignored) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *request_ack =
            COAP_MSG(ACK, EMPTY, ID(0), TOKEN(nth_token(0)));

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    expect_recv(&env, request_ack);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));

    // retransmission
    expect_send(&env, request);
    avs_sched_run(env.sched);

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
}

AVS_UNIT_TEST(udp_async_client, send_error) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    avs_coap_exchange_id_t id;

    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));

    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    avs_sched_run(env.sched);
}

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_async_client, block_response) {
#        define REQUEST_PAYLOAD "gib payload pls"
#        define DATA_33B "123456789 123456789 123456789 123"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 PAYLOAD(REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 16)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(2, 16)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 16, DATA_33B)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 16, DATA_33B)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(2, 16, DATA_33B)),
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // handlers should be called only after receiving responses

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef DATA_33B
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_response_interrupt) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD);
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                     BLOCK2_RES(0, 16, DATA_1KB));

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_abort_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // the used-defined handler aborts the exchange, causing another handler
    // call
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_async_client, block_response_last_block_without_block2_opt) {
#        define REQUEST_PAYLOAD "gib payload pls"
#        define DATA_17B "123456789 1234567"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(1);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    // Receiving a response without BLOCK2 should cause a failure
    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 PAYLOAD(REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 16)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 16, DATA_17B)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)), PAYLOAD("1")),
    };

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // handlers should be called only after receiving responses

    // receiving first response should make the context call handler and send
    // request for next block
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // receiving a response without BLOCK2 should cause exchange failure
    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef DATA_17B
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_with_explicit_block1) {
#        define REQUEST_PAYLOAD DATA_1KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(2, 1024, REQUEST_PAYLOAD)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(1, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(2, 1024, false)),
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // first Continue
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // second Continue
    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // upon receiving the response, handler should be called and no more
    // retransmissions scheduled
    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_with_broken_block1) {
#        define REQUEST_PAYLOAD DATA_1KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(2, 1024, REQUEST_PAYLOAD)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(1, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(2, 1024, false)),
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t id;

    // set "has_more" flag to false in the requested header, even though there
    // actually is more data - this flag will be overwritten before sending
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                      .block1 = {
                          .type = AVS_COAP_BLOCK1,
                          .seq_num = 0,
                          .size = 1024,
                          .has_more = false
                      },
                      PAYLOAD(REQUEST_PAYLOAD))
                     ->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // first Continue
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // second Continue
    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // upon receiving the response, handler should be called and no more
    // retransmissions scheduled
    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_without_explicit_block1) {
#        define REQUEST_PAYLOAD DATA_1KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const avs_coap_request_header_t *request_header_without_block1 =
            &COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD)
                     ->request_header;

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(2, 1024, REQUEST_PAYLOAD)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 1024, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(1, 1024, true)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(2, 1024, false)),
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, request_header_without_block1,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // first Continue
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // second Continue
    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // upon receiving the response, handler should be called and no more
    // retransmissions scheduled
    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, nonconfirmable_block_request) {
#        define REQUEST_PAYLOAD DATA_1KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const avs_coap_request_header_t *request_header_without_block1 =
            &COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), NO_PAYLOAD)
                     ->request_header;

    const test_msg_t *requests[] = {
        COAP_MSG(NON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(NON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(1, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(NON, GET, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(2, 1024, REQUEST_PAYLOAD)),
    };

    expect_send(&env, requests[0]);
    expect_send(&env, requests[1]);
    expect_send(&env, requests[2]);
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, NULL, request_header_without_block1,
            test_payload_writer, &test_payload, NULL, NULL));
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_with_cancel_in_payload_writer) {
#        define REQUEST_PAYLOAD DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    avs_coap_exchange_id_t id;

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1,
        .coap_ctx = env.coap_ctx,
        .cancel_exchange = false
    };

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                                         BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                     BLOCK1_RES(0, 1024, true));

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, test_payload_writer,
            &test_payload, test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // first Continue
    test_payload.exchange_id = id;
    test_payload.cancel_exchange = true;

    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // after receiving first Continue, payload_writer call is supposed to
    // cancel the exchange

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_block1_renegotiation) {
#        define REQUEST_PAYLOAD DATA_1KB DATA_16B "?"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(64, 16, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_REQ(65, 16, REQUEST_PAYLOAD))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTINUE, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_RES(0, 16, true)),
        COAP_MSG(ACK, CONTINUE, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_RES(64, 16, true)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK1_RES(65, 16, true))
    };

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_block2_renegotiation) {
#        define RESPONSE_PAYLOAD DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(0, 1024)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 512)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(3, 256))
    };
    const test_msg_t *responses[] = {
        // The server responds with a smaller block size than requested. We
        // should use that size for all further blocks.
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(2, 256, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(3, 256, RESPONSE_PAYLOAD))
    };

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_request_early_block2_response) {
    // Server may issue a non-Continue response even though we're not done
    // sending the request yet. In such case, we should stop generating any
    // more requests and start handling the response instead.
    //
    // The server may send a BLOCK-wise response to the BLOCK request. We
    // need to make sure we can handle it.

#        define REQUEST_PAYLOAD DATA_1KB "?"
#        define RESPONSE_PAYLOAD DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(0, 1024, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_AND_2_RES(0, 1024, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD))
    };

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, request_for_non_first_block_of_payload) {
#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB DATA_1KB DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(2, 1024)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(3, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(2, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(3, 1024, RESPONSE_PAYLOAD)),
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block2_request_and_too_big_response) {
#        define RESPONSE_PAYLOAD DATA_1KB
    const size_t input_buffer_size = 1024;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&AVS_COAP_DEFAULT_UDP_TX_PARAMS, input_buffer_size, 4096,
                       NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(0, 1024)),
        // the server responded with packet that did not fit into input buffer,
        // and async layer decided to retry the request with smaller block size
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(0, 512)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 512))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(0, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 512, RESPONSE_PAYLOAD))
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_send(&env, requests[i]);
        expect_recv(&env, responses[i]);
    }

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    avs_sched_run(env.sched);

    // the library sent a retry request with smaller block size, and we need to
    // handle response to it
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1]);

    // regular blockwise transfer continuation
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, valid_etag_in_blocks) {
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD), ETAG("tag")),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD), ETAG("tag"))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = requests[0]->request_header.code
            },
            NULL, NULL, test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, regular_request_and_too_big_response) {
#        define RESPONSE_PAYLOAD DATA_1KB
    const size_t input_buffer_size = 1024;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&AVS_COAP_DEFAULT_UDP_TX_PARAMS, input_buffer_size, 4096,
                       NULL);

    const test_msg_t *requests[] = {
        // NOTE: non-block request
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        // the server responded with packet that did not fit into input buffer,
        // and async layer decided to retry the request with smaller block size
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(0, 512)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 512))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(0, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 512, RESPONSE_PAYLOAD))
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_send(&env, requests[i]);
        expect_recv(&env, responses[i]);
    }

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    avs_sched_run(env.sched);

    // the library sent a retry request with smaller block size, and we need to
    // handle response to it
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1]);

    // regular blockwise transfer continuation
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client,
              regular_request_with_payload_and_too_big_response) {
#        define RESPONSE_PAYLOAD DATA_1KB
#        define REQUEST_PAYLOAD "RandomStuff"
    const size_t input_buffer_size = 1024;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&AVS_COAP_DEFAULT_UDP_TX_PARAMS, input_buffer_size, 4096,
                       NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 PAYLOAD(REQUEST_PAYLOAD)),
        // the server responded with BLOCK2 that did not fit into input buffer,
        // and async layer decided to retry the request with smaller block size
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_REQ_WITH_REGULAR_PAYLOAD(0, 512, REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 512))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(0, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 512, RESPONSE_PAYLOAD))
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_send(&env, requests[i]);
        expect_recv(&env, responses[i]);
    }

    avs_coap_exchange_id_t id;
    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1,
        .coap_ctx = env.coap_ctx,
        .cancel_exchange = false
    };
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    avs_sched_run(env.sched);

    test_payload.expected_payload_offset = 0;
    test_payload.exchange_id = id;

    // the library sent a retry request with smaller block size, and we need to
    // handle response to it
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1]);

    // regular blockwise transfer continuation
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, regular_request_and_too_big_nonblock_response) {
#        define RESPONSE_PAYLOAD DATA_1KB
    const size_t input_buffer_size = 1024;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&AVS_COAP_DEFAULT_UDP_TX_PARAMS, input_buffer_size, 4096,
                       NULL);

    const test_msg_t *requests[] = {
        // NOTE: non-block request
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        // the server responded with packet that did not fit into input buffer,
        // and async layer decided to retry the request with smaller block size
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(0, 512)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(1, 512))
    };
    const test_msg_t *responses[] = {
        // NOTE: non-block response
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 PAYLOAD(RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(0, 512, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 512, RESPONSE_PAYLOAD))
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(requests); ++i) {
        expect_send(&env, requests[i]);
        expect_recv(&env, responses[i]);
    }

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    avs_sched_run(env.sched);

    // the library sent a retry request with smaller block size, and we need to
    // handle response to it
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1]);

    // regular blockwise transfer continuation
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, invalid_etag_in_blocks) {
#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD), ETAG("tag")),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD), ETAG("nje"))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = requests[0]->request_header.code
            },
            NULL, NULL, test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, etag_in_not_all_responses) {
#        define RESPONSE_PAYLOAD DATA_1KB "!"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD), ETAG("tag")),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = requests[0]->request_header.code
            },
            NULL, NULL, test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0]);

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

#        define INVALID_BLOCK2(Seq, Size, Payload) \
            .block2 = {                            \
                .type = AVS_COAP_BLOCK2,           \
                .seq_num = Seq,                    \
                .size = Size,                      \
                .has_more = true                   \
            },                                     \
            .payload = Payload,                    \
            .payload_size =                        \
                    (assert(sizeof(Payload) - 1 < Size), sizeof(Payload) - 1)

AVS_UNIT_TEST(udp_async_client, invalid_block_opt_in_response) {
    // response with BLOCK2.has_more == 1 and BLOCK2.size != payload size
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(0, 1024));
    const test_msg_t *response =
            COAP_MSG(ACK, BAD_OPTION, ID(0), TOKEN(nth_token(0)),
                     INVALID_BLOCK2(0, 1024, "test"));

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
    ASSERT_NULL(env.expects_list);
}

AVS_UNIT_TEST(udp_async_client, block_response_skip) {
#        define REQUEST_PAYLOAD "gib payload pls"
#        define DATA_49B "123456789 123456789 123456789 123456789 123456789"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 PAYLOAD(REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(2, 16)),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)), BLOCK2_REQ(3, 16)),
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 16, DATA_49B)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(2, 16, DATA_49B)),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)),
                 BLOCK2_RES(3, 16, DATA_49B)),
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // handlers should be called only after receiving responses

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0],
                        .next_response_payload_offset = 40);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_send(&env, requests[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[1],
                        .expected_payload_offset = 8);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef DATA_49B
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(udp_async_client, block_response_initial_skip) {
    static const char RESPONSE_PAYLOAD[] = DATA_1KB DATA_1KB DATA_1KB "?";

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)), BLOCK2_REQ(1, 1024)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)), BLOCK2_REQ(3, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)),
                 BLOCK2_RES(3, 1024, RESPONSE_PAYLOAD))
    };
    AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(requests) == AVS_ARRAY_SIZE(responses),
                      mismatched_requests_responses_lists);

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(const avs_coap_request_header_t) {
                .code = AVS_COAP_CODE_GET
            },
            NULL, NULL, test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    ASSERT_OK(avs_coap_client_set_next_response_payload_offset(env.coap_ctx, id,
                                                               1500));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                        responses[0],
                        .next_response_payload_offset = 3072,
                        .expected_payload_offset = 476);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, responses[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#    else // WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_async_client_no_block, block2_response) {
#        define RESPONSE_PAYLOAD "test"
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *request = COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));

    // Equivalent to
    // COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)),
    //          BLOCK2_RES(0, 16, RESPONSE_PAYLOAD))
    // but we're unable to easily construct such message if BLOCK support is
    // disabled.
    const uint8_t response[] = { 0x68, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, 0x0A,
                                 0xFF, 0x74, 0x65, 0x73, 0x74 };
    avs_coap_exchange_id_t id;

    ASSERT_OK(avs_coap_client_send_async_request(env.coap_ctx,
                                                 &id,
                                                 &request->request_header,
                                                 NULL,
                                                 NULL,
                                                 test_response_handler,
                                                 &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    avs_unit_mocksock_input(env.mocksock, response, sizeof(response));
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
#        undef RESPONSE_PAYLOAD
}

#    endif // WITH_AVS_COAP_BLOCK

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
