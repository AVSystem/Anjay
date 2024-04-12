/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_opaque.h"

#ifdef FLUF_WITH_OPAQUE

#    define EXT_DATA_BUF_SIZE 64

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

        buff_ctx->remaining_bytes = entry->value.bytes_or_string.chunk_length;
        buff_ctx->bytes_in_internal_buff = 0;
        buff_ctx->is_extended_type = true;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_BYTES: {
        if (entry->value.external_data.length
                && !entry->value.external_data.get_external_data) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        buff_ctx->remaining_bytes = entry->value.external_data.length;
        buff_ctx->bytes_in_internal_buff = 0;
        buff_ctx->is_extended_type = true;
        break;
    }
    default: { return FLUF_IO_ERR_FORMAT; }
    }

    return 0;
}

int _fluf_opaque_out_init(fluf_io_out_ctx_t *ctx) {
    ctx->_encoder._opaque.entry_added = false;
    return 0;
}

int _fluf_opaque_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                   const fluf_io_out_entry_t *entry) {
    assert(ctx);
    assert(ctx->_format == FLUF_COAP_FORMAT_OPAQUE_STREAM);
    assert(entry);

    if (ctx->_encoder._opaque.entry_added) {
        return FLUF_IO_ERR_LOGIC;
    }

    int res = prepare_payload(entry, &ctx->_buff);
    if (res) {
        return res;
    }
    ctx->_encoder._opaque.entry_added = true;
    return 0;
}

int _fluf_opaque_get_extended_data_payload(void *out_buff,
                                           size_t out_buff_len,
                                           size_t *inout_copied_bytes,
                                           fluf_io_buff_t *ctx,
                                           const fluf_io_out_entry_t *entry) {
    assert(out_buff && out_buff_len && inout_copied_bytes);
    assert(entry->type == FLUF_DATA_TYPE_BYTES
           || entry->type == FLUF_DATA_TYPE_EXTERNAL_BYTES);

    int ret;
    size_t bytes_to_copy;

    switch (entry->type) {

    case FLUF_DATA_TYPE_BYTES: {
        bytes_to_copy = AVS_MIN(out_buff_len, ctx->remaining_bytes);
        memcpy((uint8_t *) out_buff,
               (const uint8_t *) entry->value.bytes_or_string.data
                       + ctx->offset,
               bytes_to_copy);
        *inout_copied_bytes = bytes_to_copy;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_BYTES: {
        bytes_to_copy = AVS_MIN(out_buff_len, ctx->remaining_bytes);
        ret = entry->value.external_data.get_external_data(
                out_buff, bytes_to_copy, ctx->offset,
                entry->value.external_data.user_args);
        if (ret) {
            return ret;
        }

        *inout_copied_bytes = bytes_to_copy;
        break;
    }
    }

    ctx->remaining_bytes -= bytes_to_copy;
    ctx->offset += bytes_to_copy;

    if (ctx->remaining_bytes) {
        return FLUF_IO_NEED_NEXT_CALL;
    } else {
        return 0;
    }
}

int _fluf_opaque_decoder_init(fluf_io_in_ctx_t *ctx,
                              const fluf_uri_path_t *request_uri) {
    assert(ctx);
    assert(request_uri);
    if (!(fluf_uri_path_has(request_uri, FLUF_ID_RID))) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    memset(&ctx->_out_value, 0x00, sizeof(ctx->_out_value));
    ctx->_out_path = *request_uri;
    ctx->_decoder._opaque.want_payload = true;
    return 0;
}

int _fluf_opaque_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                      void *buff,
                                      size_t buff_size,
                                      bool payload_finished) {
    if (ctx->_decoder._opaque.want_payload
            && !ctx->_decoder._opaque.payload_finished) {
        ctx->_out_value.bytes_or_string.offset +=
                ctx->_out_value.bytes_or_string.chunk_length;
        ctx->_out_value.bytes_or_string.chunk_length = buff_size;
        if (buff_size) {
            ctx->_out_value.bytes_or_string.data = buff;
        } else {
            ctx->_out_value.bytes_or_string.data = NULL;
        }
        if (payload_finished) {
            ctx->_out_value.bytes_or_string.full_length_hint =
                    ctx->_out_value.bytes_or_string.offset
                    + ctx->_out_value.bytes_or_string.chunk_length;
        } else {
            ctx->_out_value.bytes_or_string.full_length_hint = 0;
        }
        ctx->_decoder._opaque.payload_finished = payload_finished;
        ctx->_decoder._opaque.want_payload = false;
        return 0;
    }
    return FLUF_IO_ERR_LOGIC;
}

int _fluf_opaque_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                   fluf_data_type_t *inout_type_bitmask,
                                   const fluf_res_value_t **out_value,
                                   const fluf_uri_path_t **out_path) {
    assert(ctx && inout_type_bitmask && out_value && out_path);
    if (ctx->_decoder._opaque.eof_already_returned) {
        return FLUF_IO_ERR_LOGIC;
    }
    *out_value = NULL;
    *out_path = &ctx->_out_path;
    *inout_type_bitmask &= FLUF_DATA_TYPE_BYTES;
    assert(*inout_type_bitmask == FLUF_DATA_TYPE_BYTES
           || *inout_type_bitmask == FLUF_DATA_TYPE_NULL);
    if (*inout_type_bitmask == FLUF_DATA_TYPE_NULL) {
        return FLUF_IO_ERR_FORMAT;
    }
    if (ctx->_decoder._opaque.want_payload) {
        if (ctx->_decoder._opaque.payload_finished) {
            ctx->_decoder._opaque.eof_already_returned = true;
            return FLUF_IO_EOF;
        }
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    ctx->_decoder._opaque.want_payload = true;
    *out_value = &ctx->_out_value;
    return 0;
}

int _fluf_opaque_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                         size_t *out_count) {
    assert(ctx);
    assert(out_count);
    *out_count = 1;
    return 0;
}

#endif // FLUF_WITH_OPAQUE
