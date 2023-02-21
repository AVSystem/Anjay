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
#    include <stdbool.h>
#    include <string.h>

#    include "anjay_cbor_types.h"
#    include "anjay_json_like_cbor_decoder.h"

#    include "../anjay_json_like_decoder_vtable.h"

#    include <avsystem/commons/avs_memory.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_utils.h>

#    include <anjay_modules/anjay_io_utils.h>
#    include <anjay_modules/anjay_utils_core.h>

VISIBILITY_SOURCE_BEGIN

#    define LOG(...) _anjay_log(cbor, __VA_ARGS__)

#    define NUM_ITEMS_INDEFINITE (-1)

typedef struct {
    /* Type of the nested structure (ANJAY_CBOR_VALUE_ARRAY or
     * ANJAY_CBOR_VALUE_MAP). */
    anjay_json_like_value_type_t type;
    /* Number of items of the entry that were parsed */
    size_t items_parsed;
    /* Number of all items to be parsed (in case of definite length), or
     * NUM_ITEMS_INDEFINITE. */
    ptrdiff_t all_items;
} cbor_nested_state_t;

static bool is_indefinite(cbor_nested_state_t *state) {
    return state->all_items == NUM_ITEMS_INDEFINITE;
}

typedef struct {
    const anjay_json_like_decoder_vtable_t *vtable;
    avs_stream_t *stream;
    anjay_json_like_decoder_state_t state;
    /**
     * This structure contains information about currently processed value. The
     * value is "processed" as long as it is not fully consumed, so for example,
     * the current_item::value_type is of type "bytes" until it gets read
     * entirely by the user.
     */
    struct {
        /* Type to be decoded or currently being decoded. */
        anjay_json_like_value_type_t value_type;

        /* A value corresponding to one of cbor_major_type_t enum values. */
        cbor_major_type_t major_type;
        /**
         * Additional (decoded) info, which may be:
         *  - extended length size,
         *  - short value.
         */
        int additional_info;
    } current_item;

    size_t nest_stack_size;
    size_t max_nest_stack_size;
    /**
     * A stack of recently entered nested types (e.g. arrays/maps). The type
     * lands on a nest_stack, if one of the following functions is called:
     *  - _anjay_cbor_decoder_enter_array(),
     *  - _anjay_cbor_decoder_enter_map().
     *
     * The last element (if any) indicates what kind of recursive structure we
     * are currently parsing. If too many nest levels are found, the parser
     * exits with error.
     */
    cbor_nested_state_t nest_stack[];
} anjay_cbor_decoder_t;

typedef enum { CBOR_DECODER_TAG_DECIMAL_FRACTION = 4 } cbor_decoder_tag_t;

static int parse_major_type(const uint8_t initial_byte) {
    return initial_byte >> 5;
}

static int parse_additional_info(const uint8_t initial_byte) {
    return initial_byte & 0x1f;
}

static int parse_ext_length_size(const anjay_cbor_decoder_t *ctx,
                                 size_t *out_ext_len_size) {
    switch (ctx->current_item.additional_info) {
    case CBOR_EXT_LENGTH_1BYTE:
        *out_ext_len_size = 1;
        break;
    case CBOR_EXT_LENGTH_2BYTE:
        *out_ext_len_size = 2;
        break;
    case CBOR_EXT_LENGTH_4BYTE:
        *out_ext_len_size = 4;
        break;
    case CBOR_EXT_LENGTH_8BYTE:
        *out_ext_len_size = 8;
        break;
    default:
        LOG(DEBUG,
            _("unexpected extended length value: ") "%d",
            (int) ctx->current_item.additional_info);
        return -1;
    }
    return 0;
}

static bool is_length_extended(const anjay_cbor_decoder_t *ctx) {
    switch (ctx->current_item.additional_info) {
    case CBOR_EXT_LENGTH_1BYTE:
    case CBOR_EXT_LENGTH_2BYTE:
    case CBOR_EXT_LENGTH_4BYTE:
    case CBOR_EXT_LENGTH_8BYTE:
        return true;
    default:
        return false;
    }
}

