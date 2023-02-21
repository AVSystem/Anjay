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

#    define REQ_HEADER_FROM_REQ(Req)           \
        &(avs_coap_request_header_t) {         \
            .code = (Req)->request_header.code \
        }

typedef struct {
    avs_coap_exchange_id_t exchange_id;
    avs_coap_client_request_state_t result;
    size_t payload_offset;
    const void *payload;
    size_t payload_size;
} expected_response_t;

typedef AVS_LIST(expected_response_t) expected_responses_list_t;

typedef struct {
    size_t next_offset;
    expected_responses_list_t expected_responses;
} response_handler_args_t;

static response_handler_args_t setup_response_handler_args(void) {
    response_handler_args_t args = { 0 };
    return args;
}

static void cleanup_response_handler_args(response_handler_args_t *args) {
    ASSERT_NULL(args->expected_responses);
}

static void expect_response_handler_call(response_handler_args_t *args,
                                         avs_coap_exchange_id_t exchange_id,
                                         avs_coap_client_request_state_t result,
                                         const void *full_payload,
                                         size_t payload_size) {
    expected_response_t *expect = AVS_LIST_NEW_ELEMENT(expected_response_t);

    *expect = (expected_response_t) {
        .exchange_id = exchange_id,
        .result = result,
        .payload_offset = args->next_offset,
        .payload = (const uint8_t *) full_payload + args->next_offset,
        .payload_size = payload_size
    };

    AVS_LIST_APPEND(&args->expected_responses, expect);
    args->next_offset += payload_size;
}

static void expect_cancel(response_handler_args_t *args,
                          avs_coap_exchange_id_t exchange_id) {
    expect_response_handler_call(args, exchange_id,
                                 AVS_COAP_CLIENT_REQUEST_CANCEL, NULL, 0);
}

static void expect_fail(response_handler_args_t *args,
                        avs_coap_exchange_id_t exchange_id) {
    expect_response_handler_call(args, exchange_id,
                                 AVS_COAP_CLIENT_REQUEST_FAIL, NULL, 0);
}

static void expect_partial_content(response_handler_args_t *args,
                                   avs_coap_exchange_id_t exchange_id,
                                   const void *full_payload,
                                   size_t payload_size) {
    expect_response_handler_call(args, exchange_id,
                                 AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
                                 full_payload, payload_size);
}

static void expect_finished_response(response_handler_args_t *args,
                                     avs_coap_exchange_id_t exchange_id,
                                     const void *full_payload,
                                     size_t payload_size) {
    expect_response_handler_call(args, exchange_id, AVS_COAP_CLIENT_REQUEST_OK,
                                 full_payload, payload_size);
}

static void handle_response(avs_coap_ctx_t *ctx,
                            avs_coap_exchange_id_t exchange_id,
                            avs_coap_client_request_state_t result,
                            const avs_coap_client_async_response_t *response,
                            avs_error_t err,
                            void *arg) {
    (void) ctx;
    (void) err;

    response_handler_args_t *args = (response_handler_args_t *) arg;

    ASSERT_NOT_NULL(arg);
    expected_response_t *expected =
            (expected_response_t *) args->expected_responses;
    ASSERT_NOT_NULL(expected);

    ASSERT_TRUE(avs_coap_exchange_id_equal(exchange_id, expected->exchange_id));
    ASSERT_EQ(result, expected->result);

    switch (result) {
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT:
    case AVS_COAP_CLIENT_REQUEST_OK:
        ASSERT_EQ(response->payload_offset, expected->payload_offset);
        ASSERT_EQ(response->payload_size, expected->payload_size);
        ASSERT_EQ_BYTES_SIZED(response->payload, expected->payload,
                              response->payload_size);
        break;

    case AVS_COAP_CLIENT_REQUEST_CANCEL:
    case AVS_COAP_CLIENT_REQUEST_FAIL:
        ASSERT_NULL(response);
        break;
    }

    AVS_LIST_DELETE(&args->expected_responses);
}

