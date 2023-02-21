/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)

#    include <math.h>

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

static void test_synchronous_requests(test_env_t *env,
                                      response_handler_args_t *args,
                                      test_exchange_t exchanges[],
                                      size_t count) {
    for (size_t i = 0; i < count; i++) {
        test_exchange_t exchange = exchanges[i];
        expect_send(env, exchange.request);
        ASSERT_OK(send_request(env->coap_ctx, exchange.request,
                               test_response_handler, args));

        expect_recv(env, exchange.response);

        size_t payload_chunks = (size_t) ceil((double) exchange.response->size
                                              / IN_BUFFER_SIZE);

        // Single call if entire payload fits into buffer or if there's no
        // payload.
        if (payload_chunks <= 1) {
            expect_response_handler_call(args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                         exchange.response);
        } else {
            const uint8_t *payload_ptr =
                    (const uint8_t *) exchange.response->msg.content.payload;
            size_t first_chunk_size = IN_BUFFER_SIZE
                                      - (exchange.response->payload_offset
                                         - exchange.response->options_offset);
            expect_response_handler_call(
                    args, AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, AVS_OK,
                    COAP_MSG(CONTENT,
                             TOKEN(exchange.response->msg.content.token),
                             PAYLOAD_EXTERNAL(payload_ptr, first_chunk_size)));
            payload_ptr += first_chunk_size;

            for (size_t chunk_no = 1; chunk_no < payload_chunks - 1;
                 chunk_no++) {
                expect_response_handler_call(
                        args, AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, AVS_OK,
                        COAP_MSG(CONTENT,
                                 TOKEN(exchange.response->msg.content.token),
                                 PAYLOAD_EXTERNAL(payload_ptr,
                                                  IN_BUFFER_SIZE)));
                payload_ptr += IN_BUFFER_SIZE;
            }

            size_t remaining_bytes =
                    (exchange.response->msg.content.payload_size
                     - first_chunk_size)
                    % IN_BUFFER_SIZE;
            expect_response_handler_call(
                    args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                    COAP_MSG(CONTENT,
                             TOKEN(exchange.response->msg.content.token),
                             PAYLOAD_EXTERNAL(payload_ptr, remaining_bytes)));
        }

        for (size_t call = 0; call < payload_chunks; call++) {
            avs_coap_borrowed_msg_t request;
            ASSERT_OK(receive_nonrequest_message(env->coap_ctx, &request));
        }
    }
}

static void send_all_requests(test_env_t *env,
                              response_handler_args_t *args,
                              test_exchange_t exchanges[],
                              size_t count) {
    for (size_t i = 0; i < count; i++) {
        expect_send(env, exchanges[i].request);
        ASSERT_OK(send_request(env->coap_ctx, exchanges[i].request,
                               test_response_handler, args));
    }
}

static void expect_concatenated_responses(test_env_t *env,
                                          response_handler_args_t *args,
                                          test_exchange_t exchanges[],
                                          size_t count) {
    // Bytes have to be concatenated before passing to avs_unit_mocksock_input.
    size_t len = 0;
    uint8_t data[65536];

    for (size_t i = 0; i < count; i++) {
        assert(len + exchanges[i].response->size <= sizeof(data));
        memcpy(data + len, exchanges[i].response->data,
               exchanges[i].response->size);
        len += exchanges[i].response->size;
        expect_response_handler_call(args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                     exchanges[i].response);
    }
    avs_unit_mocksock_input(env->mocksock, data, len);
}

static void
expect_concatenated_responses_reversed(test_env_t *env,
                                       response_handler_args_t *args,
                                       test_exchange_t exchanges[],
                                       size_t count) {
    // Bytes have to be concatenated before passing to avs_unit_mocksock_input.
    size_t len = 0;
    uint8_t data[65536];

    for (ptrdiff_t i = (ptrdiff_t) count - 1; i >= 0; --i) {
        assert(len + exchanges[i].response->size <= sizeof(data));
        memcpy(data + len, exchanges[i].response->data,
               exchanges[i].response->size);
        len += exchanges[i].response->size;
        expect_response_handler_call(args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                     exchanges[i].response);
    }

    avs_unit_mocksock_input(env->mocksock, data, len);
}

