/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_JSON_LIKE_CBOR_DECODER_H
#define ANJAY_IO_JSON_LIKE_CBOR_DECODER_H

#include <anjay/anjay_config.h>

#include "../anjay_json_like_decoder.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Only decimal fractions or indefinite length bytes can cause nesting.
 */
#define MAX_SIMPLE_CBOR_NEST_STACK_SIZE 1

/**
 * LwM2M requires wrapping entries in [ {} ], but keys/values that are a string
 * (byte/text) or a decimal fraction add another level of nesting.
 */
#define MAX_SENML_CBOR_NEST_STACK_SIZE 3

#ifdef ANJAY_WITH_LWM2M_GATEWAY
/**
 * Compared to variant without support for LwM2M gateway:
 * - the paths can be up to 5 components long, as they might contain a prefix
 *   which selects an end device,
 * - the prefix is a string; it can be a key directly, or an initial element of
 *   an array key,
 * - the prefix, if any, is the first component of a path, therefore only the
 *   root map can contain such keys.
 *
 * This means that:
 * - the root map can be 5 levels deep now (so that's 1 more),
 * - CBOR decoder's stack when parsing a key can grow by 2 levels now (array key
 *   with prefix as indefinite text string), but that is valid only for the root
 *   map, of which the maximum stack growth determined by inner maps will be
 *   larger anyway.
 *
 * Therefore, the maximum stack size is 1+1+1+1+2 = 6;
 */
#    define MAX_LWM2M_CBOR_NEST_STACK_SIZE 6
#else // ANJAY_WITH_LWM2M_GATEWAY
/**
 * LwM2M CBOR is a tree of nested maps. Root map is up to 4 levels deep. This
 * happens in case there's a value of multi-instance resource, and key for each
 * nested map adds only 1 path component, in a form like:
 * {<key>: {<key>: {<key>: {<key>: <value>}}}}
 *
 * When parsing a map, CBOR decoder's stack grows by 1 + whathever is incurred
 * by its contents, i.e. key-value pairs.
 *
 * In LwM2M CBOR, each key is an uint, or an array of uints (possibly of size
 * just 1), which needs 1 nesting level.
 *
 * The value is:
 * - a scalar, or
 * - an indefinite length string (byte/text) or a decimal fraction, which needs
 *   1 nesting level, or
 * - a nested map (unless we're at maximum depth).
 *
 * Therefore, when entering the innermost map CBOR decoder's stack will grow by
 * 1+1=2 levels at max. For outer maps it's 1 + maximum growth incurred by
 * contents, which essentially is 1 + maximum growth incurred by inner maps.
 *
 * Therefore, the maximum stack size is 1+1+1+2 = 5;
 */
#    define MAX_LWM2M_CBOR_NEST_STACK_SIZE 5
#endif // ANJAY_WITH_LWM2M_GATEWAY

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
