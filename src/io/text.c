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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <avsystem/commons/stream.h>
#include <avsystem/commons/base64.h>

#include <anjay/anjay.h>

#include "../utils.h"
#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

/////////////////////////////////////////////////////////////////////// ENCODING

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    uint8_t bytes_cached[2];
    size_t num_bytes_cached;
    size_t num_bytes_left;
    bool initialized;
} text_out_bytes_ctx_t;

typedef struct {
    const anjay_output_ctx_vtable_t *vtable;
    text_out_bytes_ctx_t bytes;
    int *errno_ptr;
    avs_stream_abstract_t *stream;
    bool finished;
} text_out_t;

static int *text_errno_ptr(anjay_output_ctx_t *ctx) {
    return ((text_out_t *) ctx)->errno_ptr;
}

#define TEXT_CHUNK_SIZE 3 * 64u
AVS_STATIC_ASSERT(TEXT_CHUNK_SIZE % 3 == 0, chunk_must_be_a_multiple_of_3);

static int text_out_encode_and_write(text_out_bytes_ctx_t *ctx,
                                     const uint8_t *buffer,
                                     const size_t buffer_size) {
    if (!buffer_size) {
        return 0;
    }
    size_t encoded_size = avs_base64_encoded_size(buffer_size);
    assert(encoded_size <= 4 * (TEXT_CHUNK_SIZE / 3) + 1);
    char encoded[encoded_size];
    int retval = avs_base64_encode(encoded, sizeof(encoded), buffer, buffer_size);
    if (retval) {
        return retval;
    }
    text_out_t *parent = AVS_CONTAINER_OF(ctx, text_out_t, bytes);
    return avs_stream_write(parent->stream, encoded, sizeof(encoded) - 1);
}

static int text_out_bytes_flush(text_out_bytes_ctx_t *ctx,
                                const uint8_t **dataptr,
                                size_t bytes_to_write) {
    uint8_t chunk[TEXT_CHUNK_SIZE];
    while (bytes_to_write > 0) {
        memcpy(chunk, ctx->bytes_cached, ctx->num_bytes_cached);
        size_t new_bytes_written =
                ANJAY_MIN(TEXT_CHUNK_SIZE - ctx->num_bytes_cached,
                          bytes_to_write);
        assert(new_bytes_written <= TEXT_CHUNK_SIZE);
        memcpy(&chunk[ctx->num_bytes_cached], *dataptr, new_bytes_written);
        *dataptr += new_bytes_written;

        int retval;
        if ((retval = text_out_encode_and_write(
                     ctx, chunk, new_bytes_written + ctx->num_bytes_cached))) {
            return retval;
        }
        bytes_to_write -= new_bytes_written;
        ctx->num_bytes_left -= new_bytes_written;
        ctx->num_bytes_cached = 0;
    }
    return 0;
}

static int text_out_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t size) {
    text_out_bytes_ctx_t *ctx = (text_out_bytes_ctx_t *) ctx_;
    if (!ctx->initialized || size > ctx->num_bytes_left) {
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

    int retval = text_out_bytes_flush(ctx, &dataptr, size - bytes_to_store);
    if (retval) {
        return retval;
    }
    assert(ctx->num_bytes_cached + bytes_to_store
               <= sizeof(ctx->bytes_cached));
    memcpy(&ctx->bytes_cached[ctx->num_bytes_cached], dataptr, bytes_to_store);
    ctx->num_bytes_cached += bytes_to_store;
    ctx->num_bytes_left -= bytes_to_store;
    return 0;
}

static const anjay_ret_bytes_ctx_vtable_t TEXT_OUT_BYTES_VTABLE = {
    .append = text_out_bytes_append
};

static anjay_ret_bytes_ctx_t *text_ret_bytes_begin(anjay_output_ctx_t *ctx_,
                                                   size_t length) {
    (void) length;
    text_out_t *ctx = (text_out_t *) ctx_;
    if (!ctx->bytes.initialized) {
        ctx->bytes.vtable = &TEXT_OUT_BYTES_VTABLE;
        ctx->bytes.num_bytes_cached = 0;
        ctx->bytes.num_bytes_left = length;
        ctx->bytes.initialized = true;
        return (anjay_ret_bytes_ctx_t *) &ctx->bytes;
    }
    return NULL;
}

static int text_ret_string(anjay_output_ctx_t *ctx_, const char *value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes.initialized) {
        return -1;
    }

    int retval = -1;
    if (!ctx->finished
            && !(retval = avs_stream_write(ctx->stream, value, strlen(value)))) {
        ctx->finished = true;
    }
    return retval;
}

