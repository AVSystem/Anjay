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

#ifndef ANJAY_WITHOUT_TLV

#    include <assert.h>
#    include <string.h>

#    include <avsystem/commons/avs_list.h>
#    include <avsystem/commons/avs_memory.h>
#    include <avsystem/commons/avs_stream.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_io_core.h"
#    include "../coap/anjay_content_format.h"
#    include "anjay_tlv.h"
#    include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    tlv_id_type_t type;
    uint16_t id;
} tlv_id_t;

#    define TLV_MAX_LENGTH ((1 << 24) - 1)

typedef struct {
    size_t data_length;
    tlv_id_t id;
    char data[];
} tlv_entry_t;

typedef struct {
    const anjay_ret_bytes_ctx_vtable_t *vtable;
    union {
        char *buffer_ptr;
        avs_stream_t *stream;
    } output;
    size_t bytes_left;
} tlv_bytes_t;

typedef struct {
    AVS_LIST(tlv_entry_t) entries;
    AVS_LIST(tlv_entry_t) *next_entry_ptr;

    // ID that will be used when serializing the next element.
    // ANJAY_ID_INVALID if it's not set.
    uint16_t next_id;

    tlv_bytes_t bytes_ctx;
} tlv_out_level_t;

typedef enum {
    TLV_OUT_LEVEL_IID = 0,
    TLV_OUT_LEVEL_RID = 1,
    TLV_OUT_LEVEL_RIID = 2,
    _TLV_OUT_LEVEL_LIMIT
} tlv_out_level_id_t;

typedef struct tlv_out_struct {
    anjay_output_ctx_t base;
    avs_stream_t *stream;
    anjay_uri_path_t root_path;
    tlv_out_level_t levels[_TLV_OUT_LEVEL_LIMIT];
    tlv_out_level_id_t level;
} tlv_out_t;

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

static int write_shortened_u32(avs_stream_t *stream, uint32_t value) {
    uint8_t length = u32_length(value);
    assert(length <= 4);
    union {
        uint32_t uval;
        char tab[4];
    } value32;
    value32.uval = avs_convert_be32(value);
    return avs_is_ok(
                   avs_stream_write(stream, value32.tab + (4 - length), length))
                   ? 0
                   : -1;
}

static size_t header_size(uint16_t id, size_t length) {
    assert(length == (uint32_t) length);
    return 1 + (size_t) u32_length(id)
           + ((length > 7) ? (size_t) u32_length((uint32_t) length) : 0);
}

static int write_header(avs_stream_t *stream,
                        tlv_id_type_t type,
                        uint16_t id,
                        size_t length) {
    if (id == ANJAY_ID_INVALID || length > TLV_MAX_LENGTH) {
        return -1;
    }
    uint8_t typefield =
            (uint8_t) (((type & 3) << 6) | ((id > UINT8_MAX) ? 0x20 : 0)
                       | typefield_length((uint32_t) length));
    if (avs_is_err(avs_stream_write(stream, &typefield, 1))
            || write_shortened_u32(stream, id)) {
        return -1;
    }
    if (length > 7) {
        return write_shortened_u32(stream, (uint32_t) length);
    }
    return 0;
}

static tlv_out_level_id_t root_level(const anjay_uri_path_t *root_path) {
    switch (_anjay_uri_path_length(root_path)) {
    case 1: // object path
        return TLV_OUT_LEVEL_IID;
    case 2: // instance path
    case 3: // resource path
        return TLV_OUT_LEVEL_RID;
    case 4: // resource instance path
        return TLV_OUT_LEVEL_RIID;
    default:
        AVS_UNREACHABLE("Invalid root path");
        return (tlv_out_level_id_t) -1;
    }
}

static inline tlv_out_level_t *current_level(tlv_out_t *ctx) {
    return &ctx->levels[ctx->level];
}

