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

#ifndef AVS_TEST_UTILS_H
#define AVS_TEST_UTILS_H

#include <math.h>

#include <avsystem/coap/token.h>

#define SCOPED_PTR(Type, Deleter) __attribute__((__cleanup__(Deleter))) Type *

avs_coap_token_t nth_token(uint64_t k);
avs_coap_token_t current_token(void);
void reset_token_generator(void);

static inline avs_coap_token_t from_bytes(const void *bytes, size_t size) {
    avs_coap_token_t token;
    memcpy(token.bytes, bytes, size);
    token.size = (uint8_t) size;
    return token;
}

#define MAKE_TOKEN(Bytes)                                                    \
    from_bytes((Bytes),                                                      \
               (ASSERT_TRUE(sizeof(Bytes) - 1 <= AVS_COAP_MAX_TOKEN_LENGTH), \
                sizeof(Bytes) - 1))

#endif /* AVS_TEST_UTILS_H */
