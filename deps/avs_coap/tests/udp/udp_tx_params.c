/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#    include <avsystem/coap/udp.h>

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

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