static tlv_id_type_t current_level_value_type(tlv_out_t *ctx) {
    AVS_ASSERT(current_level(ctx)->next_id != ANJAY_ID_INVALID,
               "Attempted to serialize value without setting path. This is a "
               "bug in resource reading logic.");
    switch (ctx->level) {
    case TLV_OUT_LEVEL_RID:
        return TLV_ID_RID;
    case TLV_OUT_LEVEL_RIID:
        return TLV_ID_RIID;
    default:
        AVS_UNREACHABLE("Attempted to serialize value with path set to neither "
                        "Resource nor Resource Instance. This is a bug in "
                        "resource reading logic.");
        return (tlv_id_type_t) -1;
    }
}

static int write_entry(avs_stream_t *stream,
                       const tlv_id_t *id,
                       const void *buf,
                       size_t length) {
    if (write_header(stream, id->type, id->id, length)
            || avs_is_err(avs_stream_write(stream, buf, length))) {
        return -1;
    }
    return 0;
}

static char *
add_buffered_entry(tlv_out_t *ctx, tlv_id_type_t type, size_t length) {
    tlv_entry_t *new_entry =
            (tlv_entry_t *) AVS_LIST_NEW_BUFFER(sizeof(tlv_entry_t) + length);
    if (!new_entry) {
        return NULL;
    }
    new_entry->data_length = length;
    new_entry->id.type = type;
    new_entry->id.id = current_level(ctx)->next_id;
    current_level(ctx)->next_id = ANJAY_ID_INVALID;
    *current_level(ctx)->next_entry_ptr = new_entry;
    AVS_LIST_ADVANCE_PTR(&current_level(ctx)->next_entry_ptr);
    return new_entry->data;
}

static int streamed_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length);

static const anjay_ret_bytes_ctx_vtable_t STREAMED_BYTES_VTABLE = {
    .append = streamed_bytes_append
};

static int streamed_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length) {
    tlv_bytes_t *ctx = (tlv_bytes_t *) ctx_;
    assert(ctx->vtable == &STREAMED_BYTES_VTABLE);
    if (length) {
        if (length > ctx->bytes_left
                || avs_is_err(avs_stream_write(ctx->output.stream, data,
                                               length))) {
            return -1;
        }
        ctx->bytes_left -= length;
    }
    return 0;
}

static int buffered_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length);

static const anjay_ret_bytes_ctx_vtable_t BUFFERED_BYTES_VTABLE = {
    .append = buffered_bytes_append
};

static int buffered_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                 const void *data,
                                 size_t length) {
    tlv_bytes_t *ctx = (tlv_bytes_t *) ctx_;
    assert(ctx->vtable == &BUFFERED_BYTES_VTABLE);
    int retval = 0;
    if (length) {
        if (length > ctx->bytes_left) {
            retval = -1;
        } else {
            memcpy(ctx->output.buffer_ptr, data, length);
            ctx->output.buffer_ptr += length;
        }
    }
    if (!retval) {
        ctx->bytes_left -= length;
    }
    return retval;
}

static anjay_ret_bytes_ctx_t *
add_entry(tlv_out_t *ctx, tlv_id_type_t type, size_t length) {
    tlv_out_level_t *out_level = current_level(ctx);
    if (length > TLV_MAX_LENGTH || out_level->bytes_ctx.bytes_left) {
        return NULL;
    }
    if (ctx->level > root_level(&ctx->root_path)) {
        if ((out_level->bytes_ctx.output.buffer_ptr =
                     add_buffered_entry(ctx, type, length))) {
            out_level->bytes_ctx.vtable = &BUFFERED_BYTES_VTABLE;
            out_level->bytes_ctx.bytes_left = length;
            return (anjay_ret_bytes_ctx_t *) &out_level->bytes_ctx;
        }
    } else {
        int retval =
                write_header(ctx->stream, type, out_level->next_id, length);
        out_level->next_id = ANJAY_ID_INVALID;
        if (!retval) {
            out_level->bytes_ctx.vtable = &STREAMED_BYTES_VTABLE;
            out_level->bytes_ctx.output.stream = ctx->stream;
            out_level->bytes_ctx.bytes_left = length;
            return (anjay_ret_bytes_ctx_t *) &out_level->bytes_ctx;
        }
    }
    return NULL;
}

