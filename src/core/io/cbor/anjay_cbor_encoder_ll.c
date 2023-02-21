/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_CBOR

#    include <assert.h>
#    include <math.h>
#    include <string.h>

#    include <avsystem/commons/avs_memory.h>
#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_common.h"
#    include "../anjay_senml_like_encoder_vtable.h"

#    include "anjay_cbor_encoder_ll.h"
#    include "anjay_cbor_types.h"

VISIBILITY_SOURCE_BEGIN

typedef struct cbor_encoder_struct {
    const anjay_senml_like_encoder_vtable_t *vtable;
} cbor_encoder_t;

static inline int write_cbor_header(avs_stream_t *stream,
                                    cbor_major_type_t major_type,
                                    uint8_t value) {
    assert(value < 32);
    uint8_t header = (uint8_t) ((((uint8_t) major_type) << 5) | value);
    return avs_is_ok(avs_stream_write(stream, &header, 1)) ? 0 : -1;
}

static int encode_type_and_number(avs_stream_t *stream,
                                  cbor_major_type_t major_type,
                                  uint64_t value) {
    if (value < 24) {
        return write_cbor_header(stream, major_type, (uint8_t) value);
    } else if (value <= UINT8_MAX) {
        uint8_t portable = (uint8_t) value;
        if (write_cbor_header(stream, major_type, CBOR_EXT_LENGTH_1BYTE)
                || avs_is_err(avs_stream_write(stream, &portable,
                                               sizeof(portable)))) {
            return -1;
        }
    } else if (value <= UINT16_MAX) {
        uint16_t portable = avs_convert_be16((uint16_t) value);
        if (write_cbor_header(stream, major_type, CBOR_EXT_LENGTH_2BYTE)
                || avs_is_err(avs_stream_write(stream, &portable,
                                               sizeof(portable)))) {
            return -1;
        }
    } else if (value <= UINT32_MAX) {
        uint32_t portable = avs_convert_be32((uint32_t) value);
        if (write_cbor_header(stream, major_type, CBOR_EXT_LENGTH_4BYTE)
                || avs_is_err(avs_stream_write(stream, &portable,
                                               sizeof(portable)))) {
            return -1;
        }
    } else {
        uint64_t portable = avs_convert_be64((uint64_t) value);
        if (write_cbor_header(stream, major_type, CBOR_EXT_LENGTH_8BYTE)
                || avs_is_err(avs_stream_write(stream, &portable,
                                               sizeof(portable)))) {
            return -1;
        }
    }
    return 0;
}

int _anjay_cbor_ll_encode_uint(avs_stream_t *stream, uint64_t value) {
    return encode_type_and_number(stream, CBOR_MAJOR_TYPE_UINT, value);
}

int _anjay_cbor_ll_encode_int(avs_stream_t *stream, int64_t value) {
    if (value >= 0) {
        return _anjay_cbor_ll_encode_uint(stream, (uint64_t) value);
    }

    value = -(value + 1);
    return encode_type_and_number(stream, CBOR_MAJOR_TYPE_NEGATIVE_INT,
                                  (uint64_t) value);
}

int _anjay_cbor_ll_encode_bool(avs_stream_t *stream, bool value) {
    return write_cbor_header(stream, CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE,
                             value ? CBOR_VALUE_BOOL_TRUE
                                   : CBOR_VALUE_BOOL_FALSE);
}

int _anjay_cbor_ll_encode_float(avs_stream_t *stream, float value) {
    uint32_t portable = avs_htonf(value);
    int retval = write_cbor_header(stream,
                                   CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE,
                                   CBOR_EXT_LENGTH_4BYTE);
    if (!retval
            && avs_is_err(
                       avs_stream_write(stream, &portable, sizeof(portable)))) {
        retval = -1;
    }
    return retval;
}

int _anjay_cbor_ll_encode_double(avs_stream_t *stream, double value) {
    if (((float) value) == value) {
        return _anjay_cbor_ll_encode_float(stream, (float) value);
    }

    uint64_t portable = avs_htond(value);
    int retval = write_cbor_header(stream,
                                   CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE,
                                   CBOR_EXT_LENGTH_8BYTE);
    if (!retval
            && avs_is_err(
                       avs_stream_write(stream, &portable, sizeof(portable)))) {
        retval = -1;
    }
    return retval;
}

int _anjay_cbor_ll_bytes_begin(avs_stream_t *stream, size_t size) {
    return encode_type_and_number(stream, CBOR_MAJOR_TYPE_BYTE_STRING, size);
}

int _anjay_cbor_ll_encode_string(avs_stream_t *stream, const char *data) {
    size_t size = strlen(data);
    int retval =
            encode_type_and_number(stream, CBOR_MAJOR_TYPE_TEXT_STRING, size);
    if (!retval && avs_is_err(avs_stream_write(stream, data, size))) {
        retval = -1;
    }
    return retval;
}

int _anjay_cbor_ll_definite_map_begin(avs_stream_t *stream,
                                      size_t items_count) {
    return encode_type_and_number(stream, CBOR_MAJOR_TYPE_MAP, items_count);
}

int _anjay_cbor_ll_definite_array_begin(avs_stream_t *stream,
                                        size_t items_count) {
    return encode_type_and_number(stream, CBOR_MAJOR_TYPE_ARRAY, items_count);
}

#endif // ANJAY_WITH_CBOR