static int text_ret_i32(anjay_output_ctx_t *ctx_, int32_t value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes.initialized) {
        return -1;
    }

    int retval = -1;
    if (!ctx->finished
            && !(retval = avs_stream_write_f(ctx->stream, "%" PRId32, value))) {
        ctx->finished = true;
    }
    return retval;
}

static int text_ret_i64(anjay_output_ctx_t *ctx_, int64_t value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes.initialized) {
        return -1;
    }

    int retval = -1;
    if (!ctx->finished
            && !(retval = avs_stream_write_f(ctx->stream, "%" PRId64, value))) {
        ctx->finished = true;
    }
    return retval;
}

static inline int text_ret_floating_point(text_out_t *ctx, double value,
                                          int precision) {
    if (ctx->bytes.initialized) {
        return -1;
    }
    int retval = -1;
    // FIXME: The spec calls for a "decimal" representation, which, in my
    // understanding, excludes exponential representation.
    // As printing floating-point numbers in C as pure decimal with sane
    // precision is tricky, let's take the spec a bit loosely for now.
    if (!ctx->finished
            && !(retval = avs_stream_write_f(ctx->stream, "%.*g",
                                             precision, value))) {
        ctx->finished = true;
    }
    return retval;
}

static int text_ret_float(anjay_output_ctx_t *ctx, float value) {
    return text_ret_floating_point((text_out_t *) ctx, value, 9);
}

static int text_ret_double(anjay_output_ctx_t *ctx, double value) {
    return text_ret_floating_point((text_out_t *) ctx, value, 17);
}

static int text_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    return text_ret_i32(ctx, value);
}

static int text_ret_objlnk(anjay_output_ctx_t *ctx_,
                           anjay_oid_t oid, anjay_iid_t iid) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes.initialized) {
        return -1;
    }
    int retval = -1;
    if (!ctx->finished
            && !(retval = avs_stream_write_f(ctx->stream,
                                             "%" PRIu16 ":%" PRIu16,
                                             oid, iid))) {
        ctx->finished = true;
    }
    return retval;
}

static int text_ret_close(anjay_output_ctx_t *ctx_) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes.initialized) {
        ctx->bytes.initialized = false;
        if (ctx->bytes.num_bytes_left != 0) {
            return -1;
        }
        return text_out_encode_and_write(&ctx->bytes, ctx->bytes.bytes_cached,
                                         ctx->bytes.num_bytes_cached);
    }
    return 0;
}

static const anjay_output_ctx_vtable_t TEXT_OUT_VTABLE = {
    .bytes_begin = text_ret_bytes_begin,
    .errno_ptr = text_errno_ptr,
    .string = text_ret_string,
    .i32 = text_ret_i32,
    .i64 = text_ret_i64,
    .f32 = text_ret_float,
    .f64 = text_ret_double,
    .boolean = text_ret_bool,
    .objlnk = text_ret_objlnk,
    .close = text_ret_close
};

anjay_output_ctx_t *
_anjay_output_text_create(avs_stream_abstract_t *stream,
                          int *errno_ptr,
                          anjay_msg_details_t *inout_details) {
    text_out_t *ctx = (text_out_t *) calloc(1, sizeof(text_out_t));
    if (ctx && ((*errno_ptr = _anjay_handle_requested_format(
                    &inout_details->format, ANJAY_COAP_FORMAT_PLAINTEXT))
            || _anjay_coap_stream_setup_response(stream, inout_details))) {
        free(ctx);
        return NULL;
    }
    if (ctx) {
        ctx->vtable = &TEXT_OUT_VTABLE;
        ctx->errno_ptr = errno_ptr;
        ctx->stream = stream;
    }
    return (anjay_output_ctx_t *) ctx;
}

