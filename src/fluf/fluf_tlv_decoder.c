/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_internal.h"
#include "fluf_tlv_decoder.h"

typedef enum {
    TLV_ID_IID = 0,
    TLV_ID_RIID = 1,
    TLV_ID_RID_ARRAY = 2,
    TLV_ID_RID = 3
} fluf_tlv_id_type_t;

static tlv_entry_t *tlv_entry_push(fluf_internal_tlv_decoder_t *tlv) {
    if (tlv->entries == NULL) {
        tlv->entries = &tlv->entries_block[0];
    } else {
        AVS_ASSERT(tlv->entries < &tlv->entries_block[FLUF_TLV_MAX_DEPTH - 1],
                   "TLV decoder stack overflow");
        tlv->entries++;
    }
    return tlv->entries;
}

static void tlv_entry_pop(fluf_internal_tlv_decoder_t *tlv) {
    assert(tlv->entries);
    if (tlv->entries == &tlv->entries_block[0]) {
        tlv->entries = NULL;
    } else {
        tlv->entries--;
    }
}

static int tlv_get_all_remaining_bytes(fluf_io_in_ctx_t *ctx,
                                       size_t *out_tlv_value_size,
                                       void **out_chunk) {
    if (ctx->_decoder._tlv.buff_size == ctx->_decoder._tlv.buff_offset) {
        return -1;
    }
    *out_chunk = ((uint8_t *) ctx->_decoder._tlv.buff)
                 + ctx->_decoder._tlv.buff_offset;
    // in the buff there could be: exactly one TLV entry, more than one TLV
    // entry as well as only a partial TLV entry
    *out_tlv_value_size = AVS_MIN(
            ctx->_decoder._tlv.entries->length
                    - ctx->_decoder._tlv.entries->bytes_read,
            ctx->_decoder._tlv.buff_size - ctx->_decoder._tlv.buff_offset);
    ctx->_decoder._tlv.entries->bytes_read += *out_tlv_value_size;
    ctx->_decoder._tlv.buff_offset += *out_tlv_value_size;
    return 0;
}

static int tlv_buff_read_by_copy(fluf_io_in_ctx_t *ctx,
                                 void *out_chunk,
                                 size_t chunk_size) {
    if (ctx->_decoder._tlv.buff_size - ctx->_decoder._tlv.buff_offset
            < chunk_size) {
        return -1;
    }
    memcpy(out_chunk,
           ((uint8_t *) ctx->_decoder._tlv.buff)
                   + ctx->_decoder._tlv.buff_offset,
           chunk_size);
    ctx->_decoder._tlv.buff_offset += chunk_size;
    return 0;
}

