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

#include <inttypes.h>
#include <stdlib.h>

#include <avsystem/commons/memory.h>
#include <avsystem/commons/stream_v_table.h>
#include <avsystem/commons/utils.h>

#include <anjay/core.h>

#include "coap/content_format.h"

#include "io/vtable.h"
#include "io_core.h"

VISIBILITY_SOURCE_BEGIN

struct anjay_output_ctx_struct {
    const anjay_output_ctx_vtable_t *vtable;
};

struct anjay_ret_bytes_ctx_struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
};

#ifdef WITH_LEGACY_CONTENT_FORMAT_SUPPORT

uint16_t _anjay_translate_legacy_content_format(uint16_t format) {
    static const char MSG_FMT[] =
            "legacy application/vnd.oma.lwm2m+%s Content-Format value: %d";

    switch (format) {
    case ANJAY_COAP_FORMAT_LEGACY_PLAINTEXT:
        anjay_log(DEBUG, MSG_FMT, "text", ANJAY_COAP_FORMAT_LEGACY_PLAINTEXT);
        return ANJAY_COAP_FORMAT_PLAINTEXT;

    case ANJAY_COAP_FORMAT_LEGACY_TLV:
        anjay_log(DEBUG, MSG_FMT, "tlv", ANJAY_COAP_FORMAT_LEGACY_TLV);
        return ANJAY_COAP_FORMAT_TLV;

    case ANJAY_COAP_FORMAT_LEGACY_JSON:
        anjay_log(DEBUG, MSG_FMT, "json", ANJAY_COAP_FORMAT_LEGACY_JSON);
        return ANJAY_COAP_FORMAT_JSON;

    case ANJAY_COAP_FORMAT_LEGACY_OPAQUE:
        anjay_log(DEBUG, MSG_FMT, "opaque", ANJAY_COAP_FORMAT_LEGACY_OPAQUE);
        return ANJAY_COAP_FORMAT_OPAQUE;

    default:
        return format;
    }
}

#endif // WITH_LEGACY_CONTENT_FORMAT_SUPPORT

int _anjay_handle_requested_format(uint16_t *out_ptr, uint16_t new_value) {
    if (*out_ptr == AVS_COAP_FORMAT_NONE) {
        *out_ptr = new_value;
    } else if (_anjay_translate_legacy_content_format(*out_ptr) != new_value) {
        return ANJAY_OUTCTXERR_FORMAT_MISMATCH;
    }
    return 0;
}

int *_anjay_output_ctx_errno_ptr(anjay_output_ctx_t *ctx) {
    assert(ctx->vtable->errno_ptr);
    return ctx->vtable->errno_ptr(ctx);
}

static inline void set_out_errno(anjay_output_ctx_t *ctx, int value) {
    int *errno_ptr = _anjay_output_ctx_errno_ptr(ctx);
    assert(errno_ptr);
    *errno_ptr = value;
}

static inline void set_errno_not_implemented(anjay_output_ctx_t *ctx) {
    set_out_errno(ctx, ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED);
}

anjay_ret_bytes_ctx_t *anjay_ret_bytes_begin(anjay_output_ctx_t *ctx,
                                             size_t length) {
    if (!ctx->vtable->bytes_begin) {
        set_errno_not_implemented(ctx);
        return NULL;
    }
    return ctx->vtable->bytes_begin(ctx, length);
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
    if (!ctx->vtable->string) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->string(ctx, value);
}

int anjay_ret_i32(anjay_output_ctx_t *ctx, int32_t value) {
    if (!ctx->vtable->i32) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->i32(ctx, value);
}

int anjay_ret_i64(anjay_output_ctx_t *ctx, int64_t value) {
    if (!ctx->vtable->i64) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->i64(ctx, value);
}

int anjay_ret_float(anjay_output_ctx_t *ctx, float value) {
    if (!ctx->vtable->f32) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->f32(ctx, value);
}

int anjay_ret_double(anjay_output_ctx_t *ctx, double value) {
    if (!ctx->vtable->f64) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->f64(ctx, value);
}

int anjay_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    if (!ctx->vtable->boolean) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->boolean(ctx, value);
}

int anjay_ret_objlnk(anjay_output_ctx_t *ctx,
                     anjay_oid_t oid,
                     anjay_iid_t iid) {
    if (!ctx->vtable->objlnk) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->objlnk(ctx, oid, iid);
}

anjay_output_ctx_t *anjay_ret_array_start(anjay_output_ctx_t *ctx) {
    if (!ctx->vtable->array_start) {
        set_errno_not_implemented(ctx);
        return NULL;
    }
    return ctx->vtable->array_start(ctx);
}

int anjay_ret_array_index(anjay_output_ctx_t *array_ctx, anjay_riid_t index) {
    return _anjay_output_set_id(array_ctx, ANJAY_ID_RIID, index);
}

int anjay_ret_array_finish(anjay_output_ctx_t *array_ctx) {
    if (!array_ctx->vtable->array_finish) {
        set_errno_not_implemented(array_ctx);
        return -1;
    }
    return array_ctx->vtable->array_finish(array_ctx);
}

anjay_output_ctx_t *_anjay_output_object_start(anjay_output_ctx_t *ctx) {
    if (!ctx->vtable->object_start) {
        set_errno_not_implemented(ctx);
        return NULL;
    }
    return ctx->vtable->object_start(ctx);
}

int _anjay_output_object_finish(anjay_output_ctx_t *ctx) {
    if (!ctx->vtable->object_finish) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->object_finish(ctx);
}

int _anjay_output_set_id(anjay_output_ctx_t *ctx,
                         anjay_id_type_t type,
                         uint16_t id) {
    if (!ctx->vtable->set_id) {
        set_errno_not_implemented(ctx);
        return -1;
    }
    return ctx->vtable->set_id(ctx, type, id);
}

