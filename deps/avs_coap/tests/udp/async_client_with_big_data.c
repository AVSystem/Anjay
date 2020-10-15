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

#    include <avsystem/coap/coap.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./big_data.h"
#    include "./utils.h"

#    ifdef WITH_AVS_COAP_BLOCK

AVS_UNIT_TEST(udp_async_client_with_big_data,
              block_request_renegotiation_seq_num_overflow) {
    // Server may ask the client to send smaller blocks than the initial one.
    // In that case, seq_num is recalculated accordingly for further blocks
    // (i.e. multiplied by prev_size/new_size). BLOCK option sequence numbers
    // are limited to 20 bits though, and seq_num recalculation may increase
    // seq_num past the limit.

    // MIN_BLOCK_SIZE (16 == 2**4) * 2**20 == 2**24 == 16MB
    static const char REQUEST_PAYLOAD[] = DATA_16MB DATA_1KB "?";

    test_env_t env __attribute__((cleanup(test_teardown_late_expects_check))) =
            test_setup_with_max_retransmit(0);

    test_payload_writer_args_t test_payload = {
        .payload = REQUEST_PAYLOAD,
        .expected_payload_offset = 1024 * (sizeof(REQUEST_PAYLOAD) / 1024 - 1),
        .payload_size = sizeof(REQUEST_PAYLOAD) - 1
    };

    const test_msg_t *requests[] = {
        COAP_MSG(CON, GET, ID(0), TOKEN(nth_token(0)),
                 BLOCK1_REQ(sizeof(REQUEST_PAYLOAD) / 1024 - 1, 1024,
                            REQUEST_PAYLOAD)),
        COAP_MSG(CON, GET, ID(1), TOKEN(nth_token(1)),
                 BLOCK1_REQ(sizeof(REQUEST_PAYLOAD) / 1024, 1024,
                            REQUEST_PAYLOAD))
    };
    const test_msg_t *responses[] = { COAP_MSG(ACK, CONTINUE, ID(0),
                                               TOKEN(nth_token(0)),
                                               BLOCK1_RES(0, 16, true)) };

    avs_coap_exchange_id_t id;

    // start the request
    ASSERT_OK(avs_coap_client_send_async_request(
            env.coap_ctx, &id, &requests[0]->request_header,
            test_payload_writer, &test_payload, test_response_handler,
            &env.expects_list));
    ASSERT_TRUE(avs_coap_exchange_id_valid(id));

    expect_send(&env, requests[0]);
    avs_sched_run(env.sched);

    // Block size renegotiation should be ignored and the request should
    // continue with previous block size.
    expect_recv(&env, responses[0]);
    expect_send(&env, requests[1]);
    expect_timeout(&env);
    ASSERT_OK(avs_coap_async_handle_incoming_packet(env.coap_ctx, NULL, NULL));

    // the exchange is not resolved - cleanup should call the handler
    expect_handler_call(&env, &id, AVS_COAP_CLIENT_REQUEST_CANCEL, NULL);
}

#    endif // WITH_AVS_COAP_BLOCK

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
