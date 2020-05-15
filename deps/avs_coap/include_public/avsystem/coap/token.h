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

#ifndef AVSYSTEM_COAP_TOKEN_H
#define AVSYSTEM_COAP_TOKEN_H

#include <avsystem/coap/avs_coap_config.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum size, in bytes, of a CoAP token allowed by RFC7252. */
#define AVS_COAP_MAX_TOKEN_LENGTH 8

/** CoAP token object. */
typedef struct {
    uint8_t size;
    char bytes[AVS_COAP_MAX_TOKEN_LENGTH];
} avs_coap_token_t;

/** All-zeros CoAP token initializer. */
#define AVS_COAP_TOKEN_EMPTY ((avs_coap_token_t) { 0 })

/**
 * @returns true if @p first and @p second CoAP tokens are equal,
 *          false otherwise.
 */
static inline bool avs_coap_token_equal(const avs_coap_token_t *first,
                                        const avs_coap_token_t *second) {
    return first->size == second->size
           && !memcmp(first->bytes, second->bytes, first->size);
}

static inline bool avs_coap_token_valid(const avs_coap_token_t *token) {
    return token->size <= AVS_COAP_MAX_TOKEN_LENGTH;
}

/**
 * A structure containing hex representation of a token that may be created by
 * @ref avs_coap_token_hex().
 */
typedef struct {
    char buf[AVS_COAP_MAX_TOKEN_LENGTH * 2 + 1];
} avs_coap_token_hex_t;

static inline const char *avs_coap_token_hex(avs_coap_token_hex_t *out_value,
                                             const avs_coap_token_t *token) {
    assert(token->size <= 8);
    if (avs_hexlify(out_value->buf, sizeof(out_value->buf), NULL, token->bytes,
                    token->size)) {
        AVS_UNREACHABLE("avs_hexlify() failed");
    }
    return out_value->buf;
}

#define AVS_COAP_TOKEN_HEX(Tok) \
    (avs_coap_token_hex(&(avs_coap_token_hex_t) { { 0 } }, (Tok)))

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_TOKEN_H