static void parse_float_or_simple_value(anjay_cbor_decoder_t *ctx) {
    assert(ctx->current_item.major_type
           == CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE);

    /* See "2.3.  Floating-Point Numbers and Values with No Content" */
    switch (ctx->current_item.additional_info) {
    case CBOR_VALUE_BOOL_FALSE:
    case CBOR_VALUE_BOOL_TRUE:
        ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_BOOL;
        break;
    case CBOR_VALUE_NULL:
        ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_NULL;
        break;
    case CBOR_VALUE_FLOAT_16:
    case CBOR_VALUE_FLOAT_32:
        ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_FLOAT;
        break;
    case CBOR_VALUE_FLOAT_64:
        ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_DOUBLE;
        break;
    case CBOR_VALUE_UNDEFINED:
        LOG(DEBUG, _("unsupported simple type undefined"));
        goto error;
    case CBOR_VALUE_IN_NEXT_BYTE:
        /* As per "Table 2: Simple Values", range 32..255 is unassigned, so
         * we may call it an error. */
    default:
        LOG(DEBUG,
            _("unsupported simple type ") "%d",
            ctx->current_item.additional_info);
        goto error;
    }
    return;

error:
    ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
}

static void ignore_tag(anjay_cbor_decoder_t *ctx) {
    assert(ctx->current_item.major_type == CBOR_MAJOR_TYPE_TAG);
    assert(ctx->current_item.additional_info
           != CBOR_DECODER_TAG_DECIMAL_FRACTION);
    size_t ext_len_size = 0;
    uint64_t ignored;
    if (is_length_extended(ctx)) {
        if (parse_ext_length_size(ctx, &ext_len_size)
                || avs_is_err(avs_stream_read_reliably(
                           ctx->stream, &ignored, ext_len_size))) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        }
    }
    (void) ignored;
}

static int nested_state_push(anjay_cbor_decoder_t *ctx);
static void nested_state_pop(anjay_cbor_decoder_t *ctx);
static cbor_nested_state_t *nested_state_top(anjay_cbor_decoder_t *ctx) {
    assert(ctx->nest_stack_size);
    if (ctx->nest_stack_size) {
        return &ctx->nest_stack[ctx->nest_stack_size - 1];
    } else {
        /* should not happen, but still */
        return NULL;
    }
}

static void preprocess_next_value(anjay_cbor_decoder_t *ctx) {
    bool data_must_follow = false;

    while (ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_OK) {
        uint8_t byte;
        avs_error_t err = avs_stream_getch(ctx->stream, (char *) &byte, NULL);
        if (avs_is_eof(err)) {
            if (data_must_follow) {
                ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            } else {
                ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_FINISHED;
            }
            return;
        } else if (avs_is_err(err)) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return;
        }

        if (byte == CBOR_INDEFINITE_STRUCTURE_BREAK) {
            /* end of the indefinite map, array or byte/text string */
            if (!ctx->nest_stack_size
                    || (nested_state_top(ctx)->type == ANJAY_JSON_LIKE_VALUE_MAP
                        && nested_state_top(ctx)->items_parsed % 2)
                    || !is_indefinite(nested_state_top(ctx))) {
                ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            } else {
                nested_state_pop(ctx);
            }
            continue;
        }
        ctx->current_item.major_type =
                (cbor_major_type_t) parse_major_type(byte);
        ctx->current_item.additional_info = parse_additional_info(byte);

        if (ctx->current_item.major_type < _CBOR_MAJOR_TYPE_BEGIN
                || ctx->current_item.major_type >= _CBOR_MAJOR_TYPE_END) {
            LOG(DEBUG,
                _("invalid major type: ") "%d",
                ctx->current_item.major_type);
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return;
        }
        if (ctx->current_item.major_type == CBOR_MAJOR_TYPE_UINT) {
            ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_UINT;
        } else if (ctx->current_item.major_type
                   == CBOR_MAJOR_TYPE_NEGATIVE_INT) {
            ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT;
        } else if (ctx->current_item.major_type
                   == CBOR_MAJOR_TYPE_BYTE_STRING) {
            ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_BYTE_STRING;
        } else if (ctx->current_item.major_type
                   == CBOR_MAJOR_TYPE_TEXT_STRING) {
            ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_TEXT_STRING;
        } else if (ctx->current_item.major_type == CBOR_MAJOR_TYPE_ARRAY) {
            ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_ARRAY;
        } else if (ctx->current_item.major_type == CBOR_MAJOR_TYPE_MAP) {
            ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_MAP;
        } else if (ctx->current_item.major_type
                   == CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE) {
            parse_float_or_simple_value(ctx);
        } else if (ctx->current_item.major_type == CBOR_MAJOR_TYPE_TAG) {
            /**
             * From section "2.4.  Optional Tagging of Items":
             * > Decoders do not need to understand tags, and thus tags may be
             * > of little value in applications where the implementation
             * > creating a particular CBOR data item and the implementation
             * > decoding that stream know the semantic meaning of each item in
             * > the data flow.
             * >
             * > [...]
             * >
             * > Understanding the semantic tags is optional for a decoder; it
             * > can just jump over the initial bytes of the tag and interpret
             * > the tagged data item itself.
             *
             * Also:
             * > The initial bytes of the tag follow the rules for positive
             * > integers (major type 0).
             *
             * However, SenML specification, "6.  CBOR Representation
             * (application/senml+cbor)" says:
             *
             * > The CBOR [RFC7049] representation is equivalent to the JSON
             * > representation, with the following changes:
             * >
             * > o  For JSON Numbers, the CBOR representation can use integers,
             * >  floating-point numbers, or decimal fractions (CBOR Tag 4);
             *
             * so, we are basically forced to support tag 4.
             */
            if (ctx->current_item.additional_info
                    == CBOR_DECODER_TAG_DECIMAL_FRACTION) {
                /**
                 * The idea, of course, is to pack decoded decimal fraction into
                 * double and just hope for the best -- there is no dedicated
                 * type in LwM2M for decimal fractions.
                 */
                ctx->current_item.value_type = ANJAY_JSON_LIKE_VALUE_DOUBLE;
            } else {
                ignore_tag(ctx);
                /* All tags must be followed with data, otherwise the CBOR
                 * payload is malformed */
                data_must_follow = true;
                continue;
            }
        } else {
            LOG(DEBUG,
                _("unsupported major type ") "%d",
                (int) ctx->current_item.major_type);
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        }
        break;
    }

    if (ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_ERROR) {
        return;
    }

    while (ctx->nest_stack_size) {
        cbor_nested_state_t *top = nested_state_top(ctx);
        if (is_indefinite(top)) {
            if (top->items_parsed == SIZE_MAX) {
                ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
                LOG(DEBUG,
                    _("number of items in indefinite map exceeded SIZE_MAX"));
            } else {
                top->items_parsed++;
            }
            return;
        } else if ((size_t) top->all_items - top->items_parsed) {
            top->items_parsed++;
            return;
        } else {
            nested_state_pop(ctx);
        }
    }
}