static int tlv_get_bytes(fluf_io_in_ctx_t *ctx) {
    size_t already_read = ctx->_out_value.bytes_or_string.chunk_length;
    int result = tlv_get_all_remaining_bytes(
            ctx, &ctx->_out_value.bytes_or_string.chunk_length,
            &ctx->_out_value.bytes_or_string.data);
    if (result == -1 && ctx->_decoder._tlv.entries->length) {
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    if (already_read) {
        ctx->_out_value.bytes_or_string.offset += already_read;
    }
    ctx->_out_value.bytes_or_string.full_length_hint =
            ctx->_decoder._tlv.entries->length;
    return 0;
}

static int tlv_get_int(fluf_io_in_ctx_t *ctx, int64_t *value) {
    /** Note: value is either &ctx->_out_value.int_value or
     * &ctx->_out_value.time_value, so its value will be preserved between
     * calls. */
    uint8_t *bytes;
    size_t bytes_read;
    if (!avs_is_power_of_2(ctx->_decoder._tlv.entries->length)
            || ctx->_decoder._tlv.entries->length > 8) {
        return FLUF_IO_ERR_FORMAT;
    }
    int result =
            tlv_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes);
    if (result == -1) {
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    if (ctx->_decoder._tlv.entries->bytes_read - bytes_read == 0) {
        *value = (bytes_read > 0 && ((int8_t) bytes[0]) < 0) ? -1 : 0;
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        *(uint64_t *) value <<= 8;
        *value += bytes[i];
    }
    return 0;
}

static int tlv_get_uint(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_read;
    if (!avs_is_power_of_2(ctx->_decoder._tlv.entries->length)
            || ctx->_decoder._tlv.entries->length > 8) {
        return FLUF_IO_ERR_FORMAT;
    }
    int result =
            tlv_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes);
    if (result == -1) {
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    if (ctx->_decoder._tlv.entries->bytes_read - bytes_read == 0) {
        ctx->_out_value.uint_value = 0;
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        ctx->_out_value.uint_value <<= 8;
        ctx->_out_value.uint_value += bytes[i];
    }
    return 0;
}

static int tlv_get_double(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_already_read = ctx->_decoder._tlv.entries->bytes_read;
    size_t bytes_read;
    int result =
            tlv_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes);
    if (result == -1) {
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    memcpy((uint8_t *) &ctx->_out_value.double_value + bytes_already_read,
           bytes, bytes_read);
    if (ctx->_decoder._tlv.entries->bytes_read
            == ctx->_decoder._tlv.entries->length) {
        switch (ctx->_decoder._tlv.entries->length) {
        case 4:
            ctx->_out_value.double_value =
                    avs_ntohf(*(uint32_t *) &ctx->_out_value.double_value);
            break;
        case 8:
            ctx->_out_value.double_value =
                    avs_ntohd(*(uint64_t *) &ctx->_out_value.double_value);
            break;
        default:
            return FLUF_IO_ERR_FORMAT;
        }
    }
    return 0;
}

static int tlv_get_bool(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_read;
    if (ctx->_decoder._tlv.entries->length != 1) {
        return FLUF_IO_ERR_FORMAT;
    }
    int result =
            tlv_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes);
    if (result == -1) {
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    switch (bytes[0]) {
    case 0:
        ctx->_out_value.bool_value = false;
        return 0;
    case 1:
        ctx->_out_value.bool_value = true;
        return 0;
    default:
        return FLUF_IO_ERR_FORMAT;
    }
}

static int tlv_get_objlnk(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_already_read = ctx->_decoder._tlv.entries->bytes_read;
    size_t bytes_read;
    if (ctx->_decoder._tlv.entries->length != 4) {
        return FLUF_IO_ERR_FORMAT;
    }
    int result =
            tlv_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes);
    if (result == -1) {
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    for (size_t i = 0; i < bytes_read; ++i) {
        if (bytes_already_read + i < 2) {
            memcpy((uint8_t *) &ctx->_out_value.objlnk.oid + bytes_already_read
                           + i,
                   &bytes[i], 1);
        } else {
            memcpy((uint8_t *) &ctx->_out_value.objlnk.iid + bytes_already_read
                           + i - 2,
                   &bytes[i], 1);
        }
    }
    if (ctx->_decoder._tlv.entries->bytes_read
            == ctx->_decoder._tlv.entries->length) {
        ctx->_out_value.objlnk.oid =
                avs_convert_be16(ctx->_out_value.objlnk.oid);
        ctx->_out_value.objlnk.iid =
                avs_convert_be16(ctx->_out_value.objlnk.iid);
    }
    return 0;
}

static int tlv_id_length_buff_read_by_copy(fluf_io_in_ctx_t *ctx,
                                           uint8_t *out,
                                           size_t length) {
    if (ctx->_decoder._tlv.id_length_buff_read_offset + length
            > sizeof(ctx->_decoder._tlv.id_length_buff)) {
        return -1;
    }
    memcpy(out,
           ((uint8_t *) ctx->_decoder._tlv.id_length_buff)
                   + ctx->_decoder._tlv.id_length_buff_read_offset,
           length);
    ctx->_decoder._tlv.id_length_buff_read_offset += length;
    return 0;
}

#define DEF_READ_SHORTENED(Type)                                           \
    static int read_shortened_##Type(fluf_io_in_ctx_t *ctx, size_t length, \
                                     Type *out) {                          \
        uint8_t bytes[sizeof(Type)];                                       \
        if (tlv_id_length_buff_read_by_copy(ctx, bytes, length)) {         \
            return -1;                                                     \
        }                                                                  \
        *out = 0;                                                          \
        for (size_t i = 0; i < length; ++i) {                              \
            *out = (Type) ((*out << 8) + bytes[i]);                        \
        }                                                                  \
        return 0;                                                          \
    }

DEF_READ_SHORTENED(uint16_t)
DEF_READ_SHORTENED(size_t)

