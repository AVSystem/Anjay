/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_JSON_LIKE_DECODER_H
#define ANJAY_IO_JSON_LIKE_DECODER_H

#include <inttypes.h>

#include <avsystem/commons/avs_stream.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    ANJAY_JSON_LIKE_VALUE_NULL,
    ANJAY_JSON_LIKE_VALUE_UINT,
    ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT,
    ANJAY_JSON_LIKE_VALUE_BYTE_STRING,
    ANJAY_JSON_LIKE_VALUE_TEXT_STRING,
    ANJAY_JSON_LIKE_VALUE_ARRAY,
    ANJAY_JSON_LIKE_VALUE_MAP,
    ANJAY_JSON_LIKE_VALUE_FLOAT,
    ANJAY_JSON_LIKE_VALUE_DOUBLE,
    ANJAY_JSON_LIKE_VALUE_BOOL
} anjay_json_like_value_type_t;

typedef enum {
    /* decoder is operational */
    ANJAY_JSON_LIKE_DECODER_STATE_OK,
    /* decoder reached end of stream */
    ANJAY_JSON_LIKE_DECODER_STATE_FINISHED,
    /* decoder could not make sense out of some part of the stream */
    ANJAY_JSON_LIKE_DECODER_STATE_ERROR
} anjay_json_like_decoder_state_t;

typedef struct anjay_json_like_decoder_struct anjay_json_like_decoder_t;

void _anjay_json_like_decoder_delete(anjay_json_like_decoder_t **ctx);

anjay_json_like_decoder_state_t
_anjay_json_like_decoder_state(const anjay_json_like_decoder_t *ctx);

/**
 * Returns the type of the current value that can be (or currently is) extracted
 * from the context.
 *
 * Before consuming (or preparing to consumption in some cases) the value with
 * one of the:
 *  - @ref _anjay_json_like_decoder_number(),
 *  - @ref _anjay_json_like_decoder_bool(),
 *  - @ref _anjay_json_like_decoder_bytes(),
 *  - @ref _anjay_json_like_decoder_enter_array(),
 *  - @ref _anjay_json_like_decoder_enter_map()
 *
 * the function is guaranteed to return same results each time it is called.
 *
 * @param[in]   ctx         Decoder context to operate on.
 * @param[out]  out_type    Ponter to a variable where next type shall be
 *                          stored.
 *
 * @returns 0 on success, negative value if an error occurred, or if the decoder
 * is not in @ref ANJAY_JSON_LIKE_DECODER_STATE_OK state.
 */
int _anjay_json_like_decoder_current_value_type(
        anjay_json_like_decoder_t *ctx, anjay_json_like_value_type_t *out_type);

/**
 * Consumes a simple boolean value.
 *
 * NOTE: May only be called when the next value type is @ref
 * ANJAY_JSON_LIKE_VALUE_BOOL, otherwise an error will be reported.
 *
 * @param[in]   ctx         Decoder context to operate on.
 * @param[out]  out_value   Pointer to a variable where the value shall
 *                          be stored.
 *
 * @returns 0 on success, negative value if an error occurred (including the
 * case where the decoder is not in @ref ANJAY_JSON_LIKE_DECODER_STATE_OK
 * state).
 */
int _anjay_json_like_decoder_bool(anjay_json_like_decoder_t *ctx,
                                  bool *out_value);

typedef struct {
    anjay_json_like_value_type_t type;
    union {
        uint64_t u64;
        int64_t i64;
        float f32;
        double f64;
    } value;
} anjay_json_like_number_t;

/**
 * Consumes a scalar value from the context.
 *
 * NOTE: May only be called when the next value type is either:
 *  - @ref ANJAY_JSON_LIKE_VALUE_UINT,
 *  - @ref ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT,
 *  - @ref ANJAY_JSON_LIKE_VALUE_FLOAT,
 *  - @ref ANJAY_JSON_LIKE_VALUE_DOUBLE.
 *
 * @param[in]   ctx         Decoder context to operate on
 * @param[out]  out_value   Pointer to a variable where the value shall
 *                          be stored.
 * @returns 0 on success, negative value if an error occurred (including the
 * case where the decoder is not in @ref ANJAY_JSON_LIKE_DECODER_STATE_OK
 * state).
 */
