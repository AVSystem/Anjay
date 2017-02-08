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

#include <assert.h>
#include <string.h>

#include <endian.h>

#include <avsystem/commons/list.h>
#include <avsystem/commons/stream.h>

#include "../io.h"
#include "tlv.h"
#include "vtable.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    tlv_id_type_t type;
    int32_t id;
} tlv_id_t;

typedef struct {
    size_t data_length;
    tlv_id_t id;
    char data[];
} tlv_entry_t;

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    char *out_ptr;
    size_t bytes_left;
} tlv_buffered_bytes_t;

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    avs_stream_abstract_t *stream;
    size_t bytes_left;
} tlv_streamed_bytes_t;

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
} tlv_null_bytes_t;

typedef union {
    tlv_buffered_bytes_t buffered;
    tlv_streamed_bytes_t streamed;
    tlv_null_bytes_t null;
} tlv_bytes_t;

typedef struct tlv_out_struct {
    const anjay_output_ctx_vtable_t *vtable;
    int *errno_ptr;
    struct tlv_out_struct *parent;
    anjay_output_ctx_t *slave;
    AVS_LIST(tlv_entry_t) entries;
    AVS_LIST(tlv_entry_t) *next_entry_ptr;
    avs_stream_abstract_t *stream;
    tlv_id_t next_id;
    tlv_bytes_t bytes_ctx;
} tlv_out_t;

static int *tlv_errno_ptr(anjay_output_ctx_t *ctx) {
    return ((tlv_out_t *) ctx)->errno_ptr;
}

static inline uint8_t u32_length(uint32_t value) {
    uint8_t result = 1;
    while ((value >>= 8)) {
        ++result;
    }
    return result;
}

static inline uint8_t typefield_length(uint32_t length) {
    if (length <= 7) {
        return (uint8_t) length;
    } else {
        return (uint8_t) (u32_length(length) << 3);
    }
}

static int write_shortened_u32(avs_stream_abstract_t *stream, uint32_t value) {
    uint8_t length = u32_length(value);
    assert(length <= 4);
    union {
        uint32_t uval;
        char tab[4];
    } value32;
    value32.uval = htobe32(value);
    return avs_stream_write(stream, value32.tab + (4 - length), length);
}

static size_t header_size(uint16_t id, size_t length) {
    assert(length == (uint32_t) length);
    return 1 + (size_t) u32_length(id) +
            ((length > 7) ? (size_t) u32_length((uint32_t) length) : 0);
}

static int write_header(avs_stream_abstract_t *stream,
                        const tlv_id_t *id,
                        size_t length) {
    if (id->id != (uint16_t) id->id || length >> 24) {
        return -1;
    }
    uint8_t typefield = (uint8_t) (
            ((id->type & 3) << 6) |
            ((id->id > UINT8_MAX) ? 0x20 : 0) |
            typefield_length((uint32_t) length));
    int retval = avs_stream_write(stream, &typefield, 1);
    if (!retval) {
        retval = write_shortened_u32(stream, (uint16_t) id->id);
    }
    if (!retval && length > 7) {
        retval = write_shortened_u32(stream, (uint32_t) length);
    }
    return retval;
}

static inline int ensure_valid_for_value(tlv_out_t *ctx) {
    return (ctx->slave
            || !(ctx->next_id.type == TLV_ID_RIID
                    || ctx->next_id.type == TLV_ID_RID)
            || ctx->next_id.id < 0) ? -1 : 0;
}

static int write_entry(avs_stream_abstract_t *stream,
                       const tlv_id_t *id,
                       const void *buf,
                       size_t length) {
    int retval = write_header(stream, id, length);
    if (!retval) {
        retval = avs_stream_write(stream, buf, length);
    }
    return retval;
}

static char *add_buffered_entry(tlv_out_t *ctx, size_t length) {
    tlv_entry_t *new_entry = (tlv_entry_t *)
            AVS_LIST_NEW_BUFFER(sizeof(tlv_entry_t) + length);
    if (!new_entry) {
        return NULL;
    }
    new_entry->data_length = length;
    new_entry->id = ctx->next_id;
    ctx->next_id.id = -1;
    *ctx->next_entry_ptr = new_entry;
    ctx->next_entry_ptr = AVS_LIST_NEXT_PTR(ctx->next_entry_ptr);
    return new_entry->data;
}

static int streamed_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length) {
    tlv_streamed_bytes_t *ctx = (tlv_streamed_bytes_t *) ctx_;
    int retval = 0;
    if (length) {
        if (length > ctx->bytes_left) {
            retval = -1;
        } else {
            retval = avs_stream_write(ctx->stream, data, length);
        }
    }
    if (!retval && !(ctx->bytes_left -= length)) {
        ctx->vtable = NULL;
    }
    return retval;
}

static const anjay_ret_bytes_ctx_vtable_t STREAMED_BYTES_VTABLE = {
    .append = streamed_bytes_append
};

