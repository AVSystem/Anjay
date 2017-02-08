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

#ifndef ANJAY_COAP_UTILS_H
#define ANJAY_COAP_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

#include <avsystem/commons/net.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    avs_net_timeout_t ack_timeout_ms;
    double ack_random_factor;
    unsigned max_retransmit;
} coap_transmission_params_t;

static inline int32_t
_anjay_coap_max_transmit_wait_ms(const coap_transmission_params_t *tx_params) {
    return (int32_t) (tx_params->ack_timeout_ms *
            ((1 << (tx_params->max_retransmit + 1)) - 1) *
                    tx_params->ack_random_factor);
}

static inline int32_t
_anjay_coap_exchange_lifetime_ms(const coap_transmission_params_t *tx_params) {
    return (int32_t) (tx_params->ack_timeout_ms *
            (((1 << tx_params->max_retransmit) - 1) *
                    tx_params->ack_random_factor + 1.0)) + 200000;
}

extern const coap_transmission_params_t _anjay_coap_DEFAULT_TX_PARAMS;
extern const coap_transmission_params_t _anjay_coap_SMS_TX_PARAMS;

/** Maximum time the client can wait for a Separate Response */
#define ANJAY_COAP_SEPARATE_RESPONSE_TIMEOUT_MS (30 * 1000)

#define ANJAY_COAP_EXT_U8 13
#define ANJAY_COAP_EXT_U16 14
#define ANJAY_COAP_EXT_RESERVED 15

#define ANJAY_COAP_EXT_U8_BASE ((uint32_t)13)
#define ANJAY_COAP_EXT_U16_BASE ((uint32_t)269)

#define ANJAY_COAP_PAYLOAD_MARKER ((uint8_t)0xFF)

#define ANJAY_FIELD_GET(field, mask, shift) (((field) & (mask)) >> (shift))
#define ANJAY_FIELD_SET(field, mask, shift, value) \
    ((field) = (uint8_t)(((field) & ~(mask)) \
                         | (uint8_t)(((value) << (shift)) & (mask))))

static inline uint16_t extract_u16(const uint8_t *data) {
    uint16_t result;
    memcpy(&result, data, sizeof(uint16_t));
    return ntohs(result);
}

bool _anjay_coap_is_valid_block_size(uint16_t size);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_UTILS_H
