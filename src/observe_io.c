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

#include <math.h>

#include "observe.h"
#include "io/vtable.h"

VISIBILITY_SOURCE_BEGIN

#define VARARG_LENGTH_IMPL(_10, _9, _8, _7, _6, _5, _4, _3, _2, _1, N, ...) N
#define VARARG_LENGTH(...) \
        VARARG_LENGTH_IMPL(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define VARARG0_IMPL(Arg, ...) Arg
#define VARARG0(...) VARARG0_IMPL(__VA_ARGS__, _)

#define OTHER_ARGS_DECL1(_)
#define OTHER_ARGS_DECL2(_, T1) , T1 _arg1__
#define OTHER_ARGS_DECL3(_, T1, T2) , T1 _arg1__, T2 _arg2__

#define OTHER_ARGS_DECL(...) \
        AVS_CONCAT(OTHER_ARGS_DECL, VARARG_LENGTH(__VA_ARGS__))(__VA_ARGS__)

#define OTHER_ARGS_CALL1
#define OTHER_ARGS_CALL2 , _arg1__
#define OTHER_ARGS_CALL3 , _arg1__, _arg2__

#define OTHER_ARGS_CALL(...) \
        AVS_CONCAT(OTHER_ARGS_CALL, VARARG_LENGTH(__VA_ARGS__))

typedef struct {
    const anjay_output_ctx_vtable_t *vtable;
    anjay_output_ctx_t *backend;
    double *out_numeric;
    bool value_already_returned;
} observe_out_t;

#define NON_NUMERIC(Rettype, Name, ...) \
static Rettype Name (anjay_output_ctx_t *ctx_ \
                     OTHER_ARGS_DECL(__VA_ARGS__)) { \
    observe_out_t *ctx = (observe_out_t *) ctx_; \
    *ctx->out_numeric = NAN; \
    ctx->value_already_returned = true; \
    return VARARG0(__VA_ARGS__) (ctx->backend OTHER_ARGS_CALL(__VA_ARGS__)); \
}

NON_NUMERIC(anjay_ret_bytes_ctx_t *, observe_bytes_begin,
            anjay_ret_bytes_begin, size_t)
NON_NUMERIC(int, observe_string, anjay_ret_string, const char *)
NON_NUMERIC(int, observe_bool,   anjay_ret_bool,   bool)
NON_NUMERIC(int, observe_objlnk, anjay_ret_objlnk, anjay_oid_t, anjay_iid_t)
NON_NUMERIC(anjay_output_ctx_t *, observe_array_start, anjay_ret_array_start)
NON_NUMERIC(anjay_output_ctx_t *, observe_object_start,
            _anjay_output_object_start)

#define NUMERIC(Typeid, Type) \
static int observe_##Typeid (anjay_output_ctx_t *ctx_, Type value) { \
    observe_out_t *ctx = (observe_out_t *) ctx_; \
    if (ctx->value_already_returned) { \
        *ctx->out_numeric = NAN; \
    } else { \
        *ctx->out_numeric = (double) value; \
        ctx->value_already_returned = true; \
    } \
    return anjay_ret_##Typeid (ctx->backend, value); \
}

NUMERIC(i32, int32_t)
NUMERIC(i64, int64_t)
NUMERIC(float, float)
NUMERIC(double, double)

static int *observe_errno_ptr(anjay_output_ctx_t *ctx) {
    return _anjay_output_ctx_errno_ptr(((observe_out_t *) ctx)->backend);
}

static int observe_set_id(anjay_output_ctx_t *ctx,
                          anjay_id_type_t type, uint16_t id) {
    return _anjay_output_set_id(((observe_out_t *) ctx)->backend, type, id);
}

static int observe_close(anjay_output_ctx_t *ctx) {
    return _anjay_output_ctx_destroy(&((observe_out_t *) ctx)->backend);
}

static const anjay_output_ctx_vtable_t OBSERVE_OUT_VTABLE = {
    .errno_ptr = observe_errno_ptr,
    .bytes_begin = observe_bytes_begin,
    .string = observe_string,
    .i32 = observe_i32,
    .i64 = observe_i64,
    .f32 = observe_float,
    .f64 = observe_double,
    .boolean = observe_bool,
    .objlnk = observe_objlnk,
    .array_start = observe_array_start,
    .object_start = observe_object_start,
    .set_id = observe_set_id,
    .close = observe_close
};

anjay_output_ctx_t *_anjay_observe_decorate_ctx(anjay_output_ctx_t *backend,
                                                double *out_numeric) {
    *out_numeric = NAN;
    observe_out_t *ctx = (observe_out_t *) calloc(1, sizeof(observe_out_t));
    if (ctx) {
        ctx->vtable = &OBSERVE_OUT_VTABLE;
        ctx->backend = backend;
        ctx->out_numeric = out_numeric;
    }
    return (anjay_output_ctx_t *) ctx;
}
