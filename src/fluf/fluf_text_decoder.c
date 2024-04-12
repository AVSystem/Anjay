/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_base64.h>
#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_text_decoder.h"

#ifdef FLUF_WITH_PLAINTEXT

int _fluf_text_decoder_init(fluf_io_in_ctx_t *ctx,
                            const fluf_uri_path_t *request_uri) {
    assert(ctx);
    assert(request_uri);
    if (!(fluf_uri_path_has(request_uri, FLUF_ID_RID))) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    memset(&ctx->_out_value, 0x00, sizeof(ctx->_out_value));
    ctx->_out_path = *request_uri;
    ctx->_decoder._text.want_payload = true;
    return 0;
}

int _fluf_text_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                    void *buff,
                                    size_t buff_size,
                                    bool payload_finished) {
    if (ctx->_decoder._text.want_payload) {
        ctx->_decoder._text.buff = buff;
        ctx->_decoder._text.buff_size = buff_size;
        ctx->_decoder._text.payload_finished = payload_finished;
        ctx->_decoder._text.want_payload = false;
        return 0;
    }
    return FLUF_IO_ERR_LOGIC;
}

static int text_get_all_remaining_bytes(fluf_io_in_ctx_t *ctx,
                                        size_t *out_chunk_size,
                                        void **out_chunk) {
    if (ctx->_decoder._text.want_payload) {
        *out_chunk = NULL;
        out_chunk_size = 0;
        return -1;
    }
    *out_chunk = ctx->_decoder._text.buff;
    *out_chunk_size = ctx->_decoder._text.buff_size;
    ctx->_decoder._text.want_payload = true;
    return 0;
}

static int choose_buffer_and_write_bytes(fluf_io_in_ctx_t *ctx,
                                         size_t *num_decoded,
                                         const char *encoded) {
    // Check which buffer to write to, based on already decoded bytes count
    // Below 3 is the maximum number of bytes that can be decoded from 4
    // base64-encoded characters
    if (ctx->_out_value.bytes_or_string.chunk_length + 3
            <= sizeof(ctx->_decoder._text.aux.abuf_b64.out_buf)) {
        if (avs_base64_decode_strict(
                    num_decoded,
                    (uint8_t *) ctx->_decoder._text.aux.abuf_b64.out_buf
                            // chunk_length increases iteratively during
                            // decoding, hence its addition here.
                            + ctx->_out_value.bytes_or_string.chunk_length,
                    sizeof(ctx->_decoder._text.aux.abuf_b64.out_buf)
                            - ctx->_decoder._text.aux.abuf_b64.out_buf_size,
                    encoded)) {
            return FLUF_IO_ERR_FORMAT;
        }
        ctx->_decoder._text.aux.abuf_b64.out_buf_size += *num_decoded;
        ctx->_out_value.bytes_or_string.data =
                ctx->_decoder._text.aux.abuf_b64.out_buf;
    } else {
        if (avs_base64_decode_strict(
                    num_decoded,
                    // here _text.buff serves as both input and output
                    // simultaneously
                    (uint8_t *) ctx->_decoder._text.buff
                            // chunk_length increases iteratively during
                            // decoding, hence its addition here.
                            + ctx->_out_value.bytes_or_string.chunk_length,
                    ctx->_decoder._text.buff_size
                            - ctx->_out_value.bytes_or_string.chunk_length,
                    encoded)) {
            return FLUF_IO_ERR_FORMAT;
        }
        ctx->_out_value.bytes_or_string.data = ctx->_decoder._text.buff;
    }
    return 0;
}

static void store_residual_bytes(fluf_io_in_ctx_t *ctx,
                                 const uint8_t *bytes,
                                 size_t bytes_size) {
    assert(ctx->_decoder._text.aux.abuf_b64.res_buf_size + bytes_size < 4);
    memcpy(ctx->_decoder._text.aux.abuf_b64.res_buf
                   + ctx->_decoder._text.aux.abuf_b64.res_buf_size,
           bytes, bytes_size);
    ctx->_decoder._text.aux.abuf_b64.res_buf_size += bytes_size;
}

static void look_for_padding(fluf_io_in_ctx_t *ctx, char *encoded) {
    for (size_t i = 0; i < 4; ++i) {
        if (encoded[i] == '=') {
            ctx->_decoder._text.padding_detected = true;
        }
    }
}