static int
cbor_decoder_current_value_type(anjay_json_like_decoder_t *ctx_,
                                anjay_json_like_value_type_t *out_type) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    if (ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_OK) {
        *out_type = ctx->current_item.value_type;
        return 0;
    }
    return -1;
}

static int parse_uint(anjay_cbor_decoder_t *ctx, uint64_t *out_value) {
    int retval = -1;
    if (!is_length_extended(ctx)) {
        *out_value = (uint64_t) ctx->current_item.additional_info;
        return 0;
    }

    size_t ext_len_size;
    if (parse_ext_length_size(ctx, &ext_len_size)) {
        goto finish;
    }
    if (ext_len_size == 1) {
        uint8_t u8;
        if (avs_is_ok(avs_stream_read_reliably(ctx->stream, &u8, sizeof(u8)))) {
            *out_value = u8;
            retval = 0;
        }
    } else if (ext_len_size == 2) {
        uint16_t u16;
        if (avs_is_ok(
                    avs_stream_read_reliably(ctx->stream, &u16, sizeof(u16)))) {
            *out_value = avs_convert_be16(u16);
            retval = 0;
        }
    } else if (ext_len_size == 4) {
        uint32_t u32;
        if (avs_is_ok(
                    avs_stream_read_reliably(ctx->stream, &u32, sizeof(u32)))) {
            *out_value = avs_convert_be32(u32);
            retval = 0;
        }
    } else if (ext_len_size == 8) {
        uint64_t u64;
        if (avs_is_ok(
                    avs_stream_read_reliably(ctx->stream, &u64, sizeof(u64)))) {
            *out_value = avs_convert_be64(u64);
            retval = 0;
        }
    } else {
        AVS_UNREACHABLE("unsupported extended length size");
    }
finish:
    if (retval) {
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    }
    return retval;
}

static int parse_size(anjay_cbor_decoder_t *ctx, size_t *out_value) {
    uint64_t u64;
    if (parse_uint(ctx, &u64) || u64 > SIZE_MAX) {
        return -1;
    }
    *out_value = (size_t) u64;
    return 0;
}

