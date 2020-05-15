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

#ifndef AVS_COAP_SRC_CODE_UTILS_H
#define AVS_COAP_SRC_CODE_UTILS_H

#include <stddef.h>
#include <stdint.h>

#include "avs_coap_parse_utils.h"

#include <avsystem/coap/code.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

/** @{
 * CoAP code class/detail accessors. See RFC7252 for details.
 */

static inline void _avs_coap_code_set_class(uint8_t *code, uint8_t cls) {
    assert(cls < 8);
    _AVS_FIELD_SET(*code, _AVS_COAP_CODE_CLASS_MASK, _AVS_COAP_CODE_CLASS_SHIFT,
                   cls);
}

static inline void _avs_coap_code_set_detail(uint8_t *code, uint8_t detail) {
    assert(detail < 32);
    _AVS_FIELD_SET(*code, _AVS_COAP_CODE_DETAIL_MASK,
                   _AVS_COAP_CODE_DETAIL_SHIFT, detail);
}

/** @} */

/** @returns true if @p code is in correct range. */
static inline bool _avs_coap_code_in_range(int code) {
    return (code >= 0 && code <= UINT8_MAX);
}

/**
 * @returns true if @p code represents a signaling message, false otherwise.
 *          Note: only 7.01 to 7.05 codes are supported, as defined in RFC 8323.
 */
static inline bool _avs_coap_code_is_signaling_message(uint8_t code) {
    // According to RFC8323, all codes from range 7.00-7.31 refer to signaling
    // messages. Only codes from range 7.01-7.05 are currently defined.
    return avs_coap_code_get_class(code) == 7;
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_CODE_UTILS_H
