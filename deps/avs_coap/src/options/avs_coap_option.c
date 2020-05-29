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

#include <avs_coap_init.h>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/option.h>

#include "options/avs_coap_option.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

VISIBILITY_SOURCE_BEGIN

static inline size_t get_ext_field_size(uint8_t base_value) {
    assert(base_value < _AVS_COAP_EXT_RESERVED);

    switch (base_value) {
    case _AVS_COAP_EXT_U8:
        return sizeof(uint8_t);
    case _AVS_COAP_EXT_U16:
        return sizeof(uint16_t);
    default:
        return 0;
    }
}

static inline uint32_t decode_ext_value(uint8_t base_value,
                                        const uint8_t *ext_value_ptr) {
    assert(base_value < _AVS_COAP_EXT_RESERVED);

    switch (base_value) {
    case _AVS_COAP_EXT_U8:
        return (uint32_t) * (const uint8_t *) ext_value_ptr
               + _AVS_COAP_EXT_U8_BASE;
    case _AVS_COAP_EXT_U16:
        return (uint32_t) extract_u16(ext_value_ptr) + _AVS_COAP_EXT_U16_BASE;
    default:
        return base_value;
    }
}

static inline bool ext_value_overflows(uint8_t base_value,
                                       const uint8_t *ext_value_ptr) {
    return base_value == _AVS_COAP_EXT_U16
           && extract_u16(ext_value_ptr) > UINT16_MAX - _AVS_COAP_EXT_U16_BASE;
}

static inline const uint8_t *ext_delta_ptr(const avs_coap_option_t *opt) {
    return opt->content;
}

static inline const uint8_t *ext_length_ptr(const avs_coap_option_t *opt) {
    return opt->content
           + get_ext_field_size(_avs_coap_option_get_short_delta(opt));
}

const uint8_t *_avs_coap_option_value(const avs_coap_option_t *opt) {
    return ext_length_ptr(opt)
           + get_ext_field_size(_avs_coap_option_get_short_length(opt));
}

int _avs_coap_option_u16_value(const avs_coap_option_t *opt,
                               uint16_t *out_value) {
    const uint8_t *value_data = _avs_coap_option_value(opt);
    uint32_t length = _avs_coap_option_content_length(opt);
    if (length > sizeof(*out_value)) {
        return -1;
    }
    *out_value = 0;
    for (size_t i = 0; i < length; ++i) {
        *out_value = (uint16_t) (*out_value << 8);
        *out_value = (uint16_t) (*out_value | value_data[i]);
    }
    return 0;
}

int _avs_coap_option_u32_value(const avs_coap_option_t *opt,
                               uint32_t *out_value) {
    const uint8_t *value_data = _avs_coap_option_value(opt);
    uint32_t length = _avs_coap_option_content_length(opt);
    if (length > sizeof(*out_value)) {
        return -1;
    }
    *out_value = 0;
    for (size_t i = 0; i < length; ++i) {
        *out_value = (uint32_t) (*out_value << 8);
        *out_value = (uint32_t) (*out_value | value_data[i]);
    }
    return 0;
}

int _avs_coap_option_string_value(const avs_coap_option_t *opt,
                                  size_t *out_option_size,
                                  char *buffer,
                                  size_t buffer_size) {
    size_t str_length = _avs_coap_option_content_length(opt);
    *out_option_size = str_length + 1;
    if (buffer_size < *out_option_size) {
        return -1;
    }
    memcpy(buffer, _avs_coap_option_value(opt), str_length);
    buffer[str_length] = '\0';
    return 0;
}

int _avs_coap_option_block_seq_number(const avs_coap_option_t *opt,
                                      uint32_t *out_seq_num) {
    uint32_t value;
    if (_avs_coap_option_u32_value(opt, &value) || value >= (1 << 24)) {
        return -1;
    }

    *out_seq_num = (value >> 4);
    return 0;
}

int _avs_coap_option_block_has_more(const avs_coap_option_t *opt,
                                    bool *out_has_more) {
    uint32_t value;
    if (_avs_coap_option_u32_value(opt, &value) || value >= (1 << 24)) {
        return -1;
    }

    *out_has_more = !!(value & 0x08);
    return 0;
}

int _avs_coap_option_block_size(const avs_coap_option_t *opt,
                                uint16_t *out_size,
                                bool *is_bert) {
    assert(out_size);
    assert(is_bert);
    uint32_t value;
    if (_avs_coap_option_u32_value(opt, &value) || value >= (1 << 24)) {
        return -1;
    }
    uint8_t size_exponent = value & 0x07;
    *is_bert = (size_exponent == AVS_COAP_OPT_BERT_SZX);

    if (*is_bert) {
        // From RFC8323:
        // "In descriptive usage, a BERT Option is interpreted in the same way
        //  as the equivalent Option with SZX == 6, except that the payload is
        //  also allowed to contain multiple blocks."
        size_exponent = AVS_COAP_OPT_BLOCK_MAX_SZX;
    }

    *out_size = (uint16_t) (1 << (size_exponent + 4));
    AVS_ASSERT(_avs_coap_is_valid_block_size(*out_size),
               "bug in out_size calculations");
    return 0;
}

uint32_t _avs_coap_option_delta(const avs_coap_option_t *opt) {
    uint32_t delta = decode_ext_value(_avs_coap_option_get_short_delta(opt),
                                      ext_delta_ptr(opt));
    assert(delta <= UINT16_MAX + _AVS_COAP_EXT_U16_BASE);
    return delta;
}

uint32_t _avs_coap_option_content_length(const avs_coap_option_t *opt) {
    uint32_t length = decode_ext_value(_avs_coap_option_get_short_length(opt),
                                       ext_length_ptr(opt));
    assert(length <= UINT16_MAX + _AVS_COAP_EXT_U16_BASE);
    return length;
}

