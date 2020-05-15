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

#ifndef AVS_COAP_SRC_UDP_OLD_MSG_H
#define AVS_COAP_SRC_UDP_OLD_MSG_H

#include <assert.h>
#include <stdint.h>

#include <avsystem/commons/avs_defs.h>

#include <avsystem/coap/option.h>

#include "options/avs_coap_option.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * @param opts Option set to iterate over.
 * @returns An CoAP Option iterator object.
 */
avs_coap_option_iterator_t _avs_coap_optit_begin(avs_coap_options_t *opts);

/**
 * Advances the @p optit iterator to the next CoAP Option.
 *
 * @param optit CoAP Option iterator to operate on.
 * @returns @p optit.
 */
avs_coap_option_iterator_t *
_avs_coap_optit_next(avs_coap_option_iterator_t *optit);

/**
 * Erases the option @p optit is currently pointing to and updates it so that
 * it points to the following option.
 *
 * @returns @p optit.
 */
avs_coap_option_iterator_t *
_avs_coap_optit_erase(avs_coap_option_iterator_t *optit);

/**
 * Checks if the @p optit points to the area after CoAP options list.
 *
 * @param optit Iterator to check.
 * @returns true if there are no more Options to iterate over (i.e. the iterator
 *          is invalidated), false if it points to a valid Option.
 */
bool _avs_coap_optit_end(const avs_coap_option_iterator_t *optit);

/**
 * @param optit Iterator to operate on.
 * @returns Number of the option currently pointed to by @p optit
 */
uint32_t _avs_coap_optit_number(const avs_coap_option_iterator_t *optit);

static inline avs_coap_option_t *
_avs_coap_optit_current(const avs_coap_option_iterator_t *optit) {
    assert(optit);
    assert(!_avs_coap_optit_end(optit));

    return (avs_coap_option_t *) optit->curr_opt;
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_OLD_MSG_H
