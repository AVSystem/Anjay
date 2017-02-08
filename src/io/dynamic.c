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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <avsystem/commons/stream.h>

#include "../io.h"
#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

/////////////////////////////////////////////////////////////////////// ENCODING

typedef struct {
    const anjay_output_ctx_vtable_t *vtable;
    int *errno_ptr;
    bool past_first_call;
    avs_stream_abstract_t *stream;
    anjay_msg_details_t details;
    anjay_id_type_t id_type;
    int32_t id;
    anjay_output_ctx_t *backend;
} dynamic_out_t;

static int *dynamic_errno_ptr(anjay_output_ctx_t *ctx) {
    return ((dynamic_out_t *) ctx)->errno_ptr;
}

static anjay_output_ctx_t *spawn_opaque(dynamic_out_t *ctx) {
    return _anjay_output_opaque_create(
            ctx->stream, ctx->errno_ptr, &ctx->details);
}

static anjay_output_ctx_t *spawn_text(dynamic_out_t *ctx) {
    return _anjay_output_text_create(
            ctx->stream, ctx->errno_ptr, &ctx->details);
}

static anjay_output_ctx_t *spawn_tlv(dynamic_out_t *ctx) {
    anjay_output_ctx_t *result = _anjay_output_tlv_create(
            ctx->stream, ctx->errno_ptr, &ctx->details);
    if (result && ctx->id >= 0
            && _anjay_output_set_id(result, ctx->id_type, (uint16_t) ctx->id)) {
        _anjay_output_ctx_destroy(&result);
    }
    return result;
}

static anjay_output_ctx_t *spawn_backend(dynamic_out_t *ctx, uint16_t format) {
    switch (_anjay_translate_legacy_content_format(format)) {
    case ANJAY_COAP_FORMAT_OPAQUE:
        return spawn_opaque(ctx);
    case ANJAY_COAP_FORMAT_PLAINTEXT:
        return spawn_text(ctx);
    case ANJAY_COAP_FORMAT_TLV:
        return spawn_tlv(ctx);
    default:
        anjay_log(ERROR, "Unsupported output format: %" PRIu16, format);
        *ctx->errno_ptr = -ANJAY_COAP_CODE_NOT_ACCEPTABLE;
        return NULL;
    }
}

static anjay_output_ctx_t *ensure_backend(dynamic_out_t *ctx,
                                          uint16_t format) {
    if (!ctx->backend) {
        ctx->backend = spawn_backend(ctx, format);
    }
    return ctx->backend;
}

static void adjust_errno(dynamic_out_t *ctx, const char *function) {
    if (!ctx->past_first_call
            && *ctx->errno_ptr == ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED) {
        // When the first call is not implemented,
        // it most likely means a format mismatch.
        // Yes, this is hack-ish.
        *ctx->errno_ptr = ANJAY_OUTCTXERR_FORMAT_MISMATCH;
    }
    switch (*ctx->errno_ptr) {
    case ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED:
        anjay_log(ERROR, "Output context method invalid in current context: %s",
                  function);
        break;
    case ANJAY_OUTCTXERR_FORMAT_MISMATCH:
        anjay_log(WARNING,
                  "Output context method conflicts with Content-Format: %s",
                  function);
    default:;
    }
    ctx->past_first_call = true;
}

static inline int process_errno(dynamic_out_t *ctx, const char *function,
                                int result) {
    adjust_errno(ctx, function);
    return result;
}

static anjay_ret_bytes_ctx_t *dynamic_ret_bytes(anjay_output_ctx_t *ctx_,
                                                size_t length) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_OPAQUE)) {
        anjay_ret_bytes_ctx_t *result =
                anjay_ret_bytes_begin(ctx->backend, length);
        adjust_errno(ctx, "ret_bytes");
        return result;
    }
    return NULL;
}

static int dynamic_ret_string(anjay_output_ctx_t *ctx_, const char *value) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_string",
                             anjay_ret_string(ctx->backend, value));
    }
    return -1;
}

static int dynamic_ret_i32(anjay_output_ctx_t *ctx_, int32_t value) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_i32",
                             anjay_ret_i32(ctx->backend, value));
    }
    return -1;
}

static int dynamic_ret_i64(anjay_output_ctx_t *ctx_, int64_t value) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_i64",
                             anjay_ret_i64(ctx->backend, value));
    }
    return -1;
}

static int dynamic_ret_float(anjay_output_ctx_t *ctx_, float value) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_float",
                             anjay_ret_float(ctx->backend, value));
    }
    return -1;
}

static int dynamic_ret_double(anjay_output_ctx_t *ctx_, double value) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_double",
                             anjay_ret_double(ctx->backend, value));
    }
    return -1;
}