static fluf_tlv_id_type_t tlv_type_from_typefield(uint8_t typefield) {
    return (fluf_tlv_id_type_t) ((typefield >> 6) & 3);
}

static fluf_id_type_t convert_id_type(uint8_t typefield) {
    switch (tlv_type_from_typefield(typefield)) {
    default:
        AVS_UNREACHABLE("Invalid TLV ID type");
    case TLV_ID_IID:
        return FLUF_ID_IID;
    case TLV_ID_RIID:
        return FLUF_ID_RIID;
    case TLV_ID_RID_ARRAY:
    case TLV_ID_RID:
        return FLUF_ID_RID;
    }
}

static int get_id(fluf_io_in_ctx_t *ctx,
                  fluf_id_type_t *out_type,
                  uint16_t *out_id,
                  bool *out_has_value,
                  size_t *out_bytes_read) {
    uint8_t typefield = ctx->_decoder._tlv.type_field;
    *out_bytes_read = 1;
    fluf_tlv_id_type_t tlv_type = tlv_type_from_typefield(typefield);
    *out_type = convert_id_type(typefield);
    size_t id_length = (typefield & 0x20) ? 2 : 1;
    if (read_shortened_uint16_t(ctx, id_length, out_id)) {
        return FLUF_IO_ERR_FORMAT;
    }
    *out_bytes_read += id_length;

    size_t length_length = ((typefield >> 3) & 3);
    if (!length_length) {
        ctx->_decoder._tlv.entries->length = (typefield & 7);
    } else if (read_shortened_size_t(ctx, length_length,
                                     &ctx->_decoder._tlv.entries->length)) {
        return FLUF_IO_ERR_FORMAT;
    }
    *out_bytes_read += length_length;
    /**
     * This may seem a little bit strange, but entries that do not have any
     * payload may be considered as having a value - that is, an empty one. On
     * the other hand, if they DO have the payload, then it only makes sense to
     * return them if they're "terminal" - i.e. they're either resource
     * instances or single resources with value.
     */
    *out_has_value = !ctx->_decoder._tlv.entries->length
                     || tlv_type == TLV_ID_RIID || tlv_type == TLV_ID_RID;
    ctx->_decoder._tlv.entries->bytes_read = 0;
    ctx->_decoder._tlv.entries->type = *out_type;
    return 0;
}

