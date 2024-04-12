/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <fluf/fluf_config.h>

#if defined(FLUF_WITH_SENML_CBOR) || defined(FLUF_WITH_LWM2M_CBOR) \
        || defined(FLUF_WITH_CBOR)

#    ifdef FLUF_WITH_CBOR_STRING_TIME
#        include <ctype.h>
#    endif // FLUF_WITH_CBOR_STRING_TIME

#    if defined(FLUF_WITH_CBOR_HALF_FLOAT) \
            || defined(FLUF_WITH_CBOR_DECIMAL_FRACTIONS)
#        include <math.h>
#    endif /* defined(FLUF_WITH_CBOR_HALF_FLOAT) || \
              defined(FLUF_WITH_CBOR_DECIMAL_FRACTIONS) */

#    include <avsystem/commons/avs_utils.h>

#    include <fluf/fluf_cbor_decoder_ll.h>

#    include "fluf_internal.h"

static int fill_prebuffer(fluf_cbor_ll_decoder_t *ctx, uint8_t min_size) {
    assert(min_size <= sizeof(ctx->prebuffer));
    if (ctx->prebuffer_size - ctx->prebuffer_offset >= min_size) {
        return 0;
    }
    if (ctx->prebuffer_offset) {
        ctx->prebuffer_size -= ctx->prebuffer_offset;
        if (ctx->prebuffer_size) {
            memmove(ctx->prebuffer,
                    ctx->prebuffer + ctx->prebuffer_offset,
                    ctx->prebuffer_size);
        }
        ctx->prebuffer_offset = 0;
    }
    if (ctx->prebuffer_size < sizeof(ctx->prebuffer)) {
        uint8_t bytes_to_copy =
                (uint8_t) AVS_MIN(sizeof(ctx->prebuffer) - ctx->prebuffer_size,
                                  (size_t) (ctx->input_end - ctx->input));
        if (bytes_to_copy) {
            memcpy(ctx->prebuffer + ctx->prebuffer_size,
                   ctx->input,
                   bytes_to_copy);
            ctx->input += bytes_to_copy;
            ctx->prebuffer_size += bytes_to_copy;
        }
    }
    if (ctx->prebuffer_size < min_size && !ctx->input_last) {
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    return 0;
}

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
static bool is_indefinite(fluf_cbor_ll_nested_state_t *state) {
    return state->all_items == FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE;
}
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0

typedef enum {
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    CBOR_DECODER_TAG_STRING_TIME = 0,
#    endif // FLUF_WITH_CBOR_STRING_TIME
    CBOR_DECODER_TAG_EPOCH_BASED_TIME = 1,
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    CBOR_DECODER_TAG_DECIMAL_FRACTION = 4,
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
} cbor_decoder_tag_t;

static inline int get_major_type(const uint8_t initial_byte) {
    return initial_byte >> 5;
}

static inline int get_additional_info(const uint8_t initial_byte) {
    return initial_byte & 0x1f;
}

static uint8_t parse_ext_length_size(const fluf_cbor_ll_decoder_t *ctx) {
    switch (get_additional_info(ctx->current_item.initial_byte)) {
    case CBOR_EXT_LENGTH_1BYTE:
        return 1;
    case CBOR_EXT_LENGTH_2BYTE:
        return 2;
    case CBOR_EXT_LENGTH_4BYTE:
        return 4;
    case CBOR_EXT_LENGTH_8BYTE:
        return 8;
    default:
        return 0;
    }
}

static void
handle_header_for_float_or_simple_value(fluf_cbor_ll_decoder_t *ctx) {
    assert(get_major_type(ctx->current_item.initial_byte)
           == CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE);

    /* See "2.3.  Floating-Point Numbers and Values with No Content" */
    switch (get_additional_info(ctx->current_item.initial_byte)) {
    case CBOR_VALUE_BOOL_FALSE:
    case CBOR_VALUE_BOOL_TRUE:
        ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_BOOL;
        break;
    case CBOR_VALUE_NULL:
        ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_NULL;
        break;
#    ifdef FLUF_WITH_CBOR_HALF_FLOAT
    case CBOR_VALUE_FLOAT_16:
#    endif // FLUF_WITH_CBOR_HALF_FLOAT
    case CBOR_VALUE_FLOAT_32:
        ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_FLOAT;
        break;
    case CBOR_VALUE_FLOAT_64:
        ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_DOUBLE;
        break;
    case CBOR_VALUE_UNDEFINED:
    case CBOR_VALUE_IN_NEXT_BYTE:
    default:
        /* As per "Table 2: Simple Values", range 32..255 is unassigned, so
         * we may call it an error. */
        ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
    }
}

static void ignore_tag(fluf_cbor_ll_decoder_t *ctx) {
    assert(get_major_type(ctx->current_item.initial_byte)
           == CBOR_MAJOR_TYPE_TAG);
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    assert(get_additional_info(ctx->current_item.initial_byte)
           != CBOR_DECODER_TAG_STRING_TIME);
#    endif // FLUF_WITH_CBOR_STRING_TIME
    assert(get_additional_info(ctx->current_item.initial_byte)
           != CBOR_DECODER_TAG_EPOCH_BASED_TIME);
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    assert(get_additional_info(ctx->current_item.initial_byte)
           != CBOR_DECODER_TAG_DECIMAL_FRACTION);
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    uint8_t ext_len_size = parse_ext_length_size(ctx);
    if (ext_len_size) {
        if (ctx->prebuffer_offset + ext_len_size > ctx->prebuffer_size) {
            assert(ctx->input_last);
            ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
        } else {
            ctx->prebuffer_offset += ext_len_size;
        }
    }
}

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
static int nested_state_push(fluf_cbor_ll_decoder_t *ctx);
static void nested_state_pop(fluf_cbor_ll_decoder_t *ctx);
static fluf_cbor_ll_nested_state_t *
nested_state_top(fluf_cbor_ll_decoder_t *ctx) {
    assert(ctx->nest_stack_size);
    return &ctx->nest_stack[ctx->nest_stack_size - 1];
}
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0

static int preprocess_next_value(fluf_cbor_ll_decoder_t *ctx) {
    while (ctx->state == FLUF_CBOR_LL_DECODER_STATE_OK) {
#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
        while (ctx->nest_stack_size) {
            fluf_cbor_ll_nested_state_t *top = nested_state_top(ctx);
            if (is_indefinite(top)
                    || (size_t) top->all_items != top->items_parsed.total) {
                break;
            }
            nested_state_pop(ctx);
        }
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0

        // We might need to skip the tag, which might be up to 8 bytes
        int result = fill_prebuffer(ctx, 9);
        if (result) {
            return result;
        }
        assert(ctx->prebuffer_offset <= ctx->prebuffer_size);
        if (ctx->prebuffer_offset == ctx->prebuffer_size) {
            // EOF
            if (ctx->after_tag
#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
                    || ctx->nest_stack_size
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
            ) {
                /* All tags must be followed with data, otherwise the CBOR
                 * payload is malformed */
                ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
            } else {
                ctx->state = FLUF_CBOR_LL_DECODER_STATE_FINISHED;
            }
            return 0;
        }

        uint8_t byte = ctx->prebuffer[ctx->prebuffer_offset++];
        if (byte == CBOR_INDEFINITE_STRUCTURE_BREAK) {
            /* end of the indefinite map, array or byte/text string */
#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
            if (ctx->nest_stack_size && is_indefinite(nested_state_top(ctx))
                    && (nested_state_top(ctx)->type != FLUF_CBOR_LL_VALUE_MAP
                        || !nested_state_top(ctx)->items_parsed.odd)) {
                nested_state_pop(ctx);
            } else
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
            {
                ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
            }
            continue;
        }
        ctx->current_item.initial_byte = byte;

        if (get_major_type(ctx->current_item.initial_byte)
                == CBOR_MAJOR_TYPE_UINT) {
            ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_UINT;
        } else if (get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_NEGATIVE_INT) {
            ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_NEGATIVE_INT;
        } else if (get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_BYTE_STRING) {
            ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_BYTE_STRING;
        } else if (get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_TEXT_STRING) {
            ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_TEXT_STRING;
        } else if (get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_ARRAY) {
            ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_ARRAY;
        } else if (get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_MAP) {
            ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_MAP;
        } else if (get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE) {
            handle_header_for_float_or_simple_value(ctx);
        } else {
            // This if ladder is supposed to be exhaustive
            assert(get_major_type(ctx->current_item.initial_byte)
                   == CBOR_MAJOR_TYPE_TAG);
            switch (get_additional_info(ctx->current_item.initial_byte)) {
#    ifdef FLUF_WITH_CBOR_STRING_TIME
            case CBOR_DECODER_TAG_STRING_TIME:
#    endif // FLUF_WITH_CBOR_STRING_TIME
            case CBOR_DECODER_TAG_EPOCH_BASED_TIME:
                if (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE) {
                    ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
                    return 0;
                }
                ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_TIMESTAMP;
                break;
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
            case CBOR_DECODER_TAG_DECIMAL_FRACTION:
                /**
                 * From section "2.4.  Optional Tagging of Items":
                 * > Decoders do not need to understand tags, and thus tags may
                 * > be of little value in applications where the implementation
                 * > creating a particular CBOR data item and the implementation
                 * > decoding that stream know the semantic meaning of each item
                 * > in the data flow.
                 * >
                 * > [...]
                 * >
                 * > Understanding the semantic tags is optional for a decoder;
                 * > it can just jump over the initial bytes of the tag and
                 * > interpret the tagged data item itself.
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
                 * > o  For JSON Numbers, the CBOR representation can use
                 * >    integers, floating-point numbers, or decimal fractions
                 * >    (CBOR Tag 4);
                 *
                 * so, we are basically forced to support tag 4.
                 *
                 * The idea, of course, is to pack decoded decimal fraction into
                 * double and just hope for the best -- there is no dedicated
                 * type in LwM2M for decimal fractions.
                 */
                if (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE) {
                    ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
                    return 0;
                }
                ctx->current_item.value_type = FLUF_CBOR_LL_VALUE_DOUBLE;
                break;
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
            default:
                ignore_tag(ctx);
                ctx->after_tag = true;
                continue;
            }
        }
        ctx->needs_preprocessing = false;
        break;
    }

    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_ERROR) {
        return 0;
    }

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
    if (ctx->nest_stack_size
            && get_major_type(ctx->current_item.initial_byte)
                           != CBOR_MAJOR_TYPE_TAG) {
        fluf_cbor_ll_nested_state_t *top = nested_state_top(ctx);
        if (is_indefinite(top)) {
            top->items_parsed.odd = !top->items_parsed.odd;
        } else {
            top->items_parsed.total++;
        }
    }
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
    return 0;
}

