/*
 * Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
