/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#if defined(ANJAY_WITH_CBOR) || defined(ANJAY_WITH_SENML_JSON)

#    include "anjay_json_like_decoder_vtable.h"

#    include <avsystem/commons/avs_log.h>

#    include "../anjay_utils_private.h"

#    define LOG(...) _anjay_log(json_like_decoder, __VA_ARGS__)

VISIBILITY_SOURCE_BEGIN

struct anjay_json_like_decoder_struct {
    const anjay_json_like_decoder_vtable_t *vtable;
};

void _anjay_json_like_decoder_delete(anjay_json_like_decoder_t **ctx) {
    if (ctx && *ctx) {
        assert((*ctx)->vtable);
        assert((*ctx)->vtable->cleanup);
        (*ctx)->vtable->cleanup(ctx);
        assert(!*ctx);
    }
}

anjay_json_like_decoder_state_t
_anjay_json_like_decoder_state(const anjay_json_like_decoder_t *ctx) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->state);
    return ctx->vtable->state(ctx);
}

int _anjay_json_like_decoder_current_value_type(
        anjay_json_like_decoder_t *ctx,
        anjay_json_like_value_type_t *out_type) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->current_value_type);
    return ctx->vtable->current_value_type(ctx, out_type);
}

int _anjay_json_like_decoder_bool(anjay_json_like_decoder_t *ctx,
                                  bool *out_value) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->read_bool);
    return ctx->vtable->read_bool(ctx, out_value);
}

int _anjay_json_like_decoder_number(anjay_json_like_decoder_t *ctx,
                                    anjay_json_like_number_t *out_value) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->number);
    return ctx->vtable->number(ctx, out_value);
}

int _anjay_json_like_decoder_bytes(anjay_json_like_decoder_t *ctx,
                                   avs_stream_t *target_stream) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->bytes);
    return ctx->vtable->bytes(ctx, target_stream);
}

int _anjay_json_like_decoder_enter_array(anjay_json_like_decoder_t *ctx) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->enter_array);
    return ctx->vtable->enter_array(ctx);
}

int _anjay_json_like_decoder_enter_map(anjay_json_like_decoder_t *ctx) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->enter_map);
    return ctx->vtable->enter_map(ctx);
}

size_t _anjay_json_like_decoder_nesting_level(anjay_json_like_decoder_t *ctx) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->nesting_level);
    return ctx->vtable->nesting_level(ctx);
}

static inline void print_number_conversion_warning(const char *expected) {
    LOG(WARNING,
        _("expected ") "%s" _(", got something else instead"),
        expected);
}

int _anjay_json_like_decoder_get_i64_from_number(
        const anjay_json_like_number_t *number, int64_t *out_value) {
    if (number->type == ANJAY_JSON_LIKE_VALUE_UINT) {
        if (number->value.u64 > INT64_MAX) {
            return -1;
        }
        *out_value = (int64_t) number->value.u64;
    } else if (number->type == ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT) {
        *out_value = number->value.i64;
    } else if (number->type == ANJAY_JSON_LIKE_VALUE_FLOAT
               && avs_double_convertible_to_int64((double) number->value.f32)) {
        *out_value = (int64_t) number->value.f32;
    } else if (number->type == ANJAY_JSON_LIKE_VALUE_DOUBLE
               && avs_double_convertible_to_int64(number->value.f64)) {
        *out_value = (int64_t) number->value.f64;
    } else {
        print_number_conversion_warning("int");
        return -1;
    }
    return 0;
}

int _anjay_json_like_decoder_get_u64_from_number(
        const anjay_json_like_number_t *number, uint64_t *out_value) {
    if (number->type == ANJAY_JSON_LIKE_VALUE_UINT) {
        *out_value = number->value.u64;
    } else if (number->type == ANJAY_JSON_LIKE_VALUE_FLOAT
               && avs_double_convertible_to_uint64(
                          (double) number->value.f32)) {
        *out_value = (uint64_t) number->value.f32;
    } else if (number->type == ANJAY_JSON_LIKE_VALUE_DOUBLE
               && avs_double_convertible_to_uint64(number->value.f64)) {
        *out_value = (uint64_t) number->value.f64;
    } else {
        print_number_conversion_warning("uint");
        return -1;
    }
    return 0;
}

int _anjay_json_like_decoder_get_double_from_number(
        const anjay_json_like_number_t *number, double *out_value) {
    switch (number->type) {
    case ANJAY_JSON_LIKE_VALUE_FLOAT:
        *out_value = number->value.f32;
        break;
    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
        *out_value = number->value.f64;
        break;
    case ANJAY_JSON_LIKE_VALUE_UINT:
        *out_value = (double) number->value.u64;
        break;
    case ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT:
        *out_value = (double) number->value.i64;
        break;
    default:
        print_number_conversion_warning("double");
        return -1;
    }
    return 0;
}

#endif // defined(ANJAY_WITH_CBOR) || defined(ANJAY_WITH_SENML_JSON)
