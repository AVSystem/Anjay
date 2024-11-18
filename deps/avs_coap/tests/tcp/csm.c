/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./helper_functions.h"

typedef struct {
    avs_coap_send_result_t result;
    avs_error_t err;
    bool has_response;
    avs_coap_borrowed_msg_t response;
} test_handler_expected_response_t;

typedef AVS_LIST(
        test_handler_expected_response_t) test_handler_expect_responses_list_t;

typedef struct {
    test_handler_expect_responses_list_t expect_responses_list;
} response_handler_args_t;

static avs_coap_send_result_handler_result_t
test_response_handler(avs_coap_ctx_t *ctx,
                      avs_coap_send_result_t result,
                      avs_error_t err,
                      const avs_coap_borrowed_msg_t *response,
                      void *arg) {
    (void) ctx;
    response_handler_args_t *args = (response_handler_args_t *) arg;

    test_handler_expect_responses_list_t *expect_list =
            &args->expect_responses_list;
    ASSERT_NOT_NULL(expect_list);
    ASSERT_NOT_NULL(*expect_list);
    const test_handler_expected_response_t *expected = *expect_list;

    ASSERT_EQ(result, expected->result);
    if (avs_is_ok(expected->err)) {
        ASSERT_OK(err);
    } else {
        ASSERT_EQ(err.category, expected->err.category);
        ASSERT_EQ(err.code, expected->err.code);
    }

    if (expected->has_response) {
        const avs_coap_borrowed_msg_t *actual_res = response;
        const avs_coap_borrowed_msg_t *expected_res = &expected->response;

        ASSERT_EQ(actual_res->code, expected_res->code);
        ASSERT_TRUE(
                avs_coap_token_equal(&actual_res->token, &expected_res->token));
        if (result != AVS_COAP_SEND_RESULT_FAIL) {
            ASSERT_EQ(actual_res->options.size, expected_res->options.size);
            ASSERT_EQ_BYTES_SIZED(actual_res->options.begin,
                                  expected_res->options.begin,
                                  actual_res->options.size);
            ASSERT_EQ(actual_res->payload_size, expected_res->payload_size);
            ASSERT_EQ_BYTES_SIZED(actual_res->payload, expected_res->payload,
                                  actual_res->payload_size);
        }
    } else {
        ASSERT_NULL(response);
    }

    AVS_LIST_DELETE(expect_list);

    return AVS_COAP_RESPONSE_ACCEPTED;
}

static void expect_response_handler_call(response_handler_args_t *args,
                                         avs_coap_send_result_t result,
                                         avs_error_t err,
                                         const test_msg_t *msg) {
    test_handler_expected_response_t *expect =
            AVS_LIST_NEW_ELEMENT(test_handler_expected_response_t);

    expect->result = result;
    expect->err = err;
    expect->has_response = (msg != NULL);
    if (msg) {
        expect->response = msg->msg.content;
    }

    AVS_LIST_APPEND(&args->expect_responses_list, expect);
}

static response_handler_args_t setup_response_handler_args(void) {
    return (response_handler_args_t) { 0 };
}

static void cleanup_response_handler_args(response_handler_args_t *args) {
    ASSERT_NULL(args->expect_responses_list);
}

AVS_UNIT_TEST(coap_tcp_csm, request_before_peer_csm) {
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);
    ASSERT_NOT_NULL(outbuf);
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_external_buffers_without_mock_clock_and_peer_csm(
                    inbuf, outbuf);
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("A token"));
    const test_msg_t *response = COAP_MSG(CONTENT, MAKE_TOKEN("A token"));

    expect_send(&env, request);
    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));

    expect_recv(&env, COAP_MSG(CSM));
    avs_coap_borrowed_msg_t borrowed_request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &borrowed_request));

    expect_recv(&env, response);
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                 response);
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &borrowed_request));
}

AVS_UNIT_TEST(coap_tcp_csm, no_peer_csm) {
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);
    ASSERT_NOT_NULL(outbuf);
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_external_buffers_without_mock_clock_and_peer_csm(
                    inbuf, outbuf);

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("A token"));
    const test_msg_t *response = COAP_MSG(CONTENT, MAKE_TOKEN("A token"));

    expect_send(&env, request);
    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));

    expect_recv(&env, response);
    expect_send(&env, COAP_MSG(ABORT, MAKE_TOKEN("A token")));
    avs_coap_borrowed_msg_t borrowed_request;
    avs_error_t err =
            receive_nonrequest_message(env.coap_ctx, &borrowed_request);
    ASSERT_FAIL(err);
    ASSERT_EQ(err.category, AVS_COAP_ERR_CATEGORY);
    ASSERT_EQ(err.code, AVS_COAP_ERR_TCP_CSM_NOT_RECEIVED);

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
}

AVS_UNIT_TEST(coap_tcp_csm, signalling_without_peer_csm) {
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);
    ASSERT_NOT_NULL(outbuf);
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_external_buffers_without_mock_clock_and_peer_csm(
                    inbuf, outbuf);

    expect_recv(&env, COAP_MSG(PING, MAKE_TOKEN("A token")));
    expect_send(&env, COAP_MSG(ABORT, MAKE_TOKEN("A token")));
    avs_coap_borrowed_msg_t borrowed_request;
    avs_error_t err =
            receive_nonrequest_message(env.coap_ctx, &borrowed_request);
    ASSERT_FAIL(err);
    ASSERT_EQ(err.category, AVS_COAP_ERR_CATEGORY);
    ASSERT_EQ(err.code, AVS_COAP_ERR_TCP_CSM_NOT_RECEIVED);
}

AVS_UNIT_TEST(coap_tcp_csm, peer_csm_timeout) {
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    ASSERT_NOT_NULL(inbuf);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);
    ASSERT_NOT_NULL(outbuf);
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_external_buffers_without_mock_clock_and_peer_csm(
                    inbuf, outbuf);

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("A token"));

    _avs_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));

    expect_send(&env, request);
    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));

    avs_time_duration_t time_to_expiry = avs_sched_time_to_next(env.sched);
    ASSERT_TRUE(avs_time_duration_valid(time_to_expiry));

    _avs_mock_clock_advance(time_to_expiry);

    expect_send(&env,
                COAP_MSG(ABORT, PAYLOAD("CSM not received within timeout")));
    avs_sched_run(env.sched);

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
}

AVS_UNIT_TEST(coap_tcp_csm, error_sending_csm) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_without_socket();

    ASSERT_NULL(env.mocksock);
    avs_unit_mocksock_create(&env.mocksock);
    avs_unit_mocksock_enable_recv_timeout_getsetopt(
            env.mocksock, avs_time_duration_from_scalar(0, AVS_TIME_S));
    ASSERT_NOT_NULL(env.mocksock);
    avs_unit_mocksock_expect_connect(env.mocksock, NULL, NULL);
    avs_net_socket_connect(env.mocksock, NULL, NULL);

    // Attempt to send CSM
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ECONNRESET));
    // Failed, now send Abort
    expect_send(&env, COAP_MSG(ABORT, PAYLOAD("failed to send CSM")));
    ASSERT_FAIL(avs_coap_ctx_set_socket(env.coap_ctx, env.mocksock));
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