static int dynamic_ret_bool(anjay_output_ctx_t *ctx_, bool value) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_bool",
                             anjay_ret_bool(ctx->backend, value));
    }
    return -1;
}

static int dynamic_ret_objlnk(anjay_output_ctx_t *ctx_,
                              anjay_oid_t oid, anjay_iid_t iid) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_PLAINTEXT)) {
        return process_errno(ctx, "ret_objlnk",
                             anjay_ret_objlnk(ctx->backend, oid, iid));
    }
    return -1;
}

static anjay_output_ctx_t *dynamic_ret_array_start(anjay_output_ctx_t *ctx_) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_TLV)) {
        anjay_output_ctx_t *result = anjay_ret_array_start(ctx->backend);
        adjust_errno(ctx, "ret_array_start");
        return result;
    }
    return NULL;
}

static anjay_output_ctx_t *dynamic_ret_object_start(anjay_output_ctx_t *ctx_) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ensure_backend(ctx, ANJAY_COAP_FORMAT_TLV)) {
        anjay_output_ctx_t *result = _anjay_output_object_start(ctx->backend);
        adjust_errno(ctx, "ret_object_start");
        return result;
    }
    return NULL;
}

static int dynamic_set_id(anjay_output_ctx_t *ctx_,
                          anjay_id_type_t type, uint16_t id) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;
    if (ctx->backend) {
        int result = _anjay_output_set_id(ctx->backend, type, id);
        if (result && !ctx->past_first_call
                && *ctx->errno_ptr == ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED) {
            // Ignore set_id fails before first ret_* call.
            // Opaque and Text output contexts do not support set_id,
            // but dm_read() calls it before each Resource.
            *ctx->errno_ptr = 0;
            return 0;
        }
        return result;
    } else {
        ctx->id_type = type;
        ctx->id = id;
        return 0;
    }
}

static int dynamic_close(anjay_output_ctx_t *ctx_) {
    dynamic_out_t *ctx = (dynamic_out_t *) ctx_;

    if (!ctx->backend) {
        return ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED;
    }
    return _anjay_output_ctx_destroy(&ctx->backend);
}

static const anjay_output_ctx_vtable_t DYNAMIC_OUT_VTABLE = {
    .errno_ptr = dynamic_errno_ptr,
    .bytes_begin = dynamic_ret_bytes,
    .string = dynamic_ret_string,
    .i32 = dynamic_ret_i32,
    .i64 = dynamic_ret_i64,
    .f32 = dynamic_ret_float,
    .f64 = dynamic_ret_double,
    .boolean = dynamic_ret_bool,
    .objlnk = dynamic_ret_objlnk,
    .array_start = dynamic_ret_array_start,
    .object_start = dynamic_ret_object_start,
    .set_id = dynamic_set_id,
    .close = dynamic_close
};

anjay_output_ctx_t *
_anjay_output_dynamic_create(avs_stream_abstract_t *stream,
                             int *errno_ptr,
                             anjay_msg_details_t *details_template) {
    dynamic_out_t *ctx = (dynamic_out_t *) calloc(1, sizeof(dynamic_out_t));
    if (!ctx) {
        return NULL;
    }
    ctx->vtable = &DYNAMIC_OUT_VTABLE;
    ctx->errno_ptr = errno_ptr;
    ctx->stream = stream;
    ctx->details = *details_template;
    ctx->id = -1;
    if (ctx->details.format != ANJAY_COAP_FORMAT_NONE
            && !ensure_backend(ctx, ctx->details.format)) {
        free(ctx);
        return NULL;
    }
    return (anjay_output_ctx_t *) ctx;
}

/////////////////////////////////////////////////////////////////////// DECODING

int _anjay_input_dynamic_create(anjay_input_ctx_t **out,
                                avs_stream_abstract_t **stream_ptr,
                                bool autoclose) {
    uint16_t format;
    int result = _anjay_coap_stream_get_content_format(*stream_ptr, &format);
    if (result) {
        return result;
    }
    switch (_anjay_translate_legacy_content_format(format)) {
    case ANJAY_COAP_FORMAT_PLAINTEXT:
        return _anjay_input_text_create(out, stream_ptr, autoclose);
    case ANJAY_COAP_FORMAT_TLV:
        return _anjay_input_tlv_create(out, stream_ptr, autoclose);
    case ANJAY_COAP_FORMAT_OPAQUE:
        return _anjay_input_opaque_create(out, stream_ptr, autoclose);
    default:
        return ANJAY_ERR_BAD_REQUEST;
    }
}

#ifdef ANJAY_TEST
#include "test/dynamic.c"
#endif
