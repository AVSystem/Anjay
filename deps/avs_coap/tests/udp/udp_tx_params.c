/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)

#    include <avsystem/coap/udp.h>

#    include <avsystem/commons/avs_unit_mock_helpers.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"
#    include "udp/avs_coap_udp_tx_params.h"

static const avs_coap_udp_tx_params_t DETERMINISTIC_TX_PARAMS = {
    .ack_timeout = {
        .seconds = 2,
        .nanoseconds = 0
    },
    .ack_random_factor = 1.0,
    .max_retransmit = 4,
    .nstart = 1
};

AVS_UNIT_TEST(udp_tx_params, correct_backoff) {
    avs_crypto_prng_ctx_t *prng_ctx = avs_crypto_prng_new(NULL, NULL);
    avs_coap_retry_state_t state;
    ASSERT_OK(_avs_coap_udp_initial_retry_state(&DETERMINISTIC_TX_PARAMS,
                                                prng_ctx, &state));
    size_t backoff_s = (size_t) DETERMINISTIC_TX_PARAMS.ack_timeout.seconds;
    ASSERT_EQ(state.retry_count, 0);
    ASSERT_EQ(state.recv_timeout.seconds, backoff_s);

    for (size_t i = 0; i < DETERMINISTIC_TX_PARAMS.max_retransmit; ++i) {
        ASSERT_FALSE(_avs_coap_udp_all_retries_sent(&state,
                                                    &DETERMINISTIC_TX_PARAMS));
        ASSERT_OK(_avs_coap_udp_update_retry_state(&state));
        backoff_s *= 2;
        ASSERT_EQ(state.recv_timeout.seconds, backoff_s);
    }
    ASSERT_TRUE(
            _avs_coap_udp_all_retries_sent(&state, &DETERMINISTIC_TX_PARAMS));
    avs_crypto_prng_free(&prng_ctx);
}

static void assert_tx_params_equal(const avs_coap_udp_tx_params_t *actual,
                                   const avs_coap_udp_tx_params_t *expected) {
    AVS_UNIT_ASSERT_EQUAL(actual->ack_timeout.seconds,
                          expected->ack_timeout.seconds);
    AVS_UNIT_ASSERT_EQUAL(actual->ack_timeout.nanoseconds,
                          expected->ack_timeout.nanoseconds);
    AVS_UNIT_ASSERT_EQUAL(actual->ack_random_factor,
                          expected->ack_random_factor);
    AVS_UNIT_ASSERT_EQUAL(actual->max_retransmit, expected->max_retransmit);
    AVS_UNIT_ASSERT_EQUAL(actual->nstart, expected->nstart);
}

AVS_UNIT_TEST(udp_tx_params, getting_and_setting_udp_tx_params) {
    // We need to set nstart to the default value, because in our tests
    // it is set to 999 by default
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_nstart(1);

    // First, check if the initial tx params are the default ones
    // (default params are specified by RFC 7252)
    const avs_coap_udp_tx_params_t *params =
            avs_coap_udp_ctx_get_tx_params(env.coap_ctx);
    ASSERT_NOT_NULL(params);
    assert_tx_params_equal(params, &AVS_COAP_DEFAULT_UDP_TX_PARAMS);

    // Try to set some invalid params (according to RFC 7252 ACK_TIMEOUT should
    // not be shorter than 1 second)
    avs_coap_udp_tx_params_t invalid_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    invalid_params.ack_timeout = (avs_time_duration_t) {
        .seconds = 0,
        .nanoseconds = 500000000
    };
    ASSERT_FAIL(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &invalid_params));

    // Make sure that the params are still default
    params = avs_coap_udp_ctx_get_tx_params(env.coap_ctx);
    ASSERT_NOT_NULL(params);
    assert_tx_params_equal(params, &AVS_COAP_DEFAULT_UDP_TX_PARAMS);

    // Set some valid parameters different than default ones
    ASSERT_OK(avs_coap_udp_ctx_set_tx_params(env.coap_ctx,
                                             &DETERMINISTIC_TX_PARAMS));

    // Try setting invalid params again
    ASSERT_FAIL(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &invalid_params));

    // Make sure that the params are unchanged - i.e. they are not set
    // to the invalid params nor reset
    params = avs_coap_udp_ctx_get_tx_params(env.coap_ctx);
    ASSERT_NOT_NULL(params);
    assert_tx_params_equal(params, &DETERMINISTIC_TX_PARAMS);
}

