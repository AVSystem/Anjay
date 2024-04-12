/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include <avsystem/commons/avs_base64.h>

#include "fluf_text_encoder.h"

#ifdef FLUF_WITH_PLAINTEXT

#    define BASE64_NO_PADDING_MULTIPIER 3
#    define BASE64_ENCODED_MULTIPLIER 4
#    define MAX_CHUNK_FOR_BASE64(x) \
        (BASE64_NO_PADDING_MULTIPIER * ((x) / BASE64_ENCODED_MULTIPLIER))

#    define EXT_DATA_BUF_SIZE (16 * BASE64_NO_PADDING_MULTIPIER)

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
    case FLUF_DATA_TYPE_STRING: {
        if (entry->value.bytes_or_string.offset != 0
                || (entry->value.bytes_or_string.full_length_hint
                    && entry->value.bytes_or_string.full_length_hint
                               != entry->value.bytes_or_string.chunk_length)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }

        size_t entry_len;
        if (!(entry_len = entry->value.bytes_or_string.chunk_length)
                && entry->value.bytes_or_string.data) {
            entry_len =
                    strlen((const char *) entry->value.bytes_or_string.data);
        }

        buff_ctx->bytes_in_internal_buff = 0;
        buff_ctx->remaining_bytes = entry_len;
        buff_ctx->is_extended_type = true;
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_STRING: {
        if (entry->value.external_data.length
                && !entry->value.external_data.get_external_data) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        buff_ctx->remaining_bytes = entry->value.external_data.length;
        buff_ctx->bytes_in_internal_buff = 0;
        buff_ctx->is_extended_type = true;
        break;
    }
    case FLUF_DATA_TYPE_INT: {
        buff_ctx->bytes_in_internal_buff =
                fluf_int64_to_string_value((char *) buff_ctx->internal_buff,
                                           entry->value.int_value);
        buff_ctx->remaining_bytes = buff_ctx->bytes_in_internal_buff;
        break;
    }
    case FLUF_DATA_TYPE_DOUBLE: {
        buff_ctx->bytes_in_internal_buff = fluf_double_to_simple_str_value(
                (char *) buff_ctx->internal_buff, entry->value.double_value);
        buff_ctx->remaining_bytes = buff_ctx->bytes_in_internal_buff;
        break;
    }
    case FLUF_DATA_TYPE_BOOL: {
        buff_ctx->bytes_in_internal_buff = 1;
        buff_ctx->internal_buff[0] = entry->value.bool_value ? '1' : '0';
        buff_ctx->remaining_bytes = buff_ctx->bytes_in_internal_buff;
        break;
    }
    case FLUF_DATA_TYPE_OBJLNK: {
        buff_ctx->bytes_in_internal_buff =
                fluf_uint16_to_string_value((char *) buff_ctx->internal_buff,
                                            entry->value.objlnk.oid);
        buff_ctx->internal_buff[buff_ctx->bytes_in_internal_buff++] = ':';
        buff_ctx->bytes_in_internal_buff += fluf_uint16_to_string_value(
                (char *) buff_ctx->internal_buff
                        + buff_ctx->bytes_in_internal_buff,
                entry->value.objlnk.iid);
        buff_ctx->remaining_bytes = buff_ctx->bytes_in_internal_buff;
        break;
    }
    case FLUF_DATA_TYPE_UINT: {
        buff_ctx->bytes_in_internal_buff =
                fluf_uint64_to_string_value((char *) buff_ctx->internal_buff,
                                            entry->value.uint_value);
        buff_ctx->remaining_bytes = buff_ctx->bytes_in_internal_buff;
        break;
    }
    case FLUF_DATA_TYPE_TIME: {
        buff_ctx->bytes_in_internal_buff =
                fluf_int64_to_string_value((char *) buff_ctx->internal_buff,
                                           entry->value.time_value);
        buff_ctx->remaining_bytes = buff_ctx->bytes_in_internal_buff;
        break;
    }
    default: { return FLUF_IO_ERR_LOGIC; }
    }
    assert(buff_ctx->bytes_in_internal_buff <= _FLUF_IO_CTX_BUFFER_LENGTH);

    return 0;
}

