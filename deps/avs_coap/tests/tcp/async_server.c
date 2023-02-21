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

#    define EXCHANGE_ID(Id)        \
        (avs_coap_exchange_id_t) { \
            .value = (Id)          \
        }

typedef enum {
    ACTION_NONE = 0,
    ACTION_FAIL,
    ACTION_CANCEL,
    ACTION_SETUP_NOT_FOUND_RESPONSE,
    ACTION_SETUP_VALID_RESPONSE,
#    ifdef WITH_AVS_COAP_OBSERVE
    ACTION_ACCEPT_OBSERVE,
#    endif // WITH_AVS_COAP_OBSERVE
    ACTION_HANDLE_INCOMING_PACKET,
    ACTION_SEND_REQUEST
} request_handler_action_t;

typedef struct {
    char *data;
    size_t size;
} payload_buf_t;

typedef struct {
    avs_coap_server_request_state_t result;
    size_t payload_offset;
    const void *payload;
    size_t payload_size;

    payload_buf_t *payload_writer_arg;
    // Optional action executed regardless of request state for simulating
    // non-standard behavior.
    request_handler_action_t action;
} expected_request_t;

typedef AVS_LIST(expected_request_t) expected_requests_list_t;

typedef struct {
    avs_coap_ctx_t *coap_ctx;
    size_t next_offset;
    avs_coap_exchange_id_t exchange_id;

    expected_requests_list_t expected_requests;
} request_handler_args_t;

static request_handler_args_t
setup_request_handler_args(avs_coap_ctx_t *ctx,
                           avs_coap_exchange_id_t exchange_id) {
    request_handler_args_t args = {
        .coap_ctx = ctx,
        .exchange_id = exchange_id
    };
    return args;
}

static void cleanup_request_handler_args(request_handler_args_t *args) {
    ASSERT_NULL(args->expected_requests);
}

static int test_payload_writer(size_t payload_offset,
                               void *payload_buf,
                               size_t payload_buf_size,
                               size_t *out_payload_chunk_size,
                               void *arg) {
    payload_buf_t *payload = (payload_buf_t *) arg;
    ASSERT_TRUE(payload_offset <= payload->size);

    *out_payload_chunk_size =
            AVS_MIN(payload_buf_size, payload->size - payload_offset);
    memcpy(payload_buf, payload->data + payload_offset,
           *out_payload_chunk_size);
    return 0;
}

static void expect_request_handler_call(request_handler_args_t *args,
                                        avs_coap_server_request_state_t result,
                                        const void *full_payload,
                                        size_t payload_size,
                                        request_handler_action_t action,
                                        payload_buf_t *payload_writer_arg) {
    expected_request_t *expect = AVS_LIST_NEW_ELEMENT(expected_request_t);
    ASSERT_NOT_NULL(expect);

    *expect = (expected_request_t) {
        .result = result,
        .payload_offset = args->next_offset,
        .payload = full_payload + args->next_offset,
        .payload_size = payload_size,
        .payload_writer_arg = payload_writer_arg,
        .action = action
    };
    AVS_LIST_APPEND(&args->expected_requests, expect);

    args->next_offset += payload_size;
}

static void expect_partial_content(request_handler_args_t *args,
                                   const void *payload,
                                   size_t payload_size,
                                   request_handler_action_t action,
                                   payload_buf_t *payload_writer_arg) {
    expect_request_handler_call(args, AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,
                                payload, payload_size, action,
                                payload_writer_arg);
}

static void expect_last_chunk(request_handler_args_t *args,
                              const void *payload,
                              size_t payload_size,
                              request_handler_action_t action,
                              payload_buf_t *payload_writer_arg) {
    expect_request_handler_call(args, AVS_COAP_SERVER_REQUEST_RECEIVED, payload,
                                payload_size, action, payload_writer_arg);
}

static void expect_cleanup(request_handler_args_t *args) {
    expect_request_handler_call(args, AVS_COAP_SERVER_REQUEST_CLEANUP, NULL, 0,
                                false, NULL);
}

