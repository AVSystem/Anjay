/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf.h>

#include "fluf_coap_udp_header.h"
#include "fluf_coap_udp_msg.h"
#include "fluf_options.h"

#define _FLUF_COAP_OPTION_HEADER_MAX_LEN 5

#define _FLUF_FIELD_GET(field, mask, shift) (((field) & (mask)) >> (shift))

#define _FLUF_COAP_OPTION_DELTA_MASK 0xF0
#define _FLUF_COAP_OPTION_DELTA_SHIFT 4
#define _FLUF_COAP_OPTION_LENGTH_MASK 0x0F
#define _FLUF_COAP_OPTION_LENGTH_SHIFT 0

#define _FLUF_COAP_OPTION_U8_MASK 0xFF
#define _FLUF_COAP_OPTION_U16_MASK 0xFF00
#define _FLUF_COAP_OPTION_U16_SHIFT 8

#define _FLUF_COAP_EXT_U8 13
#define _FLUF_COAP_EXT_U16 14

#define _FLUF_COAP_EXT_U8_BASE ((int) 13)
#define _FLUF_COAP_EXT_U16_BASE ((int) 269)

static int update_extended_option(uint16_t *value,
                                  const uint8_t **buff_pointer,
                                  const uint8_t *buff_end) {
    if (*value == _FLUF_COAP_EXT_U8) {
        if (*buff_pointer >= buff_end) {
            return FLUF_ERR_BUFF;
        }
        (*buff_pointer)++;
        *value = _FLUF_COAP_EXT_U8_BASE + **buff_pointer;
    } else if (*value == _FLUF_COAP_EXT_U16) {
        if (*buff_pointer + 1 >= buff_end) {
            return FLUF_ERR_BUFF;
        }
        (*buff_pointer)++;
        *value = _FLUF_COAP_EXT_U16_BASE
                 + ((uint16_t) (**buff_pointer << _FLUF_COAP_OPTION_U16_SHIFT)
                    & _FLUF_COAP_OPTION_U16_MASK);
        (*buff_pointer)++;
        *value += **buff_pointer;
    }

    return 0;
}

static int get_option_from_buff(const uint8_t **buff_pointer,
                                const uint8_t *buff_end,
                                fluf_coap_option_t *opt,
                                uint16_t last_opt_number) {
    opt->option_number =
            _FLUF_FIELD_GET(**buff_pointer, _FLUF_COAP_OPTION_DELTA_MASK,
                            _FLUF_COAP_OPTION_DELTA_SHIFT);
    opt->payload_len =
            _FLUF_FIELD_GET(**buff_pointer, _FLUF_COAP_OPTION_LENGTH_MASK,
                            _FLUF_COAP_OPTION_LENGTH_SHIFT);

    int res = update_extended_option(&(opt->option_number), buff_pointer,
                                     buff_end);
    if (res) {
        return res;
    }
    opt->option_number += last_opt_number;
    size_t temp_payload_len = opt->payload_len;
    res = update_extended_option((uint16_t *) &temp_payload_len, buff_pointer,
                                 buff_end);
    if (res) {
        return res;
    }
    opt->payload_len = temp_payload_len;

    // move to payload position
    (*buff_pointer)++;
    if (*buff_pointer + opt->payload_len > buff_end) {
        return FLUF_ERR_BUFF;
    }
    opt->payload = *buff_pointer;
    (*buff_pointer) += opt->payload_len;

    return 0;
}