static int ensure_value_or_error_available(fluf_cbor_ll_decoder_t *ctx) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || !ctx->needs_preprocessing) {
        return 0;
    }
    return preprocess_next_value(ctx);
}

static int parse_uint(fluf_cbor_ll_decoder_t *ctx, uint64_t *out_value) {
    uint8_t ext_len_size = parse_ext_length_size(ctx);
    if (!ext_len_size) {
        *out_value =
                (uint64_t) get_additional_info(ctx->current_item.initial_byte);
        if (*out_value >= CBOR_EXT_LENGTH_1BYTE) {
            // Invalid short primitive value
            ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
            return FLUF_IO_ERR_FORMAT;
        }
        return 0;
    }

    int result = fill_prebuffer(ctx, ext_len_size);
    if (result) {
        return result;
    }
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
    } value;
    if (ctx->prebuffer_offset + ext_len_size > ctx->prebuffer_size) {
        assert(ctx->input_last);
        ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
        return FLUF_IO_ERR_FORMAT;
    }
    memcpy(&value, ctx->prebuffer + ctx->prebuffer_offset, ext_len_size);
    ctx->prebuffer_offset += ext_len_size;
    switch (ext_len_size) {
    case 1:
        *out_value = value.u8;
        return 0;
    case 2:
        *out_value = avs_convert_be16(value.u16);
        return 0;
    case 4:
        *out_value = avs_convert_be32(value.u32);
        return 0;
    case 8:
        *out_value = avs_convert_be64(value.u64);
        return 0;
    default:
        AVS_UNREACHABLE("unsupported extended length size");
        return FLUF_IO_ERR_LOGIC;
    }
}

static int parse_size(fluf_cbor_ll_decoder_t *ctx, size_t *out_value) {
    uint64_t u64;
    int result = parse_uint(ctx, &u64);
    if (result) {
        return result;
    }
    if (u64 > SIZE_MAX) {
        return FLUF_IO_ERR_FORMAT;
    }
    *out_value = (size_t) u64;
    return 0;
}

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
static int parse_ptrdiff(fluf_cbor_ll_decoder_t *ctx, ptrdiff_t *out_value) {
    size_t size;
    int result = parse_size(ctx, &size);
    if (result) {
        return result;
    }
    if (size > SIZE_MAX / 2) {
        return FLUF_IO_ERR_FORMAT;
    }
    *out_value = (ptrdiff_t) size;
    return 0;
}