/////////////////////////////////////////////////////////////////////// DECODING

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    avs_stream_abstract_t *stream;
    bool autoclose;
    // if bytes_mode == true, then only raw bytes can be read from the context
    // and any other reading operation will fail
    bool bytes_mode;
    uint8_t bytes_cached[3];
    size_t num_bytes_cached;
    char msg_finished;
} text_in_t;

static int has_valid_padding(const char *buffer,
                             size_t size,
                             bool msg_finished) {
    const char *last = buffer + size;
    /* Note: buffer is size+1 in length. Last byte is a NULL terminator though. */
    assert(!*last);
    return last - 1 >= buffer && *(last - 1) == '=' && !msg_finished ? -1 : 0;
}

static void text_get_some_bytes_cache_flush(text_in_t *ctx,
                                            uint8_t **out_buf,
                                            size_t *buf_size) {
    size_t bytes_to_copy = ANJAY_MIN(ctx->num_bytes_cached, *buf_size);
    memcpy(*out_buf, ctx->bytes_cached, bytes_to_copy);
    memmove(ctx->bytes_cached, ctx->bytes_cached + bytes_to_copy,
            sizeof(ctx->bytes_cached) - bytes_to_copy);
    ctx->num_bytes_cached -= bytes_to_copy;
    *buf_size -= bytes_to_copy;
    *out_buf += bytes_to_copy;
}

static int text_get_some_bytes(anjay_input_ctx_t *ctx_,
                               size_t *out_bytes_read,
                               bool *out_msg_finished,
                               void *out_buf,
                               size_t buf_size) {
    text_in_t *ctx = (text_in_t *) ctx_;
    ctx->bytes_mode = true;
    uint8_t *current = (uint8_t *) out_buf;
    *out_msg_finished = false;
    *out_bytes_read = 0;

    text_get_some_bytes_cache_flush(ctx, &current, &buf_size);
    // 4bytes + null terminator
    char encoded[5];
    size_t stream_bytes_read;
    char stream_msg_finished = 0;

    while (buf_size > 0) {
        if (avs_stream_read(ctx->stream, &stream_bytes_read, &stream_msg_finished,
                            encoded, sizeof(encoded) - 1)) {
            return -1;
        }
        encoded[stream_bytes_read] = '\0';
        if (stream_bytes_read % 4) {
            return -1;
        }
        if (has_valid_padding(encoded, stream_bytes_read,
                              !!stream_msg_finished)) {
            return -1;
        }
        assert(ctx->num_bytes_cached == 0);
        ssize_t num_decoded =
                avs_base64_decode_strict((uint8_t *) ctx->bytes_cached,
                                         sizeof(ctx->bytes_cached), encoded);
        if (num_decoded < 0) {
            return (int) num_decoded;
        }
        ctx->num_bytes_cached = (size_t) num_decoded;
        text_get_some_bytes_cache_flush(ctx, &current, &buf_size);
        if (stream_msg_finished) {
            break;
        }
    }
    ctx->msg_finished = !!stream_msg_finished;
    *out_msg_finished = ctx->msg_finished && !ctx->num_bytes_cached;
    *out_bytes_read = (size_t) (current - (uint8_t *) out_buf);
    return 0;
}

static int text_get_string(anjay_input_ctx_t *ctx,
                           char *out_buf,
                           size_t buf_size) {
    if (!buf_size || ((text_in_t *) ctx)->bytes_mode) {
        return -1;
    }
    char message_finished = 0;
    char *ptr = out_buf;
    char *endptr = out_buf + (buf_size - 1);
    do {
        size_t bytes_read = 0;
        int retval = avs_stream_read(((text_in_t *) ctx)->stream,
                                     &bytes_read, &message_finished, ptr,
                                     (size_t) (endptr - ptr));
        if (retval) {
            return retval;
        }
        ptr += bytes_read;
    } while (!message_finished && ptr < endptr);
    *ptr = '\0';
    return message_finished ? 0 : ANJAY_BUFFER_TOO_SHORT;
}

