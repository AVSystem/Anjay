/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#include <stdlib.h>

#include <avsystem/commons/avs_stream.h>

#include "../coap/anjay_content_format.h"

#include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    avs_stream_t *stream;
    size_t bytes_left;
} opaque_bytes_t;

static int opaque_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                   const void *data,
                                   size_t length) {
    opaque_bytes_t *ctx = (opaque_bytes_t *) ctx_;
    if (!length) {
        return 0;
    }
    if (length <= ctx->bytes_left
            && avs_is_ok(avs_stream_write(ctx->stream, data, length))) {
        ctx->bytes_left -= length;
        return 0;
    }
    return -1;
}

static const anjay_ret_bytes_ctx_vtable_t OPAQUE_BYTES_VTABLE = {
    .append = opaque_ret_bytes_append
};

typedef enum {
    STATE_INITIAL,
    STATE_PATH_SET,
    STATE_RETURNING
} opaque_out_state_t;

typedef struct {
    anjay_output_ctx_t base;
    opaque_out_state_t state;
    opaque_bytes_t bytes;
} opaque_out_t;

static int opaque_ret_bytes(anjay_output_ctx_t *ctx_,
                            size_t length,
                            anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    opaque_out_t *ctx = (opaque_out_t *) ctx_;
    if (ctx->state == STATE_PATH_SET) {
        ctx->state = STATE_RETURNING;
        ctx->bytes.bytes_left = length;
        *out_bytes_ctx = (anjay_ret_bytes_ctx_t *) &ctx->bytes;
        return 0;
    }
    return -1;
}

static int opaque_set_path(anjay_output_ctx_t *ctx_,
                           const anjay_uri_path_t *path) {
    opaque_out_t *ctx = (opaque_out_t *) ctx_;
    if (ctx->state == STATE_PATH_SET) {
        return -1;
    } else if (ctx->state != STATE_INITIAL
               || !_anjay_uri_path_has(path, ANJAY_ID_RID)) {
        return ANJAY_OUTCTXERR_FORMAT_MISMATCH;
    }
    ctx->state = STATE_PATH_SET;
    return 0;
}

static int opaque_clear_path(anjay_output_ctx_t *ctx_) {
    opaque_out_t *ctx = (opaque_out_t *) ctx_;
    if (ctx->state != STATE_PATH_SET) {
        return -1;
    }
    ctx->state = STATE_INITIAL;
    return 0;
}

static int opaque_ret_close(anjay_output_ctx_t *ctx_) {
    opaque_out_t *ctx = (opaque_out_t *) ctx_;
    return ctx->state == STATE_RETURNING ? 0
                                         : ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED;
}

static const anjay_output_ctx_vtable_t OPAQUE_OUT_VTABLE = {
    .bytes_begin = opaque_ret_bytes,
    .set_path = opaque_set_path,
    .clear_path = opaque_clear_path,
    .close = opaque_ret_close
};

anjay_output_ctx_t *_anjay_output_opaque_create(avs_stream_t *stream) {
    opaque_out_t *ctx = (opaque_out_t *) avs_calloc(1, sizeof(opaque_out_t));
    if (ctx) {
        ctx->base.vtable = &OPAQUE_OUT_VTABLE;
        ctx->bytes.vtable = &OPAQUE_BYTES_VTABLE;
        ctx->bytes.stream = stream;
    }
    return (anjay_output_ctx_t *) ctx;
}

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    avs_stream_t *stream;
    bool msg_finished;

    anjay_uri_path_t request_uri;
} opaque_in_t;

static int opaque_get_some_bytes(anjay_input_ctx_t *ctx,
                                 size_t *out_bytes_read,
                                 bool *out_message_finished,
                                 void *out_buf,
                                 size_t buf_size) {
    avs_error_t err =
            avs_stream_read(((opaque_in_t *) ctx)->stream, out_bytes_read,
                            out_message_finished, out_buf, buf_size);
    ((opaque_in_t *) ctx)->msg_finished = *out_message_finished;
    return avs_is_ok(err) ? 0 : -1;
}

static int opaque_in_close(anjay_input_ctx_t *ctx_) {
    (void) ctx_;
    return 0;
}

static int opaque_in_get_path(anjay_input_ctx_t *ctx_,
                              anjay_uri_path_t *out_path,
                              bool *out_is_array) {
    opaque_in_t *ctx = (opaque_in_t *) ctx_;
    if (ctx->msg_finished) {
        return ANJAY_GET_PATH_END;
    }
    if (!_anjay_uri_path_has(&ctx->request_uri, ANJAY_ID_RID)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_is_array = false;
    *out_path = ctx->request_uri;
    return 0;
}

static int opaque_in_next_entry(anjay_input_ctx_t *ctx) {
    (void) ctx;
    return 0;
}

static int bad_request() {
    return ANJAY_ERR_BAD_REQUEST;
}

static const anjay_input_ctx_vtable_t OPAQUE_IN_VTABLE = {
    .some_bytes = opaque_get_some_bytes,
    .close = opaque_in_close,
    .string = (anjay_input_ctx_string_t) bad_request,
    .integer = (anjay_input_ctx_integer_t) bad_request,
    .floating = (anjay_input_ctx_floating_t) bad_request,
    .boolean = (anjay_input_ctx_boolean_t) bad_request,
    .objlnk = (anjay_input_ctx_objlnk_t) bad_request,
    .get_path = opaque_in_get_path,
    .update_root_path = (anjay_input_ctx_update_root_path_t) bad_request,
    .next_entry = opaque_in_next_entry
};

int _anjay_input_opaque_create(anjay_input_ctx_t **out,
                               avs_stream_t **stream_ptr,
                               const anjay_uri_path_t *request_uri) {
    opaque_in_t *ctx = (opaque_in_t *) avs_calloc(1, sizeof(opaque_in_t));
    *out = (anjay_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }

    ctx->vtable = &OPAQUE_IN_VTABLE;
    ctx->stream = *stream_ptr;
    ctx->request_uri = request_uri ? *request_uri : MAKE_ROOT_PATH();

    return 0;
}