static void validate_request(const avs_coap_server_async_request_t *actual,
                             const expected_request_t *expected) {
    ASSERT_NOT_NULL(actual);
    ASSERT_EQ(actual->header.code, AVS_COAP_CODE_GET);
    ASSERT_EQ(actual->payload_offset, expected->payload_offset);
    ASSERT_EQ(actual->payload_size, expected->payload_size);
    ASSERT_EQ_BYTES_SIZED(actual->payload, expected->payload,
                          actual->payload_size);
}

static int handle_new_request(avs_coap_server_ctx_t *ctx,
                              const avs_coap_request_header_t *request,
                              void *arg);

static int test_request_handler(avs_coap_request_ctx_t *ctx,
                                avs_coap_exchange_id_t exchange_id,
                                avs_coap_server_request_state_t result,
                                const avs_coap_server_async_request_t *request,
                                const avs_coap_observe_id_t *observe_id,
                                void *arg) {
    (void) observe_id;

    ASSERT_NOT_NULL(arg);
    request_handler_args_t *args = (request_handler_args_t *) arg;
    expected_request_t *expected =
            (expected_request_t *) args->expected_requests;
    ASSERT_NOT_NULL(expected);

    ASSERT_TRUE(avs_coap_exchange_id_equal(exchange_id, args->exchange_id));
    ASSERT_EQ(result, expected->result);

    if (result == AVS_COAP_SERVER_REQUEST_CLEANUP) {
        ASSERT_NULL(request);
        AVS_LIST_DELETE(&args->expected_requests);
        return 0;
    }

    validate_request(request, expected);

    int retval = 0;
    if (args->expected_requests->action != ACTION_NONE) {
        switch (args->expected_requests->action) {
        case ACTION_CANCEL:
            // Delete from list first, because cancel will call this handler
            // again.
            AVS_LIST_DELETE(&args->expected_requests);
            avs_coap_exchange_cancel(args->coap_ctx, exchange_id);
            return 0;

        case ACTION_SETUP_NOT_FOUND_RESPONSE:
        case ACTION_SETUP_VALID_RESPONSE:
            ASSERT_OK(avs_coap_server_setup_async_response(
                    ctx,
                    &(avs_coap_response_header_t) {
                        .code = (args->expected_requests->action
                                 == ACTION_SETUP_VALID_RESPONSE)
                                        ? AVS_COAP_CODE_VALID
                                        : AVS_COAP_CODE_NOT_FOUND
                    },
                    expected->payload_writer_arg ? test_payload_writer : NULL,
                    expected->payload_writer_arg));
            break;

#    ifdef WITH_AVS_COAP_OBSERVE
        case ACTION_ACCEPT_OBSERVE:
            ASSERT_OK(
                    avs_coap_observe_async_start(ctx, *observe_id, NULL, NULL));
            retval = AVS_COAP_CODE_CONTENT;
            break;
#    endif // WITH_AVS_COAP_OBSERVE

        case ACTION_FAIL:
            retval = -1;
            break;

        case ACTION_HANDLE_INCOMING_PACKET:
            ASSERT_FAIL(handle_incoming_packet(ctx->coap_ctx,
                                               handle_new_request, NULL));
            retval = AVS_COAP_CODE_CONTENT;
            break;

        case ACTION_SEND_REQUEST: {
            avs_coap_request_header_t header = {
                .code = AVS_COAP_CODE_GET
            };
            ASSERT_OK(avs_coap_client_send_async_request(
                    ctx->coap_ctx, NULL, &header, NULL, NULL, NULL, NULL));
            retval = AVS_COAP_CODE_CONTENT;
            break;
        }

        default:
            ASSERT_NULL("unexpected");
        }
        AVS_LIST_DELETE(&args->expected_requests);
        return retval;
    }

    retval = -1;
    switch (result) {
    case AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT:
        retval = 0;
        break;

    case AVS_COAP_SERVER_REQUEST_RECEIVED:
        if (expected->payload_writer_arg) {
            ASSERT_OK(avs_coap_server_setup_async_response(
                    ctx,
                    &(avs_coap_response_header_t) {
                        .code = AVS_COAP_CODE_CONTENT
                    },
                    test_payload_writer, expected->payload_writer_arg));
            retval = 0;
        } else {
            retval = AVS_COAP_CODE_CONTENT;
        }
        break;

    default:
        break;
    }

    AVS_LIST_DELETE(&args->expected_requests);
    return retval;
}