AVS_UNIT_TEST(tcp_async_client,
              cancel_exchange_after_receiving_first_chunk_of_response) {
#    define RESPONSE_PAYLOAD "raz dwa trzy"
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args_res1
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();
    response_handler_args_t args_res2
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *req = COAP_MSG(GET, TOKEN(nth_token(1)));
    const test_msg_t *res =
            COAP_MSG(CONTENT, TOKEN(nth_token(1)), PAYLOAD(RESPONSE_PAYLOAD));

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, REQ_HEADER_FROM_REQ(req), NULL, NULL,
            handle_response, &args_res1));

    expect_send(&env, req);
    expect_sliced_recv(&env, res, res->payload_offset + 1);
    avs_sched_run(env.sched);

    expect_partial_content(&args_res1, id, RESPONSE_PAYLOAD, 1);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_cancel(&args_res1, id);
    avs_coap_exchange_cancel(env.coap_ctx, id);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    req = COAP_MSG(GET, TOKEN(nth_token(2)));
    res = COAP_MSG(CONTENT, TOKEN(nth_token(2)), PAYLOAD(RESPONSE_PAYLOAD));

    // Second request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, REQ_HEADER_FROM_REQ(req), NULL, NULL,
            handle_response, &args_res2));

    expect_send(&env, req);
    expect_recv(&env, res);
    avs_sched_run(env.sched);

    expect_finished_response(&args_res2, id, res->msg.content.payload,
                             res->msg.content.payload_size);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#    undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_client, repeated_non_repeatable_critical_option) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request = COAP_MSG(PUT, TOKEN(nth_token(1)));
    // Accept option in response only for test purposes.
    const test_msg_t *response = COAP_MSG(BAD_OPTION, TOKEN(nth_token(1)),
                                          ACCEPT(1), DUPLICATED_ACCEPT(2));

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = request->request_header.code
            },
            NULL, NULL, handle_response, &args));

    expect_send(&env, request);
    expect_recv(&env, response);
    avs_sched_run(env.sched);

    expect_fail(&args, id);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#    ifdef WITH_AVS_COAP_BLOCK

#        define INVALID_BLOCK2(Seq, Size, Payload) \
            .block1 = {                            \
                .type = AVS_COAP_BLOCK2,           \
                .seq_num = Seq,                    \
                .size = Size,                      \
                .has_more = true                   \
            },                                     \
            .payload = Payload,                    \
            .payload_size =                        \
                    (assert(sizeof(Payload) - 1 < Size), sizeof(Payload) - 1)

AVS_UNIT_TEST(tcp_async_client, invalid_block_opt_in_response) {
    // response with BLOCK2.has_more == 1 and BLOCK2.size != payload size
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *request =
            COAP_MSG(GET, TOKEN(nth_token(1)), BLOCK2_REQ(0, 1024));
    const test_msg_t *response = COAP_MSG(BAD_OPTION, TOKEN(nth_token(1)),
                                          INVALID_BLOCK2(0, 1024, DATA_32B));

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(env.coap_ctx, &id,
                                                 &request->request_header, NULL,
                                                 NULL, handle_response, &args));

    expect_send(&env, request);
    expect_recv(&env, response);
    avs_sched_run(env.sched);

    expect_has_buffered_data_check(&env, true);
    expect_fail(&args, id);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    ASSERT_NULL(args.expected_responses);
}

