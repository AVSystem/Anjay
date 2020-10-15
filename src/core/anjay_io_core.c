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

#include <inttypes.h>
#include <stdlib.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_v_table.h>
#include <avsystem/commons/avs_utils.h>

#include <anjay/core.h>

#include "coap/anjay_content_format.h"

#include "anjay_io_core.h"
#include "io/anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

struct anjay_dm_list_ctx_struct {
    const anjay_dm_list_ctx_vtable_t *vtable;
};

void anjay_dm_emit(anjay_dm_list_ctx_t *ctx, uint16_t id) {
    assert(ctx->vtable && ctx->vtable->emit);
    ctx->vtable->emit(ctx, id);
}

struct anjay_ret_bytes_ctx_struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
};

#ifdef ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT

uint16_t _anjay_translate_legacy_content_format(uint16_t format) {
    static const char MSG_FMT[] =
            "legacy application/vnd.oma.lwm2m+%s Content-Format value: %d";

    switch (format) {
    case ANJAY_COAP_FORMAT_LEGACY_PLAINTEXT:
        anjay_log(DEBUG, MSG_FMT, _("text"),
                  ANJAY_COAP_FORMAT_LEGACY_PLAINTEXT);
        return AVS_COAP_FORMAT_PLAINTEXT;

    case ANJAY_COAP_FORMAT_LEGACY_TLV:
        anjay_log(DEBUG, MSG_FMT, _("tlv"), ANJAY_COAP_FORMAT_LEGACY_TLV);
        return AVS_COAP_FORMAT_OMA_LWM2M_TLV;

    case ANJAY_COAP_FORMAT_LEGACY_JSON:
        anjay_log(DEBUG, MSG_FMT, _("json"), ANJAY_COAP_FORMAT_LEGACY_JSON);
        return AVS_COAP_FORMAT_OMA_LWM2M_JSON;

    case ANJAY_COAP_FORMAT_LEGACY_OPAQUE:
        anjay_log(DEBUG, MSG_FMT, _("opaque"), ANJAY_COAP_FORMAT_LEGACY_OPAQUE);
        return AVS_COAP_FORMAT_OCTET_STREAM;

    default:
        return format;
    }
}

#endif // ANJAY_WITH_LEGACY_CONTENT_FORMAT_SUPPORT

anjay_ret_bytes_ctx_t *anjay_ret_bytes_begin(anjay_output_ctx_t *ctx,
                                             size_t length) {
    anjay_ret_bytes_ctx_t *bytes_ctx = NULL;
    int result = _anjay_output_bytes_begin(ctx, length, &bytes_ctx);
    assert(!result == !!bytes_ctx);
    (void) result;
    return bytes_ctx;
}

int anjay_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx,
                           const void *data,
                           size_t length) {
    assert(ctx && ctx->vtable && ctx->vtable->append);
    return ctx->vtable->append(ctx, data, length);
}

int anjay_ret_bytes(anjay_output_ctx_t *ctx, const void *data, size_t length) {
    anjay_ret_bytes_ctx_t *bytes = anjay_ret_bytes_begin(ctx, length);
    if (!bytes) {
        return -1;
    } else {
        return anjay_ret_bytes_append(bytes, data, length);
    }
}

