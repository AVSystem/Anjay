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

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include <avsystem/commons/avs_errno.h>

#    include <avsystem/coap/coap.h>

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
    expect_send(&env, requests[0]);
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

    // Receiving response should make the context call handler,
    // which attempts to send requests[2]. That message is supposed to be held
    // until we receive response to requests[1] instead.
    avs_unit_mocksock_input(env.mocksock, malformed_response,
                            malformed_response_size);
    expect_send(&env, requests[1]);
    expect_timeout(&env);

    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // It doesn't matter much, but why are these cleaned up in reverse order?
    expect_handler_call(&env, &ids[2], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
    expect_handler_call(&env, &ids[1], AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);

    free(malformed_response);
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