static int handle_new_request(avs_coap_server_ctx_t *ctx,
                              const avs_coap_request_header_t *request,
                              void *arg) {
    (void) request;

    avs_coap_exchange_id_t id =
            avs_coap_server_accept_async_request(ctx, test_request_handler,
                                                 arg);
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));
    return 0;
}

AVS_UNIT_TEST(tcp_async_server, handle_request_partial) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(0)), PAYLOAD("PlacLaduj"));
    const test_msg_t *res = COAP_MSG(CONTENT, TOKEN(nth_token(0)));

    expect_sliced_recv(&env, req, req->payload_offset + 4);

    expect_partial_content(&args, req->msg.content.payload, 4, ACTION_NONE,
                           NULL);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_send(&env, res);

    expect_last_chunk(&args, req->msg.content.payload, 5, ACTION_NONE, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_server,
              send_response_after_receiving_first_payload_chunk) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(0)), PAYLOAD("PlacLaduj"));
    const test_msg_t *res = COAP_MSG(VALID, TOKEN(nth_token(0)));

    avs_unit_mocksock_input(env.mocksock, req->data, req->size - 1);
    expect_send(&env, res);
    expect_has_buffered_data_check(&env, false);

    expect_partial_content(&args, req->msg.content.payload,
                           sizeof("PlacLaduj") - 2, ACTION_SETUP_VALID_RESPONSE,
                           NULL);
    expect_cleanup(&args);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    avs_unit_mocksock_input(env.mocksock, req->data + req->size - 1, 1);
    expect_has_buffered_data_check(&env, false);
    // Request handler shouldn't be called
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server, handle_incoming_packet_in_request_handler) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(0)), PAYLOAD("PlacLaduj"));
    const test_msg_t *res = COAP_MSG(CONTENT, TOKEN(nth_token(0)));

    expect_recv(&env, req);
    expect_send(&env, res);
    expect_has_buffered_data_check(&env, false);
    expect_last_chunk(&args, req->msg.content.payload,
                      req->msg.content.payload_size,
                      ACTION_HANDLE_INCOMING_PACKET, NULL);
    expect_cleanup(&args);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server, connection_closed_by_peer) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    expect_send(&env, COAP_MSG(ABORT));
    env.aborted = true;
    // Check if there's no infinite loop inside.
    // mocksock is returning success and 0 bytes received. IRL it means, that
    // connection was closed by peer.
    avs_error_t err =
            handle_incoming_packet(env.coap_ctx, handle_new_request, NULL);
    ASSERT_TRUE(avs_is_err(err));
    ASSERT_EQ(err.code, AVS_COAP_ERR_TCP_CONN_CLOSED);
}