const avs_base64_config_t AVS_BASE64_CONFIG = {
    .alphabet = AVS_BASE64_CHARS,
    .padding_char = '=',
    .allow_whitespace = false,
    .require_padding = true,
    .without_null_termination = true
};

static size_t encode_base64_payload(char *out_buff,
                                    size_t out_buff_len,
                                    void *entry_buf,
                                    size_t bytes_to_encode) {
    if (!bytes_to_encode) {
        return 0;
    }

    assert(avs_base64_encoded_size_custom(bytes_to_encode, AVS_BASE64_CONFIG)
           <= out_buff_len);
    avs_base64_encode_custom(out_buff, out_buff_len,
                             (const uint8_t *) entry_buf, bytes_to_encode,
                             AVS_BASE64_CONFIG);

    return avs_base64_encoded_size_custom(bytes_to_encode, AVS_BASE64_CONFIG);
}

int _fluf_text_encoder_init(fluf_io_out_ctx_t *ctx) {
    ctx->_encoder._text.entry_added = false;
    return 0;
}

int _fluf_text_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                 const fluf_io_out_entry_t *entry) {
    assert(ctx);
    assert(ctx->_format == FLUF_COAP_FORMAT_PLAINTEXT);
    assert(entry);

    if (ctx->_encoder._text.entry_added) {
        return FLUF_IO_ERR_LOGIC;
    }

    int res = prepare_payload(entry, &ctx->_buff);
    if (res) {
        return res;
    }
    ctx->_encoder._text.entry_added = true;
    return 0;
}

static inline void shift_ctx(fluf_io_buff_t *buff_ctx, size_t bytes_read) {
    buff_ctx->remaining_bytes -= bytes_read;
    buff_ctx->offset += bytes_read;
}

