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
#include <avsystem/commons/avs_base64.h>
#include <avsystem/commons/avs_stream.h>

#include <anjay/core.h>

#include "../anjay_utils_private.h"
#include "anjay_base64_out.h"
#include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

typedef struct base64_ret_bytes_ctx {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    avs_stream_t *stream;
    avs_base64_config_t config;
    uint8_t bytes_cached[2];
    size_t num_bytes_cached;
    size_t num_bytes_left;
} base64_ret_bytes_ctx_t;

#define TEXT_CHUNK_SIZE (3 * 64u)
AVS_STATIC_ASSERT(TEXT_CHUNK_SIZE % 3 == 0, chunk_must_be_a_multiple_of_3);

static int base64_ret_encode_and_write(base64_ret_bytes_ctx_t *ctx,
                                       const uint8_t *buffer,
                                       const size_t buffer_size) {
    if (!buffer_size) {
        return 0;
    }
    char encoded[4 * (TEXT_CHUNK_SIZE / 3) + 1];
    size_t encoded_size;
    if (ctx->config.padding_char) {
        encoded_size = avs_base64_encoded_size(buffer_size);
    } else {
        encoded_size = avs_base64_encoded_size_without_padding(buffer_size);
    }
    assert(encoded_size <= sizeof(encoded));
    int retval = avs_base64_encode_custom(encoded, encoded_size, buffer,
                                          buffer_size, ctx->config);
    if (retval) {
        return retval;
    }
    if (avs_is_err(avs_stream_write(ctx->stream, encoded, encoded_size - 1))) {
        return -1;
    }
    return 0;
}

static int base64_ret_bytes_flush(base64_ret_bytes_ctx_t *ctx,
                                  const uint8_t **dataptr,
                                  size_t bytes_to_write) {
    uint8_t chunk[TEXT_CHUNK_SIZE];
    while (bytes_to_write > 0) {
        memcpy(chunk, ctx->bytes_cached, ctx->num_bytes_cached);
        size_t new_bytes_written =
                AVS_MIN(TEXT_CHUNK_SIZE - ctx->num_bytes_cached,
                        bytes_to_write);
        assert(new_bytes_written <= TEXT_CHUNK_SIZE);
        memcpy(&chunk[ctx->num_bytes_cached], *dataptr, new_bytes_written);
        *dataptr += new_bytes_written;

        int retval;
        if ((retval = base64_ret_encode_and_write(
                     ctx, chunk, new_bytes_written + ctx->num_bytes_cached))) {
            return retval;
        }
        bytes_to_write -= new_bytes_written;
        ctx->num_bytes_left -= new_bytes_written;
        ctx->num_bytes_cached = 0;
    }
    return 0;
}

static int base64_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                   const void *data,
                                   size_t size) {
    base64_ret_bytes_ctx_t *ctx = (base64_ret_bytes_ctx_t *) ctx_;
    if (size > ctx->num_bytes_left) {
        return -1;
    }
    const uint8_t *dataptr = (const uint8_t *) data;
    size_t bytes_to_store;
    if (size + ctx->num_bytes_cached < 3) {
        bytes_to_store = size;
    } else {
        bytes_to_store = (ctx->num_bytes_cached + size) % 3;
    }
    assert(bytes_to_store <= 2);

    int retval = base64_ret_bytes_flush(ctx, &dataptr, size - bytes_to_store);
    if (retval) {
        return retval;
    }
    assert(ctx->num_bytes_cached + bytes_to_store <= sizeof(ctx->bytes_cached));
    memcpy(&ctx->bytes_cached[ctx->num_bytes_cached], dataptr, bytes_to_store);
    ctx->num_bytes_cached += bytes_to_store;
    ctx->num_bytes_left -= bytes_to_store;
    return 0;
}

static const anjay_ret_bytes_ctx_vtable_t BASE64_OUT_BYTES_VTABLE = {
    .append = base64_ret_bytes_append
};

anjay_ret_bytes_ctx_t *_anjay_base64_ret_bytes_ctx_new(
        avs_stream_t *stream, avs_base64_config_t config, size_t length) {
    base64_ret_bytes_ctx_t *ctx = (base64_ret_bytes_ctx_t *) avs_calloc(
            1, sizeof(base64_ret_bytes_ctx_t));
    if (ctx) {
        ctx->vtable = &BASE64_OUT_BYTES_VTABLE;
        ctx->stream = stream;
        ctx->config = config;
        ctx->num_bytes_left = length;
    }
    return (anjay_ret_bytes_ctx_t *) ctx;
}

int _anjay_base64_ret_bytes_ctx_close(anjay_ret_bytes_ctx_t *ctx_) {
    base64_ret_bytes_ctx_t *ctx = (base64_ret_bytes_ctx_t *) ctx_;
    if (ctx->num_bytes_left != 0) {
        /* Some bytes were not written as we have expected */
        return 0;
    }
    return base64_ret_encode_and_write(ctx, ctx->bytes_cached,
                                       ctx->num_bytes_cached);
}

void _anjay_base64_ret_bytes_ctx_delete(anjay_ret_bytes_ctx_t **ctx_) {
    if (!ctx_ || !*ctx_) {
        return;
    }
    base64_ret_bytes_ctx_t *ctx = (base64_ret_bytes_ctx_t *) *ctx_;
    assert(ctx->vtable == &BASE64_OUT_BYTES_VTABLE);
    avs_free(ctx);
    *ctx_ = NULL;
}