static response_handler_args_t setup_response_handler_args(void) {
    return (response_handler_args_t) { 0 };
}

static void cleanup_response_handler_args(response_handler_args_t *args) {
    ASSERT_NULL(args->expect_responses_list);
}

AVS_UNIT_TEST(coap_tcp_requesting, single_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("A token"));
    const test_msg_t *response = COAP_MSG(CONTENT, MAKE_TOKEN("A token"));

    expect_send(&env, request);
    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));

    expect_recv(&env, response);
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                 response);
    avs_coap_borrowed_msg_t borrowed_request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &borrowed_request));
}

AVS_UNIT_TEST(coap_tcp_requesting, two_synchronous_requests) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("1234")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("1234"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("5678")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("5678"))
        }
    };

    test_synchronous_requests(&env, &args, exchanges,
                              AVS_ARRAY_SIZE(exchanges));
}

AVS_UNIT_TEST(coap_tcp_requesting, two_synchronous_requests_with_payload) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("1234"), PAYLOAD("ABCDE")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("1234"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("5678"), PAYLOAD("FGHIJ")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("5678"))
        }
    };

    test_synchronous_requests(&env, &args, exchanges,
                              AVS_ARRAY_SIZE(exchanges));
}

AVS_UNIT_TEST(coap_tcp_requesting, two_asynchronous_requests) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("AA token")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("AA token"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("BB token")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("BB token"))
        }
    };

    send_all_requests(&env, &args, exchanges, AVS_ARRAY_SIZE(exchanges));
    expect_concatenated_responses(&env, &args, exchanges,
                                  AVS_ARRAY_SIZE(exchanges));

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting, two_asynchronous_requests_with_options) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("1")),
            // Test requires any option, so PATH macro can be used.
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("1"), PATH("first"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("2")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("2"), PATH("second"))
        }
    };

    send_all_requests(&env, &args, exchanges, AVS_ARRAY_SIZE(exchanges));
    expect_concatenated_responses(&env, &args, exchanges,
                                  AVS_ARRAY_SIZE(exchanges));

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting,
              two_asynchronous_requests_with_payload_in_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("AA token")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("AA token"),
                                 PAYLOAD("12345678 12345678"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("BB token")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("BB token"),
                                 PAYLOAD("87654321 87654321"))
        }
    };

    send_all_requests(&env, &args, exchanges, AVS_ARRAY_SIZE(exchanges));
    expect_concatenated_responses(&env, &args, exchanges,
                                  AVS_ARRAY_SIZE(exchanges));

    // TODO eagain if message isn't finished
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting,
              two_asynchronous_requests_with_reversed_responses_order) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("AA token")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("AA token"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("BB token")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("BB token"))
        }
    };

    send_all_requests(&env, &args, exchanges, AVS_ARRAY_SIZE(exchanges));
    expect_concatenated_responses_reversed(&env, &args, exchanges,
                                           AVS_ARRAY_SIZE(exchanges));

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting, sliced_response) {
    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("12345678")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("12345678"))
    };

    for (size_t pos = 1; pos < exchange.response->size; pos++) {
        test_env_t env = test_setup();
        response_handler_args_t args = setup_response_handler_args();

        expect_send(&env, exchange.request);
        ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                               test_response_handler, &args));

        expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                     exchange.response);

        avs_coap_borrowed_msg_t request;
        avs_unit_mocksock_input(env.mocksock, exchange.response->data, pos);
        avs_unit_mocksock_input(env.mocksock,
                                exchange.response->data + pos,
                                exchange.response->size - pos);

        ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
        if (pos != exchange.response->token_offset
                && pos != exchange.response->options_offset) {
            ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
        }

        cleanup_response_handler_args(&args);
        test_teardown(&env);
    }
}

