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

#    include "tests/utils.h"

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./helper_functions.h"

static void test_request_handler_impl(avs_coap_ctx_t *ctx,
                                      const avs_coap_borrowed_msg_t *request,
                                      avs_buffer_t *payload_buffer,
                                      const test_msg_t *msg,
                                      size_t payload_offset,
                                      size_t payload_size,
                                      bool ignore_request_flag) {
    bool request_finished = (request->payload_offset + request->payload_size
                             == request->total_payload_size);

    ASSERT_EQ(request->payload_size, payload_size);
    ASSERT_EQ_BYTES_SIZED(request->token.bytes, msg->msg.content.token.bytes,
                          msg->msg.content.token.size);
    ASSERT_EQ_BYTES_SIZED(request->payload,
                          msg->msg.content.payload + payload_offset,
                          payload_size);

    if (ignore_request_flag) {
        ignore_request(ctx, &request->token);
    }

    if (request->payload_size) {
        avs_buffer_append_bytes(payload_buffer, request->payload,
                                request->payload_size);
    }

    ASSERT_EQ(request_finished,
              (payload_offset + payload_size == msg->msg.content.payload_size));
    if (request_finished) {
        avs_coap_borrowed_msg_t borrowed_msg = {
            .code = AVS_COAP_CODE_CONTENT,
            .token = request->token,
            .payload = avs_buffer_data(payload_buffer),
            .payload_size = avs_buffer_data_size(payload_buffer)
        };

        if (request_finished) {
            ASSERT_OK(send_response(ctx, &borrowed_msg));
            avs_buffer_reset(payload_buffer);
        }
    }
}

static void test_request_handler(avs_coap_ctx_t *ctx,
                                 const avs_coap_borrowed_msg_t *request,
                                 avs_buffer_t *payload_buffer,
                                 const test_msg_t *msg,
                                 size_t payload_offset,
                                 size_t payload_size) {
    return test_request_handler_impl(ctx, request, payload_buffer, msg,
                                     payload_offset, payload_size, false);
}

static void
test_canceling_request_handler(avs_coap_ctx_t *ctx,
                               const avs_coap_borrowed_msg_t *request,
                               avs_buffer_t *payload_buffer,
                               const test_msg_t *msg,
                               size_t payload_offset,
                               size_t payload_size) {
    return test_request_handler_impl(ctx, request, payload_buffer, msg,
                                     payload_offset, payload_size, true);
}

static avs_buffer_t *setup_request_handler_payload_buffer(void) {
    avs_buffer_t *result = NULL;
    ASSERT_OK(avs_buffer_create(&result, 1024));
    return result;
}

AVS_UNIT_TEST(coap_tcp_responding, single_get_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123"), PAYLOAD("DATA")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"), PAYLOAD("DATA"))
    };

    expect_recv(&env, exchange.request);
    expect_send(&env, exchange.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchange.request, 0,
                         exchange.request->msg.content.payload_size);
}

AVS_UNIT_TEST(coap_tcp_responding, request_with_one_byte_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("123"), PATH("")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("123"))
    };

    expect_recv(&env, exchange.request);
    expect_send(&env, exchange.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchange.request, 0,
                         exchange.request->msg.content.payload_size);
}

AVS_UNIT_TEST(coap_tcp_responding, multiple_get_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("1234"), PAYLOAD("ABCDE")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("1234"), PAYLOAD("ABCDE"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("5678"), PAYLOAD("FGHIJ")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("5678"), PAYLOAD("FGHIJ"))
        }
    };

    expect_recv(&env, exchanges[0].request);
    expect_send(&env, exchanges[0].response);

    expect_recv(&env, exchanges[1].request);
    expect_send(&env, exchanges[1].response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchanges[0].request, 0,
                         exchanges[0].request->msg.content.payload_size);
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchanges[1].request, 0,
                         exchanges[0].request->msg.content.payload_size);
}

AVS_UNIT_TEST(coap_tcp_responding, payload_always_nonzero) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    test_exchange_t exchanges[] = {
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("12345678"),
                                PAYLOAD("some payload"),
                                PATH("opts will occupy whole buffer")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("12345678"),
                                 PAYLOAD("some payload"))
        },
        {
            .request = COAP_MSG(GET, MAKE_TOKEN("12345678"),
                                PAYLOAD("some payload"),
                                PATH("options'll be 1 byte shorter")),
            .response = COAP_MSG(CONTENT, MAKE_TOKEN("12345678"),
                                 PAYLOAD("some payload"))
        }
    };

    expect_recv(&env, exchanges[0].request);
    expect_send(&env, exchanges[0].response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchanges[0].request, 0,
                         exchanges[0].request->msg.content.payload_size);

    expect_recv(&env, exchanges[1].request);
    expect_send(&env, exchanges[1].response);

    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchanges[1].request, 0, 1);
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchanges[1].request, 1,
                         exchanges[1].request->msg.content.payload_size - 1);
}

