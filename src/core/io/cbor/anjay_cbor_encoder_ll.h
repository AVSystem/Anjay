/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_CBOR_ENCODER_LL_H
#define ANJAY_IO_CBOR_ENCODER_LL_H

#include <avsystem/commons/avs_stream.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * This is a stateless low-level CBOR encoder.
 *
 * User is responsible for ensuring, that all declared bytes or map elements
 * were written before encoding other value type.
 */

int _anjay_cbor_ll_encode_uint(avs_stream_t *stream, uint64_t value);

int _anjay_cbor_ll_encode_int(avs_stream_t *stream, int64_t value);

int _anjay_cbor_ll_encode_bool(avs_stream_t *stream, bool value);

int _anjay_cbor_ll_encode_float(avs_stream_t *stream, float value);

int _anjay_cbor_ll_encode_double(avs_stream_t *stream, double value);

int _anjay_cbor_ll_encode_string(avs_stream_t *stream, const char *data);

int _anjay_cbor_ll_bytes_begin(avs_stream_t *stream, size_t size);

static inline int _anjay_cbor_ll_bytes_append(avs_stream_t *stream,
                                              const void *data,
                                              size_t size) {
    return avs_is_ok(avs_stream_write(stream, data, size)) ? 0 : -1;
}

int _anjay_cbor_ll_definite_map_begin(avs_stream_t *stream, size_t items_count);

int _anjay_cbor_ll_definite_array_begin(avs_stream_t *stream,
                                        size_t items_count);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_IO_CBOR_ENCODER_LL_H
