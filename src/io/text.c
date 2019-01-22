/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <avsystem/commons/base64.h>
#include <avsystem/commons/stream.h>

#include <anjay/core.h>

#include "../coap/content_format.h"
#include "../utils_core.h"
#include "base64_out.h"
#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

/////////////////////////////////////////////////////////////////////// ENCODING

typedef struct {
    const anjay_output_ctx_vtable_t *vtable;
    anjay_ret_bytes_ctx_t *bytes;
    int *errno_ptr;
    avs_stream_abstract_t *stream;
    bool finished;
} text_out_t;

static int *text_errno_ptr(anjay_output_ctx_t *ctx) {
    return ((text_out_t *) ctx)->errno_ptr;
}

static anjay_ret_bytes_ctx_t *text_ret_bytes_begin(anjay_output_ctx_t *ctx_,
                                                   size_t length) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return NULL;
    }
    ctx->bytes = _anjay_base64_ret_bytes_ctx_new(ctx->stream, length);
    return ctx->bytes;
}

static int text_ret_string(anjay_output_ctx_t *ctx_, const char *value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return -1;
    }

    int retval = -1;
    if (!ctx->finished
            && !(retval =
                         avs_stream_write(ctx->stream, value, strlen(value)))) {
        ctx->finished = true;
    }
    return retval;
}

static int text_ret_i32(anjay_output_ctx_t *ctx_, int32_t value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
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
    if (ctx->bytes) {
        return -1;
    }

    int retval = -1;
    if (!ctx->finished
            && !(retval = avs_stream_write_f(ctx->stream, "%" PRId64, value))) {
        ctx->finished = true;
    }
    return retval;
}

static inline int
text_ret_floating_point(text_out_t *ctx, double value, int precision) {
    if (ctx->bytes) {
        return -1;
    }
    int retval = -1;
    // FIXME: The spec calls for a "decimal" representation, which, in my
    // understanding, excludes exponential representation.
    // As printing floating-point numbers in C as pure decimal with sane
    // precision is tricky, let's take the spec a bit loosely for now.
    if (!ctx->finished
            && !(retval = avs_stream_write_f(ctx->stream, "%.*g", precision,
                                             value))) {
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

static int
text_ret_objlnk(anjay_output_ctx_t *ctx_, anjay_oid_t oid, anjay_iid_t iid) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return -1;
    }
    int retval = -1;
    if (!ctx->finished
            && !(retval = avs_stream_write_f(
                         ctx->stream, "%" PRIu16 ":%" PRIu16, oid, iid))) {
        ctx->finished = true;
    }
    return retval;
}