static void test_too_big_option(test_env_t *env) {
    test_exchange_t exchange = {
        .request =
                COAP_MSG(GET, MAKE_TOKEN("12345678"), PAYLOAD("some payload"),
                         PATH("this is really long option value wwwwww")),
        .response = COAP_MSG(INTERNAL_SERVER_ERROR,
                             MAKE_TOKEN("12345678")
#    ifdef WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
                                     ,
                             PAYLOAD("options too big")
#    endif // WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
                                     )
    };

    expect_recv(env, exchange.request);
    expect_send(env, exchange.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env->coap_ctx, &request));
    ASSERT_OK(receive_nonrequest_message(env->coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_responding, request_with_too_big_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    test_too_big_option(&env);
}

AVS_UNIT_TEST(coap_tcp_responding,
              request_with_too_big_options_and_then_valid_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    test_too_big_option(&env);

    test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("12345678"),
                            PAYLOAD("some payload"), PATH("opt")),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("12345678"),
                             PAYLOAD("some payload"))
    };

    expect_recv(&env, exchange.request);
    expect_send(&env, exchange.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer,
                         exchange.request, 0,
                         exchange.request->msg.content.payload_size);
}

AVS_UNIT_TEST(coap_tcp_responding, big_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    const char *payload_32B = "abcdefghijklmnopqrstuvwxyz123456";

    const test_msg_t *full_request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"),
                     PAYLOAD_EXTERNAL(payload_32B, strlen(payload_32B)));
    size_t first_chunk_size =
            IN_BUFFER_SIZE
            - (full_request->payload_offset - full_request->options_offset);

    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("12345678"),
                     PAYLOAD_EXTERNAL(payload_32B, strlen(payload_32B)));

    expect_recv(&env, full_request);
    expect_send(&env, response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer, full_request,
                         0, first_chunk_size);
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_request_handler(env.coap_ctx, &request, payload_buffer, full_request,
                         first_chunk_size,
                         full_request->msg.content.payload_size
                                 - first_chunk_size);
}

AVS_UNIT_TEST(coap_tcp_responding, request_sliced_after_short_header) {
#    define MSG_PAYLOAD "raz"
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    const test_msg_t *request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"), PAYLOAD(MSG_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("12345678"), PAYLOAD(MSG_PAYLOAD));

    avs_unit_mocksock_input(env.mocksock, request->data, request->token_offset);

    avs_coap_borrowed_msg_t borrowed_request;

    avs_unit_mocksock_input(env.mocksock, request->data + request->token_offset,
                            request->size - request->token_offset);
    expect_send(&env, response);
    ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
    test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                         request, 0, request->msg.content.payload_size);
#    undef MSG_PAYLOAD
}

AVS_UNIT_TEST(coap_tcp_responding, request_sliced_between_begin_and_payload) {
#    define MSG_PAYLOAD "raz dwa trzy cztery piec"

    const test_msg_t *request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"), PATH("test", "path"),
                     PAYLOAD(MSG_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("12345678"), PAYLOAD(MSG_PAYLOAD));

    for (size_t slice_pos = 1; slice_pos < request->payload_offset;
         slice_pos++) {
        test_env_t env = test_setup();
        avs_buffer_t *payload_buffer = setup_request_handler_payload_buffer();
        avs_unit_mocksock_input(env.mocksock, request->data, slice_pos);

        avs_coap_borrowed_msg_t borrowed_request;
        if (slice_pos != 2 // because TCP ctx tries to read 2 bytes first
                && slice_pos != request->token_offset
                && slice_pos != request->options_offset) {
            // non-request, because it isn't a complete message yet
            ASSERT_OK(receive_nonrequest_message(env.coap_ctx,
                                                 &borrowed_request));
        }

        avs_unit_mocksock_input(env.mocksock, request->data + slice_pos,
                                request->size - slice_pos);
        size_t first_chunk_size =
                IN_BUFFER_SIZE
                - (request->payload_offset - request->options_offset);
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, 0, first_chunk_size);

        expect_send(&env, response);
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));

        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, first_chunk_size,
                             request->msg.content.payload_size
                                     - first_chunk_size);

        avs_buffer_free(&payload_buffer);
        test_teardown(&env);
    }