static int nested_state_push(fluf_cbor_ll_decoder_t *ctx) {
    assert(ctx->state == FLUF_CBOR_LL_DECODER_STATE_OK);
    assert(ctx->current_item.value_type == FLUF_CBOR_LL_VALUE_ARRAY
           || ctx->current_item.value_type == FLUF_CBOR_LL_VALUE_MAP
           || ((ctx->current_item.value_type == FLUF_CBOR_LL_VALUE_BYTE_STRING
                || ctx->current_item.value_type
                           == FLUF_CBOR_LL_VALUE_TEXT_STRING)
               && get_additional_info(ctx->current_item.initial_byte)
                          == CBOR_EXT_LENGTH_INDEFINITE));

    fluf_cbor_ll_nested_state_t state = {
        .type = ctx->current_item.value_type
    };

    int result = FLUF_IO_ERR_LOGIC;
    if (ctx->nest_stack_size == AVS_ARRAY_SIZE(ctx->nest_stack)) {
        result = FLUF_IO_ERR_FORMAT;
        goto error;
    }

    switch (state.type) {
    case FLUF_CBOR_LL_VALUE_ARRAY:
        if (get_additional_info(ctx->current_item.initial_byte)
                == CBOR_EXT_LENGTH_INDEFINITE) {
            /* indefinite array */
            state.all_items = FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE;
        } else if ((result = parse_ptrdiff(ctx, &state.all_items))) {
            goto error;
        }
        break;
    case FLUF_CBOR_LL_VALUE_MAP:
        if (get_additional_info(ctx->current_item.initial_byte)
                == CBOR_EXT_LENGTH_INDEFINITE) {
            /* indefinite map */
            state.all_items = FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE;
        } else if ((result = parse_ptrdiff(ctx, &state.all_items))) {
            goto error;
        } else if (state.all_items > PTRDIFF_MAX / 2) {
            result = FLUF_IO_ERR_FORMAT;
            goto error;
        } else {
            /**
             * A map contains (key, value) pairs, which, in effect doubles the
             * number of expected entries.
             */
            state.all_items *= 2;
        }
        break;
#        ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
    case FLUF_CBOR_LL_VALUE_BYTE_STRING:
    case FLUF_CBOR_LL_VALUE_TEXT_STRING:
        state.all_items = FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE;
        break;
#        endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
    default:
        AVS_UNREACHABLE("this switch statement must be exhaustive");
        goto error;
    }

    ctx->nest_stack_size++;
    *nested_state_top(ctx) = state;
    return 0;
error:
    if (result < 0) {
        ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
    }
    return result;
}

static void nested_state_pop(fluf_cbor_ll_decoder_t *ctx) {
    assert(is_indefinite(nested_state_top(ctx))
           || ((size_t) nested_state_top(ctx)->all_items
               - nested_state_top(ctx)->items_parsed.total)
                      == 0);

    ctx->nest_stack_size--;
}
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0

static int decode_uint(fluf_cbor_ll_decoder_t *ctx, uint64_t *out_value) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
                && ctx->subparser_type
                           != FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME)) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_UINT) {
        return FLUF_IO_ERR_FORMAT;
    }
    assert(!ctx->needs_preprocessing);
    int retval = parse_uint(ctx, out_value);
    if (retval <= 0) {
        ctx->needs_preprocessing = true;
        ctx->after_tag = false;
    }
    return retval;
}

static int decode_negative_int(fluf_cbor_ll_decoder_t *ctx,
                               int64_t *out_value) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
                && ctx->subparser_type
                           != FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME)) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_NEGATIVE_INT) {
        return FLUF_IO_ERR_FORMAT;
    }
    assert(!ctx->needs_preprocessing);
    uint64_t u64;
    int result = parse_uint(ctx, &u64);
    if (result) {
        return result;
    }
    /* equivalent to if (u64 >= -INT64_MIN) */
    if (u64 >= (uint64_t) INT64_MAX + 1) {
        ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
        return FLUF_IO_ERR_FORMAT;
    }
    *out_value = -(int64_t) u64 - INT64_C(1);
    ctx->needs_preprocessing = true;
    ctx->after_tag = false;
    return 0;
}

#    ifdef FLUF_WITH_CBOR_HALF_FLOAT
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
#    endif // FLUF_WITH_CBOR_HALF_FLOAT

static int decode_float(fluf_cbor_ll_decoder_t *ctx, float *out_value) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
                && ctx->subparser_type
                           != FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME)) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_FLOAT) {
        return FLUF_IO_ERR_FORMAT;
    }
    assert(!ctx->needs_preprocessing);
    int result;
#    ifdef FLUF_WITH_CBOR_HALF_FLOAT
    if (get_additional_info(ctx->current_item.initial_byte)
            == CBOR_VALUE_FLOAT_16) {
        uint16_t value;
        if ((result = fill_prebuffer(ctx, sizeof(value)))) {
            return result;
        }
        if (ctx->prebuffer_offset + sizeof(value) > ctx->prebuffer_size) {
            result = FLUF_IO_ERR_FORMAT;
        } else {
            memcpy(&value,
                   ctx->prebuffer + ctx->prebuffer_offset,
                   sizeof(value));
            ctx->prebuffer_offset += sizeof(value);
            *out_value = decode_half_float(avs_convert_be16(value));
        }
    } else
#    endif // FLUF_WITH_CBOR_HALF_FLOAT
    {
        assert(get_additional_info(ctx->current_item.initial_byte)
               == CBOR_VALUE_FLOAT_32);
        uint32_t value;
        if ((result = fill_prebuffer(ctx, sizeof(value)))) {
            return result;
        }
        if (ctx->prebuffer_offset + sizeof(value) > ctx->prebuffer_size) {
            result = FLUF_IO_ERR_FORMAT;
        } else {
            memcpy(&value,
                   ctx->prebuffer + ctx->prebuffer_offset,
                   sizeof(value));
            ctx->prebuffer_offset += sizeof(value);
            *out_value = avs_ntohf(value);
        }
    }
    if (result) {
        assert(result < 0);
        ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
    } else {
        ctx->needs_preprocessing = true;
        ctx->after_tag = false;
    }
    return result;
}

#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
static int reinterpret_fraction_component_as_double(fluf_cbor_ll_decoder_t *ctx,
                                                    double *out_value) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK) {
        return FLUF_IO_ERR_FORMAT;
    }
    assert(!ctx->needs_preprocessing);
    if (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_UINT
            && ctx->current_item.value_type
                           != FLUF_CBOR_LL_VALUE_NEGATIVE_INT) {
        return FLUF_IO_ERR_FORMAT;
    }
    uint64_t value;
    int result = parse_uint(ctx, &value);
    if (result <= 0) {
        ctx->needs_preprocessing = true;
        ctx->after_tag = false;
    }
    if (result) {
        return result;
    }
    *out_value = (double) value;
    if (ctx->current_item.value_type == FLUF_CBOR_LL_VALUE_NEGATIVE_INT) {
        *out_value = -*out_value - 1.0;
    }
    return 0;
}

