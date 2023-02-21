/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_JSON_LIKE_CBOR_DECODER_H
#define ANJAY_IO_JSON_LIKE_CBOR_DECODER_H

#include "../anjay_json_like_decoder.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Only decimal fractions or indefinite length bytes can cause nesting.
 */
#define MAX_SIMPLE_CBOR_NEST_STACK_SIZE 1

/**
 * The LwM2M requires wrapping entries in [ {} ], but decimal fractions or
 * indefinite length bytes add another level of nesting in form of an array.
 */
#define MAX_SENML_CBOR_NEST_STACK_SIZE 3

/**
 * In LwM2M CBOR, there may be {a: {b: {c: {[d]: value}}}}
 * Decimal fractions or indefinite length bytes don't add extra level here.
 */
#define MAX_LWM2M_CBOR_NEST_STACK_SIZE 5

anjay_json_like_decoder_t *_anjay_cbor_decoder_new(avs_stream_t *stream,
                                                   size_t max_nesting_depth);

typedef struct {
    bool indefinite;
    // Indefinite length struct may be completely empty.
    bool empty;
    // Used only for indefinite length bytes.
    size_t initial_nesting_level;
    // If indefinite, this contains available bytes only for the current chunk.
    size_t bytes_available;
} anjay_io_cbor_bytes_ctx_t;

int _anjay_io_cbor_get_bytes_ctx(anjay_json_like_decoder_t *ctx_,
                                 anjay_io_cbor_bytes_ctx_t *out_bytes_ctx);

int _anjay_io_cbor_get_some_bytes(anjay_json_like_decoder_t *ctx_,
                                  anjay_io_cbor_bytes_ctx_t *bytes_ctx,
                                  void *out_buf,
                                  size_t buf_size,
                                  size_t *out_bytes_read,
                                  bool *out_message_finished);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_IO_JSON_LIKE_CBOR_DECODER_H
