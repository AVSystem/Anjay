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

#ifndef ANJAY_WITHOUT_PLAINTEXT

#    include <ctype.h>
#    include <errno.h>
#    include <inttypes.h>
#    include <limits.h>
#    include <stdlib.h>
#    include <string.h>

#    include <avsystem/commons/avs_base64.h>
#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_utils.h>

#    include <anjay/core.h>

#    include "../anjay_utils_private.h"
#    include "../coap/anjay_content_format.h"
#    include "anjay_base64_out.h"
#    include "anjay_common.h"
#    include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

/////////////////////////////////////////////////////////////////////// ENCODING

typedef enum { STATE_INITIAL, STATE_PATH_SET, STATE_FINISHED } text_out_state_t;

typedef struct {
    anjay_output_ctx_t base;
    anjay_ret_bytes_ctx_t *bytes;
    avs_stream_t *stream;
    text_out_state_t state;
} text_out_t;

static int text_ret_bytes_begin(anjay_output_ctx_t *ctx_,
                                size_t length,
                                anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes || ctx->state != STATE_PATH_SET) {
        return -1;
    }
    ctx->bytes = _anjay_base64_ret_bytes_ctx_new(
            ctx->stream, AVS_BASE64_DEFAULT_STRICT_CONFIG, length);
    *out_bytes_ctx = ctx->bytes;
    return *out_bytes_ctx ? 0 : -1;
}

static int text_ret_string(anjay_output_ctx_t *ctx_, const char *value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return -1;
    }

    if (ctx->state == STATE_PATH_SET
            && avs_is_ok(avs_stream_write(ctx->stream, value, strlen(value)))) {
        ctx->state = STATE_FINISHED;
        return 0;
    }
    return -1;
}

static int text_ret_integer(anjay_output_ctx_t *ctx_, int64_t value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return -1;
    }

    if (ctx->state == STATE_PATH_SET
            && avs_is_ok(avs_stream_write_f(ctx->stream, "%s",
                                            AVS_INT64_AS_STRING(value)))) {
        ctx->state = STATE_FINISHED;
        return 0;
    }
    return -1;
}

static int text_ret_double(anjay_output_ctx_t *ctx_, double value) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return -1;
    }
    // FIXME: The spec calls for a "decimal" representation, which, in my
    // understanding, excludes exponential representation.
    // As printing floating-point numbers in C as pure decimal with sane
    // precision is tricky, let's take the spec a bit loosely for now.
    if (ctx->state == STATE_PATH_SET
            && avs_is_ok(avs_stream_write_f(ctx->stream, "%s",
                                            AVS_DOUBLE_AS_STRING(value, 17)))) {
        ctx->state = STATE_FINISHED;
        return 0;
    }
    return -1;
}

static int text_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    return text_ret_integer(ctx, value);
}

static int
text_ret_objlnk(anjay_output_ctx_t *ctx_, anjay_oid_t oid, anjay_iid_t iid) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->bytes) {
        return -1;
    }
    if (ctx->state == STATE_PATH_SET
            && avs_is_ok(avs_stream_write_f(ctx->stream, "%" PRIu16 ":%" PRIu16,
                                            oid, iid))) {
        ctx->state = STATE_FINISHED;
        return 0;
    }
    return -1;
}

static int text_set_path(anjay_output_ctx_t *ctx_,
                         const anjay_uri_path_t *path) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->state == STATE_PATH_SET) {
        return -1;
    } else if (ctx->state != STATE_INITIAL || ctx->bytes
               || !_anjay_uri_path_has(path, ANJAY_ID_RID)) {
        return ANJAY_OUTCTXERR_FORMAT_MISMATCH;
    }
    ctx->state = STATE_PATH_SET;
    return 0;
}

static int text_clear_path(anjay_output_ctx_t *ctx_) {
    text_out_t *ctx = (text_out_t *) ctx_;
    if (ctx->state != STATE_PATH_SET || ctx->bytes) {
        return -1;
    }
    ctx->state = STATE_INITIAL;
    return 0;
}

static int text_ret_close(anjay_output_ctx_t *ctx_) {
    text_out_t *ctx = (text_out_t *) ctx_;
    int result = 0;
    if (ctx->bytes) {
        result = _anjay_base64_ret_bytes_ctx_close(ctx->bytes);
        _anjay_base64_ret_bytes_ctx_delete(&ctx->bytes);
    } else if (ctx->state != STATE_FINISHED) {
        result = ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED;
    }
    return result;
}

static const anjay_output_ctx_vtable_t TEXT_OUT_VTABLE = {
    .bytes_begin = text_ret_bytes_begin,
    .string = text_ret_string,
    .integer = text_ret_integer,
    .floating = text_ret_double,
    .boolean = text_ret_bool,
    .objlnk = text_ret_objlnk,
    .set_path = text_set_path,
    .clear_path = text_clear_path,
    .close = text_ret_close
};

anjay_output_ctx_t *_anjay_output_text_create(avs_stream_t *stream) {
    text_out_t *ctx = (text_out_t *) avs_calloc(1, sizeof(text_out_t));
    if (ctx) {
        ctx->base.vtable = &TEXT_OUT_VTABLE;
        ctx->stream = stream;
    }
    return (anjay_output_ctx_t *) ctx;
}