AVS_UNIT_TEST(tcp_async_server, handle_request_with_options_partial) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req = COAP_MSG(GET, TOKEN(nth_token(0)), ACCEPT(123),
                                     PAYLOAD("PlacLaduj"));
    const test_msg_t *res = COAP_MSG(CONTENT, TOKEN(nth_token(0)));

    expect_sliced_recv(&env, req, req->payload_offset + 4);

    expect_partial_content(&args, req->msg.content.payload, 4, ACTION_NONE,
                           NULL);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_send(&env, res);

    expect_last_chunk(&args, req->msg.content.payload, 5, ACTION_NONE, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_server, empty_token) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req = COAP_MSG(GET, PAYLOAD("PlacLaduj"));
    const test_msg_t *res = COAP_MSG(CONTENT);

    expect_recv(&env, req);
    expect_last_chunk(&args, req->msg.content.payload, sizeof("PlacLaduj") - 1,
                      ACTION_NONE, NULL);
    expect_send(&env, res);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server,
              cancel_exchange_after_receiving_first_chunk_of_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(1)), PAYLOAD("poprosze"));
    const test_msg_t *res =
            COAP_MSG(INTERNAL_SERVER_ERROR, TOKEN(nth_token(1)));

    avs_unit_mocksock_input(env.mocksock, req->data, req->size - 5);
    expect_send(&env, res);

    expect_partial_content(&args, req->msg.content.payload, 3, ACTION_CANCEL,
                           NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    // Handler shouldn't be called again.
    avs_unit_mocksock_input(env.mocksock, req->data + req->size - 5, 5);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server,
              setup_response_after_receiving_first_chunk_of_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(1)), PAYLOAD("poprosze"));
    const test_msg_t *res = COAP_MSG(VALID, TOKEN(nth_token(1)));

    avs_unit_mocksock_input(env.mocksock, req->data, req->size - 5);
    expect_send(&env, res);

    expect_partial_content(&args, req->msg.content.payload, 3,
                           ACTION_SETUP_VALID_RESPONSE, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    // Handler shouldn't be called again.
    avs_unit_mocksock_input(env.mocksock, req->data + req->size - 5, 5);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server,
              message_with_bad_options_and_then_valid_message) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = { COAP_MSG(GET, TOKEN(nth_token(0)),
                                              ACCEPT(1), DUPLICATED_ACCEPT(2)),
                                     COAP_MSG(GET, TOKEN(nth_token(1))) };

    const test_msg_t *responses[] = { COAP_MSG(BAD_OPTION, TOKEN(nth_token(0))),
                                      COAP_MSG(CONTENT, TOKEN(nth_token(1))) };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_last_chunk(&args, NULL, 0, ACTION_NONE, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server, too_long_option_and_then_valid_message) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)),
                 PATH("ilecietrzebacenictentylkosiedowie ")),
        COAP_MSG(GET, TOKEN(nth_token(1)))
    };

    const test_msg_t *responses[] = { COAP_MSG(INTERNAL_SERVER_ERROR,
                                               TOKEN(nth_token(0))
#    ifdef WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
                                                       ,
                                               PAYLOAD("options too big")
#    endif // WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
                                                       ),
                                      COAP_MSG(CONTENT, TOKEN(nth_token(1))) };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_last_chunk(&args, NULL, 0, ACTION_NONE, NULL);
    expect_has_buffered_data_check(&env, true);
    expect_cleanup(&args);
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server, malformed_options) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    // 0.01 Get, 1 byte of truncated option.
    // Such message should be handled and Bad Option response should be sent.
    uint8_t buf[] = { 0x10, 0x01, 0x11 };
    avs_unit_mocksock_input(env.mocksock, buf, sizeof(buf));
    expect_send(&env, COAP_MSG(BAD_OPTION));
    expect_has_buffered_data_check(&env, false);

    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_server, message_sliced_after_valid_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    uint8_t buf[] = {
        0x20, // 2 bytes of options + payload
        0x41, // 4.01 Created
        0x10, // If-Match empty option
        0x10, // If-Match empty option
        0x20  // first byte of the next message
    };
    const size_t first_input_size = 3;
    avs_unit_mocksock_input(env.mocksock, buf, first_input_size);
    expect_has_buffered_data_check(&env, true);
    avs_unit_mocksock_input(env.mocksock, buf + first_input_size,
                            sizeof(buf) - first_input_size);
    expect_has_buffered_data_check(&env, true);

    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_server, receive_ping_with_payload) {
#    define PAYLOAD_DATA \
        "abcdefgh12345678abcdefgh12345678abcdefgh12345678abcdefgh12345678"

    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    const test_msg_t *ping = COAP_MSG(PING, PAYLOAD(PAYLOAD_DATA));
    const test_msg_t *pong = COAP_MSG(PONG, CUSTODY);

    expect_recv(&env, ping);
    expect_has_buffered_data_check(&env, true);
    expect_has_buffered_data_check(&env, true);
    expect_send(&env, pong);

    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_server, send_request_in_request_handler) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *incoming_request = COAP_MSG(GET, MAKE_TOKEN("A token"));
    const test_msg_t *outgoing_response =
            COAP_MSG(CONTENT, MAKE_TOKEN("A token"));

    const test_msg_t *outgoing_request = COAP_MSG(GET, TOKEN(nth_token(1)));
    const test_msg_t *incoming_response =
            COAP_MSG(CONTENT, TOKEN(nth_token(1)));

    expect_recv(&env, incoming_request);
    expect_last_chunk(&args, NULL, 0, ACTION_SEND_REQUEST, NULL);

    expect_send(&env, outgoing_request);
    expect_send(&env, outgoing_response);

    expect_cleanup(&args);

    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, incoming_response);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(tcp_async_server, incoming_small_bert1_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(0)), BERT1_REQ(0, 2048, DATA_16B));
    const test_msg_t *res =
            COAP_MSG(CONTENT, TOKEN(nth_token(0)), BERT1_RES(0, false));

    expect_recv(&env, req);
    expect_send(&env, res);
    expect_last_chunk(&args, DATA_16B, 16, false, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server, incoming_big_bert1_request) {
#        define REQUEST_PAYLOAD DATA_2KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BERT1_REQ(0, 2048, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BERT1_REQ(2, 2048, REQUEST_PAYLOAD))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTINUE, TOKEN(nth_token(0)), BERT1_RES(0, true)),
        COAP_MSG(CONTENT, TOKEN(nth_token(1)), BERT1_RES(2, false))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_partial_content(&args, REQUEST_PAYLOAD, 28, false, NULL);
    expect_has_buffered_data_check(&env, true);
    expect_partial_content(&args, REQUEST_PAYLOAD, 2020, false, NULL);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_last_chunk(&args, REQUEST_PAYLOAD, 1, false, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, incoming_bigger_bert1_request) {
#        define REQUEST_PAYLOAD DATA_2KB DATA_2KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BERT1_REQ(0, 2048, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BERT1_REQ(2, 2048, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(2)), BERT1_REQ(4, 2048, REQUEST_PAYLOAD))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTINUE, TOKEN(nth_token(0)), BERT1_RES(0, true)),
        COAP_MSG(CONTINUE, TOKEN(nth_token(1)), BERT1_RES(2, true)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)), BERT1_RES(4, false))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_partial_content(&args, REQUEST_PAYLOAD, 28, false, NULL);
    expect_has_buffered_data_check(&env, true);
    expect_partial_content(&args, REQUEST_PAYLOAD, 2020, false, NULL);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_partial_content(&args, REQUEST_PAYLOAD, 28, false, NULL);
    expect_has_buffered_data_check(&env, true);
    expect_partial_content(&args, REQUEST_PAYLOAD, 2020, false, NULL);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    expect_last_chunk(&args, REQUEST_PAYLOAD, 1, false, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, incoming_bert2_request) {
#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BERT2_REQ(0)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BERT2_REQ(1)),
        COAP_MSG(GET, TOKEN(nth_token(2)), BERT2_REQ(2))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, TOKEN(nth_token(0)),
                 BERT2_RES(0, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(1)),
                 BERT2_RES(1, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)),
                 BERT2_RES(2, 1024, RESPONSE_PAYLOAD))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_last_chunk(&args, NULL, 0, false,
                      &(payload_buf_t) {
                          .data = RESPONSE_PAYLOAD,
                          .size = sizeof(RESPONSE_PAYLOAD) - 1
                      });
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, sliced_block_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *req =
            COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_16B));
    const test_msg_t *res =
            COAP_MSG(CONTENT, BLOCK1_RES(0, 16, false), TOKEN(nth_token(0)));

    expect_sliced_recv(&env, req, req->payload_offset + 11);
    expect_partial_content(&args, req->msg.content.payload, 11, ACTION_NONE,
                           NULL);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_send(&env, res);
    expect_last_chunk(&args, req->msg.content.payload, 5, false, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_server, incoming_request_for_big_payload) {
#        define RESPONSE_PAYLOAD DATA_1KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0))),
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024)),
        COAP_MSG(GET, TOKEN(nth_token(2)), BLOCK2_REQ(2, 1024))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, TOKEN(nth_token(0)),
                 BLOCK2_RES(0, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)),
                 BLOCK2_RES(2, 1024, RESPONSE_PAYLOAD))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_last_chunk(&args, NULL, 0, false,
                      &(payload_buf_t) {
                          .data = RESPONSE_PAYLOAD,
                          .size = sizeof(RESPONSE_PAYLOAD) - 1
                      });
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, incoming_request_block_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