static int get_type_and_header(fluf_io_in_ctx_t *ctx) {
    if (ctx->_decoder._tlv.type_field == 0xFF) {
        if (tlv_buff_read_by_copy(ctx, &ctx->_decoder._tlv.type_field, 1)) {
            ctx->_decoder._tlv.want_payload = true;
            return FLUF_IO_WANT_NEXT_PAYLOAD;
        }
        if (ctx->_decoder._tlv.type_field == 0xFF) {
            return FLUF_IO_ERR_FORMAT;
        }
        size_t id_length = (ctx->_decoder._tlv.type_field & 0x20) ? 2 : 1;
        size_t length_length = ((ctx->_decoder._tlv.type_field >> 3) & 3);
        ctx->_decoder._tlv.id_length_buff_bytes_need =
                id_length + length_length;
    }
    if (ctx->_decoder._tlv.id_length_buff_bytes_need > 0) {
        if (ctx->_decoder._tlv.buff_size - ctx->_decoder._tlv.buff_offset
                <= 0) {
            ctx->_decoder._tlv.want_payload = true;
            return FLUF_IO_WANT_NEXT_PAYLOAD;
        }
        size_t bytes_to_read = AVS_MIN(
                ctx->_decoder._tlv.id_length_buff_bytes_need,
                ctx->_decoder._tlv.buff_size - ctx->_decoder._tlv.buff_offset);
        if (tlv_buff_read_by_copy(
                    ctx,
                    ctx->_decoder._tlv.id_length_buff
                            + ctx->_decoder._tlv.id_length_buff_write_offset,
                    bytes_to_read)) {
            return FLUF_IO_ERR_FORMAT;
        }
        ctx->_decoder._tlv.id_length_buff_write_offset += bytes_to_read;
        ctx->_decoder._tlv.id_length_buff_bytes_need -= bytes_to_read;
        if (ctx->_decoder._tlv.id_length_buff_bytes_need == 0) {
            return 0;
        }
        ctx->_decoder._tlv.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    return 0;
}

static int tlv_get_path(fluf_io_in_ctx_t *ctx) {
    if (ctx->_decoder._tlv.has_path) {
        ctx->_out_path = ctx->_decoder._tlv.current_path;
        return 0;
    }
    bool has_value = false;
    fluf_id_type_t type;
    uint16_t id;
    while (!has_value) {
        int result;
        if ((result = get_type_and_header(ctx))) {
            return result;
        }
        tlv_entry_t *parent = ctx->_decoder._tlv.entries;
        if (!tlv_entry_push(&ctx->_decoder._tlv)) {
            return FLUF_COAP_CODE_INTERNAL_SERVER_ERROR;
        }
        size_t header_len;
        if ((result = get_id(ctx, &type, &id, &has_value, &header_len))) {
            return result;
        }
        if (id == FLUF_ID_INVALID) {
            return FLUF_IO_ERR_FORMAT;
        }
        if (parent) {
            // Assume the child entry is fully read (which is in fact necessary
            // to be able to return back to the parent).
            parent->bytes_read +=
                    ctx->_decoder._tlv.entries->length + header_len;
            if (parent->bytes_read > parent->length) {
                return FLUF_IO_ERR_FORMAT;
            }
        }
        ctx->_decoder._tlv.current_path.ids[(size_t) type] = id;
        ctx->_decoder._tlv.current_path.uri_len = (size_t) type + 1;

        if (fluf_uri_path_outside_base(&ctx->_decoder._tlv.current_path,
                                       &ctx->_decoder._tlv.uri_path)) {
            return FLUF_IO_ERR_FORMAT;
        }
        ctx->_decoder._tlv.type_field = 0xFF;
    }
    ctx->_out_path = ctx->_decoder._tlv.current_path;
    ctx->_decoder._tlv.has_path = true;
    return 0;
}

static int tlv_next_entry(fluf_io_in_ctx_t *ctx) {
    if (!ctx->_decoder._tlv.has_path) {
        // Next entry is already available and should be processed.
        return 0;
    }
    if (!ctx->_decoder._tlv.entries) {
        return FLUF_IO_ERR_FORMAT;
    }
    if (ctx->_decoder._tlv.entries->length
            > ctx->_decoder._tlv.entries->bytes_read) {
        void *ignored;
        size_t ignored_bytes_read;
        int result =
                tlv_get_all_remaining_bytes(ctx, &ignored_bytes_read, &ignored);
        if (result) {
            return result;
        }
    }
    ctx->_decoder._tlv.has_path = false;
    ctx->_decoder._tlv.type_field = 0xFF;
    while (ctx->_decoder._tlv.entries
           && ctx->_decoder._tlv.entries->length
                      == ctx->_decoder._tlv.entries->bytes_read) {
        ctx->_decoder._tlv.current_path.ids[ctx->_decoder._tlv.entries->type] =
                FLUF_ID_INVALID;
        ctx->_decoder._tlv.current_path.uri_len =
                (size_t) ctx->_decoder._tlv.entries->type;
        tlv_entry_pop(&ctx->_decoder._tlv);
    }
    return 0;
}

int _fluf_tlv_decoder_init(fluf_io_in_ctx_t *ctx,
                           const fluf_uri_path_t *request_uri) {
    assert(ctx);
    assert(request_uri);
    assert(!fluf_uri_path_equal(request_uri, &FLUF_MAKE_ROOT_PATH()));
    memset(&ctx->_out_value, 0x00, sizeof(ctx->_out_value));
    memset(&ctx->_out_path, 0x00, sizeof(ctx->_out_path));
    ctx->_decoder._tlv.uri_path = *request_uri;
    ctx->_decoder._tlv.current_path = ctx->_decoder._tlv.uri_path;
    ctx->_decoder._tlv.type_field = 0xFF;
    ctx->_decoder._tlv.want_payload = true;

    return 0;
}

int _fluf_tlv_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                   void *buff,
                                   size_t buff_size,
                                   bool payload_finished) {
    if (ctx->_decoder._tlv.want_payload) {
        ctx->_decoder._tlv.buff = buff;
        ctx->_decoder._tlv.buff_size = buff_size;
        ctx->_decoder._tlv.buff_offset = 0;
        ctx->_decoder._tlv.payload_finished = payload_finished;
        ctx->_decoder._tlv.want_payload = false;
        return 0;
    }
    return FLUF_IO_ERR_LOGIC;
}