static int ensure_fraction_component_available(fluf_cbor_ll_decoder_t *ctx,
                                               double *out_value) {
    if (!isnan(*out_value)) {
        return 0;
    }
    int result = ensure_value_or_error_available(ctx);
    if (result
            || ctx->nest_stack_size
                           != ctx->subparser.decimal_fraction.array_level
            || (result = reinterpret_fraction_component_as_double(ctx,
                                                                  out_value))) {
        return result ? result : FLUF_IO_ERR_FORMAT;
    }
    assert(!isnan(*out_value));
    return 0;
}

static int decode_decimal_fraction(fluf_cbor_ll_decoder_t *ctx,
                                   double *out_value) {
    int result = 0;
    /**
     * RFC7049 "2.4.3.  Decimal Fractions and Bigfloats":
     *
     * > A decimal fraction or a bigfloat is represented as a tagged array
     * > that contains exactly two integer numbers: an exponent e and a
     * > mantissa m.  Decimal fractions (tag 4) use base-10 exponents; the
     * > value of a decimal fraction data item is m*(10**e).
     */
    if (ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_NONE) {
        size_t current_level;
        if ((result =
                     fluf_cbor_ll_decoder_nesting_level(ctx, &current_level))) {
            return result;
        }
        assert(get_major_type(ctx->current_item.initial_byte)
                       == CBOR_MAJOR_TYPE_TAG
               || ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK);
        ctx->subparser.decimal_fraction.array_level = current_level + 1;
        ctx->subparser.decimal_fraction.entered_array = false;
        ctx->subparser.decimal_fraction.exponent = NAN;
        ctx->subparser.decimal_fraction.mantissa = NAN;
        ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION;
        ctx->needs_preprocessing = true;
        ctx->after_tag = true;
    } else if (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION) {
        return FLUF_IO_ERR_FORMAT;
    }
    if (!ctx->subparser.decimal_fraction.entered_array) {
        if ((result = ensure_value_or_error_available(ctx))
                || ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
                || ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_ARRAY
                || (result = nested_state_push(ctx))) {
            return result ? result : FLUF_IO_ERR_FORMAT;
        }
        ctx->needs_preprocessing = true;
        ctx->after_tag = false;
        ctx->subparser.decimal_fraction.entered_array = true;
    }
    if ((result = ensure_fraction_component_available(
                 ctx, &ctx->subparser.decimal_fraction.exponent))
            || (result = ensure_fraction_component_available(
                        ctx, &ctx->subparser.decimal_fraction.mantissa))) {
        return result;
    }
    if ((result = ensure_value_or_error_available(ctx))
            || ctx->state == FLUF_CBOR_LL_DECODER_STATE_ERROR
            || (ctx->state == FLUF_CBOR_LL_DECODER_STATE_OK
                && ctx->nest_stack_size
                           == ctx->subparser.decimal_fraction.array_level)) {
        return result ? result : FLUF_IO_ERR_FORMAT;
    }
    *out_value = ctx->subparser.decimal_fraction.mantissa
                 * pow(10.0, ctx->subparser.decimal_fraction.exponent);
    ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_NONE;
    return 0;
}
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS

static int decode_double(fluf_cbor_ll_decoder_t *ctx, double *out_value) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK) {
        return FLUF_IO_ERR_LOGIC;
    }
    assert(!ctx->needs_preprocessing);
    int result;

#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    /**
     * NOTE: This if is safe, because decimal fraction tag (4) does not
     * conflict with any kind of floating-point-type value. Also we wouldn't
     * land in this function for non-floating-point types (as ensured by the
     * if above).
     */
    if (ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION
            || (ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_NONE
                && ctx->current_item.value_type == FLUF_CBOR_LL_VALUE_DOUBLE
                && get_additional_info(ctx->current_item.initial_byte)
                           == CBOR_DECODER_TAG_DECIMAL_FRACTION)) {
        assert(ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION
               || get_major_type(ctx->current_item.initial_byte)
                          == CBOR_MAJOR_TYPE_TAG);
        result = decode_decimal_fraction(ctx, out_value);
    } else
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
            if (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_DOUBLE) {
        return FLUF_IO_ERR_FORMAT;
    } else {
        uint64_t value;
        if (!(result = fill_prebuffer(ctx, sizeof(value)))) {
            if (ctx->prebuffer_offset + sizeof(value) > ctx->prebuffer_size) {
                ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
                result = FLUF_IO_ERR_FORMAT;
            } else {
                memcpy(&value,
                       ctx->prebuffer + ctx->prebuffer_offset,
                       sizeof(value));
                ctx->prebuffer_offset += sizeof(value);
                *out_value = avs_ntohd(value);
            }
        }
        if (result <= 0) {
            ctx->needs_preprocessing = true;
            ctx->after_tag = false;
        }
    }
    return result;
}

static int decode_simple_number(fluf_cbor_ll_decoder_t *ctx,
                                fluf_cbor_ll_number_t *out_value) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK) {
        return FLUF_IO_ERR_FORMAT;
    }
    out_value->type = ctx->current_item.value_type;
    switch (out_value->type) {
    case FLUF_CBOR_LL_VALUE_UINT:
        return decode_uint(ctx, &out_value->value.u64);
    case FLUF_CBOR_LL_VALUE_NEGATIVE_INT:
        return decode_negative_int(ctx, &out_value->value.i64);
    case FLUF_CBOR_LL_VALUE_FLOAT:
        return decode_float(ctx, &out_value->value.f32);
    case FLUF_CBOR_LL_VALUE_DOUBLE:
        return decode_double(ctx, &out_value->value.f64);
    default:
        return FLUF_IO_ERR_FORMAT;
    }
}

static int cbor_get_bytes_size(fluf_cbor_ll_decoder_t *ctx,
                               size_t *out_bytes_size) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
                && ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_STRING
                && ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_BYTES
#    ifdef FLUF_WITH_CBOR_STRING_TIME
                && ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_STRING_TIME
#    endif // FLUF_WITH_CBOR_STRING_TIME
                )
            || (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_BYTE_STRING
                && ctx->current_item.value_type
                           != FLUF_CBOR_LL_VALUE_TEXT_STRING)) {
        return FLUF_IO_ERR_FORMAT;
    }
    return parse_size(ctx, out_bytes_size);
}

