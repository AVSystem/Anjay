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

#ifndef ANJAY_COAP_IDSOURCE_AUTO_H
#define ANJAY_COAP_IDSOURCE_AUTO_H

#include "id_source.h"
#include "../../utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Creates automatic CoAP message identity generator which uses @p rand_seed
 * to feed internal pseudo-random number generator required to generate initial
 * CoAP Message ID and tokens of specified size @p token_size.
 *
 * On every call to @ref _anjay_coap_id_source_get returned Message ID will
 * be increased by one, returned Token on the other hand is always generated
 * at random.
 *
 * @param rand_seed     Seed used internally to generate identities.
 * @param token_size    Requested token size. Might be zero, in which case
 *                      returned token will always be @p ANJAY_COAP_TOKEN_EMPTY
 *
 * @returns pointer to a identity source object or NULL in case of error.
 */
coap_id_source_t *_anjay_coap_id_source_auto_new(anjay_rand_seed_t rand_seed,
                                                 size_t token_size);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_IDSOURCE_RANDOM_H