static int tlv_ret_bytes(anjay_output_ctx_t *ctx_,
                         size_t length,
                         anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    *out_bytes_ctx = add_entry(ctx, current_level_value_type(ctx), length);
    return *out_bytes_ctx ? 0 : -1;
}

static int tlv_ret_string(anjay_output_ctx_t *ctx, const char *value) {
    return anjay_ret_bytes(ctx, value, strlen(value));
}

#    define DEF_IRET(Half, Bits)                                      \
        static int tlv_ret_i##Bits(anjay_output_ctx_t *ctx,           \
                                   int##Bits##_t value) {             \
            if (value == (int##Half##_t) value) {                     \
                return tlv_ret_i##Half(ctx, (int##Half##_t) value);   \
            }                                                         \
            uint##Bits##_t portable =                                 \
                    avs_convert_be##Bits((uint##Bits##_t) value);     \
            return anjay_ret_bytes(ctx, &portable, sizeof(portable)); \
        }

static int tlv_ret_i8(anjay_output_ctx_t *ctx, int8_t value) {
    return anjay_ret_bytes(ctx, &value, 1);
}

DEF_IRET(8, 16)
DEF_IRET(16, 32)
DEF_IRET(32, 64)

static int tlv_ret_float(anjay_output_ctx_t *ctx, float value) {
    uint32_t portable = avs_htonf(value);
    return anjay_ret_bytes(ctx, &portable, sizeof(portable));
}

static int tlv_ret_double(anjay_output_ctx_t *ctx, double value) {
    if (((double) ((float) value)) == value) {
        return tlv_ret_float(ctx, (float) value);
    } else {
        uint64_t portable = avs_htond(value);
        return anjay_ret_bytes(ctx, &portable, sizeof(portable));
    }
}

static int tlv_ret_bool(anjay_output_ctx_t *ctx, bool value) {
    return tlv_ret_i8(ctx, value);
}

static int
tlv_ret_objlnk(anjay_output_ctx_t *ctx, anjay_oid_t oid, anjay_iid_t iid) {
    uint32_t portable =
            avs_convert_be32(((uint32_t) oid << 16) | (uint32_t) iid);
    return anjay_ret_bytes(ctx, &portable, sizeof(portable));
}

static void tlv_slave_start(tlv_out_t *ctx);

static int tlv_slave_finish(tlv_out_t *ctx) {
    assert(ctx->level > root_level(&ctx->root_path));
    size_t data_size = 0;
    {
        tlv_entry_t *entry = NULL;
        AVS_LIST_FOREACH(entry, current_level(ctx)->entries) {
            data_size += header_size(entry->id.id, entry->data_length)
                         + entry->data_length;
        }
    }
    char *buffer = (char *) (data_size ? avs_malloc(data_size) : NULL);
    int retval = ((!data_size || buffer) ? 0 : -1);
    avs_stream_outbuf_t outbuf = AVS_STREAM_OUTBUF_STATIC_INITIALIZER;
    avs_stream_outbuf_set_buffer(&outbuf, buffer, data_size);
    AVS_LIST_CLEAR(&current_level(ctx)->entries) {
        if (!retval) {
            retval = write_entry((avs_stream_t *) &outbuf,
                                 &current_level(ctx)->entries->id,
                                 current_level(ctx)->entries->data,
                                 current_level(ctx)->entries->data_length);
        }
    }
    ctx->level = (tlv_out_level_id_t) (ctx->level - 1);
    if (!retval) {
        size_t length = avs_stream_outbuf_offset(&outbuf);
        anjay_ret_bytes_ctx_t *bytes = NULL;
        switch (ctx->level) {
        case TLV_OUT_LEVEL_RID:
            bytes = add_entry(ctx, TLV_ID_RID_ARRAY, length);
            break;
        case TLV_OUT_LEVEL_IID:
            bytes = add_entry(ctx, TLV_ID_IID, length);
            break;
        default:;
        }
        retval = !bytes ? -1 : anjay_ret_bytes_append(bytes, buffer, length);
    }
    avs_free(buffer);
    return retval;
}

