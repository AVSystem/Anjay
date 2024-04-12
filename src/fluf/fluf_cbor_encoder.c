/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <string.h>

#include <fluf/fluf_cbor_encoder_ll.h>
#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_cbor_encoder.h"
#include "fluf_internal.h"

#if defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)
// HACK:
// The size of the internal_buff has been calculated so that a
// single record never exceeds its size.
int _fluf_cbor_encode_value(fluf_io_buff_t *buff_ctx,
                            const fluf_io_out_entry_t *entry) {
    size_t buf_pos = buff_ctx->bytes_in_internal_buff;

    switch (entry->type) {
    case FLUF_DATA_TYPE_BYTES: {
        if (entry->value.bytes_or_string.offset != 0
                || (entry->value.bytes_or_string.full_length_hint
                    && entry->value.bytes_or_string.full_length_hint
                               != entry->value.bytes_or_string.chunk_length)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        buf_pos += fluf_cbor_ll_bytes_begin(
                &buff_ctx->internal_buff[buf_pos],
                entry->value.bytes_or_string.chunk_length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = entry->value.bytes_or_string.chunk_length;
        break;
    }
    case FLUF_DATA_TYPE_STRING: {
        if (entry->value.bytes_or_string.offset != 0
                || (entry->value.bytes_or_string.full_length_hint
                    && entry->value.bytes_or_string.full_length_hint
                               != entry->value.bytes_or_string.chunk_length)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        size_t string_length = entry->value.bytes_or_string.chunk_length;
        if (!string_length && entry->value.bytes_or_string.data
                && *(const char *) entry->value.bytes_or_string.data) {
            string_length =
                    strlen((const char *) entry->value.bytes_or_string.data);
        }
        buf_pos += fluf_cbor_ll_string_begin(&buff_ctx->internal_buff[buf_pos],
                                             string_length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = string_length;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_BYTES: {
        buf_pos += fluf_cbor_ll_bytes_begin(&buff_ctx->internal_buff[buf_pos],
                                            entry->value.external_data.length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = entry->value.external_data.length;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_STRING: {
        buf_pos += fluf_cbor_ll_string_begin(&buff_ctx->internal_buff[buf_pos],
                                             entry->value.external_data.length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = entry->value.external_data.length;
        break;
    }
    case FLUF_DATA_TYPE_TIME: {
        buf_pos += fluf_cbor_ll_encode_tag(&buff_ctx->internal_buff[buf_pos],
                                           CBOR_TAG_INTEGER_DATE_TIME);
        buf_pos += fluf_cbor_ll_encode_int(&buff_ctx->internal_buff[buf_pos],
                                           entry->value.time_value);
        break;
    }
    case FLUF_DATA_TYPE_INT: {
        buf_pos += fluf_cbor_ll_encode_int(&buff_ctx->internal_buff[buf_pos],
                                           entry->value.int_value);
        break;
    }
    case FLUF_DATA_TYPE_DOUBLE: {
        buf_pos += fluf_cbor_ll_encode_double(&buff_ctx->internal_buff[buf_pos],
                                              entry->value.double_value);
        break;
    }
    case FLUF_DATA_TYPE_BOOL: {
        buf_pos += fluf_cbor_ll_encode_bool(&buff_ctx->internal_buff[buf_pos],
                                            entry->value.bool_value);
        break;
    }
    case FLUF_DATA_TYPE_OBJLNK: {
        buf_pos += _fluf_io_out_add_objlink(buff_ctx, buf_pos,
                                            entry->value.objlnk.oid,
                                            entry->value.objlnk.iid);
        break;
    }
    case FLUF_DATA_TYPE_UINT: {
        buf_pos += fluf_cbor_ll_encode_uint(&buff_ctx->internal_buff[buf_pos],
                                            entry->value.uint_value);
        break;
    }
    default: { return FLUF_IO_ERR_IO_TYPE; }
    }
    assert(buf_pos <= _FLUF_IO_CTX_BUFFER_LENGTH);
    buff_ctx->bytes_in_internal_buff = buf_pos;
    buff_ctx->remaining_bytes += buff_ctx->bytes_in_internal_buff;

    return 0;
}
#endif // defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)

#ifdef FLUF_WITH_CBOR
int _fluf_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                 const fluf_io_out_entry_t *entry) {
    assert(ctx->_format == FLUF_COAP_FORMAT_CBOR);

    if (ctx->_encoder._cbor.entry_added) {
        return FLUF_IO_ERR_LOGIC;
    }

    int res = _fluf_cbor_encode_value(&ctx->_buff, entry);
    if (res) {
        return res;
    }
    ctx->_encoder._cbor.entry_added = true;
    return 0;
}

int _fluf_cbor_encoder_init(fluf_io_out_ctx_t *ctx) {
    ctx->_encoder._cbor.entry_added = false;
    return 0;
}
#endif // FLUF_WITH_CBOR
