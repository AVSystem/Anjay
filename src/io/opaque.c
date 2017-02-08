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

#include <stdlib.h>

#include <avsystem/commons/stream.h>

#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    avs_stream_abstract_t *stream;
    size_t bytes_left;
} opaque_bytes_t;

static int opaque_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                   const void *data,
                                   size_t length) {
    opaque_bytes_t *ctx = (opaque_bytes_t *) ctx_;
    int retval = 0;
    if (length) {
        if (length > ctx->bytes_left) {
            retval = -1;
        } else if (!(retval = avs_stream_write(ctx->stream, data, length))) {
            ctx->bytes_left -= length;
        }
    }
    return retval;
}

static const anjay_ret_bytes_ctx_vtable_t OPAQUE_BYTES_VTABLE = {
    .append = opaque_ret_bytes_append
};

typedef struct {
    const anjay_output_ctx_vtable_t *vtable;
    int *errno_ptr;
    bool initialized;
    opaque_bytes_t bytes;
} opaque_out_t;

static int *opaque_errno_ptr(anjay_output_ctx_t *ctx) {
    return ((opaque_out_t *) ctx)->errno_ptr;
}

static anjay_ret_bytes_ctx_t *opaque_ret_bytes(anjay_output_ctx_t *ctx_,
                                               size_t length) {
    opaque_out_t *ctx = (opaque_out_t *) ctx_;
    if (!ctx->initialized) {
        ctx->initialized = true;
        ctx->bytes.bytes_left = length;
        return (anjay_ret_bytes_ctx_t *) &ctx->bytes;
    }
    return NULL;
}

static const anjay_output_ctx_vtable_t OPAQUE_OUT_VTABLE = {
    .errno_ptr = opaque_errno_ptr,
    .bytes_begin = opaque_ret_bytes
};

anjay_output_ctx_t *
_anjay_output_opaque_create(avs_stream_abstract_t *stream,
                            int *errno_ptr,
                            anjay_msg_details_t *inout_details) {
    opaque_out_t *ctx = (opaque_out_t *) calloc(1, sizeof(opaque_out_t));
    if (ctx && ((*errno_ptr = _anjay_handle_requested_format(
                    &inout_details->format, ANJAY_COAP_FORMAT_OPAQUE))
            || _anjay_coap_stream_setup_response(stream, inout_details))) {
        free(ctx);
        return NULL;
    }
    if (ctx) {
        ctx->vtable = &OPAQUE_OUT_VTABLE;
        ctx->errno_ptr = errno_ptr;
        ctx->bytes.vtable = &OPAQUE_BYTES_VTABLE;
        ctx->bytes.stream = stream;
    }
    return (anjay_output_ctx_t *) ctx;
}

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    avs_stream_abstract_t *stream;
    bool autoclose;
} opaque_in_t;

static int opaque_get_some_bytes(anjay_input_ctx_t *ctx,
                                 size_t *out_bytes_read,
                                 bool *out_message_finished,
                                 void *out_buf,
                                 size_t buf_size) {
    char message_finished;
    int retval = avs_stream_read(((opaque_in_t *) ctx)->stream, out_bytes_read,
                                 &message_finished, out_buf, buf_size);
    *out_message_finished = message_finished;
    return retval;
}

static int opaque_in_close(anjay_input_ctx_t *ctx_) {
    opaque_in_t *ctx = (opaque_in_t *) ctx_;
    if (ctx->autoclose) {
        avs_stream_cleanup(&ctx->stream);
    }
    return 0;
}

static const anjay_input_ctx_vtable_t OPAQUE_IN_VTABLE = {
    .some_bytes = opaque_get_some_bytes,
    .close = opaque_in_close
};

int _anjay_input_opaque_create(anjay_input_ctx_t **out,
                               avs_stream_abstract_t **stream_ptr,
                               bool autoclose) {
    opaque_in_t *ctx = (opaque_in_t *) calloc(1, sizeof(opaque_in_t));
    *out = (anjay_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }

    ctx->vtable = &OPAQUE_IN_VTABLE;
    ctx->stream = *stream_ptr;
    if (autoclose) {
        ctx->autoclose = true;
        *stream_ptr = NULL;
    }
    return 0;
}
