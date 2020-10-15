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

#    include "./big_data.h"
#    include "./utils.h"

typedef struct {
    test_env_t *env;
    const test_msg_t *msg;
    avs_coap_exchange_id_t *exchange_id;
} test_env_with_msg_t;

static void
msg_sending_response_handler(avs_coap_ctx_t *ctx,
                             avs_coap_exchange_id_t exchange_id,
                             avs_coap_client_request_state_t result,
                             const avs_coap_client_async_response_t *response,
                             avs_error_t err,
                             void *arg_) {
    (void) exchange_id;
    (void) result;
    (void) response;
    (void) err;

    test_env_with_msg_t *arg = (test_env_with_msg_t *) arg_;

    ASSERT_OK(avs_coap_client_send_async_request(
            ctx, arg->exchange_id, &arg->msg->request_header, NULL, NULL,
            test_response_handler, &arg->env->expects_list));
}

AVS_UNIT_TEST(udp_fuzzer, send_in_response_handler_while_message_is_held) {
    // - NSTART = 1
    // - CON message 1 is sent
    // - CON message 2 is sent
    // - Response to message is received, but has malformed options
    // - Message 1 is removed from ctx->unconfirmed_messages to disallow
    //   cancelling it from user-defined handler while we are operating on it
    // - User-defined handler for message 1 is called with "fail" state
    // - Response handler sends CON message 3. At this point,
    //   ctx->unconfirmed_messages contains just one entry - message 2 - which
    //   is held until handling of another message finishes to not exceed
    //   NSTART. enqueue_unconfirmed is called, finds out that current_nstart ==
    //   0, so message 3 is sent immediately and marked as "not held".
    // - Program exits user-defined handler
    // - UDP context figures out that handling a message was done, so next held
    //   message (2) can be resumed without violating NSTART
    // - We end up with 2 "not held" messages, but NSTART = 1, so an assertion
    //   fails.
    //
    // Case fixed by https://phabricator.avsystem.com/D9067
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_with_nstart(1);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2)))
    };
    const test_msg_t *response =
            COAP_MSG(ACK, CONTENT, ID(0), TOKEN(nth_token(0)));

    const size_t malformed_response_size = response->size + 1;
    uint8_t *malformed_response = (uint8_t *) malloc(malformed_response_size);
    memcpy(malformed_response, response->data, response->size);
    // invalid option value: 1b of option data is expected, but there is none
    malformed_response[response->size] = 0x01;

    avs_coap_exchange_id_t ids[3];

    // a request should be sent
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[0], &requests[0]->request_header, NULL, NULL,
            msg_sending_response_handler,
            &(test_env_with_msg_t) {
                .env = &env,
                .msg = requests[2],
                .exchange_id = &ids[2]
            }));

    // second one should be held due to NSTART = 1
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[1], &requests[1]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // Receiving response should make the context call handler,
    // which attempts to send requests[2]. That message is supposed to be held
    // until we receive response to requests[1] instead.
    avs_unit_mocksock_input(env.mocksock, malformed_response,
                            malformed_response_size);
    expect_timeout(&env);

    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    expect_send(&env, requests[1]);
    avs_sched_run(env.sched);

    // It doesn't matter much, but why are these cleaned up in reverse order?
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);

    free(malformed_response);
}

#    ifdef WITH_AVS_COAP_BLOCK
AVS_UNIT_TEST(udp_fuzzer, udp_bert_request) {
    static const char REQUEST_PAYLOAD[] = DATA_1KB "?";

    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_with_nstart(1);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BERT1_REQ(0, 1024, REQUEST_PAYLOAD))
    };

    avs_coap_exchange_id_t id;
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_FAIL, NULL);
    avs_sched_run(env.sched);
}

AVS_UNIT_TEST(udp_fuzzer, nonconfirmable_broken_block_size_recalculation) {
    static const char REQUEST_PAYLOAD[] = DATA_16KB;

    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup(&AVS_COAP_DEFAULT_UDP_TX_PARAMS, 4096, 32, NULL);

    const test_msg_t *requests[] = {
        COAP_MSG(NON, PUT, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(13, 1024, REQUEST_PAYLOAD))
    };

    ASSERT_FAIL(avs_coap_client_send_async_request(
            env.coap_ctx, NULL, &requests[0]->request_header,
            test_payload_writer,
            &(test_payload_writer_args_t) {
                .payload = REQUEST_PAYLOAD,
                .payload_size = sizeof(REQUEST_PAYLOAD) - 1,
                .expected_payload_offset = 13 * 1024
            },
            NULL, NULL));
}
#    endif // WITH_AVS_COAP_BLOCK

typedef struct {
    test_env_t *env;
    avs_coap_exchange_id_t *out_id;
} call_avs_sched_run_handler_args_t;

static void
call_avs_sched_run_handler(avs_coap_ctx_t *ctx,
                           avs_coap_exchange_id_t exchange_id,
                           avs_coap_client_request_state_t result,
                           const avs_coap_client_async_response_t *response,
                           avs_error_t err,
                           void *args_) {
    (void) exchange_id;
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ(result, AVS_COAP_CLIENT_REQUEST_FAIL);
    ASSERT_NULL(response);
    ASSERT_EQ(err.category, AVS_ERRNO_CATEGORY);
    ASSERT_EQ(err.code, AVS_ECONNREFUSED);

    call_avs_sched_run_handler_args_t *args =
            (call_avs_sched_run_handler_args_t *) args_;
    ASSERT_OK(avs_coap_client_send_async_request(
            ctx, args->out_id,
            &(const avs_coap_request_header_t) {
                .code = AVS_COAP_CODE_GET
            },
            NULL, NULL, test_response_handler, &args->env->expects_list));
    avs_sched_run(args->env->sched);
}