static int text_get_bytes(fluf_io_in_ctx_t *ctx) {
    /**
     * This function utilizes an approach by using the input buffer as
     * the output buffer for decoding base64 data. The concept involves
     * overwriting the input data with the decoded output data, taking advantage
     * of the fact that the decoded data is generally shorter than the encoded
     * data.
     *
     * However, the process is not straightforward due to potential residual
     * data from previous payload feeding. Consider a scenario where the first
     * feed includes 7 characters (4 decoded, 3 needs to be preserved as
     * residual), and the second feed provides only one character. This results
     * in a situation where 4 characters need to be decoded into 3 bytes, but
     * the input/output buffer is only 1 byte long.
     *
     * Even if the second feed is longer, such as 13 characters, it cannot write
     * the first 9 decoded bytes into the input/output buffer. This limitation
     * arises because, in this scenario, only one character is taken from the
     * input buffer (keeping in mind the 3 characters preserved from the
     * previous feed). Attempting to write 3 resulting bytes into the buffer
     * leads to the corruption of the input data.
     *
     * The solution to this problem is an auxiliary buffer that is used to store
     * the first 9 bytes of decoded data. If there are more than 9 bytes, the
     * rest is stored in the input/output buffer.
     */

    uint8_t *bytes;
    size_t bytes_read;
    if (text_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes)) {
        if (ctx->_decoder._text.payload_finished) {
            ctx->_decoder._text.eof_already_returned = true;
            return FLUF_IO_EOF;
        }
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }

    // if there are any residual bytes from previous feeding, they are
    // concatenated with new bytes
    size_t bytes_to_decode =
            ctx->_decoder._text.aux.abuf_b64.res_buf_size + bytes_read;
    // if there are less than 4 bytes, it is not enough to decode them
    if (bytes_to_decode < 4) {
        if (ctx->_decoder._text.payload_finished) {
            if (bytes_to_decode == 0) {
                // received empty bytes
                return 0;
            }
            return FLUF_IO_ERR_FORMAT;
        } else {
            // need them later so store it in residual buffer
            if (bytes_read > 0) {
                store_residual_bytes(ctx, bytes, bytes_read);
            }
            ctx->_decoder._text.want_payload = true;
            return FLUF_IO_WANT_NEXT_PAYLOAD;
        }
    }

    size_t previously_read = ctx->_out_value.bytes_or_string.chunk_length;
    ctx->_out_value.bytes_or_string.chunk_length = 0;
    ctx->_decoder._text.aux.abuf_b64.out_buf_size = 0;
    char encoded[5];
    while (bytes_to_decode >= 4) {
        if (ctx->_decoder._text.padding_detected && bytes_read > 0) {
            return FLUF_IO_ERR_FORMAT;
        }
        // firstly, check if there are bytes from previous feeding
        if (ctx->_decoder._text.aux.abuf_b64.res_buf_size > 0) {
            memcpy(encoded,
                   ctx->_decoder._text.aux.abuf_b64.res_buf,
                   ctx->_decoder._text.aux.abuf_b64.res_buf_size);
            memcpy(encoded + ctx->_decoder._text.aux.abuf_b64.res_buf_size,
                   bytes,
                   4 - ctx->_decoder._text.aux.abuf_b64.res_buf_size);
            bytes += 4 - ctx->_decoder._text.aux.abuf_b64.res_buf_size;
            ctx->_decoder._text.aux.abuf_b64.res_buf_size = 0;
            encoded[4] = '\0';
        } else {
            // use bytes from current feeding if auxiliary buffer is empty
            memcpy(encoded, bytes, 4);
            encoded[4] = '\0';
            bytes += 4;
        }
        bytes_to_decode -= 4;

        look_for_padding(ctx, encoded);

        size_t num_decoded;
        int res = choose_buffer_and_write_bytes(ctx, &num_decoded, encoded);
        if (res) {
            return res;
        }
        ctx->_out_value.bytes_or_string.chunk_length += num_decoded;
    }
    // up to 9 bytes (AUX_OUT_BUFF_FOR_BASE64_SIZE) are stored in
    // _text.aux.abuf_b64.out_buf, rest in _text.buff, so we need to copy them
    // to _text.buff if needed
    if (ctx->_out_value.bytes_or_string.chunk_length
            > sizeof(ctx->_decoder._text.aux.abuf_b64.out_buf)) {
        memcpy(ctx->_decoder._text.buff,
               ctx->_decoder._text.aux.abuf_b64.out_buf,
               ctx->_decoder._text.aux.abuf_b64.out_buf_size);
    }

    // store residual bytes
    if (bytes_to_decode > 0) {
        store_residual_bytes(ctx, bytes, bytes_to_decode);
    }
    if (previously_read) {
        ctx->_out_value.bytes_or_string.offset += previously_read;
    }
    if (ctx->_decoder._text.payload_finished) {
        ctx->_out_value.bytes_or_string.full_length_hint =
                ctx->_out_value.bytes_or_string.offset
                + ctx->_out_value.bytes_or_string.chunk_length;
    }
    return 0;
}

