/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_UDP_UDP_TX_PARAMS_H
#define AVS_COAP_SRC_UDP_UDP_TX_PARAMS_H

#include <stdint.h>

#include <avsystem/commons/avs_time.h>

#include <avsystem/coap/udp.h>

#include "avs_coap_udp_ctx.h"

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

static inline avs_error_t
_avs_coap_udp_initial_retry_state(avs_coap_udp_ctx_t *ctx,
                                  avs_coap_retry_state_t *out_retry_state) {
    uint32_t random;
    if (avs_crypto_prng_bytes(ctx->base.prng_ctx,
                              (unsigned char *) &random,
                              sizeof(random))) {
        return _avs_coap_err(AVS_COAP_ERR_PRNG_FAIL);
    }

    double random_factor = ((double) random / (double) UINT32_MAX)
                           * (ctx->tx_params.ack_random_factor - 1.0);

    *out_retry_state = (avs_coap_retry_state_t) {
        .retries_left = ctx->tx_params.max_retransmit,
        .recv_timeout = avs_time_duration_fmul(ctx->tx_params.ack_timeout,
                                               1.0 + random_factor)
    };
    return AVS_OK;
}

#ifdef AVS_UNIT_TESTING
#    include "../tests/udp/tx_params_mock.h"
#endif // AVS_UNIT_TESTING

static inline int
_avs_coap_udp_update_retry_state(avs_coap_udp_ctx_t *ctx,
                                 avs_coap_retry_state_t *retry_state) {
    retry_state->recv_timeout =
            avs_time_duration_mul(retry_state->recv_timeout, 2);
    --retry_state->retries_left;
    if (!avs_time_duration_valid(retry_state->recv_timeout)) {
        return -1;
    }
    (void) ctx;
    return 0;
}

/**
 * @returns true if all packets in a retransmission sequence were already
 * sent, false otherwise.
 */
static inline bool
_avs_coap_udp_all_retries_sent(const avs_coap_retry_state_t *retry_state) {
    return retry_state->retries_left <= 0;
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_UDP_TX_PARAMS_H
