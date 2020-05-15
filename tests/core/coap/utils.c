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

#include <anjay_init.h>

#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/coap.h>

#include "utils.h"

static uint64_t GLOBAL_TOKEN_VALUE;

void reset_token_generator(void) {
    GLOBAL_TOKEN_VALUE = 0;
}

avs_error_t _avs_coap_ctx_generate_token(avs_coap_ctx_t *ctx,
                                         avs_coap_token_t *out_token);

avs_error_t _avs_coap_ctx_generate_token(avs_coap_ctx_t *ctx,
                                         avs_coap_token_t *out_token) {
    (void) ctx;
    *out_token = nth_token(GLOBAL_TOKEN_VALUE++);
    return AVS_OK;
}

avs_coap_token_t nth_token(uint64_t k) {
    union {
        uint8_t bytes[sizeof(uint64_t)];
        uint64_t value;
    } v;
    v.value = avs_convert_be64(k);

    avs_coap_token_t token;
    token.size = sizeof(v.bytes);
    memcpy(token.bytes, v.bytes, sizeof(v.bytes));
    return token;
}

avs_coap_token_t current_token(void) {
    return nth_token(GLOBAL_TOKEN_VALUE);
}