static int initialize_bytes_subparser(fluf_cbor_ll_decoder_t *ctx) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }

    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_BYTE_STRING
                && ctx->current_item.value_type
                           != FLUF_CBOR_LL_VALUE_TEXT_STRING)) {
        return FLUF_IO_ERR_FORMAT;
    }

    size_t bytes_available = 0;
    if (get_additional_info(ctx->current_item.initial_byte)
            == CBOR_EXT_LENGTH_INDEFINITE) {
#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
        if ((result = nested_state_push(ctx))) {
            return result;
        } else {
            ctx->needs_preprocessing = true;
            ctx->after_tag = false;
        }
#    else  // FLUF_WITH_CBOR_INDEFINITE_BYTES
        return FLUF_IO_ERR_FORMAT;
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
    } else if ((result = cbor_get_bytes_size(ctx, &bytes_available))) {
        if (result < 0) {
            ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
        }
        return result;
    }

    ctx->subparser.string_or_bytes_or_string_time.bytes_available =
            bytes_available;
#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
    ctx->subparser.string_or_bytes_or_string_time.initial_nesting_level =
            ctx->nest_stack_size;
    ctx->subparser.string_or_bytes_or_string_time.indefinite =
            (get_additional_info(ctx->current_item.initial_byte)
             == CBOR_EXT_LENGTH_INDEFINITE);
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
    return 0;
}

#    ifdef FLUF_WITH_CBOR_STRING_TIME
static int64_t year_to_days(uint16_t year, bool *out_is_leap) {
    // NOTE: Gregorian calendar rules are used proleptically here, which means
    // that dates before 1583 will not align with historical documents. Negative
    // dates handling might also be confusing (i.e. year == -1 means 2 BC).
    //
    // These rules are, however, consistent with the ISO 8601 convention that
    // ASN.1 GeneralizedTime type references, not to mention that X.509
    // certificates are generally not expected to contain dates before 1583 ;)

    static const int64_t LEAP_YEARS_IN_CYCLE = 97;
    static const int64_t LEAP_YEARS_UNTIL_1970 = 478;

    *out_is_leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);

    uint8_t cycles = (uint8_t) (year / 400);
    uint16_t years_since_cycle_start = year % 400;

    int leap_years_since_cycle_start = (*out_is_leap ? 0 : 1)
                                       + years_since_cycle_start / 4
                                       - years_since_cycle_start / 100;
    int64_t leap_years_since_1970 = cycles * LEAP_YEARS_IN_CYCLE
                                    + leap_years_since_cycle_start
                                    - LEAP_YEARS_UNTIL_1970;
    return (year - 1970) * 365 + leap_years_since_1970;
}