static int encode_bytes(char *encoded_buf,
                        size_t encoded_buf_size,
                        size_t *out_copied_bytes,
                        const fluf_io_out_entry_t *entry,
                        size_t input_offset,
                        size_t *bytes_to_encode) {
    assert(encoded_buf);
    assert(entry);
    assert(bytes_to_encode);

    size_t copied_bytes = 0;
    switch (entry->type) {
    case FLUF_DATA_TYPE_BYTES: {
        copied_bytes = encode_base64_payload(
                encoded_buf, encoded_buf_size,
                (uint8_t *) entry->value.bytes_or_string.data + input_offset,
                *bytes_to_encode);
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_BYTES: {
        if (*bytes_to_encode && !entry->value.external_data.get_external_data) {
            return FLUF_IO_ERR_INPUT_ARG;
        }

        char ext_data_buf[EXT_DATA_BUF_SIZE] = { 0 };
        *bytes_to_encode = AVS_MIN(*bytes_to_encode, EXT_DATA_BUF_SIZE);
        int ret = entry->value.external_data.get_external_data(
                ext_data_buf, *bytes_to_encode, input_offset,
                entry->value.external_data.user_args);
        if (ret) {
            return ret;
        }

        copied_bytes = encode_base64_payload(encoded_buf, encoded_buf_size,
                                             ext_data_buf, *bytes_to_encode);
        break;
    }
    }

    if (out_copied_bytes) {
        *out_copied_bytes = copied_bytes;
    }

    return 0;
}

static int get_extended_data(void *out_buff,
                             size_t out_buff_len,
                             size_t *out_copied_bytes,
                             fluf_io_buff_t *buff_ctx,
                             const fluf_io_out_entry_t *entry) {
    size_t bytes_to_get;
    assert(*out_copied_bytes == 0);
    int ret;
    /* Copy cached base64 bytes to out_buff */
    if (buff_ctx->b64_cache.cache_offset) {
        bytes_to_get = AVS_MIN(sizeof(buff_ctx->b64_cache.buf)
                                       - buff_ctx->b64_cache.cache_offset,
                               out_buff_len);
        memcpy((char *) out_buff,
               buff_ctx->b64_cache.buf + buff_ctx->b64_cache.cache_offset,
               bytes_to_get);
        *out_copied_bytes = bytes_to_get;
        buff_ctx->b64_cache.cache_offset += bytes_to_get;
        /* Clear Base64 cache */
        if (buff_ctx->b64_cache.cache_offset
                >= sizeof(buff_ctx->b64_cache.buf)) {
            buff_ctx->b64_cache.cache_offset = 0;
        }
    }

    /* Exit if whole buff was filled with cached bytes */
    size_t not_used_size = out_buff_len - *out_copied_bytes;
    if (!not_used_size && buff_ctx->remaining_bytes) {
        return FLUF_IO_NEED_NEXT_CALL;
    }

    /* Encode next chunk of remaining_bytes to out_buff */
    while ((not_used_size = out_buff_len - *out_copied_bytes)
                   > BASE64_NO_PADDING_MULTIPIER
           && buff_ctx->remaining_bytes) {
        bytes_to_get = AVS_MIN(MAX_CHUNK_FOR_BASE64(not_used_size),
                               buff_ctx->remaining_bytes);
        size_t copied_bytes;
        ret = encode_bytes(((char *) out_buff) + *out_copied_bytes,
                           not_used_size, &copied_bytes, entry,
                           buff_ctx->offset, &bytes_to_get);
        if (ret) {
            return ret;
        }
        *out_copied_bytes += copied_bytes;
        shift_ctx(buff_ctx, bytes_to_get);
    }

    /* Fill empty bytes in out_buff */
    if (buff_ctx->remaining_bytes && not_used_size) {
        assert(!buff_ctx->b64_cache.cache_offset);
        assert(not_used_size <= sizeof(buff_ctx->b64_cache.buf));
        size_t bytes_to_append =
                AVS_MIN(MAX_CHUNK_FOR_BASE64(sizeof(buff_ctx->b64_cache.buf)),
                        buff_ctx->remaining_bytes);
        ret = encode_bytes((char *) buff_ctx->b64_cache.buf,
                           BASE64_ENCODED_MULTIPLIER, NULL, entry,
                           buff_ctx->offset, &bytes_to_append);
        if (ret) {
            return ret;
        }
        memcpy((char *) out_buff + *out_copied_bytes, buff_ctx->b64_cache.buf,
               not_used_size);
        *out_copied_bytes += not_used_size;
        buff_ctx->b64_cache.cache_offset = not_used_size;
        shift_ctx(buff_ctx, bytes_to_append);
    }

    return 0;
}

int _fluf_text_get_extended_data_payload(void *out_buff,
                                         size_t out_buff_len,
                                         size_t *inout_copied_bytes,
                                         fluf_io_buff_t *buff_ctx,
                                         const fluf_io_out_entry_t *entry) {
    assert(out_buff && out_buff_len && inout_copied_bytes && buff_ctx && entry);
    assert(*inout_copied_bytes == 0);
    int ret;
    size_t bytes_to_get;

    switch (entry->type) {
    case FLUF_DATA_TYPE_BYTES:
    case FLUF_DATA_TYPE_EXTERNAL_BYTES: {
        ret = get_extended_data(out_buff, out_buff_len, inout_copied_bytes,
                                buff_ctx, entry);
        if (ret) {
            return ret;
        }

        break;
    }
    case FLUF_DATA_TYPE_STRING: {
        bytes_to_get = AVS_MIN(out_buff_len, buff_ctx->remaining_bytes);
        memcpy((uint8_t *) out_buff,
               (const uint8_t *) entry->value.bytes_or_string.data
                       + buff_ctx->offset,
               bytes_to_get);
        *inout_copied_bytes = bytes_to_get;
        shift_ctx(buff_ctx, bytes_to_get);
        break;
    }
    case FLUF_DATA_TYPE_EXTERNAL_STRING: {
        bytes_to_get = AVS_MIN(out_buff_len, buff_ctx->remaining_bytes);
        if (bytes_to_get && !entry->value.external_data.get_external_data) {
            return FLUF_IO_ERR_INPUT_ARG;
        }

        ret = entry->value.external_data.get_external_data(
                out_buff, bytes_to_get, buff_ctx->offset,
                entry->value.external_data.user_args);
        if (ret) {
            return ret;
        }
        *inout_copied_bytes = bytes_to_get;
        shift_ctx(buff_ctx, bytes_to_get);
        break;
    }
    }

    if (buff_ctx->remaining_bytes || buff_ctx->b64_cache.cache_offset) {
        return FLUF_IO_NEED_NEXT_CALL;
    } else {
        return 0;
    }
}

#endif // FLUF_WITH_PLAINTEXT