static int tlv_start_aggregate(anjay_output_ctx_t *ctx_) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    if (ctx->level == TLV_OUT_LEVEL_RID) {
        if (current_level(ctx)->next_id != ANJAY_ID_INVALID) {
            // STARTING THE RESOURCE INSTANCE ARRAY
            // We have been called after set_path() on a Resource path - hence
            // the current level is RID and we have a valid next_id. We're
            // starting aggregate on the Resource level, i.e., an array of
            // Resource Instances - so we're starting the slave context that
            // will expect Resource Instance entries, or serialize to an empty
            // array if no Resource Instances will follow.
            tlv_slave_start(ctx);
        } else {
            AVS_ASSERT(_anjay_uri_path_leaf_is(&ctx->root_path, ANJAY_ID_IID),
                       "Called tlv_start_aggregate in inappropriate state");
            // INSTANCE IS THE ROOT
            // This case will happen if the TLV context is rooted at the
            // Instance level, i.e., we're responding to a Read with URI
            // pointing to an Object Instance. In that case, the TLV context is
            // configured so that Resource entities are serialized at the top
            // level (hence the top level is RID, because we cannot serialize
            // anything above it, but ID is not set yet), so there is nothing to
            // do to "start the aggregate", we are already the aggregate we are
            // looking for. read_instance() calls start_aggregate() before
            // iterating over resources, so to make it work, we just return
            // success.
        }
    } else {
        assert(ctx->level == TLV_OUT_LEVEL_IID);
        assert(current_level(ctx)->next_id != ANJAY_ID_INVALID);
        // STARTING THE OBJECT INSTANCE
        // We have been called after set_path() on an Object Instance path -
        // hence the current level is IID and we have a valid next_id. We're
        // starting aggregate on the Instance level, i.e. an array of Resources
        // - so we're starting the slave context that will expect Resource
        // entries, or serialize to an empty array if no Resources will follow.
        tlv_slave_start(ctx);
    }
    return 0;
}

static inline tlv_out_level_id_t leaf_level(const anjay_uri_path_t *path) {
    size_t path_length = _anjay_uri_path_length(path);
    static const tlv_out_level_id_t MAPPING[] = {
        // mapping from path length, length==2 means instance path
        [2] = TLV_OUT_LEVEL_IID,
        [3] = TLV_OUT_LEVEL_RID,
        [4] = TLV_OUT_LEVEL_RIID
    };
    assert(path_length >= 2 && path_length <= 4);
    return MAPPING[path_length];
}

static inline uint16_t id_from_path(const anjay_uri_path_t *path,
                                    tlv_out_level_id_t level) {
    static const anjay_id_type_t LEVEL_MAPPING[] = {
        [TLV_OUT_LEVEL_IID] = ANJAY_ID_IID,
        [TLV_OUT_LEVEL_RID] = ANJAY_ID_RID,
        [TLV_OUT_LEVEL_RIID] = ANJAY_ID_RIID
    };
    assert(level == TLV_OUT_LEVEL_IID || level == TLV_OUT_LEVEL_RID
           || level == TLV_OUT_LEVEL_RIID);
    anjay_id_type_t id_type = LEVEL_MAPPING[level];
    assert(_anjay_uri_path_has(path, id_type));
    return path->ids[id_type];
}

