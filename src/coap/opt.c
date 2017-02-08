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

#include "opt.h"

#include <assert.h>
#include <endian.h>
#include <stdio.h>

#include "log.h"
#include "msg_internal.h"

VISIBILITY_SOURCE_BEGIN

static inline size_t get_ext_field_size(uint8_t base_value) {
    assert(base_value < ANJAY_COAP_EXT_RESERVED);

    switch (base_value) {
    case ANJAY_COAP_EXT_U8:
        return sizeof(uint8_t);
    case ANJAY_COAP_EXT_U16:
        return sizeof(uint16_t);
    default:
        return 0;
    }
}

static inline uint32_t decode_ext_value(uint8_t base_value,
                                        const uint8_t *ext_value_ptr) {
    assert(base_value < ANJAY_COAP_EXT_RESERVED);

    switch (base_value) {
    case ANJAY_COAP_EXT_U8:
        return (uint32_t) * (const uint8_t *)ext_value_ptr + ANJAY_COAP_EXT_U8_BASE;
    case ANJAY_COAP_EXT_U16:
        return (uint32_t)extract_u16(ext_value_ptr) + ANJAY_COAP_EXT_U16_BASE;
    default:
        return base_value;
    }
}

static inline bool ext_value_overflows(uint8_t base_value,
                                       const uint8_t *ext_value_ptr) {
    return base_value == ANJAY_COAP_EXT_U16
           && extract_u16(ext_value_ptr) > UINT16_MAX - ANJAY_COAP_EXT_U16_BASE;
}

static inline const uint8_t *ext_delta_ptr(const anjay_coap_opt_t *opt) {
    return opt->content;
}

static inline const uint8_t *ext_length_ptr(const anjay_coap_opt_t *opt) {
    return opt->content + get_ext_field_size(_anjay_coap_opt_get_short_delta(opt));
}

const uint8_t *_anjay_coap_opt_value(const anjay_coap_opt_t *opt) {
    return ext_length_ptr(opt)
           + get_ext_field_size(_anjay_coap_opt_get_short_length(opt));
}

int _anjay_coap_opt_u8_value(const anjay_coap_opt_t *opt,
                             uint8_t *out_value) {
    uint32_t length = _anjay_coap_opt_content_length(opt);
    if (length > sizeof(uint8_t)) {
        return -1;
    }

    *out_value = 0;
    memcpy(out_value, _anjay_coap_opt_value(opt), length);
    return 0;
}

