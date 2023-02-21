/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_PARSE_UTILS_H
#define AVS_COAP_SRC_PARSE_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define _AVS_FIELD_GET(field, mask, shift) (((field) & (mask)) >> (shift))
#define _AVS_FIELD_SET(field, mask, shift, value) \
    ((field) = (uint8_t) (((field) & ~(mask))     \
                          | (uint8_t) (((value) << (shift)) & (mask))))

static inline uint16_t extract_u16(const uint8_t *data) {
    uint16_t result;
    memcpy(&result, data, sizeof(uint16_t));
    return avs_convert_be16(result);
}

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_PARSE_UTILS_H