int anjay_ret_string(anjay_output_ctx_t *ctx, const char *value) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->string) {
        result = ctx->vtable->string(ctx, value);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int anjay_ret_i64(anjay_output_ctx_t *ctx, int64_t value) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->integer) {
        result = ctx->vtable->integer(ctx, value);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int anjay_ret_double(anjay_output_ctx_t *ctx, double value) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->floating) {
        result = ctx->vtable->floating(ctx, value);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int anjay_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->boolean) {
        result = ctx->vtable->boolean(ctx, value);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int anjay_ret_objlnk(anjay_output_ctx_t *ctx,
                     anjay_oid_t oid,
                     anjay_iid_t iid) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->objlnk) {
        result = ctx->vtable->objlnk(ctx, oid, iid);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int _anjay_output_bytes_begin(anjay_output_ctx_t *ctx,
                              size_t length,
                              anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    assert(out_bytes_ctx);
    assert(!*out_bytes_ctx);
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->bytes_begin) {
        result = ctx->vtable->bytes_begin(ctx, length, out_bytes_ctx);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int _anjay_output_start_aggregate(anjay_output_ctx_t *ctx) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->start_aggregate) {
        result = ctx->vtable->start_aggregate(ctx);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int _anjay_output_set_path(anjay_output_ctx_t *ctx,
                           const anjay_uri_path_t *path) {
    // NOTE: We explicitly consider NULL set_path() to be always successful,
    // to simplify implementation of outbuf_ctx and the like.
    int result = 0;
    if (ctx->vtable->set_path) {
        result = ctx->vtable->set_path(ctx, path);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int _anjay_output_clear_path(anjay_output_ctx_t *ctx) {
    int result = ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED;
    if (ctx->vtable->clear_path) {
        result = ctx->vtable->clear_path(ctx);
    }
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int _anjay_output_set_time(anjay_output_ctx_t *ctx, double value) {
    if (!ctx->vtable->set_time) {
        // Deliberately ignore set_time fails - non-SenML formats will just omit
        // the timestamps, this is fine.
        return 0;
    }
    int result = ctx->vtable->set_time(ctx, value);
    _anjay_update_ret(&ctx->error, result);
    return result;
}

int _anjay_output_ctx_destroy(anjay_output_ctx_t **ctx_ptr) {
    anjay_output_ctx_t *ctx = *ctx_ptr;
    int result = 0;
    if (ctx) {
        result = ctx->error;
        if (ctx->vtable->close) {
            _anjay_update_ret(&result, ctx->vtable->close(ctx));
        }
        avs_free(ctx);
        *ctx_ptr = NULL;
    }
    return result;
}

int _anjay_output_ctx_destroy_and_process_result(
        anjay_output_ctx_t **out_ctx_ptr, int result) {
    int destroy_result = _anjay_output_ctx_destroy(out_ctx_ptr);
    if (destroy_result != ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED) {
        return destroy_result ? destroy_result : result;
    } else if (result) {
        return result;
    } else {
        anjay_log(
                ERROR,
                _("unable to determine resource type: anjay_ret_* not ") _(
                        "called during successful resource_read handler call"));
        return ANJAY_ERR_INTERNAL;
    }
}

static int get_some_bytes(anjay_input_ctx_t *ctx,
                          size_t *out_bytes_read,
                          bool *out_message_finished,
                          void *out_buf,
                          size_t buf_size) {
    if (!ctx->vtable->some_bytes) {
        return -1;
    }
    return ctx->vtable->some_bytes(ctx, out_bytes_read, out_message_finished,
                                   out_buf, buf_size);
}

int anjay_get_bytes(anjay_input_ctx_t *ctx,
                    size_t *out_bytes_read,
                    bool *out_message_finished,
                    void *out_buf,
                    size_t buf_size) {
    char *buf_ptr = (char *) out_buf;
    size_t buf_left = buf_size;
    while (true) {
        size_t tmp_bytes_read = 0;
        int retval = get_some_bytes(ctx, &tmp_bytes_read, out_message_finished,
                                    buf_ptr, buf_left);
        buf_ptr += tmp_bytes_read;
        buf_left -= tmp_bytes_read;
        if (retval || *out_message_finished || !buf_left) {
            *out_bytes_read = buf_size - buf_left;
            return retval;
        }
    }
}

int anjay_get_string(anjay_input_ctx_t *ctx, char *out_buf, size_t buf_size) {
    if (!ctx->vtable->string) {
        return -1;
    }
    if (buf_size == 0) {
        // At least terminating nullbyte must fit into the buffer!
        return ANJAY_BUFFER_TOO_SHORT;
    }
    return ctx->vtable->string(ctx, out_buf, buf_size);
}

int anjay_get_i32(anjay_input_ctx_t *ctx, int32_t *out) {
    int64_t tmp;
    int result = anjay_get_i64(ctx, &tmp);
    if (!result) {
        if (tmp < INT32_MIN || tmp > INT32_MAX) {
            result = ANJAY_ERR_BAD_REQUEST;
        } else {
            *out = (int32_t) tmp;
        }
    }
    return result;
}

int anjay_get_i64(anjay_input_ctx_t *ctx, int64_t *out) {
    if (!ctx->vtable->integer) {
        return -1;
    }
    return ctx->vtable->integer(ctx, out);
}

int anjay_get_float(anjay_input_ctx_t *ctx, float *out) {
    double tmp;
    int result = anjay_get_double(ctx, &tmp);
    if (!result) {
        *out = (float) tmp;
    }
    return result;
}

int anjay_get_double(anjay_input_ctx_t *ctx, double *out) {
    if (!ctx->vtable->floating) {
        return -1;
    }
    return ctx->vtable->floating(ctx, out);
}

int anjay_get_bool(anjay_input_ctx_t *ctx, bool *out) {
    if (!ctx->vtable->boolean) {
        return -1;
    }
    return ctx->vtable->boolean(ctx, out);
}

int anjay_get_objlnk(anjay_input_ctx_t *ctx,
                     anjay_oid_t *out_oid,
                     anjay_iid_t *out_iid) {
    if (!ctx->vtable->objlnk) {
        return -1;
    }
    return ctx->vtable->objlnk(ctx, out_oid, out_iid);
}

int _anjay_input_get_path(anjay_input_ctx_t *ctx,
                          anjay_uri_path_t *out_path,
                          bool *out_is_array) {
    if (!ctx->vtable->get_path) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    bool ignored_is_array;
    if (!out_is_array) {
        out_is_array = &ignored_is_array;
    }
    anjay_uri_path_t ignored_path;
    if (!out_path) {
        out_path = &ignored_path;
    }
    (void) ignored_is_array;
    (void) ignored_path;
    return ctx->vtable->get_path(ctx, out_path, out_is_array);
}

int _anjay_input_update_root_path(anjay_input_ctx_t *ctx,
                                  const anjay_uri_path_t *root_path) {
    if (!ctx->vtable->update_root_path) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return ctx->vtable->update_root_path(ctx, root_path);
}

int _anjay_input_next_entry(anjay_input_ctx_t *ctx) {
    if (!ctx->vtable->next_entry) {
        return -1;
    }
    return ctx->vtable->next_entry(ctx);
}

int _anjay_input_ctx_destroy(anjay_input_ctx_t **ctx_ptr) {
    int retval = 0;
    anjay_input_ctx_t *ctx = *ctx_ptr;
    if (ctx) {
        if (ctx->vtable->close) {
            retval = ctx->vtable->close(*ctx_ptr);
        }
        avs_free(ctx);
        *ctx_ptr = NULL;
    }
    return retval;
}

#ifdef ANJAY_TEST
#    include "tests/core/io.c"
#endif
