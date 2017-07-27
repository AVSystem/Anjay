/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_TX_PARAMS_H
#define ANJAY_COAP_TX_PARAMS_H

#include <stdint.h>
#include <sys/time.h>

#include <avsystem/commons/net.h>
#include <anjay_modules/time.h>

#include "../utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline bool
_anjay_coap_tx_params_valid(const anjay_coap_tx_params_t *tx_params,
                            const char **error_details) {
    // ACK_TIMEOUT below 1 second would violate the guidelines of [RFC5405].
    // -- RFC 7252, 4.8.1
    if (tx_params->ack_timeout_ms < 1000) {
        if (error_details) {
            *error_details = "ACK_TIMEOUT below 1000 milliseconds";
        }
        return false;
    }

    // ACK_RANDOM_FACTOR MUST NOT be decreased below 1.0, and it SHOULD have
    // a value that is sufficiently different from 1.0 to provide some
    // protection from synchronization effects.
    // -- RFC 7252, 4.8.1
    if (tx_params->ack_random_factor <= 1.0) {
        if (error_details) {
            *error_details = "ACK_RANDOM_FACTOR less than or equal to 1.0";
        }
        return false;
    }
    if (error_details) {
        *error_details = NULL;
    }
    return true;
}

static inline int32_t
_anjay_coap_max_transmit_wait_ms(const anjay_coap_tx_params_t *tx_params) {
    return (int32_t) (tx_params->ack_timeout_ms *
            ((1 << (tx_params->max_retransmit + 1)) - 1) *
                    tx_params->ack_random_factor);
}

static inline int32_t
_anjay_coap_exchange_lifetime_ms(const anjay_coap_tx_params_t *tx_params) {
    return (int32_t) (tx_params->ack_timeout_ms *
            (((1 << tx_params->max_retransmit) - 1) *
                    tx_params->ack_random_factor + 1.0)) + 200000;
}

static inline struct timespec
_anjay_coap_exchange_lifetime(const anjay_coap_tx_params_t *tx_params) {
    struct timespec result;
    _anjay_time_from_ms(&result, _anjay_coap_exchange_lifetime_ms(tx_params));
    return result;
}

static inline int32_t
_anjay_coap_max_transmit_span_ms(const anjay_coap_tx_params_t *tx_params) {
    return (int32_t)((double)tx_params->ack_timeout_ms
                     * (double)((1 << tx_params->max_retransmit) - 1)
                     * tx_params->ack_random_factor);
}

static inline struct timespec
_anjay_coap_max_transmit_span(const anjay_coap_tx_params_t *tx_params) {
    struct timespec result;
    _anjay_time_from_ms(&result, _anjay_coap_max_transmit_span_ms(tx_params));
    return result;
}

/** Maximum time the client can wait for a Separate Response */
#define ANJAY_COAP_SEPARATE_RESPONSE_TIMEOUT_MS (30 * 1000)


typedef struct {
    unsigned retry_count;
    int32_t recv_timeout_ms;
} coap_retry_state_t;

void _anjay_coap_update_retry_state(coap_retry_state_t *retry_state,
                                    const anjay_coap_tx_params_t *tx_params,
                                    anjay_rand_seed_t *rand_seed);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_TX_PARAMS_H