static int text_ret_close(anjay_output_ctx_t *ctx_) {
    text_out_t *ctx = (text_out_t *) ctx_;
    int result = 0;
    if (ctx->bytes) {
        result = _anjay_base64_ret_bytes_ctx_close(ctx->bytes);
        _anjay_base64_ret_bytes_ctx_delete(&ctx->bytes);
    }
    return result;
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
    text_out_t *ctx = (text_out_t *) avs_calloc(1, sizeof(text_out_t));
    if (ctx
            && ((*errno_ptr = _anjay_handle_requested_format(
                         &inout_details->format, ANJAY_COAP_FORMAT_PLAINTEXT))
                || _anjay_coap_stream_setup_response(stream, inout_details))) {
        avs_free(ctx);
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

static int
has_valid_padding(const char *buffer, size_t size, bool msg_finished) {
    const char *last = buffer + size;
    // Note: buffer is size+1 in length. Last byte is a NULL terminator though.
    assert(!*last);
    return last - 1 >= buffer && *(last - 1) == '=' && !msg_finished ? -1 : 0;
}

static void text_get_some_bytes_cache_flush(text_in_t *ctx,
                                            uint8_t **out_buf,
                                            size_t *buf_size) {
    size_t bytes_to_copy = AVS_MIN(ctx->num_bytes_cached, *buf_size);
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
        if (avs_stream_read(ctx->stream, &stream_bytes_read,
                            &stream_msg_finished, encoded,
                            sizeof(encoded) - 1)) {
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

static int
text_get_string(anjay_input_ctx_t *ctx, char *out_buf, size_t buf_size) {
    if (!buf_size || ((text_in_t *) ctx)->bytes_mode) {
        return -1;
    }
    char message_finished = 0;
    char *ptr = out_buf;
    char *endptr = out_buf + (buf_size - 1);
    do {
        size_t bytes_read = 0;
        int retval = avs_stream_read(((text_in_t *) ctx)->stream, &bytes_read,
                                     &message_finished, ptr,
                                     (size_t) (endptr - ptr));
        if (retval) {
            return retval;
        }
        ptr += bytes_read;
    } while (!message_finished && ptr < endptr);
    *ptr = '\0';
    return message_finished ? 0 : ANJAY_BUFFER_TOO_SHORT;
}

static int map_str_conversion_result(const char *input, const char *endptr) {
    return (!*input || isspace((unsigned char) *input) || errno || !endptr
            || *endptr)
                   ? -1
                   : 0;
}

int _anjay_safe_strtof(const char *in, float *value) {
    errno = 0;
    char *endptr = NULL;
    *value = strtof(in, &endptr);
    return map_str_conversion_result(in, endptr);
}

int _anjay_safe_strtoll(const char *in, long long *value) {
    errno = 0;
    char *endptr = NULL;
    *value = strtoll(in, &endptr, 0);
    return map_str_conversion_result(in, endptr);
}

int _anjay_safe_strtod(const char *in, double *value) {
    errno = 0;
    char *endptr = NULL;
    *value = strtod(in, &endptr);
    return map_str_conversion_result(in, endptr);
}

static int map_get_string_error(int retval) {
    /**
     * NOTE: this function should be used ONLY when getting data to a fixed
     * buffer and when we know for sure that the input cannot be longer.
     */
    if (retval == ANJAY_BUFFER_TOO_SHORT) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return retval;
}

#define MAX_LONG_LONG_BUF_SIZE 32
static int text_get_i64(anjay_input_ctx_t *ctx, int64_t *value) {
    char buf[MAX_LONG_LONG_BUF_SIZE];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return map_get_string_error(retval);
    }
    long long ll;
    if (_anjay_safe_strtoll(buf, &ll)
#if LLONG_MAX != INT64_MAX
            || ll < INT64_MIN || ll > INT64_MAX
#endif
    ) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *value = (int64_t) ll;
    return 0;
}
#undef MAX_LONG_LONG_BUF_SIZE

static int text_get_i32(anjay_input_ctx_t *ctx, int32_t *value) {
    int64_t i64;
    int retval = text_get_i64(ctx, &i64);
    if (retval) {
        return retval;
    }
    if (i64 < INT32_MIN || i64 > INT32_MAX) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *value = (int32_t) i64;
    return 0;
}

static int text_get_bool(anjay_input_ctx_t *ctx, bool *value) {
    int64_t i64;
    int retval = text_get_i64(ctx, &i64);
    if (retval) {
        return retval;
    }
    if (i64 == 0) {
        *value = false;
    } else if (i64 == 1) {
        *value = true;
    } else {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int text_get_f(anjay_input_ctx_t *ctx, float *value) {
    char buf[ANJAY_MAX_FLOAT_STRING_SIZE];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return map_get_string_error(retval);
    }
    if (_anjay_safe_strtof(buf, value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int text_get_d(anjay_input_ctx_t *ctx, double *value) {
    char buf[ANJAY_MAX_DOUBLE_STRING_SIZE];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return map_get_string_error(retval);
    }
    if (_anjay_safe_strtod(buf, value)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int text_get_objlnk(anjay_input_ctx_t *ctx,
                           anjay_oid_t *out_oid,
                           anjay_iid_t *out_iid) {
    char buf[sizeof("65535:65535")];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return map_get_string_error(retval);
    }
    char *colon = strchr(buf, ':');
    if (!colon) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *colon = '\0';
    long long oid;
    long long iid;
    if (_anjay_safe_strtoll(buf, &oid) || _anjay_safe_strtoll(colon + 1, &iid)
            || oid < 0 || oid > UINT16_MAX || iid < 0 || iid > UINT16_MAX) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_oid = (anjay_oid_t) oid;
    *out_iid = (anjay_iid_t) iid;
    return 0;
}

static int text_in_close(anjay_input_ctx_t *ctx_) {
    text_in_t *ctx = (text_in_t *) ctx_;
    if (ctx->autoclose) {
        return avs_stream_cleanup(&ctx->stream);
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
    text_in_t *ctx = (text_in_t *) avs_calloc(1, sizeof(text_in_t));
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
#    include "test/text.c"
#endif