#        define REQUEST_PAYLOAD DATA_2KB DATA_1KB "?"
#        define RESPONSE_PAYLOAD "!" DATA_1KB

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BERT1_REQ(0, 2048, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BERT1_REQ(2, 2048, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(2)), BLOCK2_REQ(1, 1024))
    };
    const test_msg_t *responses[] = {
        COAP_MSG(CONTINUE, TOKEN(nth_token(0)), BERT1_RES(0, true)),
        COAP_MSG(CONTENT, TOKEN(nth_token(1)),
                 BERT1_AND_BLOCK2_RES(2, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_partial_content(&args, REQUEST_PAYLOAD, 28, false, NULL);
    expect_has_buffered_data_check(&env, true);
    expect_partial_content(&args, REQUEST_PAYLOAD, 2020, false, NULL);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_partial_content(&args, REQUEST_PAYLOAD, 28, false, NULL);
    expect_has_buffered_data_check(&env, true);
    expect_last_chunk(&args, REQUEST_PAYLOAD, 997, false,
                      &(payload_buf_t) {
                          .data = RESPONSE_PAYLOAD,
                          .size = sizeof(RESPONSE_PAYLOAD) - 1
                      });
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);

    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef REQUEST_PAYLOAD
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, big_error_after_first_block) {
#        define RESPONSE_PAYLOAD DATA_2KB
    test_env_t env = test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_32B)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK2_REQ(1, 1024))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(NOT_FOUND, TOKEN(nth_token(0)),
                 BLOCK1_AND_2_RES(0, 16, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(NOT_FOUND, TOKEN(nth_token(1)),
                 BLOCK2_RES(1, 1024, RESPONSE_PAYLOAD))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_has_buffered_data_check(&env, true);
    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_has_buffered_data_check(&env, false);

    expect_partial_content(&args, DATA_32B, 16, ACTION_SETUP_NOT_FOUND_RESPONSE,
                           &(payload_buf_t) {
                               .data = RESPONSE_PAYLOAD,
                               .size = sizeof(RESPONSE_PAYLOAD) - 1
                           });
    expect_cleanup(&args);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    test_teardown(&env);
    cleanup_request_handler_args(&args);
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, block1_req_after_sent_response) {
#        define RESPONSE_PAYLOAD DATA_2KB
    test_env_t env = test_setup_with_custom_sized_buffers(2048, 2048);
    request_handler_args_t args1 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));
    request_handler_args_t args2 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(2));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_64B)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK1_REQ(1, 16, DATA_64B)),
        COAP_MSG(GET, TOKEN(nth_token(2)), BLOCK1_REQ(2, 16, DATA_64B))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTINUE, TOKEN(nth_token(0)), BLOCK1_RES(0, 16, true)),
        COAP_MSG(NOT_FOUND, TOKEN(nth_token(1)),
                 BLOCK1_AND_2_RES(1, 16, 1024, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTINUE, TOKEN(nth_token(2)), BLOCK1_RES(2, 16, true))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_has_buffered_data_check(&env, false);
    expect_partial_content(&args1, DATA_64B, 16, ACTION_NONE, NULL);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args1));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_has_buffered_data_check(&env, false);
    expect_partial_content(&args1, DATA_64B, 16,
                           ACTION_SETUP_NOT_FOUND_RESPONSE,
                           &(payload_buf_t) {
                               .data = RESPONSE_PAYLOAD,
                               .size = sizeof(RESPONSE_PAYLOAD) - 1
                           });
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args1));

    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    expect_has_buffered_data_check(&env, false);
    args2.next_offset = 32;
    expect_partial_content(&args2, DATA_64B, 16, ACTION_NONE, NULL);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args2));

    expect_cleanup(&args1);
    expect_cleanup(&args2);
    test_teardown(&env);
    cleanup_request_handler_args(&args1);
    cleanup_request_handler_args(&args2);