AVS_UNIT_TEST(coap_tcp_requesting, sliced_response_with_payload) {
    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("12345678")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("12345678"), PAYLOAD("123"))
    };

    for (size_t pos = 1; pos < exchange.response->size; pos++) {
        test_env_t env = test_setup();
        response_handler_args_t args = setup_response_handler_args();
        bool payload_splitted =
                (pos > exchange.response->size
                               - exchange.response->msg.content.payload_size)
                && pos < exchange.response->size;
        size_t first_chunk_size = 0;
        const uint8_t *payload_ptr =
                (const uint8_t *) exchange.response->msg.content.payload;

        if (payload_splitted) {
            first_chunk_size = exchange.response->msg.content.payload_size
                               - (exchange.response->size - pos);
            expect_response_handler_call(
                    &args, AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, AVS_OK,
                    COAP_MSG(CONTENT, MAKE_TOKEN("12345678"),
                             PAYLOAD_EXTERNAL(payload_ptr, first_chunk_size)));
            payload_ptr += first_chunk_size;
        }
        expect_response_handler_call(
                &args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                COAP_MSG(CONTENT, MAKE_TOKEN("12345678"),
                         PAYLOAD_EXTERNAL(
                                 payload_ptr,
                                 exchange.response->msg.content.payload_size
                                         - first_chunk_size)));
        expect_send(&env, exchange.request);
        ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                               test_response_handler, &args));

        avs_unit_mocksock_input(env.mocksock, exchange.response->data, pos);
        avs_unit_mocksock_input(env.mocksock, exchange.response->data + pos,
                                exchange.response->size - pos);

        avs_coap_borrowed_msg_t request;
        ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
        if (pos != exchange.response->token_offset
                && pos != exchange.response->options_offset) {
            ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
        }

        cleanup_response_handler_args(&args);
        test_teardown(&env);
    }
}

AVS_UNIT_TEST(coap_tcp_requesting, payload_as_big_as_buffer) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("A token")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("A token"),
                             PAYLOAD("xzznzzhmupjhnwwvgqtnwvayipxmjift"))
    };

    test_synchronous_requests(&env, &args, &exchange, 1);
}

AVS_UNIT_TEST(coap_tcp_requesting, payload_bigger_than_buffer) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("A token")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("A token"),
                             PAYLOAD("grngmywzejbodfbfvnmnqoueynsbqnsmt"))
    };

    test_synchronous_requests(&env, &args, &exchange, 1);
}

AVS_UNIT_TEST(coap_tcp_requesting, two_big_synchronous_requests) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("123")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"),
                                 PAYLOAD("erbzjattddxdxajluqtdenmsbfwinsvutafcg"
                                         "nwhsmhbqzsapxkhtdspirrvrssdm"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("456")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("456"),
                                 PAYLOAD("podfmebmwkesgalzwkatwzvybxzihwcnrxsco"
                                         "lnibrymgdzjflhtjvovlqwqcinpe"))
        }
    };

    test_synchronous_requests(&env, &args, exchanges,
                              AVS_ARRAY_SIZE(exchanges));
}

AVS_UNIT_TEST(coap_tcp_requesting, empty_message) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    // Request with empty message should be ignored.
    const test_msg_t *request = COAP_MSG(EMPTY, MAKE_TOKEN("123"));
    expect_recv(&env, request);

    avs_coap_borrowed_msg_t borrowed_request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &borrowed_request));
}

AVS_UNIT_TEST(coap_tcp_requesting, create_exchange_and_do_nothing) {
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("123"));

    expect_send(&env, request);
    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
}