static int parse_ptrdiff(anjay_cbor_decoder_t *ctx, ptrdiff_t *out_value) {
    size_t size;
    if (parse_size(ctx, &size) || size > SIZE_MAX / 2) {
        return -1;
    }
    *out_value = (ptrdiff_t) size;
    return 0;
}

static int nested_state_push(anjay_cbor_decoder_t *ctx) {
    assert(ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_OK);
    assert(ctx->current_item.value_type == ANJAY_JSON_LIKE_VALUE_ARRAY
           || ctx->current_item.value_type == ANJAY_JSON_LIKE_VALUE_MAP
           || ((ctx->current_item.value_type
                        == ANJAY_JSON_LIKE_VALUE_BYTE_STRING
                || ctx->current_item.value_type
                           == ANJAY_JSON_LIKE_VALUE_TEXT_STRING)
               && ctx->current_item.additional_info
                          == CBOR_EXT_LENGTH_INDEFINITE));

    cbor_nested_state_t state = {
        .type = ctx->current_item.value_type
    };

    if (ctx->nest_stack_size == ctx->max_nest_stack_size) {
        LOG(DEBUG,
            _("too many nested structures, the limit is: ") "%u",
            (unsigned) ctx->max_nest_stack_size);
        goto error;
    }

    switch (state.type) {
    case ANJAY_JSON_LIKE_VALUE_ARRAY:
        if (ctx->current_item.additional_info == CBOR_EXT_LENGTH_INDEFINITE) {
            /* indefinite array */
            state.all_items = NUM_ITEMS_INDEFINITE;
        } else if (parse_ptrdiff(ctx, &state.all_items)) {
            LOG(DEBUG, _("could not parse array length"));
            goto error;
        }
        break;
    case ANJAY_JSON_LIKE_VALUE_MAP:
        if (ctx->current_item.additional_info == CBOR_EXT_LENGTH_INDEFINITE) {
            /* indefinite map */
            state.all_items = NUM_ITEMS_INDEFINITE;
        } else if (parse_ptrdiff(ctx, &state.all_items)
                   || state.all_items > PTRDIFF_MAX / 2) {
            LOG(DEBUG,
                _("map length could not be parsed, or there is too many items "
                  "in the map"));
            goto error;
        } else {
            /**
             * A map contains (key, value) pairs, which, in effect doubles the
             * number of expected entries.
             */
            state.all_items *= 2;
        }
        break;
    case ANJAY_JSON_LIKE_VALUE_BYTE_STRING:
    case ANJAY_JSON_LIKE_VALUE_TEXT_STRING:
        state.all_items = NUM_ITEMS_INDEFINITE;
        break;
    default:
        AVS_UNREACHABLE("this switch statement must be exhaustive");
        goto error;
    }

    ctx->nest_stack_size++;
    *nested_state_top(ctx) = state;
    return 0;
error:
    ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    return -1;
}

static void nested_state_pop(anjay_cbor_decoder_t *ctx) {
    assert(is_indefinite(nested_state_top(ctx))
           || ((size_t) nested_state_top(ctx)->all_items
               - nested_state_top(ctx)->items_parsed)
                      == 0);

    ctx->nest_stack_size--;
}

static int decode_uint(anjay_cbor_decoder_t *ctx, uint64_t *out_value) {
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type != ANJAY_JSON_LIKE_VALUE_UINT) {
        return -1;
    }
    int retval = parse_uint(ctx, out_value);
    preprocess_next_value(ctx);
    return retval;
}

static int decode_negative_int(anjay_cbor_decoder_t *ctx, int64_t *out_value) {
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type
                           != ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT) {
        return -1;
    }
    uint64_t u64;
    if (parse_uint(ctx, &u64)) {
        return -1;
    }
    /* equivalent to if (u64 >= -INT64_MIN) */
    if (u64 >= (uint64_t) INT64_MAX + 1) {
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        return -1;
    }
    *out_value = -(int64_t) u64 - INT64_C(1);
    preprocess_next_value(ctx);
    return 0;
}

static float decode_half_float(uint16_t half) {
    /* Code adapted from https://tools.ietf.org/html/rfc7049#appendix-D */
    const int exponent = (half >> 10) & 0x1f;
    const int mantissa = half & 0x3ff;
    float value;
    if (exponent == 0) {
        value = ldexpf((float) mantissa, -24);
    } else if (exponent != 31) {
        value = ldexpf((float) (mantissa + 1024), exponent - 25);
    } else if (mantissa == 0) {
        value = INFINITY;
    } else {
        value = NAN;
    }
    return (half & 0x8000) ? -value : value;
}