static int buffered_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length) {
    tlv_buffered_bytes_t *ctx = (tlv_buffered_bytes_t *) ctx_;
    int retval = 0;
    if (length) {
        if (length > ctx->bytes_left) {
            retval = -1;
        } else {
            memcpy(ctx->out_ptr, data, length);
            ctx->out_ptr += length;
        }
    }
    if (!retval && !(ctx->bytes_left -= length)) {
        ctx->vtable = NULL;
    }
    return retval;
}

static const anjay_ret_bytes_ctx_vtable_t BUFFERED_BYTES_VTABLE = {
    .append = buffered_bytes_append
};

static anjay_ret_bytes_ctx_t *add_entry(tlv_out_t *ctx, size_t length) {
    if (length >> 24 || ctx->bytes_ctx.null.vtable) {
        return NULL;
    }
    if (ctx->stream) {
        int retval = write_header(ctx->stream, &ctx->next_id, length);
        ctx->next_id.id = -1;
        if (!retval) {
            ctx->bytes_ctx.streamed.vtable = &STREAMED_BYTES_VTABLE;
            ctx->bytes_ctx.streamed.stream = ctx->stream;
            ctx->bytes_ctx.streamed.bytes_left = length;
            return (anjay_ret_bytes_ctx_t *) &ctx->bytes_ctx.streamed;
        }
    } else if (ctx->parent) {
        if ((ctx->bytes_ctx.buffered.out_ptr =
                add_buffered_entry(ctx, length))) {
            ctx->bytes_ctx.buffered.vtable = &BUFFERED_BYTES_VTABLE;
            ctx->bytes_ctx.buffered.bytes_left = length;
            return (anjay_ret_bytes_ctx_t *) &ctx->bytes_ctx.buffered;
        }
    }
    return NULL;
}

static anjay_ret_bytes_ctx_t *tlv_ret_bytes(anjay_output_ctx_t *ctx_,
                                            size_t length) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    if (ensure_valid_for_value(ctx)) {
        return NULL;
    }
    return add_entry(ctx, length);
}

static int tlv_ret_string(anjay_output_ctx_t *ctx, const char *value) {
    return anjay_ret_bytes(ctx, value, strlen(value));
}

#define DEF_IRET(Half, Bits) \
static int tlv_ret_i##Bits(anjay_output_ctx_t *ctx, int##Bits##_t value) { \
    if (value == (int##Half##_t) value) { \
        return tlv_ret_i##Half(ctx, (int##Half##_t) value); \
    } \
    uint##Bits##_t portable = htobe##Bits ((uint##Bits##_t) value); \
    return anjay_ret_bytes(ctx, &portable, sizeof(portable)); \
}

static int tlv_ret_i8(anjay_output_ctx_t *ctx, int8_t value) {
    return anjay_ret_bytes(ctx, &value, 1);
}

DEF_IRET( 8, 16)
DEF_IRET(16, 32)
DEF_IRET(32, 64)

static int tlv_ret_float(anjay_output_ctx_t *ctx, float value) {
    uint32_t portable = _anjay_htonf(value);
    return anjay_ret_bytes(ctx, &portable, sizeof(portable));
}

static int tlv_ret_double(anjay_output_ctx_t *ctx, double value) {
    if (((double) ((float) value)) == value) {
        return tlv_ret_float(ctx, (float) value);
    } else {
        uint64_t portable = _anjay_htond(value);
        return anjay_ret_bytes(ctx, &portable, sizeof(portable));
    }
}

static int tlv_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    return tlv_ret_i8(ctx, value);
}

static int tlv_ret_objlnk(anjay_output_ctx_t *ctx,
                          anjay_oid_t oid, anjay_iid_t iid) {
    uint32_t portable = htobe32(((uint32_t) oid << 16) | (uint32_t) iid);
    return anjay_ret_bytes(ctx, &portable, sizeof(portable));
}

static anjay_output_ctx_t *tlv_slave_start(tlv_out_t *ctx,
                                           tlv_id_type_t expected_type,
                                           tlv_id_type_t new_type,
                                           tlv_id_type_t inner_type);

static int tlv_slave_finish(tlv_out_t *ctx, tlv_id_type_t next_id_type) {
    if (!ctx->parent) {
        return -1;
    }
    size_t data_size = 0;
    {
        tlv_entry_t *entry = NULL;
        AVS_LIST_FOREACH(entry, ctx->entries) {
            data_size += header_size((uint16_t) entry->id.id,
                                     entry->data_length) +
                    entry->data_length;
        }
    }
    char *buffer = (char *) (data_size ? malloc(data_size) : NULL);
    int retval = ((!data_size || buffer) ? 0 : -1);
    avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&outbuf, buffer, data_size);
    AVS_LIST_CLEAR(&ctx->entries) {
        if (!retval) {
            retval = write_entry((avs_stream_abstract_t *) &outbuf,
                                 &ctx->entries->id,
                                 ctx->entries->data,
                                 ctx->entries->data_length);
        }
    }
    if (!retval) {
        size_t length = avs_stream_outbuf_offset(&outbuf);
        anjay_ret_bytes_ctx_t *bytes = add_entry(ctx->parent, length);
        retval = !bytes ? -1 : anjay_ret_bytes_append(bytes, buffer, length);
    }
    free(buffer);
    ctx->parent->next_id.type = next_id_type;
    _anjay_output_ctx_destroy((anjay_output_ctx_t **) &ctx);
    return retval;
}