static size_t prepare_option_header(uint8_t *opt_header,
                                    uint16_t previous_opt_number,
                                    uint16_t opt_number,
                                    size_t payload_size) {
    size_t header_size = 1;

    uint16_t new_opt_number = opt_number - previous_opt_number;

    if (new_opt_number < _FLUF_COAP_EXT_U8_BASE) {
        opt_header[0] =
                (uint8_t) (new_opt_number << _FLUF_COAP_OPTION_DELTA_SHIFT);
    } else if (new_opt_number < _FLUF_COAP_EXT_U16_BASE) {
        opt_header[0] =
                (uint8_t) (_FLUF_COAP_EXT_U8 << _FLUF_COAP_OPTION_DELTA_SHIFT);
        header_size++;
        opt_header[header_size - 1] =
                (uint8_t) (new_opt_number - _FLUF_COAP_EXT_U8_BASE);
    } else {
        opt_header[0] = _FLUF_COAP_EXT_U16 << _FLUF_COAP_OPTION_DELTA_SHIFT;
        header_size++;
        new_opt_number = new_opt_number - _FLUF_COAP_EXT_U16_BASE;
        opt_header[header_size - 1] =
                ((uint8_t) new_opt_number >> _FLUF_COAP_OPTION_U16_SHIFT)
                & _FLUF_COAP_OPTION_U8_MASK;
        header_size++;
        opt_header[header_size - 1] =
                (uint8_t) new_opt_number & _FLUF_COAP_OPTION_U8_MASK;
    }

    if (payload_size < _FLUF_COAP_EXT_U8_BASE) {
        opt_header[0] += (uint8_t) payload_size;
    } else if (payload_size < _FLUF_COAP_EXT_U16_BASE) {
        opt_header[0] += _FLUF_COAP_EXT_U8;
        header_size++;
        opt_header[header_size - 1] =
                (uint8_t) (payload_size - _FLUF_COAP_EXT_U8_BASE);
    } else {
        opt_header[0] += _FLUF_COAP_EXT_U16;
        header_size++;
        opt_header[header_size - 1] = ((payload_size - _FLUF_COAP_EXT_U16_BASE)
                                       >> _FLUF_COAP_OPTION_U16_SHIFT)
                                      & _FLUF_COAP_OPTION_U8_MASK;
        header_size++;
        opt_header[header_size - 1] = (payload_size - _FLUF_COAP_EXT_U16_BASE)
                                      & _FLUF_COAP_OPTION_U8_MASK;
    }

    return header_size;
}