int _fluf_tlv_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                fluf_data_type_t *inout_type_bitmask,
                                const fluf_res_value_t **out_value,
                                const fluf_uri_path_t **out_path) {
    assert(ctx);
    assert(inout_type_bitmask);
    assert(out_value);
    assert(out_path);
    int result;
    if (ctx->_decoder._tlv.want_payload) {
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    *out_value = NULL;
    *out_path = NULL;
    if (ctx->_decoder._tlv.payload_finished
            && (ctx->_decoder._tlv.buff_size == ctx->_decoder._tlv.buff_offset)
            && !ctx->_decoder._tlv.want_disambiguation) {
        *out_path = NULL;
        return FLUF_IO_EOF;
    }

    if (!ctx->_decoder._tlv.entries || !ctx->_decoder._tlv.has_path) {
        memset(&ctx->_out_value, 0x00, sizeof(ctx->_out_value));
        memset(&ctx->_out_path, 0x00, sizeof(ctx->_out_path));
        result = tlv_get_path(ctx);
        if (result) {
            *out_path = NULL;
            if (result == FLUF_IO_WANT_NEXT_PAYLOAD
                    && ctx->_decoder._tlv.payload_finished) {
                return FLUF_IO_ERR_FORMAT;
            }
            return result;
        }
        *out_path = &ctx->_out_path;
        if (ctx->_decoder._tlv.entries->length == 0) {
            if (ctx->_decoder._tlv.entries->type == FLUF_ID_IID
                    || ctx->_decoder._tlv.entries->type == FLUF_ID_RIID) {
                *inout_type_bitmask = FLUF_DATA_TYPE_NULL;
                return tlv_next_entry(ctx);
            }
        }
    }

    ctx->_decoder._tlv.want_disambiguation = false;
    switch (*inout_type_bitmask) {
    case FLUF_DATA_TYPE_NULL:
        return FLUF_IO_ERR_FORMAT;
    case FLUF_DATA_TYPE_BYTES:
    case FLUF_DATA_TYPE_STRING: {
        result = tlv_get_bytes(ctx);
        break;
    }
    case FLUF_DATA_TYPE_INT:
        result = tlv_get_int(ctx, &ctx->_out_value.int_value);
        break;
    case FLUF_DATA_TYPE_UINT:
        result = tlv_get_uint(ctx);
        break;
    case FLUF_DATA_TYPE_DOUBLE:
        result = tlv_get_double(ctx);
        break;
    case FLUF_DATA_TYPE_BOOL:
        result = tlv_get_bool(ctx);
        break;
    case FLUF_DATA_TYPE_OBJLNK:
        result = tlv_get_objlnk(ctx);
        break;
    case FLUF_DATA_TYPE_TIME:
        result = tlv_get_int(ctx, &ctx->_out_value.time_value);
        break;
    default:
        ctx->_decoder._tlv.want_disambiguation = true;
        *out_path = &ctx->_out_path;
        return FLUF_IO_WANT_TYPE_DISAMBIGUATION;
    }
    if (result) {
        if (result == FLUF_IO_WANT_NEXT_PAYLOAD
                && ctx->_decoder._tlv.payload_finished) {
            return FLUF_IO_ERR_FORMAT;
        }
        return result;
    }

    // reason about parsing state
    if (ctx->_decoder._tlv.entries->bytes_read
            == ctx->_decoder._tlv.entries->length) {
        if ((result = tlv_next_entry(ctx))) {
            return result;
        }
        *out_path = &ctx->_out_path;
        *out_value = &ctx->_out_value;
        return 0;
    } else {
        if (!ctx->_decoder._tlv.payload_finished
                && (ctx->_decoder._tlv.buff_size
                    == ctx->_decoder._tlv.buff_offset)) {
            if (*inout_type_bitmask == FLUF_DATA_TYPE_BYTES
                    || *inout_type_bitmask == FLUF_DATA_TYPE_STRING) {
                *out_path = &ctx->_out_path;
                *out_value = &ctx->_out_value;
                return 0;
            }
            ctx->_decoder._tlv.want_payload = true;
            return FLUF_IO_WANT_NEXT_PAYLOAD;
        }
        return FLUF_IO_ERR_FORMAT;
    }
}