static anjay_output_ctx_t *tlv_ret_array_start(anjay_output_ctx_t *ctx) {
    return tlv_slave_start((tlv_out_t *) ctx,
                           TLV_ID_RID, TLV_ID_RID_ARRAY, TLV_ID_RIID);
}

static int tlv_ret_array_index(anjay_output_ctx_t *ctx_, anjay_riid_t riid) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    if (ctx->slave || ctx->next_id.type != TLV_ID_RIID
            || ctx->next_id.id >= 0) {
        return -1;
    }
    ctx->next_id.id = riid;
    return 0;
}

static int tlv_ret_array_finish(anjay_output_ctx_t *ctx_) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    if (ctx->next_id.type != TLV_ID_RIID) {
        return -1;
    }
    return tlv_slave_finish(ctx, TLV_ID_RID);
}

static anjay_output_ctx_t *tlv_ret_object_start(anjay_output_ctx_t *ctx) {
    return tlv_slave_start((tlv_out_t *) ctx,
                           TLV_ID_IID, TLV_ID_IID, TLV_ID_RID);
}

static int tlv_ret_object_finish(anjay_output_ctx_t *ctx_) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    if (!ctx->parent || ctx->parent->next_id.type != TLV_ID_IID) {
        return -1;
    }
    return tlv_slave_finish(ctx, TLV_ID_IID);
}

static int tlv_set_id(anjay_output_ctx_t *ctx_,
                      anjay_id_type_t type, uint16_t id) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    if (ctx->slave) {
        return -1;
    }
    switch (type) {
    case ANJAY_ID_IID:
        ctx->next_id.type = TLV_ID_IID;
        break;
    case ANJAY_ID_RID:
        ctx->next_id.type = TLV_ID_RID;
        break;
    case ANJAY_ID_RIID:
        ctx->next_id.type = TLV_ID_RIID;
        break;
    default:
        return -1;
    }
    ctx->next_id.id = id;
    return 0;
}

static int tlv_output_close(anjay_output_ctx_t *ctx_) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    AVS_LIST_CLEAR(&ctx->entries);
    int retval = _anjay_output_ctx_destroy(&ctx->slave);
    if (ctx->parent) {
        ctx->parent->next_id.id = -1;
        ctx->parent->slave = NULL;
    }
    return retval;
}

static const anjay_output_ctx_vtable_t TLV_OUT_VTABLE = {
    tlv_errno_ptr,
    tlv_ret_bytes,
    tlv_ret_string,
    tlv_ret_i32,
    tlv_ret_i64,
    tlv_ret_float,
    tlv_ret_double,
    tlv_ret_bool,
    tlv_ret_objlnk,
    tlv_ret_array_start,
    tlv_ret_array_index,
    tlv_ret_array_finish,
    tlv_ret_object_start,
    tlv_ret_object_finish,
    tlv_set_id,
    tlv_output_close
};

static anjay_output_ctx_t *tlv_slave_start(tlv_out_t *ctx,
                                           tlv_id_type_t expected_type,
                                           tlv_id_type_t new_type,
                                           tlv_id_type_t inner_type) {
    tlv_out_t *object = NULL;
    if (ctx->slave
            || ctx->next_id.type != expected_type
            || ctx->next_id.id < 0
            || !(object = (tlv_out_t *) calloc(1, sizeof(tlv_out_t)))) {
        return NULL;
    }
    object->vtable = &TLV_OUT_VTABLE;
    object->errno_ptr = ctx->errno_ptr;
    object->parent = ctx;
    object->next_entry_ptr = &object->entries;
    object->next_id.type = inner_type;
    object->next_id.id = -1;
    ctx->next_id.type = new_type;
    return (ctx->slave = (anjay_output_ctx_t *) object);
}

anjay_output_ctx_t *
_anjay_output_raw_tlv_create(avs_stream_abstract_t *stream) {
    tlv_out_t *ctx = (tlv_out_t *) calloc(1, sizeof(tlv_out_t));

    if (ctx) {
        ctx->vtable = &TLV_OUT_VTABLE;
        ctx->errno_ptr = NULL;
        ctx->next_entry_ptr = &ctx->entries;
        ctx->stream = stream;
        ctx->next_id.id = -1;
    }
    return (anjay_output_ctx_t *) ctx;
}

anjay_output_ctx_t *
_anjay_output_tlv_create(avs_stream_abstract_t *stream,
                         int *errno_ptr,
                         anjay_msg_details_t *inout_details) {
    anjay_output_ctx_t *ctx = _anjay_output_raw_tlv_create(stream);
    if (ctx && ((*errno_ptr = _anjay_handle_requested_format(
                    &inout_details->format, ANJAY_COAP_FORMAT_TLV))
            || _anjay_coap_stream_setup_response(stream, inout_details))) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

#ifdef ANJAY_TEST
#include "test/tlv_out.c"
#endif