static int month_to_days(uint8_t month, bool is_leap) {
    static const uint16_t MONTH_LENGTHS[] = {
        31, 28 /* or 29 */, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int days = (is_leap && month > 2) ? 1 : 0;
    for (int i = 0; i < month - 1; ++i) {
        days += MONTH_LENGTHS[i];
    }
    return days;
}

static int64_t
convert_date_midnight_utc(uint16_t year, uint8_t month, uint8_t day) {
    bool is_leap;
    int64_t result = year_to_days(year, &is_leap);
    result += month_to_days(month, is_leap);
    result += day - 1;
    return result * 86400;
}

static int parse_time_string(fluf_cbor_ll_number_t *out_value,
                             const char *time_string) {
    if (!isdigit(time_string[0]) || !isdigit(time_string[1])
            || !isdigit(time_string[2]) || !isdigit(time_string[3])
            || time_string[4] != '-') {
        return FLUF_IO_ERR_FORMAT;
    }
    uint16_t year =
            (uint16_t) ((time_string[0] - '0') * 1000
                        + (time_string[1] - '0') * 100
                        + (time_string[2] - '0') * 10 + (time_string[3] - '0'));
    if (!isdigit(time_string[5]) || !isdigit(time_string[6])
            || time_string[7] != '-') {
        return FLUF_IO_ERR_FORMAT;
    }
    uint8_t month =
            (uint8_t) ((time_string[5] - '0') * 10 + (time_string[6] - '0'));
    if (month < 1 || month > 12 || !isdigit(time_string[8])
            || !isdigit(time_string[9])
            || (time_string[10] != 'T' && time_string[10] != 't')) {
        return FLUF_IO_ERR_FORMAT;
    }
    uint8_t day =
            (uint8_t) ((time_string[8] - '0') * 10 + (time_string[9] - '0'));
    if (day < 1 || day > 31 || !isdigit(time_string[11])
            || !isdigit(time_string[12]) || time_string[13] != ':') {
        return FLUF_IO_ERR_FORMAT;
    }
    int64_t timestamp = convert_date_midnight_utc(year, month, day);
    uint8_t hour =
            (uint8_t) ((time_string[11] - '0') * 10 + (time_string[12] - '0'));
    if (hour > 23 || !isdigit(time_string[14]) || !isdigit(time_string[15])
            || time_string[16] != ':') {
        return FLUF_IO_ERR_FORMAT;
    }
    timestamp += (int64_t) hour * 3600;
    uint8_t minute =
            (uint8_t) ((time_string[14] - '0') * 10 + (time_string[15] - '0'));
    if (minute > 59 || !isdigit(time_string[17]) || !isdigit(time_string[18])) {
        return FLUF_IO_ERR_FORMAT;
    }
    timestamp += (int64_t) minute * 60;
    uint8_t second =
            (uint8_t) ((time_string[17] - '0') * 10 + (time_string[18] - '0'));
    if (second > 60) {
        return FLUF_IO_ERR_FORMAT;
    }
    timestamp += second;
    uint32_t nanosecond = 0;
    size_t index = 19;
    size_t ns_digits = 0;
    if (time_string[index] == '.') {
        ++index;
        while (ns_digits < 9 && isdigit(time_string[index])) {
            nanosecond =
                    (nanosecond * 10) + (uint32_t) (time_string[index] - '0');
            ++index;
            ++ns_digits;
        }
        while (ns_digits++ < 9) {
            nanosecond *= 10;
        }
    }
    int32_t tzoffset_seconds_east = 0;
    if (time_string[index] == 'Z' || time_string[index] == 'z') {
        ++index;
    } else {
        if ((time_string[index] != '+' && time_string[index] != '-')
                || !isdigit(time_string[index + 1])
                || !isdigit(time_string[index + 2])
                || time_string[index + 3] != ':'
                || !isdigit(time_string[index + 4])
                || !isdigit(time_string[index + 5])) {
            return FLUF_IO_ERR_FORMAT;
        }
        uint8_t tzoffset_hours = (uint8_t) ((time_string[index + 1] - '0') * 10
                                            + (time_string[index + 2] - '0'));
        uint8_t tzoffset_minutes =
                (uint8_t) ((time_string[index + 4] - '0') * 10
                           + (time_string[index + 5] - '0'));
        if (tzoffset_minutes > 59) {
            return FLUF_IO_ERR_FORMAT;
        }
        tzoffset_seconds_east = (int32_t) tzoffset_hours * 3600
                                + (int32_t) tzoffset_minutes * 60;
        if (time_string[index] == '-') {
            tzoffset_seconds_east = -tzoffset_seconds_east;
        }
        index += 6;
    }
    if (time_string[index]) {
        return FLUF_IO_ERR_FORMAT;
    }
    timestamp -= tzoffset_seconds_east;
    if (nanosecond) {
        out_value->type = FLUF_CBOR_LL_VALUE_DOUBLE;
        out_value->value.f64 = (double) timestamp + (double) nanosecond / 1.0e9;
    } else if (timestamp >= 0) {
        out_value->type = FLUF_CBOR_LL_VALUE_UINT;
        out_value->value.u64 = (uint64_t) timestamp;
    } else {
        out_value->type = FLUF_CBOR_LL_VALUE_NEGATIVE_INT;
        out_value->value.i64 = timestamp;
    }
    return 0;
}
#    endif // FLUF_WITH_CBOR_STRING_TIME

static int decode_timestamp(fluf_cbor_ll_decoder_t *ctx,
                            fluf_cbor_ll_number_t *out_value) {
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK) {
        return FLUF_IO_ERR_LOGIC;
    }
    assert(!ctx->needs_preprocessing);

    if (ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_NONE) {
#    ifdef FLUF_WITH_CBOR_STRING_TIME
        if (get_additional_info(ctx->current_item.initial_byte)
                == CBOR_DECODER_TAG_STRING_TIME) {
            memset(&ctx->subparser.string_or_bytes_or_string_time, 0,
                   sizeof(ctx->subparser.string_or_bytes_or_string_time));
            ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_STRING_TIME;
            ctx->needs_preprocessing = true;
            ctx->after_tag = true;
        } else
#    endif // FLUF_WITH_CBOR_STRING_TIME
        {
            assert(get_additional_info(ctx->current_item.initial_byte)
                   == CBOR_DECODER_TAG_EPOCH_BASED_TIME);
            ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME;
            ctx->needs_preprocessing = true;
            ctx->after_tag = true;
        }
    }

    int result;
    switch (ctx->subparser_type) {
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    case FLUF_CBOR_LL_SUBPARSER_STRING_TIME: {
        if (!ctx->subparser.string_or_bytes_or_string_time.string_time
                     .initialized) {
            if ((result = initialize_bytes_subparser(ctx))) {
                return result;
            }
            if (get_major_type(ctx->current_item.initial_byte)
                    != CBOR_MAJOR_TYPE_TEXT_STRING) {
                ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
                return FLUF_IO_ERR_FORMAT;
            }
            ctx->subparser.string_or_bytes_or_string_time.string_time
                    .initialized = true;
        }
        bool message_finished = false;
        while (!message_finished) {
            const void *buf;
            size_t buf_size;
            if ((result = fluf_cbor_ll_decoder_bytes_get_some(
                         &ctx->subparser.string_or_bytes_or_string_time, &buf,
                         &buf_size, &message_finished))) {
                return result;
            }
            if (buf_size) {
                if (ctx->subparser.string_or_bytes_or_string_time.string_time
                                    .bytes_read
                                + buf_size
                        >= sizeof(ctx->subparser.string_or_bytes_or_string_time
                                          .string_time.buffer)) {
                    ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
                    return FLUF_IO_ERR_FORMAT;
                }
                memcpy(ctx->subparser.string_or_bytes_or_string_time.string_time
                                       .buffer
                               + ctx->subparser.string_or_bytes_or_string_time
                                         .string_time.bytes_read,
                       buf,
                       buf_size);
                ctx->subparser.string_or_bytes_or_string_time.string_time
                        .bytes_read += buf_size;
            }
        }
        // after message_finished, fluf_cbor_ll_decoder_bytes_get_some() will
        // reset subparser_type to FLUF_CBOR_LL_SUBPARSER_NONE
        assert(ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_NONE);
        assert(ctx->subparser.string_or_bytes_or_string_time.string_time
                       .bytes_read
               < sizeof(ctx->subparser.string_or_bytes_or_string_time
                                .string_time.buffer));
        ctx->subparser.string_or_bytes_or_string_time.string_time
                .buffer[ctx->subparser.string_or_bytes_or_string_time
                                .string_time.bytes_read] = '\0';
        if ((result = parse_time_string(
                     out_value,
                     ctx->subparser.string_or_bytes_or_string_time.string_time
                             .buffer))) {
            ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
        }
        return result;
    }
#    endif // FLUF_WITH_CBOR_STRING_TIME
    case FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME: {
        if (!(result = decode_simple_number(ctx, out_value))) {
            ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_NONE;
        }
        return result;
    }
    default: {
        AVS_UNREACHABLE("Invalid subparser type");
        return FLUF_IO_ERR_LOGIC;
    }
    }
}

#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
static int try_preprocess_next_bytes_chunk(fluf_cbor_ll_decoder_t *ctx,
                                           bool *out_message_finished) {
#        ifdef FLUF_WITH_CBOR_STRING_TIME
    assert(ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_STRING
           || ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_BYTES
           || ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_STRING_TIME);
#        else  // FLUF_WITH_CBOR_STRING_TIME
    assert(ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_STRING
           || ctx->subparser_type == FLUF_CBOR_LL_SUBPARSER_BYTES);
#        endif // FLUF_WITH_CBOR_STRING_TIME
    assert(ctx->subparser.string_or_bytes_or_string_time.indefinite);
    assert(!ctx->subparser.string_or_bytes_or_string_time.bytes_available);
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->subparser.string_or_bytes_or_string_time.initial_nesting_level
            == ctx->nest_stack_size) {
        if ((result = cbor_get_bytes_size(
                     ctx, &ctx->subparser.string_or_bytes_or_string_time
                                   .bytes_available))
                < 0) {
            ctx->state = FLUF_CBOR_LL_DECODER_STATE_ERROR;
        }
        *out_message_finished = false;
        return result;
    } else {
        *out_message_finished = true;
        return 0;
    }
}
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES

