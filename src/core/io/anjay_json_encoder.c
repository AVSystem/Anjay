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

#if defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON)

#    include <avsystem/commons/avs_base64.h>
#    include <avsystem/commons/avs_log.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_io_core.h"
#    include "../coap/anjay_content_format.h"
#    include "anjay_base64_out.h"
#    include "anjay_senml_like_encoder_vtable.h"

#    include <inttypes.h>

VISIBILITY_SOURCE_BEGIN

#    define json_log(level, ...) _anjay_log(json, level, __VA_ARGS__)

#    define JSON_CONTEXT_LEVEL_ARRAY 0
#    define JSON_CONTEXT_LEVEL_MAP 1
#    define JSON_CONTEXT_LEVEL_BYTES 2

#    define JSON_MAX_CONTEXT_LEVEL 2

typedef enum {
    SENML_LIKE_DATA_BASENAME,
    SENML_LIKE_DATA_NAME,
    SENML_LIKE_DATA_VALUE,
    SENML_LIKE_DATA_STRING,
    SENML_LIKE_DATA_BOOL,
    SENML_LIKE_DATA_OPAQUE,
    SENML_LIKE_DATA_TIME,
    SENML_LIKE_DATA_OBJLNK
} senml_like_data_type_t;

typedef struct json_encoder_struct json_encoder_t;

typedef int (*key_encoder_t)(json_encoder_t *, senml_like_data_type_t);

struct json_encoder_struct {
    const anjay_senml_like_encoder_vtable_t *vtable;
    key_encoder_t key_encoder;
    avs_base64_config_t base64_config;
    avs_stream_t *stream;
    anjay_ret_bytes_ctx_t *bytes;
    uint8_t level;
    bool needs_separator;
};

static int begin_pair(json_encoder_t *ctx, senml_like_data_type_t type);

static int write_quoted_string(avs_stream_t *stream, const char *value) {
    if (avs_is_err(avs_stream_write(stream, "\"", 1))) {
        return -1;
    }
    for (size_t i = 0; i < strlen(value); ++i) {
        /**
         * RFC 4627 section 2.5 Strings:
         *
         * "(...)
         *  All Unicode characters may be placed within the
         *  quotation marks except for the characters that must be escaped:
         *  quotation mark, reverse solidus, and the control characters (U+0000
         *  through U+001F).
         * "
         */
        if ((value[i] == '\\' || value[i] == '"')
                && avs_is_err(avs_stream_write(stream, "\\", 1))) {
            return -1;
        }

        avs_error_t err;
        if ((uint8_t) value[i] >= 0x20 && (uint8_t) value[i] < 127) {
            err = avs_stream_write(stream, &value[i], 1);
        } else if (value[i] == '\b') {
            err = avs_stream_write(stream, "\\b", 2);
        } else if (value[i] == '\f') {
            err = avs_stream_write(stream, "\\f", 2);
        } else if (value[i] == '\n') {
            err = avs_stream_write(stream, "\\n", 2);
        } else if (value[i] == '\r') {
            err = avs_stream_write(stream, "\\r", 2);
        } else if (value[i] == '\t') {
            err = avs_stream_write(stream, "\\t", 2);
        } else {
            const int nibble0 = ((uint8_t) value[i] >> 4) & 0xF;
            const int nibble1 = ((uint8_t) value[i]) & 0xF;
            err = avs_stream_write_f(stream, "\\u00%x%x", nibble0, nibble1);
        }

        if (avs_is_err(err)) {
            return -1;
        }
    }
    return avs_is_ok(avs_stream_write(stream, "\"", 1)) ? 0 : -1;
}

static inline void nested_context_push(json_encoder_t *ctx, uint8_t level) {
    assert(ctx);
    assert(ctx->level < JSON_MAX_CONTEXT_LEVEL);
    assert(ctx->level == level - 1);
    (void) level;
    ctx->level++;
}

static inline void nested_context_pop(json_encoder_t *ctx) {
    (void) ctx;
    assert(ctx->level);
    ctx->level--;
}

static inline int maybe_write_time(json_encoder_t *ctx, double time_s) {
    if (!isnan(time_s)) {
        if (begin_pair(ctx, SENML_LIKE_DATA_TIME)
                || avs_is_err(avs_stream_write_f(ctx->stream, "%s",
                                                 AVS_DOUBLE_AS_STRING(time_s,
                                                                      17)))) {
            return -1;
        }
    }
    return 0;
}

static inline int maybe_write_name(json_encoder_t *ctx, const char *name) {
    int retval = 0;
    if (name) {
        (void) ((retval = begin_pair(ctx, SENML_LIKE_DATA_NAME))
                || (retval = write_quoted_string(ctx->stream, name)));
    }
    return retval;
}