#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, valid_response_after_first_block) {
    test_env_t env = test_setup();
    request_handler_args_t args_req1 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));
    request_handler_args_t args_req2 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(2));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_32B)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK1_REQ(1, 16, DATA_32B))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(VALID, BLOCK1_RES(0, 16, false), TOKEN(nth_token(0))),
        COAP_MSG(VALID, BLOCK1_RES(1, 16, false), TOKEN(nth_token(1)))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_has_buffered_data_check(&env, false);
    expect_partial_content(&args_req1, DATA_32B, 16,
                           ACTION_SETUP_VALID_RESPONSE, NULL);
    expect_cleanup(&args_req1);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req1));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    // New exchange, starting from block no 1.
    args_req2.next_offset = 16;
    expect_last_chunk(&args_req2, DATA_32B, 16, ACTION_SETUP_VALID_RESPONSE,
                      NULL);
    expect_cleanup(&args_req2);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req2));

    cleanup_request_handler_args(&args_req1);
    cleanup_request_handler_args(&args_req2);
    test_teardown(&env);
}

AVS_UNIT_TEST(tcp_async_server, valid_response_after_second_block) {
#        define REQUEST_PAYLOAD DATA_32B DATA_16B
    test_env_t env = test_setup();
    request_handler_args_t args_req1 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));
    request_handler_args_t args_req2 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(2));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK1_REQ(1, 16, REQUEST_PAYLOAD)),
        COAP_MSG(GET, TOKEN(nth_token(2)), BLOCK1_REQ(2, 16, REQUEST_PAYLOAD)),
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTINUE, BLOCK1_RES(0, 16, true), TOKEN(nth_token(0))),
        COAP_MSG(VALID, BLOCK1_RES(1, 16, false), TOKEN(nth_token(1))),
        COAP_MSG(VALID, BLOCK1_RES(2, 16, false), TOKEN(nth_token(2)))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_partial_content(&args_req1, REQUEST_PAYLOAD, 16, ACTION_NONE, NULL);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req1));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_partial_content(&args_req1, REQUEST_PAYLOAD, 16,
                           ACTION_SETUP_VALID_RESPONSE, NULL);
    expect_cleanup(&args_req1);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_recv(&env, requests[2]);
    expect_send(&env, responses[2]);
    // New exchange, starting from block no 2.
    args_req2.next_offset = 32;
    expect_last_chunk(&args_req2, REQUEST_PAYLOAD, 16,
                      ACTION_SETUP_VALID_RESPONSE, NULL);
    expect_cleanup(&args_req2);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req2));

    cleanup_request_handler_args(&args_req1);
    cleanup_request_handler_args(&args_req2);
    test_teardown(&env);