static int bytes_get_some_impl(fluf_cbor_ll_decoder_bytes_ctx_t *bytes_ctx,
                               const void **out_buf,
                               size_t *out_buf_size,
                               bool *out_message_finished) {
    assert(bytes_ctx);
    fluf_cbor_ll_decoder_t *ctx =
            AVS_CONTAINER_OF(bytes_ctx, fluf_cbor_ll_decoder_t, subparser);
    if (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_STRING
            && ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_BYTES
#    ifdef FLUF_WITH_CBOR_STRING_TIME
            && ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_STRING_TIME
#    endif // FLUF_WITH_CBOR_STRING_TIME
    ) {
        return FLUF_IO_ERR_LOGIC;
    }

    *out_message_finished = false;
#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
    int result = 0;
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_OK && bytes_ctx->indefinite
            && !ctx->subparser.string_or_bytes_or_string_time.bytes_available
            && (result = try_preprocess_next_bytes_chunk(
                        ctx, out_message_finished))) {
        return result;
    }

    if (*out_message_finished) {
        *out_buf = NULL;
        *out_buf_size = 0;
    } else
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
    {
        if (ctx->prebuffer_size > ctx->prebuffer_offset) {
            size_t prebuffered_bytes =
                    ctx->prebuffer_size - ctx->prebuffer_offset;
            assert(ctx->input >= ctx->input_begin);
            size_t can_rewind_by = (size_t) (ctx->input - ctx->input_begin);
            if (can_rewind_by < prebuffered_bytes) {
                // Can't "unbuffer everything" - next payload already provided
                // return the prebuffer
                *out_buf = ctx->prebuffer + ctx->prebuffer_offset;
                *out_buf_size =
                        AVS_MIN(prebuffered_bytes, bytes_ctx->bytes_available);
                ctx->prebuffer_offset += (uint8_t) *out_buf_size;
                goto finish;
            } else {
                // Rewind already prebuffered bytes and then continue
                ctx->prebuffer_size = ctx->prebuffer_offset;
                ctx->input -= prebuffered_bytes;
            }
        }
        assert(ctx->prebuffer_offset == ctx->prebuffer_size);
        *out_buf = ctx->input;
        assert(ctx->input_end >= ctx->input);
        *out_buf_size = AVS_MIN((size_t) (ctx->input_end - ctx->input),
                                bytes_ctx->bytes_available);
        ctx->input += *out_buf_size;
    finish:
        bytes_ctx->bytes_available -= *out_buf_size;
        if (!bytes_ctx->bytes_available) {
#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
            *out_message_finished =
                    !ctx->subparser.string_or_bytes_or_string_time.indefinite;
#    else  // FLUF_WITH_CBOR_INDEFINITE_BYTES
            *out_message_finished = true;
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
            ctx->needs_preprocessing = true;
            ctx->after_tag = false;
        } else {
            *out_message_finished = false;
            if (!*out_buf_size) {
                return ctx->input_last ? FLUF_IO_ERR_FORMAT
                                       : FLUF_IO_WANT_NEXT_PAYLOAD;
            }
        }
    }
    if (*out_message_finished) {
        ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_NONE;
    }
    return 0;
}

void fluf_cbor_ll_decoder_init(fluf_cbor_ll_decoder_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = FLUF_CBOR_LL_DECODER_STATE_OK;
    ctx->needs_preprocessing = true;
    ctx->after_tag = false;
}

int fluf_cbor_ll_decoder_feed_payload(fluf_cbor_ll_decoder_t *ctx,
                                      const void *buff,
                                      size_t buff_len,
                                      bool payload_finished) {
    if (ctx->input != ctx->input_end || ctx->input_last) {
        return FLUF_IO_ERR_LOGIC;
    }
    ctx->input_begin = (const uint8_t *) buff;
    ctx->input = ctx->input_begin;
    ctx->input_end = ctx->input_begin;
    if (buff_len) {
        // NOTE: if buff_len is 0, then buff == NULL is technically legal
        // but UBSan complains about adding a zero offset to a null pointer
        ctx->input_end += buff_len;
    }
    ctx->input_last = payload_finished;
    return 0;
}

int fluf_cbor_ll_decoder_errno(fluf_cbor_ll_decoder_t *ctx) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    switch (ctx->state) {
    case FLUF_CBOR_LL_DECODER_STATE_OK:
        return 0;
    case FLUF_CBOR_LL_DECODER_STATE_FINISHED:
        return FLUF_IO_EOF;
    case FLUF_CBOR_LL_DECODER_STATE_ERROR:
        return FLUF_IO_ERR_FORMAT;
    }
    AVS_UNREACHABLE("invalid fluf_cbor_ll_decoder_state_t value");
    return FLUF_IO_ERR_LOGIC;
}

int fluf_cbor_ll_decoder_current_value_type(
        fluf_cbor_ll_decoder_t *ctx, fluf_cbor_ll_value_type_t *out_type) {
    switch (ctx->subparser_type) {
    case FLUF_CBOR_LL_SUBPARSER_NONE: {
        int result = ensure_value_or_error_available(ctx);
        if (result) {
            return result;
        }
        if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
            return FLUF_IO_ERR_LOGIC;
        }
        if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_OK) {
            *out_type = ctx->current_item.value_type;
            return 0;
        }
        break;
    }
    case FLUF_CBOR_LL_SUBPARSER_STRING:
        *out_type = FLUF_CBOR_LL_VALUE_TEXT_STRING;
        return 0;
    case FLUF_CBOR_LL_SUBPARSER_BYTES:
        *out_type = FLUF_CBOR_LL_VALUE_BYTE_STRING;
        return 0;
    case FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME:
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    case FLUF_CBOR_LL_SUBPARSER_STRING_TIME:
#    endif // FLUF_WITH_CBOR_STRING_TIME
        *out_type = FLUF_CBOR_LL_VALUE_TIMESTAMP;
        return 0;
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    case FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION:
        *out_type = FLUF_CBOR_LL_VALUE_DOUBLE;
        return 0;
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    }
    return FLUF_IO_ERR_FORMAT;
}

int fluf_cbor_ll_decoder_null(fluf_cbor_ll_decoder_t *ctx) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
            || ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_NULL) {
        return FLUF_IO_ERR_FORMAT;
    }
    ctx->needs_preprocessing = true;
    ctx->after_tag = false;
    return 0;
}

int fluf_cbor_ll_decoder_bool(fluf_cbor_ll_decoder_t *ctx, bool *out_value) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
            || ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_BOOL) {
        return FLUF_IO_ERR_FORMAT;
    }
    switch (get_additional_info(ctx->current_item.initial_byte)) {
    case CBOR_VALUE_BOOL_FALSE:
        *out_value = false;
        break;
    case CBOR_VALUE_BOOL_TRUE:
        *out_value = true;
        break;
    default:
        AVS_UNREACHABLE("expected boolean, but got something else instead");
        return FLUF_IO_ERR_LOGIC;
    }
    ctx->needs_preprocessing = true;
    ctx->after_tag = false;
    return 0;
}