static int maybe_write_separator(json_encoder_t *ctx) {
    if (ctx->needs_separator) {
        ctx->needs_separator = false;
        if (avs_is_err(avs_stream_write(ctx->stream, ",", 1))) {
            return -1;
        }
    }
    return 0;
}

#    ifdef ANJAY_WITH_LWM2M_JSON
static int encode_key(json_encoder_t *ctx, senml_like_data_type_t type) {
    const char *key = NULL;
    switch (type) {
    case SENML_LIKE_DATA_BASENAME:
        key = "\"bn\":";
        break;
    case SENML_LIKE_DATA_NAME:
        key = "\"n\":";
        break;
    case SENML_LIKE_DATA_VALUE:
        key = "\"v\":";
        break;
    case SENML_LIKE_DATA_STRING:
    case SENML_LIKE_DATA_OPAQUE:
        key = "\"sv\":";
        break;
    case SENML_LIKE_DATA_BOOL:
        key = "\"bv\":";
        break;
    case SENML_LIKE_DATA_TIME:
        key = "\"t\":";
        break;
    case SENML_LIKE_DATA_OBJLNK:
        key = "\"ov\":";
        break;
    default:
        AVS_UNREACHABLE("invalid data type");
        return -1;
    }
    return avs_is_ok(avs_stream_write(ctx->stream, key, strlen(key))) ? 0 : -1;
}

static int element_begin(anjay_senml_like_encoder_t *ctx_,
                         const char *basename,
                         const char *name,
                         double time_s) {
    assert(basename == NULL);
    (void) basename;
    json_encoder_t *ctx = (json_encoder_t *) ctx_;

    nested_context_push(ctx, JSON_CONTEXT_LEVEL_MAP);
    if (maybe_write_separator(ctx)
            || avs_is_err(avs_stream_write(ctx->stream, "{", 1))
            || maybe_write_name(ctx, name) || maybe_write_time(ctx, time_s)) {
        return -1;
    }
    return 0;
}

static int encoder_cleanup(anjay_senml_like_encoder_t **ctx_) {
    json_encoder_t *ctx = (json_encoder_t *) *ctx_;
    int retval = -1;

    if (ctx->level == JSON_CONTEXT_LEVEL_ARRAY
            && avs_is_ok(avs_stream_write(ctx->stream, "]}", 2))) {
        retval = 0;
    }

    avs_free(*ctx_);
    *ctx_ = NULL;
    return retval;
}
#    endif // ANJAY_WITH_LWM2M_JSON

static int begin_pair(json_encoder_t *ctx, senml_like_data_type_t type) {
    int retval = -1;
    if (ctx->level == JSON_CONTEXT_LEVEL_MAP) {
        (void) ((retval = maybe_write_separator(ctx))
                || (retval = ctx->key_encoder(ctx, type)));
    }
    // Separator is in fact needed after encoding value, not key. Anyway,
    // maybe_encode_basename() isn't called before encoding a value, so setting
    // this flag here allows to simplify encode_*() functions.
    ctx->needs_separator = true;
    return retval;
}

static int encode_uint(anjay_senml_like_encoder_t *ctx_, uint64_t value) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    if (begin_pair(ctx, SENML_LIKE_DATA_VALUE)
            || avs_is_err(avs_stream_write_f(ctx->stream, "%s",
                                             AVS_UINT64_AS_STRING(value)))) {
        return -1;
    }
    return 0;
}

static int encode_int(anjay_senml_like_encoder_t *ctx_, int64_t value) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    if (begin_pair(ctx, SENML_LIKE_DATA_VALUE)
            || avs_is_err(avs_stream_write_f(ctx->stream, "%s",
                                             AVS_INT64_AS_STRING(value)))) {
        return -1;
    }
    return 0;
}

// Source of format specifiers:
// https://randomascii.wordpress.com/2012/03/08/float-precisionfrom-zero-to-100-digits-2/

static int encode_double(anjay_senml_like_encoder_t *ctx_, double value) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    if (begin_pair(ctx, SENML_LIKE_DATA_VALUE)
            || avs_is_err(avs_stream_write_f(
                       ctx->stream, "%s", AVS_DOUBLE_AS_STRING(value, 17)))) {
        return -1;
    }
    return 0;
}

static int encode_bool(anjay_senml_like_encoder_t *ctx_, bool value) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    if (begin_pair(ctx, SENML_LIKE_DATA_BOOL)
            || avs_is_err(avs_stream_write_f(ctx->stream, "%s",
                                             value ? "true" : "false"))) {
        return -1;
    }
    return 0;
}

static int encode_string(anjay_senml_like_encoder_t *ctx_, const char *value) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    int retval;
    (void) ((retval = begin_pair(ctx, SENML_LIKE_DATA_STRING))
            || (retval = write_quoted_string(ctx->stream, value)));
    return retval;
}