#        undef REQUEST_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_server, error_after_first_block) {
    test_env_t env = test_setup();
    request_handler_args_t args =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *request =
            COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_32B));
    const test_msg_t *response =
            COAP_MSG(NOT_FOUND, TOKEN(nth_token(0)), BLOCK1_RES(0, 16, false));

    expect_recv(&env, request);
    expect_send(&env, response);

    expect_partial_content(&args, DATA_32B, 16, ACTION_SETUP_NOT_FOUND_RESPONSE,
                           NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));

    cleanup_request_handler_args(&args);
    test_teardown(&env);
}

AVS_UNIT_TEST(tcp_async_server, next_block_when_it_is_not_expected) {
    test_env_t env = test_setup();
    request_handler_args_t args_req1 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));
    request_handler_args_t args_req2 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(2));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_32B)),
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK1_REQ(1, 16, DATA_32B))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(NOT_FOUND, TOKEN(nth_token(0)), BLOCK1_RES(0, 16, false)),
        COAP_MSG(CONTENT, TOKEN(nth_token(1)), BLOCK1_RES(1, 16, false))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    expect_partial_content(&args_req1, DATA_32B, 16,
                           ACTION_SETUP_NOT_FOUND_RESPONSE, NULL);
    expect_cleanup(&args_req1);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req1));

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    args_req2.next_offset = 16;
    expect_last_chunk(&args_req2, DATA_32B, 16, ACTION_NONE, NULL);
    expect_cleanup(&args_req2);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req2));
    cleanup_request_handler_args(&args_req2);

    test_teardown(&env);
    cleanup_request_handler_args(&args_req1);
}

AVS_UNIT_TEST(tcp_async_server, incomplete_request) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));

    const test_msg_t *request =
            COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(1, 16, DATA_32B));
    const test_msg_t *response =
            COAP_MSG(CONTENT, TOKEN(nth_token(0)), BLOCK1_RES(1, 16, false));

    expect_recv(&env, request);
    expect_send(&env, response);

    // Faked counter, incoming message payload offset will be 16.
    args.next_offset = 16;
    expect_last_chunk(&args, DATA_32B, 16, ACTION_NONE, NULL);
    expect_cleanup(&args);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args));
}