static int text_get_string(fluf_io_in_ctx_t *ctx) {
    ctx->_out_value.bytes_or_string.full_length_hint = 0;
    size_t already_read = ctx->_out_value.bytes_or_string.chunk_length;
    if (text_get_all_remaining_bytes(
                ctx, &ctx->_out_value.bytes_or_string.chunk_length,
                (void **) &ctx->_out_value.bytes_or_string.data)) {
        if (ctx->_decoder._text.payload_finished) {
            ctx->_decoder._text.eof_already_returned = true;
            return FLUF_IO_EOF;
        }
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    if (already_read) {
        ctx->_out_value.bytes_or_string.offset += already_read;
    }
    if (ctx->_decoder._text.payload_finished) {
        ctx->_out_value.bytes_or_string.full_length_hint =
                ctx->_out_value.bytes_or_string.offset
                + ctx->_out_value.bytes_or_string.chunk_length;
        if (ctx->_out_value.bytes_or_string.chunk_length == 0) {
            ctx->_out_value.bytes_or_string.data = NULL;
        }
    }
    return 0;
}

static int maybe_ask_for_next_payload(fluf_io_in_ctx_t *ctx,
                                      uint8_t **bytes,
                                      size_t *bytes_read) {
    if (text_get_all_remaining_bytes(ctx, bytes_read, (void **) bytes)) {
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    if (*bytes_read + ctx->_decoder._text.aux.abuf.size
            >= sizeof(ctx->_decoder._text.aux.abuf.buf)) {
        return FLUF_IO_ERR_FORMAT;
    }
    memcpy(ctx->_decoder._text.aux.abuf.buf + ctx->_decoder._text.aux.abuf.size,
           *bytes, *bytes_read);
    ctx->_decoder._text.aux.abuf.size += *bytes_read;
    if (!ctx->_decoder._text.payload_finished) {
        ctx->_decoder._text.want_payload = true;
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    } else if (*bytes_read == 0) {
        return FLUF_IO_ERR_FORMAT;
    }
    return 0;
}

static int text_get_int(fluf_io_in_ctx_t *ctx, int64_t *out_value) {
    uint8_t *bytes;
    size_t bytes_read;
    int result = maybe_ask_for_next_payload(ctx, &bytes, &bytes_read);
    if (result) {
        return result;
    }

    if (fluf_string_to_int64_value(out_value, ctx->_decoder._text.aux.abuf.buf,
                                   ctx->_decoder._text.aux.abuf.size)) {
        return FLUF_IO_ERR_FORMAT;
    }
    ctx->_decoder._text.return_eof_next_time = true;
    return 0;
}

static int text_get_uint(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_read;
    int result = maybe_ask_for_next_payload(ctx, &bytes, &bytes_read);
    if (result) {
        return result;
    }

    if (fluf_string_to_uint64_value(&ctx->_out_value.uint_value,
                                    ctx->_decoder._text.aux.abuf.buf,
                                    ctx->_decoder._text.aux.abuf.size)) {
        return FLUF_IO_ERR_FORMAT;
    }
    ctx->_decoder._text.return_eof_next_time = true;
    return 0;
}

static int text_get_double(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_read;
    int result = maybe_ask_for_next_payload(ctx, &bytes, &bytes_read);
    if (result) {
        return result;
    }

    if (fluf_string_to_simple_double_value(&ctx->_out_value.double_value,
                                           ctx->_decoder._text.aux.abuf.buf,
                                           ctx->_decoder._text.aux.abuf.size)) {
        return FLUF_IO_ERR_FORMAT;
    }
    ctx->_decoder._text.return_eof_next_time = true;
    return 0;
}

static int text_get_bool(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_read;
    if (text_get_all_remaining_bytes(ctx, &bytes_read, (void **) &bytes)) {
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }
    if (bytes_read > 1) {
        return FLUF_IO_ERR_FORMAT;
    }

    if (ctx->_decoder._text.aux.abuf.size == 0) {
        memcpy(ctx->_decoder._text.aux.abuf.buf, bytes, bytes_read);
    } else if (bytes_read > 0) {
        return FLUF_IO_ERR_FORMAT;
    }

    if (!ctx->_decoder._text.payload_finished) {
        return FLUF_IO_WANT_NEXT_PAYLOAD;
    }

    switch (ctx->_decoder._text.aux.abuf.buf[0]) {
    case '0':
        ctx->_out_value.bool_value = false;
        ctx->_decoder._text.return_eof_next_time = true;
        return 0;
    case '1':
        ctx->_out_value.bool_value = true;
        ctx->_decoder._text.return_eof_next_time = true;
        return 0;
    default:
        return FLUF_IO_ERR_FORMAT;
    }
}

static int parse_objlnk(char *objlnk,
                        size_t obj_lnk_size,
                        fluf_oid_t *out_oid,
                        fluf_iid_t *out_iid) {
    if (strlen(objlnk) > 2 * (AVS_UINT_STR_BUF_SIZE(uint16_t) - 1) + 1) {
        return -1;
    }
    char *colon = strchr(objlnk, ':');
    if (!colon) {
        return -1;
    }
    *colon = '\0';
    uint32_t oid;
    uint32_t iid;
    if (fluf_string_to_uint32_value(&oid, objlnk, (size_t) (colon - objlnk))
            || fluf_string_to_uint32_value(&iid,
                                           colon + 1,
                                           (size_t) ((objlnk + obj_lnk_size - 1)
                                                     - colon))
            || oid > UINT16_MAX || iid > UINT16_MAX) {
        return -1;
    }
    *out_oid = (fluf_oid_t) oid;
    *out_iid = (fluf_iid_t) iid;
    return 0;
}

static int text_get_objlnk(fluf_io_in_ctx_t *ctx) {
    uint8_t *bytes;
    size_t bytes_read;
    int result = maybe_ask_for_next_payload(ctx, &bytes, &bytes_read);
    if (result) {
        return result;
    }

    if (parse_objlnk(ctx->_decoder._text.aux.abuf.buf,
                     ctx->_decoder._text.aux.abuf.size,
                     &ctx->_out_value.objlnk.oid,
                     &ctx->_out_value.objlnk.iid)) {
        return FLUF_IO_ERR_FORMAT;
    }
    ctx->_decoder._text.return_eof_next_time = true;
    return 0;
}

int _fluf_text_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                 fluf_data_type_t *inout_type_bitmask,
                                 const fluf_res_value_t **out_value,
                                 const fluf_uri_path_t **out_path) {
    assert(ctx && inout_type_bitmask && out_value && out_path);
    int result;
    if (ctx->_decoder._text.eof_already_returned) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (ctx->_decoder._text.return_eof_next_time) {
        ctx->_decoder._text.eof_already_returned = true;
        return FLUF_IO_EOF;
    }
    *out_path = &ctx->_out_path;
    switch (*inout_type_bitmask) {
    case FLUF_DATA_TYPE_NULL:
        return FLUF_IO_ERR_FORMAT;
    case FLUF_DATA_TYPE_BYTES:
        result = text_get_bytes(ctx);
        break;
    case FLUF_DATA_TYPE_STRING: {
        result = text_get_string(ctx);
        break;
    }
    case FLUF_DATA_TYPE_INT:
        result = text_get_int(ctx, &ctx->_out_value.int_value);
        break;
    case FLUF_DATA_TYPE_UINT:
        result = text_get_uint(ctx);
        break;
    case FLUF_DATA_TYPE_DOUBLE:
        result = text_get_double(ctx);
        break;
    case FLUF_DATA_TYPE_BOOL:
        result = text_get_bool(ctx);
        break;
    case FLUF_DATA_TYPE_OBJLNK:
        result = text_get_objlnk(ctx);
        break;
    case FLUF_DATA_TYPE_TIME:
        result = text_get_int(ctx, &ctx->_out_value.time_value);
        break;
    default:
        return FLUF_IO_WANT_TYPE_DISAMBIGUATION;
    }
    if (result) {
        if (result == FLUF_IO_ERR_FORMAT) {
            *out_path = NULL;
        }
        *out_value = NULL;
        return result;
    }
    *out_value = &ctx->_out_value;
    return 0;
}

int _fluf_text_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                       size_t *out_count) {
    assert(ctx);
    assert(out_count);
    *out_count = 1;
    return 0;
}

#endif // FLUF_WITH_PLAINTEXT
