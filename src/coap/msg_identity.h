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

#ifndef ANJAY_COAP_MSG_IDENTITY_H
#define ANJAY_COAP_MSG_IDENTITY_H

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_COAP_MAX_TOKEN_LENGTH 8

typedef struct {
    char bytes[ANJAY_COAP_MAX_TOKEN_LENGTH];
} anjay_coap_token_t;

#define ANJAY_COAP_TOKEN_EMPTY ((anjay_coap_token_t){{0}})

static inline bool _anjay_coap_token_equal(const anjay_coap_token_t *first,
                                           size_t first_size,
                                           const anjay_coap_token_t *second,
                                           size_t second_size) {
    return first_size == second_size
        && !memcmp(first->bytes, second->bytes, first_size);
}

typedef struct anjay_coap_msg_identity {
    uint16_t msg_id;
    anjay_coap_token_t token;
    size_t token_size;
} anjay_coap_msg_identity_t;

#define ANJAY_COAP_MSG_IDENTITY_EMPTY ((anjay_coap_msg_identity_t){0,{{0}},0})

static inline
bool _anjay_coap_identity_equal(const anjay_coap_msg_identity_t *a,
                                const anjay_coap_msg_identity_t *b) {
    return a->msg_id == b->msg_id
        && a->token_size == b->token_size
        && !memcmp(&a->token, &b->token, a->token_size);
}

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_MSG_IDENTITY_H