#define INT_GETTER(Bits) \
    int _anjay_coap_opt_u##Bits##_value(const anjay_coap_opt_t *opt, \
                                        uint##Bits##_t *out_value) { \
        uint32_t length = _anjay_coap_opt_content_length(opt); \
        if (length > sizeof(uint##Bits##_t)) { \
            return -1; \
        } \
        uint##Bits##_t tmp = 0; \
        memcpy(((char *) &tmp) + (sizeof(uint##Bits##_t) - length), \
               _anjay_coap_opt_value(opt), length); \
        *out_value = be##Bits##toh(tmp); \
        return 0; \
    }

INT_GETTER(16)
INT_GETTER(32)
INT_GETTER(64)

int _anjay_coap_opt_string_value(const anjay_coap_opt_t *opt,
                                 size_t *out_bytes_read,
                                 char *buffer,
                                 size_t buffer_size) {
    size_t str_length = _anjay_coap_opt_content_length(opt);
    if (buffer_size <= str_length) {
        return -1;
    }
    memcpy(buffer, _anjay_coap_opt_value(opt), str_length);
    buffer[str_length] = '\0';
    *out_bytes_read = str_length + 1;
    return 0;
}

int _anjay_coap_opt_block_seq_number(const anjay_coap_opt_t *opt,
                                     uint32_t *out_seq_num) {
    uint32_t value;
    if (_anjay_coap_opt_u32_value(opt, &value)
            || value >= (1 << 24)) {
        return -1;
    }

    *out_seq_num = (value >> 4);
    return 0;
}

int _anjay_coap_opt_block_has_more(const anjay_coap_opt_t *opt,
                                   bool *out_has_more) {
    uint32_t value;
    if (_anjay_coap_opt_u32_value(opt, &value)
            || value >= (1 << 24)) {
        return -1;
    }

    *out_has_more = !!(value & 0x08);
    return 0;
}

int _anjay_coap_opt_block_size(const anjay_coap_opt_t *opt,
                               uint16_t *out_size) {
    uint32_t value;
    if (_anjay_coap_opt_u32_value(opt, &value)
            || value >= (1 << 24)) {
        return -1;
    }

    *out_size = (uint16_t)(1 << ((value & 0x07) + 4));
    if (!_anjay_coap_is_valid_block_size(*out_size)) {
        return -1;
    }
    return 0;
}

uint32_t _anjay_coap_opt_delta(const anjay_coap_opt_t *opt) {
    return decode_ext_value(_anjay_coap_opt_get_short_delta(opt),
                            ext_delta_ptr(opt));
}

uint32_t _anjay_coap_opt_content_length(const anjay_coap_opt_t *opt) {
    return decode_ext_value(_anjay_coap_opt_get_short_length(opt),
                            ext_length_ptr(opt));
}

static inline bool is_delta_valid(const anjay_coap_opt_t *opt,
                                  size_t max_opt_bytes) {
    uint8_t short_delta = _anjay_coap_opt_get_short_delta(opt);
    if (short_delta == ANJAY_COAP_EXT_RESERVED) {
        return false;
    }

    size_t required_bytes = 1 + get_ext_field_size(short_delta);
    return required_bytes <= max_opt_bytes
           && !ext_value_overflows(short_delta, ext_delta_ptr(opt));
}

static inline bool is_length_valid(const anjay_coap_opt_t *opt,
                                  size_t max_opt_bytes) {
    uint8_t short_length = _anjay_coap_opt_get_short_length(opt);
    if (short_length == ANJAY_COAP_EXT_RESERVED) {
        return false;
    }

    uint8_t short_delta = _anjay_coap_opt_get_short_delta(opt);
    size_t required_bytes = 1 + get_ext_field_size(short_delta)
                              + get_ext_field_size(short_length);
    return required_bytes <= max_opt_bytes
           && !ext_value_overflows(short_length, ext_length_ptr(opt));
}

bool _anjay_coap_opt_is_valid(const anjay_coap_opt_t *opt,
                              size_t max_opt_bytes) {
    if (max_opt_bytes == 0
           || !is_delta_valid(opt, max_opt_bytes)
           || !is_length_valid(opt, max_opt_bytes)) {
        return false;
    }

    uint32_t length = (uint32_t)_anjay_coap_opt_sizeof(opt);
    return opt->content + length >= opt->content
            && length <= max_opt_bytes;
}

size_t _anjay_coap_opt_sizeof(const anjay_coap_opt_t *opt) {
    const uint8_t *endptr = _anjay_coap_opt_value(opt) + _anjay_coap_opt_content_length(opt);

    assert((const uint8_t *)opt < endptr);
    return (size_t)(endptr - (const uint8_t *)opt);
}

void _anjay_coap_opt_debug_print(const anjay_coap_opt_t *opt) {
    coap_log(DEBUG, "opt: delta %u, length %u, content:",
             _anjay_coap_opt_delta(opt),
             _anjay_coap_opt_content_length(opt));

    const uint8_t *value = _anjay_coap_opt_value(opt);
    for (size_t i = 0; i < _anjay_coap_opt_content_length(opt); ++i) {
        coap_log(DEBUG, "%02x", value[i]);
    }
}

#ifdef ANJAY_TEST
#include "test/opt.c"
#endif // ANJAY_TEST