#define DEF_GETNUM(Type, Bufsize, ...) \
static int safe_strto##Type (const char *in, TYPE##Type *value) { \
    char *endptr = NULL; \
    if (!*in || isspace(*in)) { \
        return -1; \
    } \
    errno = 0; \
    TYPE##Type tmp = strto##Type (in, &endptr __VA_ARGS__); \
    if (errno || !endptr || *endptr) { \
        return -1; \
    } \
    *value = tmp; \
    return 0; \
} \
\
static int text_get_##Type (anjay_input_ctx_t *ctx, TYPE##Type *value) { \
    char buf[Bufsize]; \
    int retval = anjay_get_string(ctx, buf, sizeof(buf)); \
    if (retval) { \
        return retval; \
    } \
    return safe_strto##Type (buf, value); \
}

#if LLONG_MAX == INT64_MAX
#define TYPEll int64_t
#else
#define TYPEll long long
#endif

#define TYPEf float
#define TYPEd double

DEF_GETNUM(ll, 32,, 0)
DEF_GETNUM(f, ANJAY_MAX_FLOAT_STRING_SIZE, )
DEF_GETNUM(d, ANJAY_MAX_DOUBLE_STRING_SIZE, )

int _anjay_safe_strtoll(const char *in, long long *value) {
    TYPEll out;
    if (safe_strtoll(in, &out)) {
        return -1;
    }
    *value = (long long)out;
    return 0;
}

int _anjay_safe_strtod(const char *in, double *value) {
    return safe_strtod(in, value);
}

#define DEF_GETI(Bits) \
static int text_get_i##Bits (anjay_input_ctx_t *ctx, int##Bits##_t *value) { \
    TYPEll ll_value; \
    int retval = text_get_ll(ctx, &ll_value); \
    if (retval) { \
        return retval; \
    } \
    if (ll_value < INT##Bits##_MIN || ll_value > INT##Bits##_MAX) { \
        return -1; \
    } \
    *value = (int##Bits##_t) ll_value; \
    return 0; \
}

#if LLONG_MAX == INT64_MAX
#define text_get_i64 text_get_ll
#else
DEF_GETI(64)
#endif

DEF_GETI(32)

static int text_get_bool(anjay_input_ctx_t *ctx, bool *value) {
    TYPEll ll_value;
    int retval = text_get_ll(ctx, &ll_value);
    if (retval) {
        return retval;
    }
    switch (ll_value) {
    case 0:
        *value = false;
        return 0;
    case 1:
        *value = true;
        return 0;
    default:
        return -1;
    }
}

static int text_get_objlnk(anjay_input_ctx_t *ctx,
                           anjay_oid_t *out_oid, anjay_iid_t *out_iid) {
    char buf[16];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return retval;
    }
    char *colon = strchr(buf, ':');
    if (!colon) {
        return -1;
    }
    *colon = '\0';
    TYPEll oid, iid;
    if (!(retval = (safe_strtoll(buf, &oid)
            || safe_strtoll(colon + 1, &iid)) ? -1 : 0)) {
        if (oid >= 0 && oid <= UINT16_MAX
                && iid >= 0 && iid <= UINT16_MAX) {
            *out_oid = (anjay_oid_t) oid;
            *out_iid = (anjay_iid_t) iid;
        } else {
            retval = -1;
        }
    }
    return retval;
}

static int text_in_close(anjay_input_ctx_t *ctx_) {
    text_in_t *ctx = (text_in_t *) ctx_;
    if (ctx->autoclose) {
        avs_stream_cleanup(&ctx->stream);
    }
    return 0;
}

static const anjay_input_ctx_vtable_t TEXT_IN_VTABLE = {
    .some_bytes = text_get_some_bytes,
    .string = text_get_string,
    .i32 = text_get_i32,
    .i64 = text_get_i64,
    .f32 = text_get_f,
    .f64 = text_get_d,
    .boolean = text_get_bool,
    .objlnk = text_get_objlnk,
    .close = text_in_close
};

int _anjay_input_text_create(anjay_input_ctx_t **out,
                             avs_stream_abstract_t **stream_ptr,
                             bool autoclose) {
    text_in_t *ctx = (text_in_t *) calloc(1, sizeof(text_in_t));
    *out = (anjay_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }

    ctx->vtable = &TEXT_IN_VTABLE;
    ctx->stream = *stream_ptr;
    if (autoclose) {
        ctx->autoclose = true;
        *stream_ptr = NULL;
    }
    return 0;
}

#ifdef ANJAY_TEST
#include "test/text.c"
#endif