static int decode_float(anjay_cbor_decoder_t *ctx, float *out_value) {
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type != ANJAY_JSON_LIKE_VALUE_FLOAT) {
        return -1;
    }
    int result = -1;
    if (ctx->current_item.additional_info == CBOR_VALUE_FLOAT_16) {
        uint16_t value;
        if (avs_is_ok(avs_stream_read_reliably(
                    ctx->stream, &value, sizeof(value)))) {
            *out_value = decode_half_float(avs_convert_be16(value));
            result = 0;
        }
    } else {
        assert(ctx->current_item.additional_info == CBOR_VALUE_FLOAT_32);
        uint32_t value;
        if (avs_is_ok(avs_stream_read_reliably(
                    ctx->stream, &value, sizeof(value)))) {
            *out_value = avs_ntohf(value);
            result = 0;
        }
    }
    if (result) {
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    } else {
        preprocess_next_value(ctx);
    }
    return result;
}

static int reinterpret_integer_as_double(anjay_json_like_decoder_t *ctx,
                                         double *out_value) {
    anjay_json_like_number_t n;
    if (_anjay_json_like_decoder_number(ctx, &n)) {
        return -1;
    }
    if (n.type == ANJAY_JSON_LIKE_VALUE_UINT) {
        *out_value = (double) n.value.u64;
    } else if (n.type == ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT) {
        *out_value = (double) n.value.i64;
    } else {
        return -1;
    }
    return 0;
}

static int decode_decimal_fraction(anjay_cbor_decoder_t *ctx,
                                   double *out_value) {
    preprocess_next_value(ctx);
    anjay_json_like_decoder_t *json_like_ctx =
            (anjay_json_like_decoder_t *) ctx;
    /**
     * RFC7049 "2.4.3.  Decimal Fractions and Bigfloats":
     *
     * > A decimal fraction or a bigfloat is represented as a tagged array
     * > that contains exactly two integer numbers: an exponent e and a
     * > mantissa m.  Decimal fractions (tag 4) use base-10 exponents; the
     * > value of a decimal fraction data item is m*(10**e).
     */
    size_t array_level =
            _anjay_json_like_decoder_nesting_level(json_like_ctx) + 1;
    if (_anjay_json_like_decoder_enter_array(json_like_ctx)) {
        return -1;
    }
    double exponent;
    double mantissa;
    if (_anjay_json_like_decoder_nesting_level(json_like_ctx) != array_level
            || reinterpret_integer_as_double(json_like_ctx, &exponent)
            || _anjay_json_like_decoder_nesting_level(json_like_ctx)
                           != array_level
            || reinterpret_integer_as_double(json_like_ctx, &mantissa)
            || _anjay_json_like_decoder_nesting_level(json_like_ctx)
                           == array_level) {
        return -1;
    }
    *out_value = mantissa * pow(10.0, exponent);
    return 0;
}

static int decode_double(anjay_cbor_decoder_t *ctx, double *out_value) {
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type != ANJAY_JSON_LIKE_VALUE_DOUBLE) {
        return -1;
    }
    int result = -1;

    /**
     * NOTE: This if is safe, because decimal fraction tag (4) does not
     * conflict with any kind of floating-point-type value. Also we wouldn't
     * land in this function for non-floating-point types (as ensured by the
     * if above).
     */
    if (ctx->current_item.additional_info
            == CBOR_DECODER_TAG_DECIMAL_FRACTION) {
        assert(ctx->current_item.major_type == CBOR_MAJOR_TYPE_TAG);
        result = decode_decimal_fraction(ctx, out_value);
    } else {
        uint64_t value;
        if (avs_is_ok(avs_stream_read_reliably(
                    ctx->stream, &value, sizeof(value)))) {
            *out_value = avs_ntohd(value);
            result = 0;
        }
    }
    if (result) {
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
    } else {
        preprocess_next_value(ctx);
    }
    return result;
}

