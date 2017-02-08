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

#include <endian.h>

#include <avsystem/commons/stream_v_table.h>

#include "../utils.h"
#include "tlv.h"
#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const avs_stream_v_table_t * vtable;
    avs_stream_abstract_t *backend;
    char finished;
} tlv_single_msg_stream_wrapper_t;

typedef struct {
    const anjay_input_ctx_vtable_t *vtable;
    tlv_single_msg_stream_wrapper_t stream;
    bool autoclose;
    anjay_input_ctx_t *child;
    anjay_id_type_t id_type;
    int32_t id;
    size_t length;
    size_t bytes_read;
} tlv_in_t;

static int tlv_get_some_bytes(anjay_input_ctx_t *ctx_,
                              size_t *out_bytes_read,
                              bool *out_message_finished,
                              void *out_buf,
                              size_t buf_size) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (ctx->id < 0) {
        anjay_id_type_t placeholder_type;
        uint16_t placeholder_id;
        int retval =
                _anjay_input_get_id(ctx_, &placeholder_type, &placeholder_id);
        if (retval) {
            return retval;
        }
    }
    char stream_finished;
    *out_bytes_read = 0;
    buf_size = ANJAY_MIN(buf_size, ctx->length - ctx->bytes_read);
    int retval = avs_stream_read((avs_stream_abstract_t *) &ctx->stream,
                                 out_bytes_read, &stream_finished,
                                 out_buf, buf_size);
    ctx->bytes_read += *out_bytes_read;
    if (retval) {
        return retval;
    }
    if (!(*out_message_finished = (ctx->bytes_read == ctx->length))
            && stream_finished) {
        return -1;
    }
    return 0;
}

static int tlv_read_to_end(anjay_input_ctx_t *ctx_,
                           size_t *out_bytes_read,
                           void *out_buf,
                           size_t buf_size) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (ctx->id >= 0 && ctx->bytes_read) {
        return -1;
    }
    bool message_finished;
    char *ptr = (char *) out_buf;
    char *endptr = ptr + buf_size;
    do {
        size_t bytes_read = 0;
        int retval = tlv_get_some_bytes(ctx_, &bytes_read, &message_finished,
                                        ptr, (size_t) (endptr - ptr));
        if (retval) {
            return retval;
        }
        ptr += bytes_read;
    } while (!message_finished && ptr < endptr);
    *out_bytes_read = (size_t) (ptr - (char *) out_buf);
    return message_finished ? 0 : -1;
}

static int tlv_get_string(anjay_input_ctx_t *ctx,
                          char *out_buf,
                          size_t buf_size) {
    if (!buf_size) {
        return -1;
    }
    size_t bytes_read = 0;
    int retval = tlv_read_to_end(ctx, &bytes_read, out_buf, buf_size - 1);
    out_buf[bytes_read] = '\0';
    return retval;
}

#define DEF_GETI(Bits) \
static int tlv_get_i##Bits (anjay_input_ctx_t *ctx, int##Bits##_t *value) { \
    uint8_t bytes[Bits / 8]; \
    size_t bytes_read = 0; \
    int retval = tlv_read_to_end(ctx, &bytes_read, bytes, sizeof(bytes)); \
    if (retval) { \
        return retval; \
    } \
    if (!_anjay_is_power_of_2(bytes_read)) { \
        return -1; \
    } \
    *value = (bytes_read > 0 && ((int8_t) bytes[0]) < 0) ? -1 : 0; \
    for (size_t i = 0; i < bytes_read; ++i) { \
        *(uint##Bits##_t *) value <<= 8; \
        *value += bytes[i]; \
    } \
    return 0; \
}

DEF_GETI(32)
DEF_GETI(64)

#define DEF_GETF(Type) \
static int tlv_get_##Type (anjay_input_ctx_t *ctx, Type *value) { \
    union { \
        uint32_t f32; \
        uint64_t f64; \
    } data; \
    size_t bytes_read = 0; \
    int retval = tlv_read_to_end(ctx, &bytes_read, &data, 8); \
    if (retval) { \
        return retval; \
    } \
    switch (bytes_read) { \
    case 4: \
        *value = (Type) _anjay_ntohf(data.f32); \
        return 0; \
    case 8: \
        *value = (Type) _anjay_ntohd(data.f64); \
        return 0; \
    default: \
        return -1; \
    } \
}

DEF_GETF(float)
DEF_GETF(double)

static int tlv_get_bool(anjay_input_ctx_t *ctx, bool *value) {
    char raw;
    size_t bytes_read = 0;
    int retval = tlv_read_to_end(ctx, &bytes_read, &raw, 1);
    if (retval) {
        return retval;
    } else if (bytes_read != 1) {
        return -1;
    }
    switch (raw) {
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

static int tlv_get_objlnk(anjay_input_ctx_t *ctx,
                          anjay_oid_t *out_oid, anjay_iid_t *out_iid) {
    AVS_STATIC_ASSERT(sizeof(uint16_t[2]) == 4, uint16_t_array_size);
    uint16_t raw[2];
    size_t bytes_read = 0;
    int retval = tlv_read_to_end(ctx, &bytes_read, raw, 4);
    if (retval) {
        return retval;
    } else if (bytes_read != 4) {
        return -1;
    }
    *out_oid = ntohs(raw[0]);
    *out_iid = ntohs(raw[1]);
    return 0;
}

#define DEF_READ_SHORTENED(Type) \
static int read_shortened_##Type (avs_stream_abstract_t *stream, \
                                  size_t length, \
                                  Type *out) { \
    uint8_t bytes[sizeof(Type)]; \
    int retval = avs_stream_read_reliably(stream, bytes, length); \
    if (retval) { \
        return retval; \
    } \
    *out = 0; \
    for (size_t i = 0; i < length; ++i) { \
        *out = (Type) ((*out << 8) + bytes[i]); \
    } \
    return 0; \
}

DEF_READ_SHORTENED(uint16_t)
DEF_READ_SHORTENED(size_t)

static int tlv_in_attach_child(anjay_input_ctx_t *ctx_,
                               anjay_input_ctx_t *child) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    int retval = _anjay_input_ctx_destroy(&ctx->child);
    if (retval) {
        return retval;
    }
    ctx->child = child;
    return 0;
}

