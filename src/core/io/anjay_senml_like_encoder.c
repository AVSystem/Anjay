/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#if defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON) \
        || defined(ANJAY_WITH_CBOR)

#    include "anjay_senml_like_encoder.h"
#    include "anjay_senml_like_encoder_vtable.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_senml_like_encode_int(anjay_senml_like_encoder_t *ctx,
                                 int64_t data) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_encode_int);
    return ctx->vtable->senml_like_encode_int(ctx, data);
}

#    ifdef ANJAY_WITH_LWM2M11
int _anjay_senml_like_encode_uint(anjay_senml_like_encoder_t *ctx,
                                  uint64_t data) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_encode_uint);
    return ctx->vtable->senml_like_encode_uint(ctx, data);
}
#    endif // ANJAY_WITH_LWM2M11

int _anjay_senml_like_encode_double(anjay_senml_like_encoder_t *ctx,
                                    double data) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_encode_double);
    return ctx->vtable->senml_like_encode_double(ctx, data);
}

int _anjay_senml_like_encode_bool(anjay_senml_like_encoder_t *ctx, bool data) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_encode_bool);
    return ctx->vtable->senml_like_encode_bool(ctx, data);
}

int _anjay_senml_like_encode_string(anjay_senml_like_encoder_t *ctx,
                                    const char *data) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_encode_string);
    return ctx->vtable->senml_like_encode_string(ctx, data);
}

int _anjay_senml_like_encode_objlnk(anjay_senml_like_encoder_t *ctx,
                                    const char *data) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_encode_objlnk);
    return ctx->vtable->senml_like_encode_objlnk(ctx, data);
}

int _anjay_senml_like_element_begin(anjay_senml_like_encoder_t *ctx,
                                    const char *basename,
                                    const char *name,
                                    double time_s) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_element_begin);
    return ctx->vtable->senml_like_element_begin(ctx, basename, name, time_s);
}

int _anjay_senml_like_element_end(anjay_senml_like_encoder_t *ctx) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_element_end);
    return ctx->vtable->senml_like_element_end(ctx);
}

int _anjay_senml_like_bytes_begin(anjay_senml_like_encoder_t *ctx,
                                  size_t size) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_bytes_begin);
    return ctx->vtable->senml_like_bytes_begin(ctx, size);
}

int _anjay_senml_like_bytes_append(anjay_senml_like_encoder_t *ctx,
                                   const void *data,
                                   size_t size) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_bytes_append);
    return ctx->vtable->senml_like_bytes_append(ctx, data, size);
}

int _anjay_senml_like_bytes_end(anjay_senml_like_encoder_t *ctx) {
    assert(ctx && ctx->vtable);
    assert(ctx->vtable->senml_like_bytes_end);
    return ctx->vtable->senml_like_bytes_end(ctx);
}

int _anjay_senml_like_encoder_cleanup(anjay_senml_like_encoder_t **ctx) {
    assert(ctx && *ctx && (*ctx)->vtable);
    assert((*ctx)->vtable->senml_like_encoder_cleanup);
    return (*ctx)->vtable->senml_like_encoder_cleanup(ctx);
}

#endif // defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON) ||
       // defined(ANJAY_WITH_CBOR)
