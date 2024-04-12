/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf_cbor_encoder_ll.h>
#include <fluf/fluf_config.h>

#include "fluf_internal.h"

static void
write_to_buffer(void *buffer, const void *data, size_t data_length) {
    memcpy(buffer, data, data_length);
}

static size_t
write_cbor_header(void *buffer, cbor_major_type_t major_type, uint8_t value) {
    uint8_t header = (uint8_t) ((((uint8_t) major_type) << 5) | value);
    write_to_buffer(buffer, &header, 1);
    return 1;
}

static size_t encode_type_and_number(void *buffer,
                                     cbor_major_type_t major_type,
                                     uint64_t value) {
    size_t bytes_written = 1;
    if (value < 24) {
        write_cbor_header(buffer, major_type, (uint8_t) value);
    } else if (value <= UINT8_MAX) {
        uint8_t portable = (uint8_t) value;
        write_cbor_header(buffer, major_type, CBOR_EXT_LENGTH_1BYTE);
        write_to_buffer(&((uint8_t *) buffer)[bytes_written], &portable,
                        sizeof(portable));
        bytes_written += sizeof(portable);
    } else if (value <= UINT16_MAX) {
        uint16_t portable = avs_convert_be16((uint16_t) value);
        write_cbor_header(buffer, major_type, CBOR_EXT_LENGTH_2BYTE);
        write_to_buffer(&((uint8_t *) buffer)[bytes_written], &portable,
                        sizeof(portable));
        bytes_written += sizeof(portable);
    } else if (value <= UINT32_MAX) {
        uint32_t portable = avs_convert_be32((uint32_t) value);
        write_cbor_header(buffer, major_type, CBOR_EXT_LENGTH_4BYTE);
        write_to_buffer(&((uint8_t *) buffer)[bytes_written], &portable,
                        sizeof(portable));
        bytes_written += sizeof(portable);
    } else {
        uint64_t portable = avs_convert_be64((uint64_t) value);
        write_cbor_header(buffer, major_type, CBOR_EXT_LENGTH_8BYTE);
        write_to_buffer(&((uint8_t *) buffer)[bytes_written], &portable,
                        sizeof(portable));
        bytes_written += sizeof(portable);
    }
    return bytes_written;
}

size_t fluf_cbor_ll_encode_uint(void *buffer, uint64_t value) {
    return encode_type_and_number(buffer, CBOR_MAJOR_TYPE_UINT, value);
}

size_t fluf_cbor_ll_encode_int(void *buffer, int64_t value) {
    if (value >= 0) {
        return fluf_cbor_ll_encode_uint(buffer, (uint64_t) value);
    }

    value = -(value + 1);
    return encode_type_and_number(buffer, CBOR_MAJOR_TYPE_NEGATIVE_INT,
                                  (uint64_t) value);
}

size_t fluf_cbor_ll_encode_bool(void *buffer, bool value) {
    return write_cbor_header(buffer, CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE,
                             value ? CBOR_VALUE_BOOL_TRUE
                                   : CBOR_VALUE_BOOL_FALSE);
}

size_t fluf_cbor_ll_encode_float(void *buffer, float value) {
    uint32_t portable = avs_htonf(value);
    size_t bytes_written =
            write_cbor_header(buffer, CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE,
                              CBOR_EXT_LENGTH_4BYTE);
    write_to_buffer(&((uint8_t *) buffer)[bytes_written], &portable,
                    sizeof(portable));
    bytes_written += sizeof(portable);
    return bytes_written;
}

size_t fluf_cbor_ll_encode_double(void *buffer, double value) {
    if (((float) value) == value) {
        return fluf_cbor_ll_encode_float(buffer, (float) value);
    }

    uint64_t portable = avs_htond(value);
    size_t bytes_written =
            write_cbor_header(buffer, CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE,
                              CBOR_EXT_LENGTH_8BYTE);
    write_to_buffer(&((uint8_t *) buffer)[bytes_written], &portable,
                    sizeof(portable));
    bytes_written += sizeof(portable);
    return bytes_written;
}

size_t fluf_cbor_ll_encode_tag(void *buff, uint64_t value) {
    return encode_type_and_number(buff, CBOR_MAJOR_TYPE_TAG, value);
}

size_t fluf_cbor_ll_string_begin(void *buffer, size_t size) {
    return encode_type_and_number(buffer, CBOR_MAJOR_TYPE_TEXT_STRING, size);
}

size_t fluf_cbor_ll_bytes_begin(void *buffer, size_t size) {
    return encode_type_and_number(buffer, CBOR_MAJOR_TYPE_BYTE_STRING, size);
}

size_t fluf_cbor_ll_definite_map_begin(void *buffer, size_t items_count) {
    return encode_type_and_number(buffer, CBOR_MAJOR_TYPE_MAP, items_count);
}

size_t fluf_cbor_ll_definite_array_begin(void *buffer, size_t items_count) {
    return encode_type_and_number(buffer, CBOR_MAJOR_TYPE_ARRAY, items_count);
}

size_t fluf_cbor_ll_indefinite_map_begin(void *buffer) {
    return write_cbor_header(buffer, CBOR_MAJOR_TYPE_MAP,
                             CBOR_EXT_LENGTH_INDEFINITE);
}

size_t fluf_cbor_ll_indefinite_map_end(void *buffer) {
    const unsigned char break_char = CBOR_INDEFINITE_STRUCTURE_BREAK;
    size_t bytes_written = sizeof(break_char);
    write_to_buffer(buffer, &break_char, bytes_written);
    return bytes_written;
}
