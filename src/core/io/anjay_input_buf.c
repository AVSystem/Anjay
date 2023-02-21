/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_LWM2M11

#    include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

static int get_complete_value_or_fail(anjay_unlocked_input_ctx_t *ctx_,
                                      void *out_value,
                                      size_t out_value_size) {
    anjay_input_buf_ctx_t *ctx = (anjay_input_buf_ctx_t *) ctx_;

    if (ctx->msg_finished) {
        return -1;
    }

    size_t bytes_read;
    if (avs_is_err(avs_stream_read(ctx->stream,
                                   &bytes_read,
                                   &ctx->msg_finished,
                                   out_value,
                                   out_value_size))
            || bytes_read != out_value_size || !ctx->msg_finished) {
        // Set it anyway to prevent reading from stream again
        ctx->msg_finished = true;
        return -1;
    }

    return 0;
}

static int input_buf_get_integer(anjay_unlocked_input_ctx_t *ctx,
                                 int64_t *value) {
    return get_complete_value_or_fail(ctx, value, sizeof(*value));
}

static int input_buf_get_uint(anjay_unlocked_input_ctx_t *ctx,
                              uint64_t *value) {
    return get_complete_value_or_fail(ctx, value, sizeof(*value));
}

static int input_buf_get_path(anjay_unlocked_input_ctx_t *ctx_,
                              anjay_uri_path_t *out_path,
                              bool *out_is_array) {
    anjay_input_buf_ctx_t *ctx = (anjay_input_buf_ctx_t *) ctx_;
    if (ctx->msg_finished) {
        return ANJAY_GET_PATH_END;
    }
    if (!_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_is_array = false;
    *out_path = ctx->path;
    return 0;
}

static int noop(anjay_unlocked_input_ctx_t *ctx) {
    (void) ctx;
    return 0;
}

static const anjay_input_ctx_vtable_t BUF_IN_VTABLE = {
    .integer = input_buf_get_integer,
    .uint = input_buf_get_uint,
    .get_path = input_buf_get_path,
    .next_entry = noop,
    .close = noop
};

anjay_input_buf_ctx_t _anjay_input_buf_ctx_init(avs_stream_t *stream,
                                                anjay_uri_path_t *path) {
    return (anjay_input_buf_ctx_t) {
        .base = {
            .vtable = &BUF_IN_VTABLE
        },
        .stream = stream,
        .path = *path
    };
}

#endif // ANJAY_WITH_LWM2M11