int _fluf_coap_options_add_data(fluf_coap_options_t *opts,
                                uint16_t opt_number,
                                const void *data,
                                size_t data_size) {
    assert(opts->buff_begin);
    assert(opts->options);
    assert(opts->options_size);

    size_t new_opt_position = opts->options_number;
    uint16_t previous_opt_number = 0;

    if (opts->options_number == opts->options_size) {
        // there is no space for new option
        return FLUF_ERR_OPTIONS_ARRAY;
    }

    // find place to insert new options
    for (size_t i = 0; i < opts->options_number; i++) {
        if (opt_number < opts->options[i].option_number) {
            new_opt_position = i;
            break;
        }
    }
    if (new_opt_position) {
        previous_opt_number = opts->options[new_opt_position - 1].option_number;
    }

    // prepare new option record
    uint8_t opt_header[_FLUF_COAP_OPTION_HEADER_MAX_LEN] = { 0 };
    size_t opt_header_len = 0;

    opt_header_len = prepare_option_header(opt_header, previous_opt_number,
                                           opt_number, data_size);
    size_t new_opt_total_size = opt_header_len + data_size;

    // check if new option fit
    // last element pointer + last element payload_len + new option total size
    // can't oversize msg buffer
    uint8_t *buff_end = opts->buff_begin + opts->buff_size;
    if (opts->options_number) {
        fluf_coap_option_t last_opt = opts->options[opts->options_number - 1];
        if (last_opt.payload + last_opt.payload_len + new_opt_total_size
                >= buff_end) {
            return FLUF_ERR_BUFF;
        }
    } else {
        if (opts->buff_begin + new_opt_total_size >= buff_end) {
            return FLUF_ERR_BUFF;
        }
    }

    size_t memory_offset = new_opt_total_size;
    uint8_t *memory_to_move_start_point = opts->buff_begin;

    // update memory buffer
    if (new_opt_position != opts->options_number) {
        // new option is not last element
        // update record that comes after the new one
        uint8_t next_opt_header[_FLUF_COAP_OPTION_HEADER_MAX_LEN] = { 0 };
        size_t next_opt_header_old_len = 0;
        size_t next_opt_header_len = 0;

        uint16_t prev_opt_number = 0;

        if (new_opt_position) {
            prev_opt_number = opts->options[new_opt_position - 1].option_number;
            memory_to_move_start_point =
                    opts->buff_begin
                    + ((opts->options[new_opt_position - 1].payload
                        + opts->options[new_opt_position - 1].payload_len)
                       - opts->buff_begin);
        }

        next_opt_header_old_len = prepare_option_header(
                next_opt_header, prev_opt_number,
                opts->options[new_opt_position].option_number,
                opts->options[new_opt_position].payload_len);
        next_opt_header_len = prepare_option_header(
                next_opt_header, opt_number,
                opts->options[new_opt_position].option_number,
                opts->options[new_opt_position].payload_len);

        memory_offset += next_opt_header_len - next_opt_header_old_len;

        // calculate size of memory block to move
        fluf_coap_option_t last_option =
                opts->options[opts->options_number - 1];
        size_t memory_to_move_size =
                (size_t) ((last_option.payload + last_option.payload_len)
                          - memory_to_move_start_point);

        memmove((memory_to_move_start_point + memory_offset),
                memory_to_move_start_point, memory_to_move_size);
        memcpy(memory_to_move_start_point, opt_header, opt_header_len);
        memcpy(memory_to_move_start_point + opt_header_len, data, data_size);
        memcpy(memory_to_move_start_point + opt_header_len + data_size,
               next_opt_header, next_opt_header_len);
    } else {
        if (new_opt_position) {
            memory_to_move_start_point =
                    opts->buff_begin
                    + ((opts->options[new_opt_position - 1].payload
                        + opts->options[new_opt_position - 1].payload_len)
                       - opts->buff_begin);
        }

        memcpy(memory_to_move_start_point, opt_header, opt_header_len);
        memcpy(memory_to_move_start_point + opt_header_len, data, data_size);
    }

    // update options array
    if (opts->options_number) {
        for (size_t i = opts->options_number - 1; i >= new_opt_position; i--) {
            memcpy((void *) &opts->options[i + 1],
                   (const void *) &opts->options[i],
                   sizeof(fluf_coap_option_t));
            opts->options[i + 1].payload =
                    opts->options[i + 1].payload + memory_offset;
            if (!i) {
                break;
            }
        }
    }
    opts->options[new_opt_position].payload_len = data_size;
    opts->options[new_opt_position].option_number = opt_number;
    opts->options[new_opt_position].payload =
            memory_to_move_start_point + opt_header_len;

    opts->options_number++;

    return 0;
}

static int add_uint(fluf_coap_options_t *opts,
                    uint16_t opt_number,
                    void *value,
                    size_t value_size) {
    uint8_t *data_ptr = (uint8_t *) value;
    size_t data_size = value_size;

    while (data_size > 0 && !(*data_ptr)) {
        data_ptr++;
        data_size--;
    }
    return _fluf_coap_options_add_data(opts, opt_number, data_ptr, data_size);
}

int _fluf_coap_options_add_u16(fluf_coap_options_t *opts,
                               uint16_t opt_number,
                               uint16_t value) {
    uint16_t portable = avs_convert_be16((uint16_t) value);
    return add_uint(opts, opt_number, &portable, sizeof(portable));
}

int _fluf_coap_options_add_u32(fluf_coap_options_t *opts,
                               uint16_t opt_number,
                               uint32_t value) {
    uint32_t portable = avs_convert_be32((uint32_t) value);
    return add_uint(opts, opt_number, &portable, sizeof(portable));
}

int _fluf_coap_options_add_u64(fluf_coap_options_t *opts,
                               uint16_t opt_number,
                               uint64_t value) {
    uint64_t portable = avs_convert_be64((uint64_t) value);
    return add_uint(opts, opt_number, &portable, sizeof(portable));
}