int fluf_cbor_ll_decoder_number(fluf_cbor_ll_decoder_t *ctx,
                                fluf_cbor_ll_number_t *out_value) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK) {
        return FLUF_IO_ERR_FORMAT;
    }
    out_value->type = (fluf_cbor_ll_value_type_t) -1;
    switch (ctx->subparser_type) {
    case FLUF_CBOR_LL_SUBPARSER_NONE:
        if (ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_TIMESTAMP) {
            return decode_simple_number(ctx, out_value);
        }
        // fall through
    case FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME:
#    ifdef FLUF_WITH_CBOR_STRING_TIME
    case FLUF_CBOR_LL_SUBPARSER_STRING_TIME:
#    endif // FLUF_WITH_CBOR_STRING_TIME
        return decode_timestamp(ctx, out_value);
#    ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    case FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION:
        out_value->type = FLUF_CBOR_LL_VALUE_DOUBLE;
        return decode_decimal_fraction(ctx, &out_value->value.f64);
#    endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    case FLUF_CBOR_LL_SUBPARSER_STRING:
    case FLUF_CBOR_LL_SUBPARSER_BYTES:;
    }
    return FLUF_IO_ERR_LOGIC;
}

int fluf_cbor_ll_decoder_bytes(fluf_cbor_ll_decoder_t *ctx,
                               fluf_cbor_ll_decoder_bytes_ctx_t **out_bytes_ctx,
                               ptrdiff_t *out_total_size) {
    *out_bytes_ctx = NULL;
    if (ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE) {
        return FLUF_IO_ERR_FORMAT;
    }
    int result = initialize_bytes_subparser(ctx);
    if (!result) {
        if (ctx->current_item.value_type == FLUF_CBOR_LL_VALUE_TEXT_STRING) {
            ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_STRING;
        } else {
            assert(ctx->current_item.value_type
                   == FLUF_CBOR_LL_VALUE_BYTE_STRING);
            ctx->subparser_type = FLUF_CBOR_LL_SUBPARSER_BYTES;
        }
        *out_bytes_ctx = &ctx->subparser.string_or_bytes_or_string_time;
        if (out_total_size) {
            if (
#    ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
                        ctx->subparser.string_or_bytes_or_string_time.indefinite
                            ||
#    endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
                            ctx->subparser.string_or_bytes_or_string_time
                                            .bytes_available
                                        > SIZE_MAX / 2) {
                *out_total_size = FLUF_CBOR_LL_DECODER_ITEMS_INDEFINITE;
            } else {
                *out_total_size =
                        (ptrdiff_t) ctx->subparser
                                .string_or_bytes_or_string_time.bytes_available;
            }
        }
    }
    return result;
}

int fluf_cbor_ll_decoder_bytes_get_some(
        fluf_cbor_ll_decoder_bytes_ctx_t *bytes_ctx,
        const void **out_buf,
        size_t *out_buf_size,
        bool *out_message_finished) {
    int result;
    do {
        result = bytes_get_some_impl(bytes_ctx, out_buf, out_buf_size,
                                     out_message_finished);
        // Empty blocks may happen in an indefinite length bytes block -
        // don't return them to the user as they are useless.
    } while (!result && !*out_buf_size && !*out_message_finished);
    return result;
}

#    if FLUF_MAX_CBOR_NEST_STACK_SIZE > 0
int fluf_cbor_ll_decoder_enter_array(fluf_cbor_ll_decoder_t *ctx,
                                     ptrdiff_t *out_size) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
            || ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_ARRAY) {
        return FLUF_IO_ERR_FORMAT;
    }
    if ((result = nested_state_push(ctx))) {
        return result;
    }
    ctx->needs_preprocessing = true;
    ctx->after_tag = false;
    if (out_size) {
        *out_size = nested_state_top(ctx)->all_items;
    }
    return 0;
}

int fluf_cbor_ll_decoder_enter_map(fluf_cbor_ll_decoder_t *ctx,
                                   ptrdiff_t *out_pair_count) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state == FLUF_CBOR_LL_DECODER_STATE_FINISHED) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK
            || ctx->subparser_type != FLUF_CBOR_LL_SUBPARSER_NONE
            || ctx->current_item.value_type != FLUF_CBOR_LL_VALUE_MAP) {
        return FLUF_IO_ERR_FORMAT;
    }
    if ((result = nested_state_push(ctx))) {
        return result;
    }
    ctx->needs_preprocessing = true;
    ctx->after_tag = false;
    if (out_pair_count) {
        *out_pair_count = nested_state_top(ctx)->all_items;
        if (*out_pair_count > 0) {
            *out_pair_count /= 2;
        }
    }
    return 0;
}

int fluf_cbor_ll_decoder_nesting_level(fluf_cbor_ll_decoder_t *ctx,
                                       size_t *out_nesting_level) {
    int result = ensure_value_or_error_available(ctx);
    if (result) {
        return result;
    }
    if (ctx->state != FLUF_CBOR_LL_DECODER_STATE_OK) {
        *out_nesting_level = 0;
        return 0;
    }
    switch (ctx->subparser_type) {
#        ifdef FLUF_WITH_CBOR_STRING_TIME
    case FLUF_CBOR_LL_SUBPARSER_STRING_TIME:
        if (!ctx->subparser.string_or_bytes_or_string_time.string_time
                     .initialized) {
            *out_nesting_level = ctx->nest_stack_size;
            return 0;
        }
#        endif // FLUF_WITH_CBOR_STRING_TIME
        // fall through
    case FLUF_CBOR_LL_SUBPARSER_STRING:
    case FLUF_CBOR_LL_SUBPARSER_BYTES:
#        ifdef FLUF_WITH_CBOR_INDEFINITE_BYTES
        if (ctx->subparser.string_or_bytes_or_string_time.indefinite) {
            *out_nesting_level = ctx->subparser.string_or_bytes_or_string_time
                                         .initial_nesting_level
                                 - 1;
            return 0;
        }
#        endif // FLUF_WITH_CBOR_INDEFINITE_BYTES
               // fall through
    case FLUF_CBOR_LL_SUBPARSER_NONE:
    case FLUF_CBOR_LL_SUBPARSER_EPOCH_BASED_TIME:
        *out_nesting_level = ctx->nest_stack_size;
        return 0;
#        ifdef FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    case FLUF_CBOR_LL_SUBPARSER_DECIMAL_FRACTION:
        *out_nesting_level = ctx->subparser.decimal_fraction.array_level - 1;
        return 0;
#        endif // FLUF_WITH_CBOR_DECIMAL_FRACTIONS
    }
    return FLUF_IO_ERR_LOGIC;
}
#    endif // FLUF_MAX_CBOR_NEST_STACK_SIZE > 0

#endif // defined(FLUF_WITH_SENML_CBOR) || defined(FLUF_WITH_LWM2M_CBOR) ||
       // defined(FLUF_WITH_CBOR)
