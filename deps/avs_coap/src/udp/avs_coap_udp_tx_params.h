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

#ifndef AVS_COAP_SRC_UDP_UDP_TX_PARAMS_H
#define AVS_COAP_SRC_UDP_UDP_TX_PARAMS_H

#include <stdint.h>

#include <avsystem/commons/avs_time.h>

#include <avsystem/coap/udp.h>

#include "../avs_coap_ctx.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * @returns MAX_TRANSMIT_SPAN value derived from @p tx_params according
 *          to the formula specified in RFC7252.
 */
static inline avs_time_duration_t
_avs_coap_udp_max_transmit_span(const avs_coap_udp_tx_params_t *tx_params) {
    return avs_time_duration_fmul(tx_params->ack_timeout,
                                  (double) ((1 << tx_params->max_retransmit)
                                            - 1)
                                          * tx_params->ack_random_factor);
}

/** Retry state object used to calculate retransmission timeouts. */
typedef struct {
    /**
     * Number of retransmissions of the original packet already sent.
     *
     * If zero, the avs_coap_retry_state_t::recv_timeout indicates how long
     * should one wait for the response before attempting a retransmission.
     *
     * The value of retry_count shall vary between 0 and MAX_RETRANSMIT
     * inclusively.
     */
    unsigned retry_count;
    /**
     * Amount of time to wait for the response (either to an initial packet or a
     * retransmitted one).
     */
    avs_time_duration_t recv_timeout;
} avs_coap_retry_state_t;

static inline avs_error_t
_avs_coap_udp_initial_retry_state(const avs_coap_udp_tx_params_t *tx_params,
                                  avs_crypto_prng_ctx_t *prng_ctx,
                                  avs_coap_retry_state_t *out_retry_state) {
    uint32_t random;
    if (avs_crypto_prng_bytes(
                prng_ctx, (unsigned char *) &random, sizeof(random))) {
        return _avs_coap_err(AVS_COAP_ERR_PRNG_FAIL);
    }

    double random_factor = ((double) random / (double) UINT32_MAX)
                           * (tx_params->ack_random_factor - 1.0);

    *out_retry_state = (avs_coap_retry_state_t) {
        .retry_count = 0,
        .recv_timeout = avs_time_duration_fmul(tx_params->ack_timeout,
                                               1.0 + random_factor)
    };
    return AVS_OK;
}

static inline int
_avs_coap_udp_update_retry_state(avs_coap_retry_state_t *retry_state) {
    retry_state->recv_timeout =
            avs_time_duration_mul(retry_state->recv_timeout, 2);
    ++retry_state->retry_count;

    return avs_time_duration_valid(retry_state->recv_timeout) ? 0 : -1;
}

/**
 * @returns true if all packets in a retransmission sequence were already sent,
 * false otherwise.
 */
static inline bool
_avs_coap_udp_all_retries_sent(const avs_coap_retry_state_t *retry_state,
                               const avs_coap_udp_tx_params_t *tx_params) {
    return retry_state->retry_count >= tx_params->max_retransmit;
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_UDP_TX_PARAMS_H
