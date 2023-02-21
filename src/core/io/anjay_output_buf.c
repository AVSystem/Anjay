/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

static int output_buf_ret_bytes(anjay_unlocked_output_ctx_t *ctx_,
                                const void *data,
                                size_t data_size) {
    anjay_output_buf_ctx_t *ctx = (anjay_output_buf_ctx_t *) ctx_;
    return avs_is_ok(avs_stream_write((avs_stream_t *) ctx->stream, data,
                                      data_size))
                   ? 0
                   : -1;
}

static int output_buf_ret_string(anjay_unlocked_output_ctx_t *ctx,
                                 const char *str) {
    return output_buf_ret_bytes(ctx, str, strlen(str));
}

#define DEFINE_RET_HANDLER(Suffix, Type)                                 \
    static int output_buf_ret_##Suffix(anjay_unlocked_output_ctx_t *ctx, \
                                       Type value) {                     \
        return output_buf_ret_bytes(ctx, &value, sizeof(value));         \
    }

DEFINE_RET_HANDLER(integer, int64_t) // output_buf_ret_integer
#ifdef ANJAY_WITH_LWM2M11
DEFINE_RET_HANDLER(uint, uint64_t) // output_buf_ret_u64
#endif                             // ANJAY_WITH_LWM2M11
DEFINE_RET_HANDLER(double, double) // output_buf_ret_double
DEFINE_RET_HANDLER(bool, bool)     // output_buf_ret_bool

static int
output_buf_ret_bytes_begin(anjay_unlocked_output_ctx_t *ctx,
                           size_t length,
                           anjay_unlocked_ret_bytes_ctx_t **out_bytes_ctx) {
    (void) length;
    *out_bytes_ctx =
            (anjay_unlocked_ret_bytes_ctx_t *) &((anjay_output_buf_ctx_t *) ctx)
                    ->ret_bytes_vtable;
    return 0;
}

static int output_buf_ret_bytes_append(anjay_unlocked_ret_bytes_ctx_t *ctx,
                                       const void *data,
                                       size_t size) {
    return output_buf_ret_bytes(
            (anjay_unlocked_output_ctx_t *) AVS_CONTAINER_OF(
                    ctx, anjay_output_buf_ctx_t, ret_bytes_vtable),
            data, size);
}

static int output_buf_ret_objlnk(anjay_unlocked_output_ctx_t *ctx,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid) {
    const uint32_t objlnk_encoded = (uint32_t) (oid << 16) | iid;
    return output_buf_ret_bytes(ctx, &objlnk_encoded, sizeof(objlnk_encoded));
}

static const anjay_output_ctx_vtable_t BUF_OUT_VTABLE = {
    .bytes_begin = output_buf_ret_bytes_begin,
    .string = output_buf_ret_string,
    .integer = output_buf_ret_integer,
#ifdef ANJAY_WITH_LWM2M11
    .uint = output_buf_ret_uint,
#endif // ANJAY_WITH_LWM2M11
    .floating = output_buf_ret_double,
    .boolean = output_buf_ret_bool,
    .objlnk = output_buf_ret_objlnk
};

static const anjay_ret_bytes_ctx_vtable_t BUF_BYTES_VTABLE = {
    .append = output_buf_ret_bytes_append
};

anjay_output_buf_ctx_t _anjay_output_buf_ctx_init(avs_stream_t *stream) {
    return (anjay_output_buf_ctx_t) {
        .base = {
            .vtable = &BUF_OUT_VTABLE
        },
        .ret_bytes_vtable = &BUF_BYTES_VTABLE,
        .stream = stream
    };
}