static anjay_id_type_t convert_id_type(uint8_t typefield) {
    tlv_id_type_t tlv_type = (tlv_id_type_t) ((typefield >> 6) & 3);
    switch (tlv_type) {
    default:
        assert(0 && "Invalid TLV ID type");
    case TLV_ID_IID:
        return ANJAY_ID_IID;
    case TLV_ID_RIID:
        return ANJAY_ID_RIID;
    case TLV_ID_RID_ARRAY:
    case TLV_ID_RID:
        return ANJAY_ID_RID;
    }
}

static int tlv_get_id(anjay_input_ctx_t *ctx_,
                      anjay_id_type_t *out_type, uint16_t *out_id) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (ctx->id >= 0) {
        *out_type = ctx->id_type;
        *out_id = (uint16_t) ctx->id;
        return 0;
    }
    uint8_t typefield;
    int retval = 0;
    size_t bytes_read = 0;
    char message_finished = 0;
    while (!retval && !bytes_read && !message_finished) {
        message_finished = 0;
        retval = avs_stream_read((avs_stream_abstract_t *) &ctx->stream,
                                 &bytes_read, &message_finished, &typefield, 1);
    }
    if (retval) {
        return retval;
    } else if (!bytes_read) {
        return ANJAY_GET_INDEX_END;
    }
    *out_type = convert_id_type(typefield);
    if ((retval =
            read_shortened_uint16_t((avs_stream_abstract_t *) &ctx->stream,
                                    (typefield & 0x20) ? 2 : 1, out_id))) {
        return retval;
    }
    size_t length_length = ((typefield >> 3) & 3);
    if (!length_length) {
        ctx->length = (typefield & 7);
    } else if ((retval =
            read_shortened_size_t((avs_stream_abstract_t *) &ctx->stream,
                                  length_length, &ctx->length))) {
        return retval;
    }
    ctx->bytes_read = 0;
    ctx->id_type = *out_type;
    ctx->id = *out_id;
    return 0;
}

static int tlv_next_entry(anjay_input_ctx_t *ctx_) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    if (ctx->id < 0) {
        return 0;
    }

    char ignore[64];
    size_t ignore_bytes_read;
    bool message_finished = false;
    while (!message_finished) {
        int retval = tlv_get_some_bytes(ctx_, &ignore_bytes_read,
                                        &message_finished,
                                        ignore, sizeof(ignore));
        if (retval) {
            return retval;
        }
    }

    ctx->id = -1;
    return 0;
}

static int tlv_in_close(anjay_input_ctx_t *ctx_) {
    tlv_in_t *ctx = (tlv_in_t *) ctx_;
    _anjay_input_ctx_destroy(&ctx->child);
    if (ctx->autoclose) {
        avs_stream_cleanup(&ctx->stream.backend);
    }
    return 0;
}

static const anjay_input_ctx_vtable_t TLV_IN_VTABLE = {
    tlv_get_some_bytes,
    tlv_get_string,
    tlv_get_i32,
    tlv_get_i64,
    tlv_get_float,
    tlv_get_double,
    tlv_get_bool,
    tlv_get_objlnk,
    tlv_in_attach_child,
    tlv_get_id,
    tlv_next_entry,
    tlv_in_close
};

static int tlv_safe_read(avs_stream_abstract_t *stream_,
                         size_t *out_bytes_read,
                         char *out_message_finished,
                         void *buffer,
                         size_t buffer_length) {
    tlv_single_msg_stream_wrapper_t *stream =
            (tlv_single_msg_stream_wrapper_t *) stream_;
    int result = 0;
    if (stream->finished) {
        *out_bytes_read = 0;
    } else {
        result = avs_stream_read(stream->backend, out_bytes_read,
                                 &stream->finished, buffer, buffer_length);
    }
    *out_message_finished = stream->finished;
    return result;
}

static const avs_stream_v_table_t TLV_SINGLE_MSG_STREAM_WRAPPER_VTABLE = {
    .read = tlv_safe_read
};

int _anjay_input_tlv_create(anjay_input_ctx_t **out,
                            avs_stream_abstract_t **stream_ptr,
                            bool autoclose) {
    tlv_in_t *ctx = (tlv_in_t *) calloc(1, sizeof(tlv_in_t));
    *out = (anjay_input_ctx_t *) ctx;
    if (!ctx) {
        return -1;
    }

    ctx->vtable = &TLV_IN_VTABLE;
    ctx->stream.vtable = &TLV_SINGLE_MSG_STREAM_WRAPPER_VTABLE;
    ctx->stream.backend = *stream_ptr;
    if (autoclose) {
        *stream_ptr = NULL;
        ctx->autoclose = true;
    }
    ctx->id = -1;
    return 0;
}

#ifdef ANJAY_TEST
#include "test/tlv_in.c"
#endif