static int encode_objlnk(anjay_senml_like_encoder_t *ctx_, const char *value) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    int retval;
    (void) ((retval = begin_pair(ctx, SENML_LIKE_DATA_OBJLNK))
            || (retval = write_quoted_string(ctx->stream, value)));
    return retval;
}

static int element_end(anjay_senml_like_encoder_t *ctx_) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;

    nested_context_pop(ctx);
    ctx->needs_separator = true;
    return avs_is_ok(avs_stream_write(ctx->stream, "}", 1)) ? 0 : -1;
}

static int bytes_begin(anjay_senml_like_encoder_t *ctx_, size_t size) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;

    nested_context_push(ctx, JSON_CONTEXT_LEVEL_BYTES);
    if (!(ctx->bytes = _anjay_base64_ret_bytes_ctx_new(
                  ctx->stream, ctx->base64_config, size))
            || maybe_write_separator(ctx)
            || ctx->key_encoder(ctx, SENML_LIKE_DATA_OPAQUE)
            || avs_is_err(avs_stream_write(ctx->stream, "\"", 1))) {
        _anjay_base64_ret_bytes_ctx_delete(&ctx->bytes);
        return -1;
    }
    return 0;
}

static int
bytes_append(anjay_senml_like_encoder_t *ctx_, const void *data, size_t size) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;
    return anjay_ret_bytes_append(ctx->bytes, data, size);
}

static int bytes_end(anjay_senml_like_encoder_t *ctx_) {
    json_encoder_t *ctx = (json_encoder_t *) ctx_;

    int retval;
    if (_anjay_base64_ret_bytes_ctx_close(ctx->bytes)
            || avs_is_err(avs_stream_write(ctx->stream, "\"", 1))) {
        retval = -1;
    } else {
        retval = 0;
    }

    _anjay_base64_ret_bytes_ctx_delete(&ctx->bytes);
    nested_context_pop(ctx);
    return retval;
}

#    define JSON_VTABLE_COMMON_DEF                 \
        .senml_like_encode_uint = encode_uint,     \
        .senml_like_encode_int = encode_int,       \
        .senml_like_encode_double = encode_double, \
        .senml_like_encode_bool = encode_bool,     \
        .senml_like_encode_string = encode_string, \
        .senml_like_encode_objlnk = encode_objlnk, \
        .senml_like_element_end = element_end,     \
        .senml_like_bytes_begin = bytes_begin,     \
        .senml_like_bytes_append = bytes_append,   \
        .senml_like_bytes_end = bytes_end

#    ifdef ANJAY_WITH_LWM2M_JSON
static const anjay_senml_like_encoder_vtable_t LWM2M_JSON_ENCODER_VTABLE = {
    JSON_VTABLE_COMMON_DEF,
    .senml_like_element_begin = element_begin,
    .senml_like_encoder_cleanup = encoder_cleanup
};
#    endif // ANJAY_WITH_LWM2M_JSON

static json_encoder_t *
_anjay_json_encoder_new(avs_stream_t *stream,
                        const anjay_senml_like_encoder_vtable_t *vtable,
                        key_encoder_t key_encoder,
                        avs_base64_config_t base64_config) {
    if (!stream) {
        json_log(DEBUG, _("no stream provided"));
        return NULL;
    }

    json_encoder_t *ctx =
            (json_encoder_t *) avs_calloc(1, sizeof(json_encoder_t));
    if (ctx) {
        ctx->stream = stream;
        ctx->vtable = vtable;
        ctx->key_encoder = key_encoder;
        ctx->base64_config = base64_config;
    } else {
        json_log(DEBUG, _("failed to allocate encoder context"));
    }
    return ctx;
}

#    ifdef ANJAY_WITH_LWM2M_JSON
static int write_lwm2m_json_response_preamble(json_encoder_t *ctx,
                                              const char *basename) {
    if (avs_is_err(avs_stream_write(ctx->stream, "{", 1))
            || (basename
                && avs_is_err(avs_stream_write_f(ctx->stream, "\"bn\":\"%s\",",
                                                 basename)))
            || avs_is_err(avs_stream_write(ctx->stream, "\"e\":[", 5))) {
        return -1;
    }
    return 0;
}

anjay_senml_like_encoder_t *
_anjay_lwm2m_json_encoder_new(avs_stream_t *stream, const char *basename) {
    json_encoder_t *ctx =
            _anjay_json_encoder_new(stream, &LWM2M_JSON_ENCODER_VTABLE,
                                    encode_key,
                                    AVS_BASE64_DEFAULT_STRICT_CONFIG);
    if (ctx && write_lwm2m_json_response_preamble(ctx, basename)) {
        avs_free(ctx);
        ctx = NULL;
    }
    return (anjay_senml_like_encoder_t *) ctx;
}
#    endif // ANJAY_WITH_LWM2M_JSON

#endif // defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON)