AVS_UNIT_TEST(udp_tx_params, ack_timeout_change) {
    // With deterministic setup it will be easier to measure the
    // differences when using different ACK timeouts
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.ack_random_factor = 1.0;
    tx_params.max_retransmit = 0;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 4096, 4096, NULL);

    const test_msg_t *failing_request =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *request = COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)));
    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &failing_request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, failing_request);
    avs_sched_run(env.sched);

    _avs_mock_clock_advance(avs_time_duration_from_scalar(2, AVS_TIME_S));

    // because the timeout expired, we expect a failure
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    avs_sched_run(env.sched);

    // we change the timeout using TX params setting function
    tx_params.ack_timeout = avs_time_duration_from_scalar(4, AVS_TIME_S);
    ASSERT_OK(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &tx_params));

    // and try to send a request once more
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    _avs_mock_clock_advance(avs_time_duration_from_scalar(2, AVS_TIME_S));

    // this time we are still waiting after 2 seconds
    avs_sched_run(env.sched);

    // and we can still handle the response
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

static inline double avg_factor(double factor) {
    return ((factor - 1.0) / 2.0) + 1.0;
}

static avs_error_t
fake_avs_coap_udp_initial_retry_state(const avs_coap_udp_tx_params_t *tx_params,
                                      avs_crypto_prng_ctx_t *prng_ctx,
                                      avs_coap_retry_state_t *out_retry_state) {
    (void) prng_ctx;

    if (!tx_params || !out_retry_state) {
        return avs_errno(AVS_EINVAL);
    }

    out_retry_state->retry_count = 0;
    out_retry_state->recv_timeout =
            avs_time_duration_fmul(tx_params->ack_timeout,
                                   avg_factor(tx_params->ack_random_factor));

    return AVS_OK;
}

AVS_UNIT_TEST(udp_tx_params, ack_random_factor_change) {
    AVS_UNIT_MOCK(_avs_coap_udp_initial_retry_state) =
            fake_avs_coap_udp_initial_retry_state;

    // factor which we will test here
    const double factor = 9.0;
    const int wait_s = 2;

    avs_coap_udp_tx_params_t tx_params = {
        .ack_timeout = avs_time_duration_from_scalar(wait_s, AVS_TIME_S),
        .ack_random_factor = factor,
        .max_retransmit = 0,
        .nstart = 1
    };
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 4096, 4096, NULL);

    const test_msg_t *failing_request =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *request = COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)));
    avs_coap_exchange_id_t id;

    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &failing_request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, failing_request);
    avs_sched_run(env.sched);

    // we wait longer than the random generator draws
    _avs_mock_clock_advance(avs_time_duration_fmul(tx_params.ack_timeout,
                                                   avg_factor(factor) + 1.0));

    // because the timeout expired, we expect a failure
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    avs_sched_run(env.sched);

    // it failed - let's try again
    tx_params.ack_timeout = avs_time_duration_from_scalar(4, AVS_TIME_S);
    ASSERT_OK(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &tx_params));

    // and try to send a request once more
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    // but now we wait shorter than the random generator draws
    _avs_mock_clock_advance(avs_time_duration_fmul(tx_params.ack_timeout,
                                                   avg_factor(factor) - 1.0));

    // this time we are still waiting after 2 seconds
    avs_sched_run(env.sched);

    // and we can still handle the response
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_tx_params, max_retransmit_change) {
    // We use deterministic setup with no random factor
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    tx_params.ack_random_factor = 1.0;
    tx_params.max_retransmit = 1;
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 4096, 4096, NULL);

    const test_msg_t *failing_request =
            COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)));
    const test_msg_t *request = COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)));
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1)));
    avs_coap_exchange_id_t id;

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &failing_request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, failing_request);
    avs_sched_run(env.sched);

    _avs_mock_clock_advance(avs_time_duration_from_scalar(2, AVS_TIME_S));

    expect_send(&env, failing_request);
    avs_sched_run(env.sched);

    _avs_mock_clock_advance(avs_time_duration_from_scalar(4, AVS_TIME_S));

    // after two trials the request should fail
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    avs_sched_run(env.sched);

    // we change the timeout using TX params setting function
    tx_params.max_retransmit = 2;
    ASSERT_OK(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &tx_params));

    // and try to send a request once more
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &request->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    _avs_mock_clock_advance(avs_time_duration_from_scalar(2, AVS_TIME_S));

    expect_send(&env, request);
    avs_sched_run(env.sched);

    _avs_mock_clock_advance(avs_time_duration_from_scalar(4, AVS_TIME_S));

    // but this time it is sent the third time
    expect_send(&env, request);
    avs_sched_run(env.sched);

    // and receives some response
    expect_recv(&env, response);
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_OK, response);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(udp_tx_params, nstart_increase) {
    // to this test the best will be high, deterministic timeout
    // with no retransmissions, for the sake of simplicity
    // and nstart=2 to test it
    avs_coap_udp_tx_params_t tx_params = {
        .ack_timeout = avs_time_duration_from_scalar(10, AVS_TIME_S),
        .ack_random_factor = 1.0,
        .max_retransmit = 0,
        .nstart = 1
    };
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 4096, 4096, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)))
    };
    avs_coap_exchange_id_t id[3];

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id[0], &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id[0]));
    expect_send(&env, requests[0]);

    // the second one should be sent as well
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id[1], &requests[1]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id[1]));

    avs_sched_run(env.sched);

    expect_recv(&env, responses[0]);
    expect_handler_call(&env, &id[0], AVS_COAP_CLIENT_REQUEST_OK, responses[0]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // we expect it to be sent after receiving the response
    expect_send(&env, requests[1]);

    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    avs_sched_run(env.sched);

    // but now we increase the nstart parameter
    tx_params.nstart = 2;
    ASSERT_OK(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &tx_params));

    // so the next request can be sent before recieving the response
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id[2], &requests[2]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id[2]));

    expect_send(&env, requests[2]);

    _avs_mock_clock_advance(avs_sched_time_to_next(env.sched));
    avs_sched_run(env.sched);

    for (int i = 1; i <= 2; i++) {
        expect_recv(&env, responses[i]);
        expect_handler_call(&env, &id[i], AVS_COAP_CLIENT_REQUEST_OK,
                            responses[i]);
        expect_has_buffered_data_check(&env, false);
        ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL,
                                                        NULL));
    }
}

