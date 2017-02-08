/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

static int output_buf_set_id(anjay_output_ctx_t *ctx,
                             anjay_id_type_t type,
                             uint16_t id) {
    (void)ctx;
    (void)type;
    (void)id;
    return 0;
}

static int output_buf_ret_bytes(anjay_output_ctx_t *ctx_,
                                const void *data,
                                size_t data_size) {
    anjay_output_buf_ctx_t *ctx = (anjay_output_buf_ctx_t *) ctx_;
    return avs_stream_write((avs_stream_abstract_t*)ctx->stream,
                            data, data_size);
}

static int output_buf_ret_string(anjay_output_ctx_t *ctx,
                                 const char *str) {
    return output_buf_ret_bytes(ctx, str, strlen(str));
}

#define DEFINE_RET_HANDLER(Suffix, Type) \
    static int output_buf_ret_##Suffix(anjay_output_ctx_t *ctx, \
                                       Type value) { \
        return output_buf_ret_bytes(ctx, &value, sizeof(value)); \
    }

DEFINE_RET_HANDLER(i64, int64_t)   // output_buf_ret_i64
DEFINE_RET_HANDLER(double, double) // output_buf_ret_double
DEFINE_RET_HANDLER(bool, bool)     // output_buf_ret_bool

#define DEFINE_FORWARD_HANDLER(Suffix, Type, Backend) \
    static int output_buf_ret_##Suffix(anjay_output_ctx_t *ctx, Type value) { \
        return output_buf_ret_##Backend(ctx, value); \
    }

DEFINE_FORWARD_HANDLER(i32, int32_t, i64)    // output_buf_ret_i32
DEFINE_FORWARD_HANDLER(float, float, double) // output_buf_ret_float

static anjay_ret_bytes_ctx_t *
output_buf_ret_bytes_begin(anjay_output_ctx_t *ctx,
                           size_t length) {
    (void)length;
    return (anjay_ret_bytes_ctx_t*)
            &((anjay_output_buf_ctx_t*)ctx)->ret_bytes_vtable;
}

static int output_buf_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx,
                                       const void *data,
                                       size_t size) {
    return output_buf_ret_bytes((anjay_output_ctx_t*)
            AVS_CONTAINER_OF(ctx, anjay_output_buf_ctx_t, ret_bytes_vtable),
            data, size);
}

static const anjay_output_ctx_vtable_t BUF_OUT_VTABLE = {
    .bytes_begin = output_buf_ret_bytes_begin,
    .string = output_buf_ret_string,
    .i32 = output_buf_ret_i32,
    .i64 = output_buf_ret_i64,
    .f32 = output_buf_ret_float,
    .f64 = output_buf_ret_double,
    .boolean = output_buf_ret_bool,
    .set_id = output_buf_set_id
};

static const anjay_ret_bytes_ctx_vtable_t BUF_BYTES_VTABLE = {
    .append = output_buf_ret_bytes_append
};

anjay_output_buf_ctx_t _anjay_output_buf_ctx_init(avs_stream_outbuf_t *stream) {
    return (anjay_output_buf_ctx_t) {
        .vtable = &BUF_OUT_VTABLE,
        .ret_bytes_vtable = &BUF_BYTES_VTABLE,
        .stream = stream
    };
}
