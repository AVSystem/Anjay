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

#include <config.h>

#include "utils.h"
#include "msg.h"

// For _anjay_is_power_of_2
#include "../utils.h"

VISIBILITY_SOURCE_BEGIN

// values taken from RFC 7252
const coap_transmission_params_t _anjay_coap_DEFAULT_TX_PARAMS = {
    .ack_timeout_ms = 2000,
    .ack_random_factor = 1.5,
    .max_retransmit = 4
};

// custom values set so that MAX_TRANSMIT_WAIT is equal to the default
// while disabling retransmissions
const coap_transmission_params_t _anjay_coap_SMS_TX_PARAMS = {
    .ack_timeout_ms = 62000,
    .ack_random_factor = 1.5,
    .max_retransmit = 0
};

bool _anjay_coap_is_valid_block_size(uint16_t size) {
    return _anjay_is_power_of_2(size)
            && size <= ANJAY_COAP_MSG_BLOCK_MAX_SIZE
            && size >= ANJAY_COAP_MSG_BLOCK_MIN_SIZE;
}