/////////////////////////////////////////////////////////////////////// DECODING

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    avs_stream_t *stream;
    // if bytes_mode == true, then only raw bytes can be read from the context
    // and any other reading operation will fail
    bool bytes_mode;
    uint8_t bytes_cached[3];
    size_t num_bytes_cached;
    bool msg_finished;

    anjay_uri_path_t request_uri;
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
    bool stream_msg_finished = false;

    while (buf_size > 0) {
        if (avs_is_err(avs_stream_read(ctx->stream, &stream_bytes_read,
                                       &stream_msg_finished, encoded,
                                       sizeof(encoded) - 1))) {
            return -1;
        }
        encoded[stream_bytes_read] = '\0';
        if (stream_bytes_read % 4) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        if (has_valid_padding(encoded, stream_bytes_read,
                              stream_msg_finished)) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        assert(ctx->num_bytes_cached == 0);
        size_t num_decoded;
        if (avs_base64_decode_strict(&num_decoded,
                                     (uint8_t *) ctx->bytes_cached,
                                     sizeof(ctx->bytes_cached), encoded)) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        ctx->num_bytes_cached = num_decoded;
        text_get_some_bytes_cache_flush(ctx, &current, &buf_size);
        if (stream_msg_finished) {
            break;
        }
    }
    ctx->msg_finished = stream_msg_finished;
    *out_msg_finished = ctx->msg_finished && !ctx->num_bytes_cached;
    *out_bytes_read = (size_t) (current - (uint8_t *) out_buf);
    return 0;
}

static int
text_get_string(anjay_input_ctx_t *ctx, char *out_buf, size_t buf_size) {
    assert(buf_size);
    text_in_t *in = (text_in_t *) ctx;
    if (in->bytes_mode) {
        return -1;
    }
    char *ptr = out_buf;
    char *endptr = out_buf + (buf_size - 1);
    do {
        size_t bytes_read = 0;
        if (avs_is_err(avs_stream_read(in->stream, &bytes_read,
                                       &in->msg_finished, ptr,
                                       (size_t) (endptr - ptr)))) {
            return -1;
        }
        ptr += bytes_read;
    } while (!in->msg_finished && ptr < endptr);
    *ptr = '\0';
    return in->msg_finished ? 0 : ANJAY_BUFFER_TOO_SHORT;
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

static int text_get_integer(anjay_input_ctx_t *ctx, int64_t *value) {
    char buf[AVS_INT_STR_BUF_SIZE(long long)];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return map_get_string_error(retval);
    }
    long long ll;
    if (_anjay_safe_strtoll(buf, &ll)
#    if LLONG_MAX != INT64_MAX
            || ll < INT64_MIN || ll > INT64_MAX
#    endif
    ) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *value = (int64_t) ll;
    return 0;
}

static int text_get_bool(anjay_input_ctx_t *ctx, bool *value) {
    int64_t i64;
    int retval = text_get_integer(ctx, &i64);
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

static int text_get_double(anjay_input_ctx_t *ctx, double *value) {
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
    char buf[MAX_OBJLNK_STRING_SIZE];
    int retval = anjay_get_string(ctx, buf, sizeof(buf));
    if (retval) {
        return map_get_string_error(retval);
    }

    if (_anjay_io_parse_objlnk(buf, out_oid, out_iid)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int text_in_close(anjay_input_ctx_t *ctx_) {
    (void) ctx_;
    return 0;
}

static int text_get_path(anjay_input_ctx_t *ctx,
                         anjay_uri_path_t *out_path,
                         bool *out_is_array) {
    text_in_t *in = (text_in_t *) ctx;
    if (in->msg_finished) {
        return ANJAY_GET_PATH_END;
    }
    if (!_anjay_uri_path_has(&in->request_uri, ANJAY_ID_RID)) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    *out_is_array = false;
    *out_path = in->request_uri;
    return 0;
}

static int text_next_entry(anjay_input_ctx_t *ctx) {
    (void) ctx;
    return 0;
}

static const anjay_input_ctx_vtable_t TEXT_IN_VTABLE = {
    .some_bytes = text_get_some_bytes,
    .string = text_get_string,
    .integer = text_get_integer,
    .floating = text_get_double,
    .boolean = text_get_bool,
    .objlnk = text_get_objlnk,
    .get_path = text_get_path,
    .next_entry = text_next_entry,
    .close = text_in_close
};

int _anjay_input_text_create(anjay_input_ctx_t **out,
                             avs_stream_t **stream_ptr,
                             const anjay_uri_path_t *request_uri) {
    text_in_t *ctx = (text_in_t *) avs_calloc(1, sizeof(text_in_t));
    *out = (anjay_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }

    ctx->vtable = &TEXT_IN_VTABLE;
    ctx->stream = *stream_ptr;
    ctx->request_uri = request_uri ? *request_uri : MAKE_ROOT_PATH();

    return 0;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/text.c"
#    endif

#endif // ANJAY_WITHOUT_PLAINTEXT