static int cbor_decoder_bool(anjay_json_like_decoder_t *ctx_, bool *out_value) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type != ANJAY_JSON_LIKE_VALUE_BOOL) {
        return -1;
    }
    switch (ctx->current_item.additional_info) {
    case CBOR_VALUE_BOOL_FALSE:
        *out_value = false;
        break;
    case CBOR_VALUE_BOOL_TRUE:
        *out_value = true;
        break;
    default:
        AVS_UNREACHABLE("expected boolean, but got something else instead");
        return -1;
    }
    preprocess_next_value(ctx);
    return 0;
}

static int cbor_get_bytes_size(anjay_cbor_decoder_t *ctx,
                               size_t *out_bytes_size) {
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || (ctx->current_item.value_type
                        != ANJAY_JSON_LIKE_VALUE_BYTE_STRING
                && ctx->current_item.value_type
                           != ANJAY_JSON_LIKE_VALUE_TEXT_STRING)
            || parse_size(ctx, out_bytes_size)) {
        return -1;
    }
    return 0;
}

static int cbor_decoder_bytes(anjay_json_like_decoder_t *ctx_,
                              avs_stream_t *target_stream) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;

    anjay_io_cbor_bytes_ctx_t bytes_ctx;
    if (_anjay_io_cbor_get_bytes_ctx(ctx_, &bytes_ctx)) {
        return -1;
    }

    bool message_finished = false;
    while (!message_finished) {
        // ignore errors here - target_stream might not even be a membuf
        avs_stream_membuf_ensure_free_bytes(target_stream,
                                            bytes_ctx.bytes_available);

        char chunk[32];
        size_t bytes_read;
        if (_anjay_io_cbor_get_some_bytes(ctx_,
                                          &bytes_ctx,
                                          &chunk,
                                          sizeof(chunk),
                                          &bytes_read,
                                          &message_finished)
                || avs_is_err(avs_stream_write(
                           target_stream, chunk, bytes_read))) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return -1;
        }
    }

    return 0;
}

static int cbor_decoder_enter_array(anjay_json_like_decoder_t *ctx_) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type != ANJAY_JSON_LIKE_VALUE_ARRAY
            || nested_state_push(ctx)) {
        return -1;
    }
    preprocess_next_value(ctx);
    return 0;
}

static int cbor_decoder_enter_map(anjay_json_like_decoder_t *ctx_) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK
            || ctx->current_item.value_type != ANJAY_JSON_LIKE_VALUE_MAP
            || nested_state_push(ctx)) {
        return -1;
    }
    preprocess_next_value(ctx);
    return 0;
}

static size_t cbor_decoder_nesting_level(anjay_json_like_decoder_t *ctx_) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    return ctx->state == ANJAY_JSON_LIKE_DECODER_STATE_OK ? ctx->nest_stack_size
                                                          : 0;
}

static int cbor_decoder_number(anjay_json_like_decoder_t *ctx_,
                               anjay_json_like_number_t *out_value) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    if (ctx->state != ANJAY_JSON_LIKE_DECODER_STATE_OK) {
        return -1;
    }
    out_value->type = ctx->current_item.value_type;
    switch (ctx->current_item.value_type) {
    case ANJAY_JSON_LIKE_VALUE_UINT:
        return decode_uint(ctx, &out_value->value.u64);
    case ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT:
        return decode_negative_int(ctx, &out_value->value.i64);
    case ANJAY_JSON_LIKE_VALUE_FLOAT:
        return decode_float(ctx, &out_value->value.f32);
    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
        return decode_double(ctx, &out_value->value.f64);
    default:
        return -1;
    }
}

static void cbor_decoder_cleanup(anjay_json_like_decoder_t **ctx) {
    if (ctx && *ctx) {
        avs_free(*ctx);
        *ctx = NULL;
    }
}

static anjay_json_like_decoder_state_t
cbor_decoder_state(const anjay_json_like_decoder_t *ctx) {
    return ((const anjay_cbor_decoder_t *) ctx)->state;
}

static const anjay_json_like_decoder_vtable_t VTABLE = {
    .state = cbor_decoder_state,
    .current_value_type = cbor_decoder_current_value_type,
    .read_bool = cbor_decoder_bool,
    .number = cbor_decoder_number,
    .bytes = cbor_decoder_bytes,
    .enter_array = cbor_decoder_enter_array,
    .enter_map = cbor_decoder_enter_map,
    .nesting_level = cbor_decoder_nesting_level,
    .cleanup = cbor_decoder_cleanup
};