AVS_UNIT_TEST(udp_fuzzer, recursive_sched_run_nstart) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_with_nstart(1);

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2))),
        COAP_MSG(CON, GET, ID(3), TOKEN(nth_token(3)))
    };
    avs_coap_exchange_id_t ids[4];
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[0], &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[1],
            &(const avs_coap_request_header_t) {
                .code = AVS_COAP_CODE_GET
            },
            NULL, NULL, call_avs_sched_run_handler,
            &(call_avs_sched_run_handler_args_t) {
                .env = &env,
                .out_id = &ids[3]
            }));
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[2],
            &(const avs_coap_request_header_t) {
                .code = AVS_COAP_CODE_GET
            },
            NULL, NULL, test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    expect_handler_call(&env, &ids[0], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    avs_coap_exchange_cancel(env.coap_ctx, ids[0]);

    // Now, id[1] will attempt to be sent. Let's fail the send operation.
    // This will cause call_avs_sched_run_handler() to be called.
    avs_unit_mocksock_output_fail(env.mocksock, avs_errno(AVS_ECONNREFUSED));
    // id[2] will then be sent normally
    expect_send(&env, requests[2]);
    avs_sched_run(env.sched);

    expect_handler_call(&env, &ids[3], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
}

#    ifdef WITH_AVS_COAP_BLOCK
AVS_UNIT_TEST(udp_fuzzer, cancel_nonconfirmable_in_payload_writer) {
#        define CONTENT DATA_1KB DATA_1KB
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_default();

    const test_msg_t *requests[] = { COAP_MSG(NON, GET, ID(0),
                                              TOKEN(nth_token(0)),
                                              BLOCK1_REQ(0, 1024, CONTENT)) };

    ASSERT_FAIL(avs_coap_client_send_async_request(
            env.coap_ctx, NULL, &requests[0]->request_header,
            test_payload_writer,
            &(test_payload_writer_args_t) {
                .payload = CONTENT,
                .payload_size = sizeof(CONTENT) - 1,
                .coap_ctx = env.coap_ctx,
                // Exchange IDs of non-confirmable requests are not exposed
                // publicly, but the user may pass a "random" value that happens
                // to match. Let's not segfault in that case.
                .exchange_id = { 1 },
                .cancel_exchange = true
            },
            NULL, NULL));
#        undef CONTENT
}
#    endif // WITH_AVS_COAP_BLOCK

typedef struct {
    avs_coap_ctx_t *coap_ctx;
    avs_coap_exchange_id_t *exchange_ids;
    size_t exchange_id_count;
} cancel_exchanges_payload_writer_args_t;

static int cancel_exchanges_payload_writer(size_t payload_offset,
                                           void *payload_buf,
                                           size_t payload_buf_size,
                                           size_t *out_payload_chunk_size,
                                           void *args_) {
    (void) payload_offset;
    (void) payload_buf;
    (void) payload_buf_size;
    *out_payload_chunk_size = 0;
    cancel_exchanges_payload_writer_args_t *args =
            (cancel_exchanges_payload_writer_args_t *) args_;
    for (size_t i = 0; i < args->exchange_id_count; ++i) {
        avs_coap_exchange_cancel(args->coap_ctx, args->exchange_ids[i]);
    }
    return 0;
}

AVS_UNIT_TEST(udp_fuzzer, complicated_deferred_send_iteration) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1))),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(2))),
        COAP_MSG(CON, GET, ID(2), TOKEN(nth_token(3)))
    };

    avs_coap_exchange_id_t ids[4];
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[0], &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[1], &requests[1]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[2], &requests[2]->request_header,
            cancel_exchanges_payload_writer,
            &(cancel_exchanges_payload_writer_args_t) {
                .coap_ctx = env.coap_ctx,
                .exchange_ids = &ids[1],
                .exchange_id_count = 2
            },
            test_response_handler, &env.expects_list));
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[3], &requests[3]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_send(&env, requests[3]);
    avs_sched_run(env.sched);

    expect_handler_call(&env, &ids[0], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[3], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
}

AVS_UNIT_TEST(udp_fuzzer, complicated_deferred_send_iteration_2) {
    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_default();

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0))),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)))
    };

    avs_coap_exchange_id_t ids[4];
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[0], &requests[0]->request_header, NULL, NULL,
            test_response_handler, &env.expects_list));
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &ids[1], &requests[1]->request_header,
            cancel_exchanges_payload_writer,
            &(cancel_exchanges_payload_writer_args_t) {
                .coap_ctx = env.coap_ctx,
                .exchange_ids = &ids[0],
                .exchange_id_count = 1
            },
            test_response_handler, &env.expects_list));

    expect_send(&env, requests[0]);
    expect_send(&env, requests[1]);
    expect_handler_call(&env, &ids[0], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    avs_sched_run(env.sched);

    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
