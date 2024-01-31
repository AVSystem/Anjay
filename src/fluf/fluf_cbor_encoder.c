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
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_cbor_encoder.h"
#include "fluf_internal.h"

// HACK:
// The size of the internal_buff has been calculated so that a
// single record never exceeds its size.
static int prepare_payload(const fluf_io_out_entry_t *entry,
                           fluf_io_buff_t *buff_ctx) {
    switch (entry->type) {
    case FLUF_DATA_TYPE_BYTES: {
        if (entry->value.bytes_or_string.offset != 0
                || (entry->value.bytes_or_string.full_length_hint
                    && entry->value.bytes_or_string.full_length_hint
                               != entry->value.bytes_or_string.chunk_length)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        buff_ctx->bytes_in_internal_buff = fluf_cbor_ll_bytes_begin(
                buff_ctx->internal_buff,
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
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_string_begin(buff_ctx->internal_buff,
                                          string_length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = string_length;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_BYTES: {
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_bytes_begin(buff_ctx->internal_buff,
                                         entry->value.external_data.length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = entry->value.external_data.length;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_STRING: {
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_string_begin(buff_ctx->internal_buff,
                                          entry->value.external_data.length);
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes = entry->value.external_data.length;
        break;
    }
    case FLUF_DATA_TYPE_TIME: {
        // time tag acording to [RFC8949 3.4.2]
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_encode_tag(buff_ctx->internal_buff,
                                        CBOR_TAG_INTEGER_DATE_TIME);
        assert(buff_ctx->bytes_in_internal_buff
                       + FLUF_CBOR_LL_SINGLE_CALL_MAX_LEN
               <= _FLUF_IO_CBOR_SIMPLE_RECORD_MAX_LENGTH);
        buff_ctx->bytes_in_internal_buff += fluf_cbor_ll_encode_int(
                &buff_ctx->internal_buff[buff_ctx->bytes_in_internal_buff],
                entry->value.time_value);
        break;
    }
    case FLUF_DATA_TYPE_INT: {
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_encode_int(buff_ctx->internal_buff,
                                        entry->value.int_value);
        break;
    }
    case FLUF_DATA_TYPE_DOUBLE: {
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_encode_double(buff_ctx->internal_buff,
                                           entry->value.double_value);
        break;
    }
    case FLUF_DATA_TYPE_BOOL: {
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_encode_bool(buff_ctx->internal_buff,
                                         entry->value.bool_value);
        break;
    }
    case FLUF_DATA_TYPE_OBJLNK: {
        buff_ctx->bytes_in_internal_buff = _fluf_io_out_add_objlink(
                buff_ctx, 0, entry->value.objlnk.oid, entry->value.objlnk.iid);
        break;
    }
    case FLUF_DATA_TYPE_UINT: {
        buff_ctx->bytes_in_internal_buff =
                fluf_cbor_ll_encode_uint(buff_ctx->internal_buff,
                                         entry->value.uint_value);
        break;
    }
    default: { return FLUF_IO_ERR_IO_TYPE; }
    }
    assert(buff_ctx->bytes_in_internal_buff <= _FLUF_IO_CTX_BUFFER_LENGTH);
    buff_ctx->remaining_bytes += buff_ctx->bytes_in_internal_buff;
    return 0;
}

int _fluf_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                 const fluf_io_out_entry_t *entry) {
    assert(ctx->_format == FLUF_COAP_FORMAT_CBOR);

    if (ctx->_encoder._cbor.entry_added) {
        return FLUF_IO_ERR_LOGIC;
    }

    int res = prepare_payload(entry, &ctx->_buff);
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
