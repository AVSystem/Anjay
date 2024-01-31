/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_IO_CBOR_DECODER_LL_H
#define FLUF_IO_CBOR_DECODER_LL_H

#if defined(FLUF_WITH_SENML_CBOR) || defined(FLUF_WITH_LWM2M_CBOR) \
        || defined(FLUF_WITH_CBOR)

#    include <avsystem/commons/avs_defs.h>

#    ifdef __cplusplus
extern "C" {
#    endif

#    if defined(FLUF_WITH_CBOR_INDEFINITE_BYTES) \
            || defined(FLUF_WITH_CBOR_DECIMAL_FRACTIONS)
#        define FLUF_MAX_SUBPARSER_NEST_STACK_SIZE 1
#    else /* defined(FLUF_WITH_CBOR_INDEFINITE_BYTES) || \
             defined(FLUF_WITH_CBOR_DECIMAL_FRACTIONS) */
#        define FLUF_MAX_SUBPARSER_NEST_STACK_SIZE 0
#    endif /* defined(FLUF_WITH_CBOR_INDEFINITE_BYTES) || \
              defined(FLUF_WITH_CBOR_DECIMAL_FRACTIONS) */

/**
 * Only decimal fractions or indefinite length bytes can cause nesting.
 */
#    ifdef FLUF_WITH_CBOR
#        define FLUF_MAX_SIMPLE_CBOR_NEST_STACK_SIZE \
            FLUF_MAX_SUBPARSER_NEST_STACK_SIZE
#    else // FLUF_WITH_CBOR
#        define FLUF_MAX_SIMPLE_CBOR_NEST_STACK_SIZE 0
#    endif // FLUF_WITH_CBOR

/**
 * The LwM2M requires wrapping entries in [ {} ], but decimal fractions or
 * indefinite length bytes add another level of nesting in form of an array.
 */
#    ifdef FLUF_WITH_SENML_CBOR
#        define FLUF_MAX_SENML_CBOR_NEST_STACK_SIZE \
            (2 + FLUF_MAX_SUBPARSER_NEST_STACK_SIZE)
#    else // FLUF_WITH_SENML_CBOR
#        define FLUF_MAX_SENML_CBOR_NEST_STACK_SIZE 0
#    endif // FLUF_WITH_SENML_CBOR

/**
 * In LwM2M CBOR, there may be {a: {b: {c: {[d]: value}}}}
 * Decimal fractions or indefinite length bytes don't add extra level here.
 */
#    ifdef FLUF_WITH_LWM2M_CBOR
#        define FLUF_MAX_LWM2M_CBOR_NEST_STACK_SIZE 5
#    else // FLUF_WITH_LWM2M_CBOR
#        define FLUF_MAX_LWM2M_CBOR_NEST_STACK_SIZE 0
#    endif // FLUF_WITH_LWM2M_CBOR

#    define FLUF_MAX_CBOR_NEST_STACK_SIZE                    \
        AVS_MAX(FLUF_MAX_SIMPLE_CBOR_NEST_STACK_SIZE,        \
                AVS_MAX(FLUF_MAX_SENML_CBOR_NEST_STACK_SIZE, \
                        FLUF_MAX_LWM2M_CBOR_NEST_STACK_SIZE))

#    define FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE (-1)

typedef struct fluf_cbor_ll_decoder_struct fluf_cbor_ll_decoder_t;

typedef enum {
    /* decoder is operational */
    FLUF_CBOR_LL_DECODER_STATE_OK,
    /* decoder reached end of stream */
    FLUF_CBOR_LL_DECODER_STATE_FINISHED,
    /* decoder could not make sense out of some part of the stream */
    FLUF_CBOR_LL_DECODER_STATE_ERROR
} fluf_cbor_ll_decoder_state_t;

typedef enum {
    FLUF_CBOR_LL_VALUE_NULL,
    FLUF_CBOR_LL_VALUE_UINT,
    FLUF_CBOR_LL_VALUE_NEGATIVE_INT,
    FLUF_CBOR_LL_VALUE_BYTE_STRING,
    FLUF_CBOR_LL_VALUE_TEXT_STRING,
    FLUF_CBOR_LL_VALUE_ARRAY,
    FLUF_CBOR_LL_VALUE_MAP,
    FLUF_CBOR_LL_VALUE_FLOAT,
    FLUF_CBOR_LL_VALUE_DOUBLE,
    FLUF_CBOR_LL_VALUE_BOOL,
    FLUF_CBOR_LL_VALUE_TIMESTAMP
} fluf_cbor_ll_value_type_t;

typedef struct {
    fluf_cbor_ll_value_type_t type;
    union {
        uint64_t u64;
        int64_t i64;
        float f32;
        double f64;
    } value;
} fluf_cbor_ll_number_t;

typedef struct {
    // If indefinite, this contains available bytes only for the current chunk.
    size_t bytes_available;
#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
    // Used only for indefinite length bytes.
    size_t initial_nesting_level;
    bool indefinite;
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    struct {
        size_t bytes_read;
        bool initialized;
        char buffer[sizeof("9999-12-31T23:59:60.999999999+99:59")];
    } string_time;
#    endif // FLUF_WITH_CBOR_STRING_TIME
} fluf_cbor_ll_decoder_bytes_ctx_t;

typedef enum {
    FLUF_CBOR_LL_SUBPARSER_NONE,
    FLUF_CBOR_LL_SUBPARSER_BYTES,
    FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME,
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    FLUF_CBOR_LL_SUBPARSER_STRING_TIME,
#    endif // FLUF_WITH_CBOR_STRING_TIME
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION,
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
} fluf_cbor_ll_subparser_type_t;

typedef struct {
    /* Type of the nested structure (FLUF_CBOR_LL_VALUE_BYTE_STRING,
     * FLUF_CBOR_LL_VALUE_TEXT_STRING, FLUF_CBOR_LL_VALUE_ARRAY or
     * FLUF_CBOR_LL_VALUE_MAP). */
    fluf_cbor_ll_value_type_t type;
    union {
        /* Number of items of the entry that were parsed */
        size_t total;
        /* For indefinite structures, only the even/odd state is tracked */
        bool odd;
    } items_parsed;
    /* Number of all items to be parsed (in case of definite length), or
     * NUM_ITEMS_INDEFINITE. */
    ptrdiff_t all_items;
} fluf_cbor_ll_nested_state_t;

struct fluf_cbor_ll_decoder_struct {
    const uint8_t *input_begin;
    const uint8_t *input;
    const uint8_t *input_end;
    bool input_last;

    uint8_t prebuffer[9];
    uint8_t prebuffer_size;
    uint8_t prebuffer_offset;

    fluf_cbor_ll_decoder_state_t state;
    bool needs_preprocessing;
    bool after_tag;
    /**
     * This structure contains information about currently processed value. The
     * value is "processed" as long as it is not fully consumed, so for example,
     * the current_item::value_type is of type "bytes" until it gets read
     * entirely by the user.
     */
    struct {
        /* Type to be decoded or currently being decoded. */
        fluf_cbor_ll_value_type_t value_type;

        /* Initial CBOR header byte of the value currently being decoded. */
        uint8_t initial_byte;
    } current_item;

    fluf_cbor_ll_subparser_type_t subparser_type;
    union {
        fluf_cbor_ll_decoder_bytes_ctx_t bytes_or_string_time;
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
        struct {
            size_t array_level;
            bool entered_array;
            double exponent;
            double mantissa;
        } decimal_fraction;
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    } subparser;

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
    size_t nest_stack_size;
    /**
     * A stack of recently entered nested types (e.g. arrays/maps). The type
     * lands on a nest_stack, if one of the following functions is called:
     *  - fluf_cbor_ll_decoder_enter_array(),
     *  - fluf_cbor_ll_decoder_enter_map().
     *
     * The last element (if any) indicates what kind of recursive structure we
     * are currently parsing. If too many nest levels are found, the parser
     * exits with error.
     */
    fluf_cbor_ll_nested_state_t nest_stack[FLUF_MAX_CBOR_NEST_STACK_SIZE];
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
};

/**
 * Initializes the low-level CBOR decoder.
 *
 * @param[out] ctx The context variable to initialize. It will be zeroed out,
 *                 and reset to the initial valid state.
 */
void fluf_cbor_ll_decoder_init(fluf_cbor_ll_decoder_t *ctx);

/**
 * Provides a data buffer to be parsed by @p ctx.
 *
 * <strong>IMPORTANT:</strong> Only the pointer to @p buff is stored, so the
 * buffer pointed to by that variable has to stay valid until the decoder is
 * discarded, or another payload is provided.
 *
 * <strong>NOTE:</strong> It is only valid to provide the input buffer either
 * immediately after calling @ref fluf_cbor_ll_decoder_init, or after some
 * operation has returned @ref FLUF_IO_WANT_NEXT_PAYLOAD.
 *
 * <strong>NOTE:</strong> The decoder may read-ahead up to 9 bytes of data
 * before actually attempting to decode it. This means that the decoder may
 * request further data chunks even to access elements that are fully contained
 * in the currently available chunk. Those will be decoded from the read-ahead
 * buffer after providing further data.
 *
 * @param ctx              The CBOR decoder context to operate on.
 *
 * @param buff             Pointer to a buffer containing incoming payload.
 *
 * @param buff_len         Size of @p buff in bytes.
 *
 * @param payload_finished Specifies whether the buffer passed is the last chunk
 *                         of a larger payload (e.g. last block of a CoAP
 *                         blockwise transfer).
 *
 *                         If determining that in advance is impractical, it is
 *                         permitted to always pass chunks with this flag set to
 *                         <c>false</c>, and then after next
 *                         @ref FLUF_IO_WANT_NEXT_PAYLOAD, pass a chunk of size
 *                         0 with this flag set to <c>true</c>.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_ERR_LOGIC if the context is not in a state in which providing
 *   a new payload is possible
 */
int fluf_cbor_ll_decoder_feed_payload(fluf_cbor_ll_decoder_t *ctx,
                                      const void *buff,
                                      size_t buff_len,
                                      bool payload_finished);

/**
 * Checks if the CBOR decoder is in some error / exceptional state.
 *
 * @param ctx The CBOR decoder context to operate on.
 *
 * @returns
 * - 0 if the decoder is in a valid state, ready for any of the data consumption
 *   functions
 * - @ref FLUF_IO_EOF if the decoder has reached the end of payload successfully
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if the decoder is in the middle of parsing
 *   some value and determining the next steps requires calling
 *   @ref fluf_cbor_ll_decoder_feed_payload
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred earlier during parsing and
 *   the decoder can no longer be used
 */
int fluf_cbor_ll_decoder_errno(fluf_cbor_ll_decoder_t *ctx);

/**
 * Returns the type of the current value that can be (or currently is) extracted
 * from the context.
 *
 * Before consuming (or preparing to consumption in some cases) the value with
 * one of the:
 *  - @ref fluf_cbor_ll_decoder_null(),
 *  - @ref fluf_cbor_ll_decoder_bool(),
 *  - @ref fluf_cbor_ll_decoder_number(),
 *  - @ref fluf_cbor_ll_decoder_bytes(),
 *  - @ref fluf_cbor_ll_decoder_enter_array(),
 *  - @ref fluf_cbor_ll_decoder_enter_map()
 *
 * the function is guaranteed to return same results each time it is called.
 *
 * @param[in]  ctx      The CBOR decoder context to operate on.
 *
 * @param[out] out_type Ponter to a variable where next type shall be stored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_current_value_type(
        fluf_cbor_ll_decoder_t *ctx, fluf_cbor_ll_value_type_t *out_type);

/**
 * Consumes a simple null value.
 *
 * NOTE: May only be called when the next value type is @ref
 * FLUF_CBOR_LL_VALUE_NULL, otherwise an error will be reported.
 *
 * @param[in] ctx The CBOR decoder context to operate on.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_null(fluf_cbor_ll_decoder_t *ctx);

/**
 * Consumes a simple boolean value.
 *
 * NOTE: May only be called when the next value type is @ref
 * FLUF_CBOR_LL_VALUE_BOOL, otherwise an error will be reported.
 *
 * @param[in]  ctx       The CBOR decoder context to operate on.
 *
 * @param[out] out_value Pointer to a variable where the value shall be stored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_bool(fluf_cbor_ll_decoder_t *ctx, bool *out_value);

/**
 * Consumes a scalar value from the context.
 *
 * NOTE: May only be called when the next value type is either:
 *  - @ref FLUF_CBOR_LL_VALUE_UINT,
 *  - @ref FLUF_CBOR_LL_VALUE_NEGATIVE_INT,
 *  - @ref FLUF_CBOR_LL_VALUE_FLOAT,
 *  - @ref FLUF_CBOR_LL_VALUE_DOUBLE,
 *  - @ref FLUF_CBOR_LL_VALUE_TIMESTAMP - in this case, the type identified in
 *    <c>out_value->type</c> will reflect the actual underlying data type, i.e.
 *    <c>out_value->type</c> will never be <c>FLUF_CBOR_LL_VALUE_TIMESTAMP</c>
 *
 * @param[in]  ctx       The CBOR decoder context to operate on.
 *
 * @param[out] out_value Pointer to a variable where the value shall be stored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_number(fluf_cbor_ll_decoder_t *ctx,
                                fluf_cbor_ll_number_t *out_value);

/**
 * Prepares for consumption of a byte or text stream element.
 *
 * NOTE: May only be called when the next value type is either:
 *  - @ref FLUF_CBOR_LL_VALUE_BYTE_STRING,
 *  - @ref FLUF_CBOR_LL_VALUE_TEXT_STRING.
 *
 * After successfully calling this function, you shall call @ref
 * fluf_cbor_ll_decoder_bytes_get_some, possibly multiple times until it sets
 * the <c>*out_message_finished</c> argument to <c>true</c>, to access the
 * actual data.
 *
 * @param[in]  ctx            The CBOR decoder context to operate on.
 *
 * @param[out] out_bytes_ctx  Pointer to a variable where the bytes context
 *                            pointer shall be stored.
 *
 * @param[out] out_total_size Pointer to a variable where the total size of the
 *                            bytes element will be stored. If the element has
 *                            an indefinite size, @ref
 *                            FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE (-1) will be
 *                            stored - the calling code will need to rely on the
 *                            <c>out_message_finished</c> argument to @ref
 *                            fluf_cbor_ll_decoder_bytes_get_some instead. If
 *                            this argument is <c>NULL</c>, it will be ignored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_bytes(fluf_cbor_ll_decoder_t *ctx,
                               fluf_cbor_ll_decoder_bytes_ctx_t **out_bytes_ctx,
                               ptrdiff_t *out_total_size);

/**
 * Consumes some amount of bytes from a byte or text stream element.
 *
 * This function shall be called after a successful call to @ref
 * fluf_cbor_ll_decoder_bytes, as many times as necessary until the
 * <c>*out_message_finished</c> argument is set to <c>true</c>, to eventually
 * access and consume the entire stream.
 *
 * <strong>NOTE:</strong> The consumed data is not copied - a pointer to either
 * the previously provided input buffer, or the context's internal read-ahead
 * buffer, is returned instead.
 *
 * @param[in]  bytes_ctx            Bytes context pointer previously returned
 *                                  by @ref fluf_cbor_ll_decoder_bytes.
 *
 * @param[out] out_buf              Pointer to a variable that will be set to a
 *                                  pointer to some portion of the stream.
 *
 * @param[out] out_buf_size         Pointer to a variable that will be set to
 *                                  the number of bytes immediately available at
 *                                  <c>*out_buf</c>. Note that this might only
 *                                  be a part of the total size of the string
 *                                  element.
 *
 * @param[out] out_message_finished Pointer to a variable that will be set to
 *                                  <c>true</c> if the currently returned block
 *                                  is the last portion of the string -
 *                                  otherwise <c>false</c>. Note that the last
 *                                  block may have a length of 0.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access further part of the byte stream
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_bytes_get_some(
        fluf_cbor_ll_decoder_bytes_ctx_t *bytes_ctx,
        const void **out_buf,
        size_t *out_buf_size,
        bool *out_message_finished);

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
/**
 * Prepares to the consumption of the array.
 *
 * NOTE: May only be called when the next value type is @ref
 * FLUF_CBOR_LL_VALUE_ARRAY.
 *
 * NOTE: The decoder has a limit of structure nesting levels. Any payload with
 * higher nesting degree will be rejected by the decoder by entering the error
 * state.
 *
 * @param[in]  ctx      The CBOR decoder context to operate on.
 *
 * @param[out] out_size Pointer to a variable where the total number of elements
 *                      in the array will be stored. If the array has an
 *                      indefinite size, @ref
 *                      FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE (-1) will be
 *                      stored - the calling code will need to rely on the @ref
 *                      fluf_cbor_ll_decoder_nesting_level function to determine
 *                      the end of the array instead. If this argument is
 *                      <c>NULL</c>, it will be ignored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_enter_array(fluf_cbor_ll_decoder_t *ctx,
                                     ptrdiff_t *out_size);

/**
 * Prepares to the consumption of the map.
 *
 * NOTE: May only be called when the next value type is @ref
 * FLUF_CBOR_LL_VALUE_MAP.
 *
 * NOTE: The decoder has a limit of structure nesting levels. Any payload with
 * higher nesting degree will be rejected by the decoder by entering the error
 * state.
 *
 * @param[in]  ctx            The CBOR decoder context to operate on.
 *
 * @param[out] out_pair_count Pointer to a variable where the total number of
 *                            element <strong>pairs</strong> in the array will
 *                            be stored. If the array has an indefinite size,
 *                            @ref FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE (-1)
 *                            will be stored - the calling code will need to
 *                            rely on the @ref
 *                            fluf_cbor_ll_decoder_nesting_level function to
 *                            determine the end of the map instead. If this
 *                            argument is <c>NULL</c>, it will be ignored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 * - @ref FLUF_IO_ERR_FORMAT if an error occurred while parsing the data
 * - @ref FLUF_IO_ERR_LOGIC if the end-of-stream has already been reached
 */
int fluf_cbor_ll_decoder_enter_map(fluf_cbor_ll_decoder_t *ctx,
                                   ptrdiff_t *out_pair_count);

/**
 * Gets the number of compound entites that the parser is currently inside.
 *
 * The number is incremented by 1 after a successfull call to @ref
 * fluf_cbor_ll_decoder_enter_array or @ref fluf_cbor_ll_decoder_enter_map, and
 * decreased after reading the last element of that array or map. In particular,
 * if the array or map has zero elements, its value will not be visibly
 * incremented at all.
 *
 * Note that if a decoding error occurred, the nesting level is assumed to be 0
 * instead of returning an explicit error.
 *
 * @param[in]  ctx               The CBOR decoder context to operate on.
 *
 * @param[out] out_nesting_level Pointer to a variable where the current nesting
 *                               level will be stored.
 *
 * @returns
 * - 0 on success
 * - @ref FLUF_IO_WANT_NEXT_PAYLOAD if end of the current payload has been
 *   reached and calling @ref fluf_cbor_ll_decoder_feed_payload is necessary to
 *   access the next value
 */
int fluf_cbor_ll_decoder_nesting_level(fluf_cbor_ll_decoder_t *ctx,
                                       size_t *out_nesting_level);
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0

#    ifdef __cplusplus
}
#    endif

#endif // defined(FLUF_WITH_SENML_CBOR) || defined(FLUF_WITH_LWM2M_CBOR) ||
       // defined(FLUF_WITH_CBOR)

#endif // FLUF_IO_CBOR_DECODER_LL_H