AVS_UNIT_TEST(tcp_async_client, sliced_block_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    // This is a second message sent by CoAP/TCP context.
    const test_msg_t *req = COAP_MSG(GET, TOKEN(nth_token(1)));
    const test_msg_t *res =
            COAP_MSG(CONTENT, TOKEN(nth_token(1)), BLOCK2_RES(0, 16, DATA_16B));

    avs_coap_exchange_id_t id;
    avs_coap_client_send_async_request(env.coap_ctx, &id,
                                       REQ_HEADER_FROM_REQ(req), NULL, NULL,
                                       handle_response, &args);

    expect_send(&env, req);
    avs_sched_run(env.sched);

    expect_sliced_recv(&env, res, res->payload_offset + 11);
    expect_partial_content(&args, id, DATA_16B, 11);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_finished_response(&args, id, DATA_16B, 5);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

AVS_UNIT_TEST(tcp_async_client, server_responded_with_bert_2049b) {
#        define RESPONSE_PAYLOAD DATA_2KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *requests[] = { COAP_MSG(GET, TOKEN(nth_token(1))),
                                     COAP_MSG(GET, TOKEN(nth_token(2)),
                                              BERT2_REQ(2)) };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, TOKEN(nth_token(1)),
                 BERT2_RES(0, 2048, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)),
                 BERT2_RES(2, 2048, RESPONSE_PAYLOAD))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = requests[0]->request_header.code
            },
            NULL, NULL, handle_response, &args));

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    avs_sched_run(env.sched);

    size_t first_chunk_size =
            OPTS_BUFFER_SIZE
            - (responses[0]->payload_offset - responses[0]->options_offset);
    expect_partial_content(&args, id, RESPONSE_PAYLOAD, first_chunk_size);
    expect_has_buffered_data_check(&env, true);
    expect_partial_content(&args, id, RESPONSE_PAYLOAD,
                           responses[0]->msg.content.payload_size
                                   - first_chunk_size);
    expect_has_buffered_data_check(&env, true);
    expect_finished_response(&args, id, RESPONSE_PAYLOAD,
                             responses[1]->msg.content.payload_size);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_client, server_responded_with_bert_3073b) {
#        define RESPONSE_PAYLOAD DATA_2KB DATA_1KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *requests[] = { COAP_MSG(GET, TOKEN(nth_token(1))),
                                     COAP_MSG(GET, TOKEN(nth_token(2)),
                                              BERT2_REQ(2)) };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, TOKEN(nth_token(1)),
                 BERT2_RES(0, 2048, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)),
                 BERT2_RES(2, 2048, RESPONSE_PAYLOAD))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = requests[0]->request_header.code
            },
            NULL, NULL, handle_response, &args));

    expect_send(&env, requests[0]);
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    avs_sched_run(env.sched);

    size_t first_chunk_size =
            OPTS_BUFFER_SIZE
            - (responses[0]->payload_offset - responses[0]->options_offset);
    expect_partial_content(&args, id, RESPONSE_PAYLOAD, first_chunk_size);
    expect_has_buffered_data_check(&env, true);
    expect_partial_content(&args, id, RESPONSE_PAYLOAD,
                           responses[0]->msg.content.payload_size
                                   - first_chunk_size);
    expect_has_buffered_data_check(&env, true);
    first_chunk_size =
            OPTS_BUFFER_SIZE
            - (responses[0]->payload_offset - responses[0]->options_offset);
    expect_partial_content(&args, id, RESPONSE_PAYLOAD, first_chunk_size);
    expect_has_buffered_data_check(&env, true);
    expect_finished_response(&args, id, RESPONSE_PAYLOAD,
                             responses[1]->msg.content.payload_size
                                     - first_chunk_size);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_client, server_responded_with_sliced_bert) {
#        define RESPONSE_PAYLOAD DATA_2KB "?"

    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_with_custom_sized_buffers(2048, 2048);
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();

    const test_msg_t *requests[] = { COAP_MSG(GET, TOKEN(nth_token(1))),
                                     COAP_MSG(GET, TOKEN(nth_token(2)),
                                              BERT2_REQ(2)) };

    const test_msg_t *responses[] = {
        COAP_MSG(CONTENT, TOKEN(nth_token(1)),
                 BERT2_RES(0, 2048, RESPONSE_PAYLOAD)),
        COAP_MSG(CONTENT, TOKEN(nth_token(2)),
                 BERT2_RES(2, 2048, RESPONSE_PAYLOAD))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id,
            &(avs_coap_request_header_t) {
                .code = requests[0]->request_header.code
            },
            NULL, NULL, handle_response, &args));

    const size_t slice_pos = 512;
    expect_send(&env, requests[0]);
    expect_sliced_recv(&env, responses[0], slice_pos);
    expect_send(&env, requests[1]);
    expect_recv(&env, responses[1]);
    avs_sched_run(env.sched);

    size_t first_chunk_size =
            OPTS_BUFFER_SIZE
            - (responses[0]->payload_offset - responses[0]->options_offset);
    expect_partial_content(&args, id, RESPONSE_PAYLOAD, first_chunk_size);

    size_t offset = first_chunk_size;
    size_t chunk_size = slice_pos - offset - responses[0]->payload_offset;
    expect_partial_content(&args, id, RESPONSE_PAYLOAD, chunk_size);
    expect_has_buffered_data_check(&env, true);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

    offset += chunk_size;
    expect_partial_content(&args, id, RESPONSE_PAYLOAD,
                           responses[0]->msg.content.payload_size - offset);
    expect_has_buffered_data_check(&env, true);
    offset = responses[0]->msg.content.payload_size;
    expect_finished_response(&args, id, RESPONSE_PAYLOAD,
                             responses[1]->msg.content.payload_size);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));

#        undef RESPONSE_PAYLOAD
}

AVS_UNIT_TEST(tcp_async_client, block_response_with_too_big_options) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
    response_handler_args_t args
            __attribute__((cleanup(cleanup_response_handler_args))) =
                    setup_response_handler_args();
    const test_msg_t *req = COAP_MSG(GET, TOKEN(nth_token(1)));
    // Path option in response only for test purposes.
    const test_msg_t *res =
            COAP_MSG(CONTENT, TOKEN(nth_token(1)), BLOCK2_RES(0, 16, DATA_16B),
                     PATH("why are you okay? you are okay"));
    ASSERT_TRUE(res->response_header.options.size > MAX_OPTS_SIZE);

    avs_coap_exchange_id_t id;
    avs_coap_client_send_async_request(env.coap_ctx, &id,
                                       REQ_HEADER_FROM_REQ(req), NULL, NULL,
                                       handle_response, &args);

    expect_send(&env, req);
    expect_recv(&env, res);
    avs_sched_run(env.sched);

    expect_has_buffered_data_check(&env, true);
    expect_fail(&args, id);
    expect_has_buffered_data_check(&env, false);
    ASSERT_OK(handle_incoming_packet(env.coap_ctx, NULL, NULL));
}

#    endif // WITH_AVS_COAP_BLOCK

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