anjay_json_like_decoder_t *_anjay_cbor_decoder_new(avs_stream_t *stream,
                                                   size_t max_nesting_depth) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) avs_calloc(
            1,
            sizeof(anjay_cbor_decoder_t)
                    + max_nesting_depth * sizeof(cbor_nested_state_t));
    if (ctx) {
        ctx->vtable = &VTABLE;
        ctx->stream = stream;
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_OK;
        ctx->max_nest_stack_size = max_nesting_depth;
        preprocess_next_value(ctx);
    }
    return (anjay_json_like_decoder_t *) ctx;
}

static int try_preprocess_next_bytes_chunk(anjay_cbor_decoder_t *ctx,
                                           anjay_io_cbor_bytes_ctx_t *bytes_ctx,
                                           bool *out_message_finished) {
    preprocess_next_value(ctx);

    if (bytes_ctx->initial_nesting_level == ctx->nest_stack_size) {
        if (cbor_get_bytes_size(ctx, &bytes_ctx->bytes_available)) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return -1;
        }
    } else {
        *out_message_finished = true;
    }

    return 0;
}

int _anjay_io_cbor_get_bytes_ctx(anjay_json_like_decoder_t *ctx_,
                                 anjay_io_cbor_bytes_ctx_t *out_bytes_ctx) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    assert(ctx->vtable == &VTABLE);
    memset(out_bytes_ctx, 0, sizeof(*out_bytes_ctx));

    if (ctx->current_item.additional_info == CBOR_EXT_LENGTH_INDEFINITE) {
        out_bytes_ctx->indefinite = true;
        if (nested_state_push(ctx)) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return -1;
        }
        out_bytes_ctx->initial_nesting_level = ctx->nest_stack_size;
        return try_preprocess_next_bytes_chunk(
                ctx, out_bytes_ctx, &out_bytes_ctx->empty);
    } else if (cbor_get_bytes_size(ctx, &out_bytes_ctx->bytes_available)) {
        ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
        return -1;
    }

    return 0;
}

static int handle_end_of_bytes(anjay_cbor_decoder_t *ctx,
                               anjay_io_cbor_bytes_ctx_t *bytes_ctx,
                               bool *out_message_finished) {
    if (bytes_ctx->indefinite) {
        if (try_preprocess_next_bytes_chunk(
                    ctx, bytes_ctx, out_message_finished)) {
            return -1;
        }
    } else {
        *out_message_finished = true;
        preprocess_next_value(ctx);
    }
    return 0;
}

int _anjay_io_cbor_get_some_bytes(anjay_json_like_decoder_t *ctx_,
                                  anjay_io_cbor_bytes_ctx_t *bytes_ctx,
                                  void *out_buf,
                                  size_t buf_size,
                                  size_t *out_bytes_read,
                                  bool *out_message_finished) {
    anjay_cbor_decoder_t *ctx = (anjay_cbor_decoder_t *) ctx_;
    assert(ctx->vtable == &VTABLE);

    if (bytes_ctx->empty) {
        *out_bytes_read = 0;
        *out_message_finished = true;
        return 0;
    }

    *out_message_finished = false;
    size_t total_bytes_read = 0;

    // This may look complicated, but we want to read more data only if:
    // - message is not finished, which is pretty obvious AND
    // - buf_size is different than 0 or bytes_ctx->bytes_available is equal to
    //   zero, cause it's possible that the next chunk (or the complete data)
    //   has the length of 0, so the read may be successful even if there's no
    //   space left in buffer.
    while (*out_message_finished == false
           && (buf_size != 0 || bytes_ctx->bytes_available == 0)) {
        // This may be equal to 0 and this is intentional.
        size_t bytes_to_read = AVS_MIN(buf_size, bytes_ctx->bytes_available);
        if (avs_is_err(avs_stream_read_reliably(
                    ctx->stream, out_buf, bytes_to_read))) {
            ctx->state = ANJAY_JSON_LIKE_DECODER_STATE_ERROR;
            return -1;
        }

        out_buf = (char *) out_buf + bytes_to_read;
        buf_size -= bytes_to_read;
        total_bytes_read += bytes_to_read;
        bytes_ctx->bytes_available -= bytes_to_read;

        if (!bytes_ctx->bytes_available
                && handle_end_of_bytes(ctx, bytes_ctx, out_message_finished)) {
            return -1;
        }
    }

    *out_bytes_read = total_bytes_read;
    return 0;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/cbor/cbor_decoder.c"
#    endif

#endif // ANJAY_WITH_CBOR