AVS_UNIT_TEST(udp_tx_params, nstart_decrease) {
    // to this test the best will be high, deterministic timeout
    // with no retransmissions, for the sake of simplicity
    // and nstart=2 to test it
    avs_coap_udp_tx_params_t tx_params = {
        .ack_timeout = avs_time_duration_from_scalar(10, AVS_TIME_S),
        .ack_random_factor = 1.0,
        .max_retransmit = 0,
        .nstart = 2
    };
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&tx_params, 4096, 4096, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(ACK, CONTENT, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(ACK, CONTENT, ID(2), TOKEN(nth_token(2)))
    };
    avs_coap_exchange_id_t id[3];

    // both requests should be sent
    for (int request = 0; request <= 1; request++) {
        ASSERT_OK(avs_coap_client_send_async_request(
                env.coap_ctx, &id[request], &requests[request]->request_header,
                NULL, NULL, test_response_handler, &env.expects_list));
        ASSERT_TRUE(avs_coap_exchange_id_valid(id[request]));
        expect_send(&env, requests[request]);
    }

    avs_sched_run(env.sched);

    // the first one gets a response
    expect_recv(&env, responses[0]);
    expect_handler_call(&env, &id[0], AVS_COAP_CLIENT_REQUEST_OK, responses[0]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    avs_sched_run(env.sched);

    // but now we decrease the nstart parameter
    tx_params.nstart = 1;
    ASSERT_OK(avs_coap_udp_ctx_set_tx_params(env.coap_ctx, &tx_params));

    // so the next request must wait for the response for the previous one
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id[2], &requests[2]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id[2]));

    avs_sched_run(env.sched);

    expect_recv(&env, responses[1]);
    expect_handler_call(&env, &id[1], AVS_COAP_CLIENT_REQUEST_OK, responses[1]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_send(&env, requests[2]);

    avs_sched_run(env.sched);

    expect_recv(&env, responses[2]);
    expect_handler_call(&env, &id[2], AVS_COAP_CLIENT_REQUEST_OK, responses[2]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