#    undef MSG_PAYLOAD
}

AVS_UNIT_TEST(coap_tcp_responding, request_sliced_after_options) {
#    define MSG_PAYLOAD "raz dwa trzy cztery piec"

    const test_msg_t *request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"), PATH("test", "path"),
                     PAYLOAD(MSG_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("12345678"), PAYLOAD(MSG_PAYLOAD));

    size_t slice_pos = request->payload_offset;
    test_env_t env = test_setup();
    avs_buffer_t *payload_buffer = setup_request_handler_payload_buffer();
    avs_unit_mocksock_input(env.mocksock, request->data, slice_pos);

    avs_coap_borrowed_msg_t borrowed_request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &borrowed_request));

    avs_unit_mocksock_input(env.mocksock, request->data + slice_pos,
                            request->size - slice_pos);
    expect_send(&env, response);
    ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
    test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                         request, 0, request->msg.content.payload_size);
    avs_buffer_free(&payload_buffer);
    test_teardown(&env);

#    undef MSG_PAYLOAD
}

AVS_UNIT_TEST(coap_tcp_responding, request_payload_sliced) {
#    define MSG_PAYLOAD "raz dwa trzy cztery piec"

    const test_msg_t *request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"), PATH("test", "path"),
                     PAYLOAD(MSG_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("12345678"), PAYLOAD(MSG_PAYLOAD));

    for (size_t slice_pos = request->payload_offset + 1;
         slice_pos < IN_BUFFER_SIZE;
         slice_pos++) {
        test_env_t env = test_setup();
        avs_buffer_t *payload_buffer = setup_request_handler_payload_buffer();
        avs_unit_mocksock_input(env.mocksock, request->data, slice_pos);

        size_t first_chunk_size = slice_pos - request->payload_offset;

        avs_coap_borrowed_msg_t borrowed_request;
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, 0, first_chunk_size);

        avs_unit_mocksock_input(env.mocksock, request->data + slice_pos,
                                request->size - slice_pos);
        expect_send(&env, response);
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, first_chunk_size,
                             request->msg.content.payload_size
                                     - first_chunk_size);

        avs_buffer_free(&payload_buffer);
        test_teardown(&env);
    }

#    undef MSG_PAYLOAD
}

AVS_UNIT_TEST(coap_tcp_responding, request_payload_sliced_twice) {
#    define MSG_PAYLOAD "raz dwa trzy cztery piec"

    const test_msg_t *request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"), PATH("test", "path"),
                     PAYLOAD(MSG_PAYLOAD));
    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("12345678"), PAYLOAD(MSG_PAYLOAD));

    for (size_t slice_pos = IN_BUFFER_SIZE + 1; slice_pos < IN_BUFFER_SIZE;
         slice_pos++) {
        test_env_t env = test_setup();
        avs_buffer_t *payload_buffer = setup_request_handler_payload_buffer();
        avs_unit_mocksock_input(env.mocksock, request->data, slice_pos);

        size_t first_chunk_size =
                IN_BUFFER_SIZE
                - (request->payload_offset - request->options_offset);

        avs_coap_borrowed_msg_t borrowed_request;
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, 0, first_chunk_size);

        size_t second_chunk_size =
                slice_pos - request->payload_offset - first_chunk_size;
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, first_chunk_size, second_chunk_size);

        size_t third_chunk_size = request->msg.content.payload_size
                                  - first_chunk_size - second_chunk_size;

        avs_unit_mocksock_input(env.mocksock, request->data + slice_pos,
                                request->size - slice_pos);
        expect_send(&env, response);
        ASSERT_OK(receive_request_message(env.coap_ctx, &borrowed_request));
        test_request_handler(env.coap_ctx, &borrowed_request, payload_buffer,
                             request, first_chunk_size + second_chunk_size,
                             third_chunk_size);

        avs_buffer_free(&payload_buffer);
        test_teardown(&env);
    }

#    undef MSG_PAYLOAD
}

