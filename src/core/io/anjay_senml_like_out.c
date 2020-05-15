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

#if defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON) \
        || defined(ANJAY_WITH_CBOR)

#    include <anjay/core.h>

#    include <assert.h>
#    include <inttypes.h>
#    include <string.h>

#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../coap/anjay_content_format.h"

#    include "anjay_common.h"
#    include "anjay_senml_like_encoder.h"
#    include "anjay_vtable.h"

#    define senml_log(level, ...) _anjay_log(senml_like_out, level, __VA_ARGS__)

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
} senml_bytes_t;

typedef struct senml_out_struct {
    anjay_output_ctx_t base;
    anjay_senml_like_encoder_t *encoder;
    anjay_uri_path_t path;
    anjay_uri_path_t base_path;
    senml_bytes_t bytes;
    bool returning_bytes;
    bool basename_written;
    double timestamp;
} senml_out_t;

static int path_to_string(const anjay_uri_path_t *path,
                          size_t start_index,
                          size_t end_index,
                          char *dest,
                          size_t size) {
    for (; start_index < end_index; start_index++) {
        int written_chars = avs_simple_snprintf(dest, size, "/%" PRIu16,
                                                path->ids[start_index]);
        if (written_chars < 0) {
            return -1;
        }
        dest += written_chars;
        size -= (size_t) written_chars;
    }
    return 0;
}

static char *maybe_get_basename(senml_out_t *ctx, char *buf, size_t size) {
    size_t base_path_length = _anjay_uri_path_length(&ctx->base_path);
    char *retptr = NULL;
    if (!ctx->basename_written && base_path_length > 0) {
        *buf = '\0';
        int result =
                path_to_string(&ctx->base_path, 0, base_path_length, buf, size);
        AVS_ASSERT(result == 0, "buffer too small");
        (void) result;
        retptr = buf;
    }
    return retptr;
}

static char *maybe_get_name(senml_out_t *ctx, char *buf, size_t size) {
    size_t base_path_length = _anjay_uri_path_length(&ctx->base_path);
    size_t path_length = _anjay_uri_path_length(&ctx->path);
    char *retptr = NULL;
    if (path_length > base_path_length) {
        *buf = '\0';
        int result = path_to_string(&ctx->path, base_path_length, path_length,
                                    buf, size);
        AVS_ASSERT(result == 0, "buffer too small");
        (void) result;
        retptr = buf;
    }
    return retptr;
}

static int finish_ret_bytes(senml_out_t *ctx) {
    int retval;
    (void) ((retval = _anjay_senml_like_bytes_end(ctx->encoder))
            || (retval = _anjay_senml_like_element_end(ctx->encoder)));
    ctx->returning_bytes = false;
    return retval;
}

static int element_begin(senml_out_t *ctx) {
    if (!_anjay_uri_path_has(&ctx->path, ANJAY_ID_RID)) {
        return -1;
    }

    if (ctx->returning_bytes) {
        int result = finish_ret_bytes(ctx);
        if (result) {
            return result;
        }
    }

    char basename_buf[MAX_PATH_STRING_SIZE];
    char name_buf[MAX_PATH_STRING_SIZE];

    char *name = maybe_get_name(ctx, name_buf, sizeof(name_buf));
    char *basename =
            maybe_get_basename(ctx, basename_buf, sizeof(basename_buf));
    ctx->basename_written = true;
    int result = _anjay_senml_like_element_begin(ctx->encoder, basename, name,
                                                 ctx->timestamp);
    ctx->timestamp = NAN;
    ctx->path = MAKE_ROOT_PATH();
    return result;
}

static int streamed_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length) {
    senml_out_t *ctx = AVS_CONTAINER_OF(ctx_, senml_out_t, bytes);
    return _anjay_senml_like_bytes_append(ctx->encoder, data, length);
}

static const anjay_ret_bytes_ctx_vtable_t STREAMED_BYTES_VTABLE = {
    .append = streamed_bytes_append
};

static int senml_ret_bytes(anjay_output_ctx_t *ctx_,
                           size_t length,
                           anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    int retval;
    if (!(retval = element_begin(ctx))
            && !(retval =
                         _anjay_senml_like_bytes_begin(ctx->encoder, length))) {
        ctx->returning_bytes = true;
        *out_bytes_ctx = (anjay_ret_bytes_ctx_t *) &ctx->bytes;
    }
    return retval;
}

static int senml_ret_string(anjay_output_ctx_t *ctx_, const char *value) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    int retval;
    (void) ((retval = element_begin(ctx))
            || (retval = _anjay_senml_like_encode_string(ctx->encoder, value))
            || (retval = _anjay_senml_like_element_end(ctx->encoder)));
    return retval;
}

static int senml_ret_integer(anjay_output_ctx_t *ctx_, int64_t value) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    int retval;
    (void) ((retval = element_begin(ctx))
            || (retval = _anjay_senml_like_encode_int(ctx->encoder, value))
            || (retval = _anjay_senml_like_element_end(ctx->encoder)));
    return retval;
}

static int senml_ret_double(anjay_output_ctx_t *ctx_, double value) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    int retval;
    (void) ((retval = element_begin(ctx))
            || (retval = _anjay_senml_like_encode_double(ctx->encoder, value))
            || (retval = _anjay_senml_like_element_end(ctx->encoder)));
    return retval;
}