static int tlv_set_path(anjay_output_ctx_t *ctx_,
                        const anjay_uri_path_t *path) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    assert(path);
    AVS_ASSERT(!_anjay_uri_path_outside_base(path, &ctx->root_path),
               "Attempted to set path outside the context's root path. "
               "This is a bug in resource reading logic.");

    tlv_out_level_id_t lowest_level = root_level(&ctx->root_path);
    tlv_out_level_id_t new_level = leaf_level(path);
    // note that when the root path is an IID path,
    // lowest_level == TLV_OUT_LEVEL_RID. That's because the lowest level
    // entities we're serializing are Resources. However, read_instance()
    // initially calls set_path() with an IID path, which causes new_level to be
    // lower than lowest_level. _anjay_uri_path_outside_base() call above makes
    // sure that we're not escaping the root, so we handle that just by
    // returning to the lowest level and not setting the ID.

    if (new_level >= lowest_level
            && current_level(ctx)->next_id != ANJAY_ID_INVALID) {
        // path already set
        return -1;
    }

    tlv_out_level_id_t finish_level = AVS_MAX(new_level, lowest_level);
    for (int i = lowest_level; i < (int) finish_level; ++i) {
        if (ctx->levels[i].next_id
                != id_from_path(path, (tlv_out_level_id_t) i)) {
            finish_level = (tlv_out_level_id_t) i;
            break;
        }
    }

    int result = 0;
    while (ctx->level > finish_level) {
        if ((result = tlv_slave_finish(ctx))) {
            return result;
        }
    }
    for (int i = ctx->level; i < (int) new_level; ++i) {
        ctx->levels[i].next_id = id_from_path(path, (tlv_out_level_id_t) i);
        tlv_slave_start(ctx);
    }
    assert(ctx->level == AVS_MAX(new_level, lowest_level));
    current_level(ctx)->next_id =
            (new_level >= lowest_level ? id_from_path(path, ctx->level)
                                       : ANJAY_ID_INVALID);
    return 0;
}

static int tlv_clear_path(anjay_output_ctx_t *ctx_) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    uint16_t *next_id = &current_level(ctx)->next_id;
    if (*next_id == ANJAY_ID_INVALID && ctx->level >= TLV_OUT_LEVEL_RID) {
        return -1;
    }
    *next_id = ANJAY_ID_INVALID;
    return 0;
}

static int tlv_output_close(anjay_output_ctx_t *ctx_) {
    tlv_out_t *ctx = (tlv_out_t *) ctx_;
    int result = 0;
    if (current_level(ctx)->next_id != ANJAY_ID_INVALID) {
        // path set but value not returned
        result = ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED;
    }
    while (ctx->level > root_level(&ctx->root_path)) {
        _anjay_update_ret(&result, tlv_slave_finish(ctx));
    }
    for (uint8_t i = 0; i < AVS_ARRAY_SIZE(ctx->levels); ++i) {
        AVS_LIST_CLEAR(&ctx->levels[i].entries);
    }
    return result;
}

static const anjay_output_ctx_vtable_t TLV_OUT_VTABLE = {
    .bytes_begin = tlv_ret_bytes,
    .string = tlv_ret_string,
    .integer = tlv_ret_i64,
    .floating = tlv_ret_double,
    .boolean = tlv_ret_bool,
    .objlnk = tlv_ret_objlnk,
    .start_aggregate = tlv_start_aggregate,
    .set_path = tlv_set_path,
    .clear_path = tlv_clear_path,
    .close = tlv_output_close
};

static void tlv_slave_start(tlv_out_t *ctx) {
    assert((size_t) (ctx->level + 1) <= AVS_ARRAY_SIZE(ctx->levels));
    ctx->level = (tlv_out_level_id_t) (ctx->level + 1);
    assert(!current_level(ctx)->entries);
    current_level(ctx)->next_entry_ptr = &current_level(ctx)->entries;
    current_level(ctx)->next_id = ANJAY_ID_INVALID;
}

anjay_output_ctx_t *_anjay_output_tlv_create(avs_stream_t *stream,
                                             const anjay_uri_path_t *uri) {
    assert(_anjay_uri_path_has(uri, ANJAY_ID_OID));
    tlv_out_t *ctx = (tlv_out_t *) avs_calloc(1, sizeof(tlv_out_t));
    if (ctx) {
        ctx->base.vtable = &TLV_OUT_VTABLE;
        ctx->stream = stream;
        ctx->root_path = *uri;
        ctx->level = root_level(uri);
        current_level(ctx)->next_entry_ptr = &current_level(ctx)->entries;
        current_level(ctx)->next_id = ANJAY_ID_INVALID;
    }
    return (anjay_output_ctx_t *) ctx;
}

#    ifdef ANJAY_TEST
#        include "tests/core/io/tlv_out.c"
#    endif

#endif // ANJAY_WITHOUT_TLV