AVS_UNIT_TEST(tcp_async_server, bad_order_of_blocks) {
    test_env_t env = test_setup();
    request_handler_args_t args_req1 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));
    request_handler_args_t args_req2 =
            setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(2));

    const test_msg_t *requests[] = {
        COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK1_REQ(1, 16, DATA_32B)),
        COAP_MSG(GET, TOKEN(nth_token(0)), BLOCK1_REQ(0, 16, DATA_32B))
    };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, TOKEN(nth_token(1)), BLOCK1_RES(1, 16, false)),
        COAP_MSG(CONTINUE, TOKEN(nth_token(0)), BLOCK1_RES(0, 16, true))
    };

    expect_recv(&env, requests[0]);
    expect_send(&env, responses[0]);
    // Faked counter, incoming message payload offset will be 16.
    args_req1.next_offset = 16;
    expect_last_chunk(&args_req1, DATA_32B, 16, ACTION_NONE, NULL);
    expect_cleanup(&args_req1);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req1));
    cleanup_request_handler_args(&args_req1);

    expect_recv(&env, requests[1]);
    expect_send(&env, responses[1]);
    expect_partial_content(&args_req2, DATA_32B, 16, ACTION_NONE, NULL);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request,
                                     &args_req2));
    expect_cleanup(&args_req2);

    test_teardown(&env);
    cleanup_request_handler_args(&args_req2);
}

AVS_UNIT_TEST(tcp_async_server, repeated_non_repeatable_critical_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    const test_msg_t *request =
            COAP_MSG(GET, TOKEN(nth_token(0)), ACCEPT(1), DUPLICATED_ACCEPT(2));
    const test_msg_t *response = COAP_MSG(BAD_OPTION, TOKEN(nth_token(0)));

    expect_recv(&env, request);
    expect_send(&env, response);

    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#    endif // WITH_AVS_COAP_BLOCK

#    ifdef WITH_AVS_COAP_OBSERVE

// Not specified in RFC 7252 and RFC 7641, but specified in RFC 8613. Another
// request with the same token shouldn't affect already registered observation
// with the same token.
AVS_UNIT_TEST(tcp_async_server, request_with_the_same_token_as_observe_token) {
#        define NOTIFY_PAYLOAD "Notifaj"
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    request_handler_args_t args1
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(1));
    request_handler_args_t args2
            __attribute__((cleanup(cleanup_request_handler_args))) =
                    setup_request_handler_args(env.coap_ctx, EXCHANGE_ID(2));

    const test_msg_t *requests[] = { COAP_MSG(GET, MAKE_TOKEN("1234"),
                                              OBSERVE(0)),
                                     COAP_MSG(GET, MAKE_TOKEN("1234")) };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, MAKE_TOKEN("1234"), OBSERVE(0)),
        COAP_MSG(CONTENT, MAKE_TOKEN("1234")),
        COAP_MSG(CONTENT, MAKE_TOKEN("1234"), OBSERVE(0),
                 PAYLOAD(NOTIFY_PAYLOAD)),
    };

    // Request with Observe option
    expect_recv(&env, requests[0]);
    expect_last_chunk(&args1, NULL, 0, ACTION_ACCEPT_OBSERVE, NULL);
    expect_send(&env, responses[0]);
    expect_cleanup(&args1);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args1));

    // Request without Observe option and the same token
    expect_recv(&env, requests[1]);
    expect_last_chunk(&args2, NULL, 0, ACTION_NONE, NULL);
    expect_send(&env, responses[1]);
    expect_cleanup(&args2);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, handle_new_request, &args2));

    avs_coap_observe_id_t observe_id = {
        .token = requests[0]->msg.content.token
    };

    payload_buf_t test_payload = {
        .data = NOTIFY_PAYLOAD,
        .size = sizeof(NOTIFY_PAYLOAD) - 1
    };

    expect_send(&env, responses[2]);

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_notify_async(
            env.coap_ctx, &id, observe_id, &responses[1]->response_header,
            AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, test_payload_writer,
            &test_payload, NULL, NULL));
    ASSERT_FALSE(avs_coap_exchange_id_valid(id));

#        undef NOTIFY_PAYLOAD
}

#    endif // WITH_AVS_COAP_OBSERVE

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