AVS_UNIT_TEST(coap_tcp_responding, duplicated_non_repeatable_critical_options) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    const char *payload = "raz dwa trzy cztery piec";

    const test_exchange_t exchange = {
        .request = COAP_MSG(GET, MAKE_TOKEN("12345678"), ACCEPT(1),
                            DUPLICATED_ACCEPT(2),
                            PAYLOAD_EXTERNAL(payload, strlen(payload))),
        .response = COAP_MSG(BAD_OPTION, MAKE_TOKEN("12345678"))
    };

    expect_recv(&env, exchange.request);
    expect_send(&env, exchange.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_responding, requests_from_two_contexts) {
    avs_shared_buffer_t *inbuf = avs_shared_buffer_new(IN_BUFFER_SIZE);
    avs_shared_buffer_t *outbuf = avs_shared_buffer_new(OUT_BUFFER_SIZE);

    test_env_t env1 =
            test_setup_with_external_buffers_without_mock_clock(inbuf, outbuf);
    test_env_t env2 =
            test_setup_with_external_buffers_without_mock_clock(inbuf, outbuf);
    _avs_mock_clock_start(avs_time_monotonic_from_scalar(0, AVS_TIME_S));

    avs_buffer_t *payload_buffer1 __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();
    avs_buffer_t *payload_buffer2 __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    const char *payload1 = "some payload which has to use shared buffer";
    test_exchange_t exchange1 = {
        .request = COAP_MSG(GET, MAKE_TOKEN("firstctx"),
                            PAYLOAD_EXTERNAL(payload1, strlen(payload1))),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("firstctx"),
                             PAYLOAD_EXTERNAL(payload1, strlen(payload1)))
    };
    size_t first_chunk_size = IN_BUFFER_SIZE
                              - (exchange1.request->payload_offset
                                 - exchange1.request->options_offset);
    expect_recv(&env1, exchange1.request);
    expect_send(&env1, exchange1.response);

    const char *payload2 = "another payload which will use shared buffer";
    test_exchange_t exchange2 = {
        .request = COAP_MSG(GET, MAKE_TOKEN("seconctx"),
                            PAYLOAD_EXTERNAL(payload2, strlen(payload2))),
        .response = COAP_MSG(CONTENT, MAKE_TOKEN("seconctx"),
                             PAYLOAD_EXTERNAL(payload2, strlen(payload2)))
    };

    size_t second_chunk_size = IN_BUFFER_SIZE
                               - (exchange2.request->payload_offset
                                  - exchange2.request->options_offset);

    expect_recv(&env2, exchange2.request);
    expect_send(&env2, exchange2.response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env1.coap_ctx, &request));
    test_request_handler(env1.coap_ctx, &request, payload_buffer1,
                         exchange1.request, 0, first_chunk_size);
    ASSERT_OK(receive_request_message(env1.coap_ctx, &request));
    test_request_handler(env1.coap_ctx, &request, payload_buffer1,
                         exchange1.request, first_chunk_size,
                         exchange1.request->msg.content.payload_size
                                 - first_chunk_size);

    ASSERT_OK(receive_request_message(env2.coap_ctx, &request));
    test_request_handler(env2.coap_ctx, &request, payload_buffer2,
                         exchange2.request, 0, second_chunk_size);
    ASSERT_OK(receive_request_message(env2.coap_ctx, &request));
    test_request_handler(env2.coap_ctx, &request, payload_buffer2,
                         exchange2.request, second_chunk_size,
                         exchange2.request->msg.content.payload_size
                                 - first_chunk_size);

    test_teardown_without_freeing_shared_buffers_and_mock_clock(&env1);
    test_teardown_without_freeing_shared_buffers_and_mock_clock(&env2);
    _avs_mock_clock_finish();

    avs_free(inbuf);
    avs_free(outbuf);
}

AVS_UNIT_TEST(coap_tcp_responding, cancel_exchange_while_receiving_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    avs_buffer_t *payload_buffer __attribute__((cleanup(avs_buffer_free))) =
            setup_request_handler_payload_buffer();

    const char *payload_32B = "abcdefghijklmnopqrstuvwxyz123456";

    const test_msg_t *full_request =
            COAP_MSG(GET, MAKE_TOKEN("12345678"),
                     PAYLOAD_EXTERNAL(payload_32B, strlen(payload_32B)));
    size_t first_chunk_size =
            IN_BUFFER_SIZE
            - (full_request->payload_offset - full_request->options_offset);

    expect_recv(&env, full_request);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_request_message(env.coap_ctx, &request));
    test_canceling_request_handler(env.coap_ctx, &request, payload_buffer,
                                   full_request, 0, first_chunk_size);
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