static inline bool is_delta_valid(const avs_coap_option_t *opt,
                                  size_t max_opt_bytes) {
    uint8_t short_delta = _avs_coap_option_get_short_delta(opt);
    if (short_delta == _AVS_COAP_EXT_RESERVED) {
        return false;
    }

    size_t required_bytes = 1 + get_ext_field_size(short_delta);
    return required_bytes <= max_opt_bytes
           && !ext_value_overflows(short_delta, ext_delta_ptr(opt));
}

static inline bool is_length_valid(const avs_coap_option_t *opt,
                                   size_t max_opt_bytes) {
    uint8_t short_length = _avs_coap_option_get_short_length(opt);
    if (short_length == _AVS_COAP_EXT_RESERVED) {
        return false;
    }

    uint8_t short_delta = _avs_coap_option_get_short_delta(opt);
    size_t required_bytes = 1 + get_ext_field_size(short_delta)
                            + get_ext_field_size(short_length);
    return required_bytes <= max_opt_bytes
           && !ext_value_overflows(short_length, ext_length_ptr(opt));
}

bool _avs_coap_option_is_valid(const avs_coap_option_t *opt,
                               size_t max_opt_bytes) {
    if (max_opt_bytes == 0 || !is_delta_valid(opt, max_opt_bytes)
            || !is_length_valid(opt, max_opt_bytes)) {
        return false;
    }

    uint32_t length = (uint32_t) _avs_coap_option_sizeof(opt);
    return (uintptr_t) opt->content + length >= (uintptr_t) opt->content
           && length <= max_opt_bytes;
}

size_t _avs_coap_option_sizeof(const avs_coap_option_t *opt) {
    const uint8_t *endptr =
            _avs_coap_option_value(opt) + _avs_coap_option_content_length(opt);

    assert((const uint8_t *) opt < endptr);
    return (size_t) (endptr - (const uint8_t *) opt);
}

static inline size_t encode_ext_value(uint8_t *ptr, uint16_t ext_value) {
    if (ext_value >= _AVS_COAP_EXT_U16_BASE) {
        uint16_t value_net_byte_order = avs_convert_be16(
                (uint16_t) (ext_value - _AVS_COAP_EXT_U16_BASE));
        avs_unaligned_put(ptr, value_net_byte_order);
        return sizeof(value_net_byte_order);
    } else if (ext_value >= _AVS_COAP_EXT_U8_BASE) {
        *ptr = (uint8_t) (ext_value - _AVS_COAP_EXT_U8_BASE);
        return 1;
    }

    return 0;
}

static inline size_t
opt_write_header(uint8_t *ptr, uint16_t opt_number_delta, uint16_t opt_length) {
    avs_coap_option_t *opt = (avs_coap_option_t *) ptr;
    ptr = opt->content;

    if (opt_number_delta >= _AVS_COAP_EXT_U16_BASE) {
        _avs_coap_option_set_short_delta(opt, _AVS_COAP_EXT_U16);
    } else if (opt_number_delta >= _AVS_COAP_EXT_U8_BASE) {
        _avs_coap_option_set_short_delta(opt, _AVS_COAP_EXT_U8);
    } else {
        _avs_coap_option_set_short_delta(opt,
                                         (uint8_t) (opt_number_delta & 0xF));
    }

    if (opt_length >= _AVS_COAP_EXT_U16_BASE) {
        _avs_coap_option_set_short_length(opt, _AVS_COAP_EXT_U16);
    } else if (opt_length >= _AVS_COAP_EXT_U8_BASE) {
        _avs_coap_option_set_short_length(opt, _AVS_COAP_EXT_U8);
    } else {
        _avs_coap_option_set_short_length(opt, (uint8_t) (opt_length & 0xF));
    }

    ptr += encode_ext_value(ptr, opt_number_delta);
    ptr += encode_ext_value(ptr, opt_length);

    return (size_t) (ptr - (uint8_t *) opt);
}

static inline bool memory_regions_overlap(const void *a,
                                          size_t a_size,
                                          const void *b,
                                          size_t b_size) {
    /*
     * Source: https://stackoverflow.com/a/3269471/2339636
     *
     * If ranges [x1, x2) and [y1, y2) overlap, there exists N such that
     *
     *     x1 <= N < x2 && y1 <= N < y2
     */

    const void *a_end = (const char *) a + a_size;
    const void *b_end = (const char *) b + b_size;
    return a < b_end && b < a_end;
}

size_t _avs_coap_option_serialize(uint8_t *buffer,
                                  size_t buffer_size,
                                  size_t opt_number_delta,
                                  const void *opt_data,
                                  size_t opt_data_size) {
    size_t opt_header_size =
            _avs_coap_get_opt_header_size(opt_number_delta, opt_data_size);

    if (opt_header_size + opt_data_size > buffer_size) {
        LOG(ERROR, _("not enough space to serialize option"));
        return 0;
    }

    assert(opt_number_delta <= UINT16_MAX);
    assert(opt_data_size <= UINT16_MAX);
    size_t header_bytes_written =
            opt_write_header(buffer, (uint16_t) opt_number_delta,
                             (uint16_t) opt_data_size);

    assert(header_bytes_written == opt_header_size);
    assert(header_bytes_written + opt_data_size <= buffer_size);

    /*
     * NOTE: buffer and opt_data  regions may overlap. This allows for resizing
     * options in-place, as long as opt_write_header call above does not touch
     * option data.
     */
    assert(!memory_regions_overlap(buffer, header_bytes_written, opt_data,
                                   opt_data_size));

    memmove(buffer + header_bytes_written,
            opt_data ? opt_data : "",
            opt_data_size);
    return header_bytes_written + opt_data_size;
}