AVS_UNIT_TEST(coap_tcp_requesting, cancel_exchange) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"))
    };

    expect_send(&env, exchange.request);
    expect_recv(&env, exchange.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                           test_response_handler, &args));
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
    cancel_delivery(env.coap_ctx, &exchange.request->msg.content.token);

    // Incoming data is interpreted as a response to canceled request and
    // ignored.
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting, cancel_during_receiving_of_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange1 = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"),
                             PAYLOAD("payload requiring two calls to handler"))
    };
    expect_send(&env, exchange1.request);
    expect_recv(&env, exchange1.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange1.request,
                           test_response_handler, &args));
    expect_response_handler_call(
            &args, AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, AVS_OK,
            COAP_MSG(CONTENT, MAKE_TOKEN("123"),
                     PAYLOAD("payload requiring two calls to ")));
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
    cancel_delivery(env.coap_ctx, &exchange1.request->msg.content.token);

    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));

    // Make additional exchange to ensure that payload which should be ignored
    // isn't interpreted as the next message.

    test_exchange_t exchange2 = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"))
    };

    expect_send(&env, exchange2.request);
    expect_recv(&env, exchange2.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange1.request,
                           test_response_handler, &args));
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                 exchange2.response);
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting, cancel_during_receiving_too_big_options) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange1 = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"),
                             PATH("long option value wwwwwwwwwwwwwwwwww"))
    };
    expect_send(&env, exchange1.request);
    expect_recv(&env, exchange1.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange1.request,
                           test_response_handler, &args));
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
    cancel_delivery(env.coap_ctx, &exchange1.request->msg.content.token);

    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));

    // Make additional exchange to ensure that payload which should be ignored
    // isn't interpreted as the next message.

    test_exchange_t exchange2 = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"))
    };

    expect_send(&env, exchange2.request);
    expect_recv(&env, exchange2.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange1.request,
                           test_response_handler, &args));
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                 exchange2.response);
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting, response_with_too_big_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("12345678")),
        .response =
                COAP_MSG(CONTENT,
                         MAKE_TOKEN("12345678"),
                         // Test requires any option, so PATH macro can be used.
                         PATH("this is really long option value wwwwww"))
    };

    expect_send(&env, exchange.request);
    expect_recv(&env, exchange.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                           test_response_handler, &args));
    expect_response_handler_call(
            &args, AVS_COAP_SEND_RESULT_FAIL,
            _avs_coap_err(AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED), NULL);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));

    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_requesting, error_in_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("12345678")),
        .response = COAP_MSG(INTERNAL_SERVER_ERROR, MAKE_TOKEN("12345678"))
    };

    expect_send(&env, exchange.request);
    expect_recv(&env, exchange.response);

    ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                           test_response_handler, &args));
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                 exchange.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

static inline bool has_scheduled_job(avs_sched_t *sched) {
    return avs_time_duration_valid(avs_sched_time_to_next(sched));
}

AVS_UNIT_TEST(coap_tcp_requesting, fail_on_timeout) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("12345678"));
    expect_send(&env, request);

    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));
    ASSERT_TRUE(has_scheduled_job(env.sched));
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_FAIL,
                                 _avs_coap_err(AVS_COAP_ERR_TIMEOUT), NULL);

    _avs_mock_clock_advance(env.timeout);
    avs_sched_run(env.sched);
    ASSERT_FALSE(has_scheduled_job(env.sched));
}

AVS_UNIT_TEST(coap_tcp_requesting,
              send_request_then_close_context_and_run_scheduler) {
    test_env_t env = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request = COAP_MSG(GET, MAKE_TOKEN("12345678"));
    expect_send(&env, request);

    ASSERT_OK(
            send_request(env.coap_ctx, request, test_response_handler, &args));
    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
    ASSERT_TRUE(has_scheduled_job(env.sched));

    test_teardown_without_freeing_scheduler(&env);

    ASSERT_FALSE(has_scheduled_job(env.sched));
    avs_sched_cleanup(&env.sched);
}