static int senml_ret_bool(anjay_output_ctx_t *ctx_, bool value) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    int retval;
    (void) ((retval = element_begin(ctx))
            || (retval = _anjay_senml_like_encode_bool(ctx->encoder, value))
            || (retval = _anjay_senml_like_element_end(ctx->encoder)));
    return retval;
}

static int
senml_ret_objlnk(anjay_output_ctx_t *ctx_, anjay_oid_t oid, anjay_iid_t iid) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    char buf[MAX_OBJLNK_STRING_SIZE];
    int retval;
    (void) (((retval = avs_simple_snprintf(buf, sizeof(buf),
                                           "%" PRIu16 ":%" PRIu16, oid, iid))
             < 0)
            || (retval = element_begin(ctx))
            || (retval = _anjay_senml_like_encode_objlnk(ctx->encoder, buf))
            || (retval = _anjay_senml_like_element_end(ctx->encoder)));
    return retval;
}

static int senml_ret_start_aggregate(anjay_output_ctx_t *ctx_) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    if (_anjay_uri_path_leaf_is(&ctx->path, ANJAY_ID_IID)
            || _anjay_uri_path_leaf_is(&ctx->path, ANJAY_ID_RID)) {
        ctx->path = MAKE_ROOT_PATH();
        return 0;
    } else {
        return -1;
    }
}

static int senml_set_path(anjay_output_ctx_t *ctx_,
                          const anjay_uri_path_t *uri) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    AVS_ASSERT(!_anjay_uri_path_outside_base(uri, &ctx->base_path),
               "Attempted to set path outside the context's base path. "
               "This is a bug in resource reading logic.");
    if (_anjay_uri_path_length(&ctx->path) > 0) {
        senml_log(ERROR, _("Path already set"));
        return -1;
    }
    ctx->path = *uri;
    return 0;
}

static int senml_clear_path(anjay_output_ctx_t *ctx_) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    if (_anjay_uri_path_length(&ctx->path) == 0) {
        senml_log(ERROR, _("Path not set"));
        return -1;
    }
    ctx->path = MAKE_ROOT_PATH();
    return 0;
}

static int senml_set_time(anjay_output_ctx_t *ctx_, double value) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    ctx->timestamp = value;
    return 0;
}

static int senml_output_close(anjay_output_ctx_t *ctx_) {
    senml_out_t *ctx = (senml_out_t *) ctx_;
    int result = 0;
    if (ctx->returning_bytes) {
        result = finish_ret_bytes(ctx);
    }
    _anjay_update_ret(&result,
                      _anjay_senml_like_encoder_cleanup(&ctx->encoder));
    if (_anjay_uri_path_length(&ctx->path) > 0) {
        senml_log(ERROR, _("set_path() called without returning a value"));
        _anjay_update_ret(&result, ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED);
    }
    return result;
}

static const anjay_output_ctx_vtable_t SENML_OUT_VTABLE = {
    .bytes_begin = senml_ret_bytes,
    .string = senml_ret_string,
    .integer = senml_ret_integer,
    .floating = senml_ret_double,
    .boolean = senml_ret_bool,
    .objlnk = senml_ret_objlnk,
    .start_aggregate = senml_ret_start_aggregate,
    .set_path = senml_set_path,
    .clear_path = senml_clear_path,
    .set_time = senml_set_time,
    .close = senml_output_close
};

anjay_output_ctx_t *_anjay_output_senml_like_create(avs_stream_t *stream,
                                                    const anjay_uri_path_t *uri,
                                                    uint16_t format) {
    senml_out_t *ctx = (senml_out_t *) avs_calloc(1, sizeof(senml_out_t));
    if (!ctx) {
        return NULL;
    }
    ctx->base.vtable = &SENML_OUT_VTABLE;
    ctx->bytes.vtable = &STREAMED_BYTES_VTABLE;
    ctx->timestamp = NAN;
    ctx->path = MAKE_ROOT_PATH();
    ctx->base_path = *uri;

    switch (format) {
#    ifdef ANJAY_WITH_LWM2M_JSON
    case AVS_COAP_FORMAT_OMA_LWM2M_JSON: {
        char basename_buf[MAX_PATH_STRING_SIZE];
        char *basename =
                maybe_get_basename(ctx, basename_buf, sizeof(basename_buf));
        ctx->encoder = _anjay_lwm2m_json_encoder_new(stream, basename);
        ctx->basename_written = true;
        break;
    }
#    endif // ANJAY_WITH_LWM2M_JSON
    default:
        senml_log(WARNING, _("unsupported content format"));
        goto error;
    }

    if (!ctx->encoder) {
        goto error;
    }

    senml_log(DEBUG, _("created SenML-like context"));
    return (anjay_output_ctx_t *) ctx;

error:
    senml_log(DEBUG, _("failed to create SenML-like encoder"));
    avs_free(ctx);
    return NULL;
}

#endif // defined(ANJAY_WITH_LWM2M_JSON) || defined(ANJAY_WITH_SENML_JSON) ||
       // defined(ANJAY_WITH_CBOR)
