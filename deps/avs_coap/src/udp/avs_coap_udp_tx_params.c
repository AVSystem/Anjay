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

#ifdef WITH_AVS_COAP_UDP

#    include <avsystem/commons/avs_time.h>
#    include <avsystem/commons/avs_utils.h>

#    include <avsystem/coap/udp.h>

#    define MODULE_NAME coap_udp
#    include <avs_coap_x_log_config.h>

VISIBILITY_SOURCE_BEGIN

const avs_coap_udp_tx_params_t AVS_COAP_DEFAULT_UDP_TX_PARAMS = {
    .ack_timeout = { 2, 0 },
    .ack_random_factor = 1.5,
    .max_retransmit = 4,
    .nstart = 1
};

bool avs_coap_udp_tx_params_valid(const avs_coap_udp_tx_params_t *tx_params,
                                  const char **error_details) {
    // ACK_TIMEOUT below 1 second would violate the guidelines of [RFC5405].
    // -- RFC 7252, 4.8.1
    if (avs_time_duration_less(tx_params->ack_timeout,
                               avs_time_duration_from_scalar(1, AVS_TIME_S))) {
        if (error_details) {
            *error_details = "ACK_TIMEOUT below 1000 milliseconds";
        }
        return false;
    }

    // ACK_RANDOM_FACTOR MUST NOT be decreased below 1.0, and it SHOULD have
    // a value that is sufficiently different from 1.0 to provide some
    // protection from synchronization effects.
    // -- RFC 7252, 4.8.1
    if (tx_params->ack_random_factor < 1.0) {
        if (error_details) {
            *error_details = "ACK_RANDOM_FACTOR less than 1.0";
        }
        return false;
    }

    if (tx_params->nstart == 0) {
        if (error_details) {
            *error_details = "NSTART less than 1 is useless";
        }
        return false;
    }

    if (error_details) {
        *error_details = NULL;
    }
    return true;
}

avs_time_duration_t
avs_coap_udp_max_transmit_wait(const avs_coap_udp_tx_params_t *tx_params) {
    return avs_time_duration_fmul(tx_params->ack_timeout,
                                  ((1 << (tx_params->max_retransmit + 1)) - 1)
                                          * tx_params->ack_random_factor);
}

avs_time_duration_t
avs_coap_udp_max_transmit_span(const avs_coap_udp_tx_params_t *tx_params) {
    return avs_time_duration_fmul(tx_params->ack_timeout,
                                  (double) ((1 << tx_params->max_retransmit)
                                            - 1)
                                          * tx_params->ack_random_factor);
}

// See https://tools.ietf.org/html/rfc7252#section-4.8.2
static const avs_time_duration_t MAX_LATENCY = { 100, 0 };

avs_time_duration_t
avs_coap_udp_exchange_lifetime(const avs_coap_udp_tx_params_t *tx_params) {
    return avs_time_duration_add(
            avs_time_duration_add(avs_coap_udp_max_transmit_span(tx_params),
                                  avs_time_duration_mul(MAX_LATENCY, 2)),
            tx_params->ack_timeout);
}

#endif // WITH_AVS_COAP_UDP