int _anjay_json_like_decoder_number(anjay_json_like_decoder_t *ctx,
                                    anjay_json_like_number_t *out_value);

/**
 * Reads encoded bytes or text into a stream provided by the caller.
 *
 * NOTE: May only be called when the next value type is either:
 *  - @ref ANJAY_JSON_LIKE_VALUE_BYTE_STRING,
 *  - @ref ANJAY_JSON_LIKE_VALUE_TEXT_STRING.
 *
 * @param ctx           Decoder context to operate on.
 *
 * @param target_stream A stream into which the read bytes will be written.
 *
 * @returns 0 on success, negative value if an error occurred (including the
 * case where the decoder is not in @ref ANJAY_JSON_LIKE_DECODER_STATE_OK
 * state).
 */
int _anjay_json_like_decoder_bytes(anjay_json_like_decoder_t *ctx,
                                   avs_stream_t *target_stream);

/**
 * Prepares to the consumption of the array.
 *
 * NOTE: May only be called when the next value type is @ref
 * ANJAY_JSON_LIKE_VALUE_ARRAY.
 *
 * NOTE: The decoder may have a limit of structure nesting levels (e.g.
 * MAX_NEST_STACK_SIZE defined in cbor_decoder.c). Any payload with higher
 * nesting degree will be rejected by the decoder by entering the @ref
 * ANJAY_JSON_LIKE_DECODER_STATE_ERROR state.
 *
 * @param[in]   ctx             Decoder context to operate on.
 *
 * @returns 0 on success, negative value if an error occurred (including the
 * case where the decoder is not in @ref ANJAY_JSON_LIKE_DECODER_STATE_OK
 * state).
 */
int _anjay_json_like_decoder_enter_array(anjay_json_like_decoder_t *ctx);

/**
 * Prepares to the consumption of the map.
 *
 * NOTE: May only be called when the next value type is @ref
 * ANJAY_JSON_LIKE_VALUE_MAP.
 *
 * NOTE: The decoder may have a limit of structure nesting levels (e.g.
 * MAX_NEST_STACK_SIZE defined in cbor_decoder.c). Any payload with higher
 * nesting degree will be rejected.
 *
 * @param[in]   ctx             Decoder context to operate on.
 *
 * @returns 0 on success, negative value if an error occurred (including the
 * case where the decoder is not in @ref ANJAY_JSON_LIKE_DECODER_STATE_OK
 * state).
 */
int _anjay_json_like_decoder_enter_map(anjay_json_like_decoder_t *ctx);

/**
 * @returns Number of compound entities that the parser is currently inside.
 *          The number is incremented by 1 after a successfull call to
 *          @ref _anjay_json_like_decoder_enter_array or
 *          @ref _anjay_json_like_decoder_enter_map, and decreased after reading
 *          the last element of that array or map. In particular, if the array
 *          or map has zero elements, its value will not be visibly incremented
 *          at all.
 */
size_t _anjay_json_like_decoder_nesting_level(anjay_json_like_decoder_t *ctx);

/**
 * Converts @ref anjay_json_like_number_t to @c int64_t , if possible.
 */
int _anjay_json_like_decoder_get_i64_from_number(
        const anjay_json_like_number_t *number, int64_t *out_value);

/**
 * Converts @ref anjay_json_like_number_t to @c uint64_t , if possible.
 */
int _anjay_json_like_decoder_get_u64_from_number(
        const anjay_json_like_number_t *number, uint64_t *out_value);

/**
 * Converts @ref anjay_json_like_number_t to @c double , if possible.
 */
int _anjay_json_like_decoder_get_double_from_number(
        const anjay_json_like_number_t *number, double *out_value);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_JSON_LIKE_DECODER_H */