AVS_UNIT_TEST(coap_tcp_requesting, reschedule_on_partial_content) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("1234567")),
        .response =
                COAP_MSG(CONTENT, MAKE_TOKEN("1234567"),
                         PAYLOAD("litwo ojczyzno moja ty jestes jak zdrowie"))
    };

    expect_send(&env, exchange.request);
    ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                           test_response_handler, &args));

    expect_recv(&env, exchange.response);

    _avs_mock_clock_advance(avs_time_duration_diff(
            env.timeout, avs_time_duration_from_scalar(1, AVS_TIME_S)));
    avs_sched_run(env.sched);

    expect_response_handler_call(
            &args, AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, AVS_OK,
            COAP_MSG(CONTENT, MAKE_TOKEN("1234567"),
                     PAYLOAD("litwo ojczyzno moja ty jestes j")));
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_TRUE(has_scheduled_job(env.sched));

    _avs_mock_clock_advance(avs_time_duration_diff(
            env.timeout, avs_time_duration_from_scalar(1, AVS_TIME_S)));
    avs_sched_run(env.sched);

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_OK, AVS_OK,
                                 COAP_MSG(CONTENT, MAKE_TOKEN("1234567"),
                                          PAYLOAD("ak zdrowie")));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));

    // implementation detail: a no-op job is still scheduled, but executing it
    // should result in nothing being scheduled for later
    _avs_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));
    avs_sched_run(env.sched);
    ASSERT_FALSE(has_scheduled_job(env.sched));
}

AVS_UNIT_TEST(coap_tcp_requesting, reschedule_when_ignoring_message) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("1234567")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("1234567"),
                             PATH("ile cie trzeba cenic ten tylko sie dowie"))
    };

    expect_send(&env, exchange.request);
    ASSERT_OK(send_request(env.coap_ctx, exchange.request,
                           test_response_handler, &args));

    expect_recv(&env, exchange.response);

    expect_response_handler_call(
            &args, AVS_COAP_SEND_RESULT_FAIL,
            _avs_coap_err(AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED), NULL);

    _avs_mock_clock_advance(avs_time_duration_diff(
            env.timeout, avs_time_duration_from_scalar(1, AVS_TIME_S)));
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_TRUE(has_scheduled_job(env.sched));

    _avs_mock_clock_advance(avs_time_duration_diff(
            env.timeout, avs_time_duration_from_scalar(1, AVS_TIME_S)));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_TRUE(has_scheduled_job(env.sched));

    _avs_mock_clock_advance(avs_time_duration_diff(
            env.timeout, avs_time_duration_from_scalar(1, AVS_TIME_S)));

    // implementation detail: a no-op job is still scheduled, but executing it
    // should result in nothing being scheduled for later
    _avs_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));
    avs_sched_run(env.sched);
    ASSERT_FALSE(has_scheduled_job(env.sched));
}

AVS_UNIT_TEST(coap_tcp_requesting,
              send_two_requests_and_cancel_the_second_one) {
    test_env_t env = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request1 = COAP_MSG(GET, MAKE_TOKEN("12345678"));
    const test_msg_t *request2 = COAP_MSG(GET, MAKE_TOKEN("87654321"));

    expect_send(&env, request1);
    expect_send(&env, request2);

    ASSERT_OK(
            send_request(env.coap_ctx, request1, test_response_handler, &args));
    ASSERT_OK(
            send_request(env.coap_ctx, request2, test_response_handler, &args));

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);
    cancel_delivery(env.coap_ctx, &request2->msg.content.token);

    expect_response_handler_call(&args, AVS_COAP_SEND_RESULT_CANCEL,
                                 _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED),
                                 NULL);

    test_teardown_without_freeing_scheduler(&env);

    ASSERT_FALSE(has_scheduled_job(env.sched));
    avs_sched_cleanup(&env.sched);
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