int _anjay_output_ctx_destroy(anjay_output_ctx_t **ctx_ptr) {
    int retval = 0;
    anjay_output_ctx_t *ctx = *ctx_ptr;
    if (ctx) {
        if (ctx->vtable->close) {
            retval = ctx->vtable->close(*ctx_ptr);
        }
        avs_free(ctx);
        *ctx_ptr = NULL;
    }
    return retval;
}

struct anjay_input_ctx_struct {
    const anjay_input_ctx_vtable_t *vtable;
};

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

typedef struct {
    const avs_stream_v_table_t *const vtable;
    anjay_input_ctx_t *backend;
} bytes_stream_t;

static int unimplemented() {
    return -1;
}

static int bytes_stream_read(avs_stream_abstract_t *stream,
                             size_t *out_bytes_read,
                             char *out_message_finished,
                             void *buffer,
                             size_t buffer_length) {
    anjay_input_ctx_t **backend_ptr = &((bytes_stream_t *) stream)->backend;
    if (*backend_ptr) {
        bool message_finished;
        int retval = anjay_get_bytes(*backend_ptr, out_bytes_read,
                                     &message_finished, buffer, buffer_length);
        if (!retval && (*out_message_finished = message_finished)) {
            *backend_ptr = NULL;
        }
        return retval;
    } else {
        *out_bytes_read = 0;
        *out_message_finished = 1;
        return 0;
    }
}

static int bytes_stream_close(avs_stream_abstract_t *stream) {
    char buf[256];
    size_t bytes_read;
    char message_finished = 0;
    while (!bytes_stream_read(stream, &bytes_read, &message_finished, buf,
                              sizeof(buf))
           && !message_finished)
        ;
    return 0;
}

avs_stream_abstract_t *_anjay_input_bytes_stream(anjay_input_ctx_t *ctx) {
    static const avs_stream_v_table_t VTABLE = {
        (avs_stream_write_some_t) unimplemented,
        (avs_stream_finish_message_t) unimplemented,
        bytes_stream_read,
        (avs_stream_peek_t) unimplemented,
        (avs_stream_reset_t) unimplemented,
        bytes_stream_close,
        (avs_stream_errno_t) unimplemented,
        NULL
    };
    bytes_stream_t specimen = { &VTABLE, ctx };
    bytes_stream_t *out = (bytes_stream_t *) avs_malloc(sizeof(bytes_stream_t));
    if (out) {
        memcpy(out, &specimen, sizeof(bytes_stream_t));
    }
    return (avs_stream_abstract_t *) out;
}

int anjay_get_string(anjay_input_ctx_t *ctx, char *out_buf, size_t buf_size) {
    if (!ctx->vtable->string) {
        return -1;
    }
    return ctx->vtable->string(ctx, out_buf, buf_size);
}

int anjay_get_i32(anjay_input_ctx_t *ctx, int32_t *out) {
    if (!ctx->vtable->i32) {
        return -1;
    }
    return ctx->vtable->i32(ctx, out);
}

int anjay_get_i64(anjay_input_ctx_t *ctx, int64_t *out) {
    if (!ctx->vtable->i64) {
        return -1;
    }
    return ctx->vtable->i64(ctx, out);
}

int anjay_get_float(anjay_input_ctx_t *ctx, float *out) {
    if (!ctx->vtable->f32) {
        return -1;
    }
    return ctx->vtable->f32(ctx, out);
}

int anjay_get_double(anjay_input_ctx_t *ctx, double *out) {
    if (!ctx->vtable->f64) {
        return -1;
    }
    return ctx->vtable->f64(ctx, out);
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

int _anjay_input_attach_child(anjay_input_ctx_t *ctx,
                              anjay_input_ctx_t *child) {
    if (!ctx->vtable->attach_child) {
        return -1;
    }
    return ctx->vtable->attach_child(ctx, child);
}

anjay_input_ctx_t *_anjay_input_nested_ctx(anjay_input_ctx_t *ctx) {
    anjay_input_ctx_t *retval = NULL;
    avs_stream_abstract_t *stream = _anjay_input_bytes_stream(ctx);
    if (stream && _anjay_input_tlv_create(&retval, &stream, true)) {
        avs_stream_cleanup(&stream);
    }
    if (retval && _anjay_input_attach_child(ctx, retval)) {
        _anjay_input_ctx_destroy(&retval);
    }
    return retval;
}

anjay_input_ctx_t *anjay_get_array(anjay_input_ctx_t *ctx) {
    anjay_id_type_t type;
    uint16_t id;
    if (_anjay_input_get_id(ctx, &type, &id) || type != ANJAY_ID_RID) {
        return NULL;
    }
    return _anjay_input_nested_ctx(ctx);
}

int _anjay_input_get_id(anjay_input_ctx_t *ctx,
                        anjay_id_type_t *out_type,
                        uint16_t *out_id) {
    if (!ctx->vtable->get_id) {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return ctx->vtable->get_id(ctx, out_type, out_id);
}

int _anjay_input_next_entry(anjay_input_ctx_t *ctx) {
    if (!ctx->vtable->next_entry) {
        return -1;
    }
    return ctx->vtable->next_entry(ctx);
}

int anjay_get_array_index(anjay_input_ctx_t *ctx, anjay_riid_t *out_index) {
    anjay_id_type_t type;
    int retval;
    if ((retval = _anjay_input_next_entry(ctx))
            || (retval = _anjay_input_get_id(ctx, &type, out_index))) {
        return retval;
    }
    return (type == ANJAY_ID_RIID) ? 0 : -1;
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
#    include "test/io.c"
#endif