int _fluf_coap_options_get_data_iterate(const fluf_coap_options_t *opts,
                                        uint16_t option_number,
                                        size_t *iterator,
                                        size_t *out_option_size,
                                        void *out_buffer,
                                        size_t out_buffer_size) {

    size_t opt_occurance = 0;
    size_t requested_opt = 0;
    if (iterator) {
        requested_opt = *iterator;
        (*iterator)++;
    }

    for (size_t i = 0; i < opts->options_number; i++) {
        if (opts->options[i].option_number == option_number) {
            if (opt_occurance == requested_opt) {
                if (out_buffer_size < opts->options[i].payload_len) {
                    return FLUF_ERR_BUFF;
                }
                memcpy(out_buffer, opts->options[i].payload,
                       opts->options[i].payload_len);
                if (out_option_size) {
                    *out_option_size = opts->options[i].payload_len;
                }
                return 0;
            } else {
                opt_occurance++;
            }
        }
    }
    return _FLUF_COAP_OPTION_MISSING;
}

int _fluf_coap_options_get_string_iterate(const fluf_coap_options_t *opts,
                                          uint16_t option_number,
                                          size_t *iterator,
                                          size_t *out_option_size,
                                          char *out_buffer,
                                          size_t out_buffer_size) {
    int res = _fluf_coap_options_get_data_iterate(opts, option_number, iterator,
                                                  out_option_size,
                                                  (void *) out_buffer,
                                                  out_buffer_size);

    if (!res) {
        if (*out_option_size + 1 <= out_buffer_size) {
            out_buffer[*out_option_size] = '\0';
            (*out_option_size)++;
        } else {
            return FLUF_ERR_BUFF;
        }
    }
    return res;
}

int _fluf_coap_options_get_u16_iterate(const fluf_coap_options_t *opts,
                                       uint16_t option_number,
                                       size_t *iterator,
                                       uint16_t *out_value) {
    uint8_t value[sizeof(uint16_t)];
    size_t out_option_size;

    int ret_val =
            _fluf_coap_options_get_data_iterate(opts, option_number, iterator,
                                                &out_option_size, &value,
                                                sizeof(uint16_t));

    if (!ret_val) {
        *out_value = 0;
        for (size_t i = 0; i < out_option_size; ++i) {
            *out_value = (uint16_t) (*out_value << 8);
            *out_value = (uint16_t) (*out_value | value[i]);
        }
    }
    return ret_val;
}

int _fluf_coap_options_get_u32_iterate(const fluf_coap_options_t *opts,
                                       uint16_t option_number,
                                       size_t *iterator,
                                       uint32_t *out_value) {
    uint8_t value[sizeof(uint32_t)];
    size_t out_option_size;

    int ret_val =
            _fluf_coap_options_get_data_iterate(opts, option_number, iterator,
                                                &out_option_size, &value,
                                                sizeof(uint32_t));

    if (!ret_val) {
        *out_value = 0;
        for (size_t i = 0; i < out_option_size; ++i) {
            *out_value = (uint32_t) (*out_value << 8);
            *out_value = (uint32_t) (*out_value | value[i]);
        }
    }
    return ret_val;
}

int _fluf_coap_options_decode(fluf_coap_options_t *opts,
                              const uint8_t *msg,
                              size_t msg_size,
                              size_t *bytes_read) {
    assert(msg);
    assert(opts->options);
    assert(opts->options_size);

    const uint8_t *buff_pointer = msg;
    const uint8_t *buff_end = msg + msg_size;
    uint16_t last_opt_number = 0;

    // clear counter
    opts->options_number = 0;

    while (buff_pointer < buff_end) {
        // end of the options field
        if (*buff_pointer == _FLUF_COAP_PAYLOAD_MARKER) {
            *bytes_read = (size_t) (buff_pointer - msg);
            return 0;
        }
        if (opts->options_number == opts->options_size) {
            // we reach the limit before 0xFF marker -> return error
            return FLUF_ERR_OPTIONS_ARRAY;
        }

        int res = get_option_from_buff(
                &buff_pointer, buff_end,
                (fluf_coap_option_t *) &opts->options[opts->options_number],
                last_opt_number);
        if (res) {
            // error in get_option_from_buff
            return res;
        }
        last_opt_number = opts->options[opts->options_number].option_number;
        opts->options_number++;
    }

    *bytes_read = (size_t) (buff_pointer - msg);
    return 0;
}
