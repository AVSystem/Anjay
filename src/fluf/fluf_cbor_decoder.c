/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <math.h>
#include <string.h>

#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf_config.h>
#include <fluf/fluf_utils.h>

#include "fluf_cbor_decoder.h"

#if defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_SENML_CBOR) \
        || defined(FLUF_WITH_LWM2M_CBOR)

int _fluf_cbor_get_i64_from_ll_number(const fluf_cbor_ll_number_t *number,
                                      int64_t *out_value,
                                      bool allow_convert_fractions) {
    if (number->type == FLUF_CBOR_LL_VALUE_UINT) {
        if (number->value.u64 > INT64_MAX) {
            return FLUF_IO_ERR_FORMAT;
        }
        *out_value = (int64_t) number->value.u64;
    } else if (number->type == FLUF_CBOR_LL_VALUE_NEGATIVE_INT) {
        *out_value = number->value.i64;
    } else {
        double input;
        if (number->type == FLUF_CBOR_LL_VALUE_FLOAT) {
            input = (double) number->value.f32;
        } else {
            assert(number->type == FLUF_CBOR_LL_VALUE_DOUBLE);
            input = number->value.f64;
        }
        if (allow_convert_fractions) {
            input = floor(input);
        }
        if (avs_double_convertible_to_int64(input)) {
            *out_value = (int64_t) input;
        } else {
            return FLUF_IO_ERR_FORMAT;
        }
    }
    return 0;
}

int _fluf_cbor_get_u64_from_ll_number(const fluf_cbor_ll_number_t *number,
                                      uint64_t *out_value) {
    if (number->type == FLUF_CBOR_LL_VALUE_UINT) {
        *out_value = number->value.u64;
    } else {
        double input;
        if (number->type == FLUF_CBOR_LL_VALUE_FLOAT) {
            input = (double) number->value.f32;
        } else if (number->type == FLUF_CBOR_LL_VALUE_DOUBLE) {
            input = number->value.f64;
        } else {
            return FLUF_IO_ERR_FORMAT;
        }
        if (avs_double_convertible_to_uint64(input)) {
            *out_value = (uint64_t) input;
        } else {
            return FLUF_IO_ERR_FORMAT;
        }
    }
    return 0;
}

int _fluf_cbor_get_double_from_ll_number(const fluf_cbor_ll_number_t *number,
                                         double *out_value) {
    switch (number->type) {
    case FLUF_CBOR_LL_VALUE_FLOAT:
        *out_value = number->value.f32;
        break;
    case FLUF_CBOR_LL_VALUE_DOUBLE:
        *out_value = number->value.f64;
        break;
    case FLUF_CBOR_LL_VALUE_UINT:
        *out_value = (double) number->value.u64;
        break;
    case FLUF_CBOR_LL_VALUE_NEGATIVE_INT:
        *out_value = (double) number->value.i64;
        break;
    default:
        AVS_UNREACHABLE("Invalid fluf_cbor_ll_number_t type");
        return FLUF_IO_ERR_FORMAT;
    }
    return 0;
}

int _fluf_cbor_get_short_string(
        fluf_cbor_ll_decoder_t *ctx,
        fluf_cbor_ll_decoder_bytes_ctx_t **bytes_ctx_ptr,
        size_t *bytes_consumed_ptr,
        char *out_string_buf,
        size_t out_string_buf_size) {
    assert(out_string_buf_size > 0);
    int result;
    if (!*bytes_ctx_ptr
            && (result =
                        fluf_cbor_ll_decoder_bytes(ctx, bytes_ctx_ptr, NULL))) {
        return result;
    }
    const void *chunk;
    size_t chunk_size;
    bool message_finished = false;
    while (!message_finished) {
        if ((result = fluf_cbor_ll_decoder_bytes_get_some(
                     *bytes_ctx_ptr, &chunk, &chunk_size, &message_finished))) {
            return result;
        }
        if (*bytes_consumed_ptr + chunk_size >= out_string_buf_size) {
            return FLUF_IO_ERR_FORMAT;
        }
        if (chunk_size) {
            memcpy(out_string_buf + *bytes_consumed_ptr, chunk, chunk_size);
            *bytes_consumed_ptr += chunk_size;
        }
    }
    out_string_buf[*bytes_consumed_ptr] = '\0';
    *bytes_ctx_ptr = NULL;
    *bytes_consumed_ptr = 0;
    return 0;
}

#endif /* defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_SENML_CBOR) || \
          defined(FLUF_WITH_LWM2M_CBOR) */

#if defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)

static fluf_data_type_t
lwm2m_type_from_cbor_ll_type(fluf_cbor_ll_value_type_t type) {
    switch (type) {
    case FLUF_CBOR_LL_VALUE_UINT:
        return FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_UINT | FLUF_DATA_TYPE_DOUBLE;
    case FLUF_CBOR_LL_VALUE_NEGATIVE_INT:
        return FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE;
    case FLUF_CBOR_LL_VALUE_BYTE_STRING:
        return FLUF_DATA_TYPE_BYTES;
    case FLUF_CBOR_LL_VALUE_TEXT_STRING:
        return FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK;
    case FLUF_CBOR_LL_VALUE_FLOAT:
        return FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_UINT | FLUF_DATA_TYPE_DOUBLE;
    case FLUF_CBOR_LL_VALUE_DOUBLE:
        return FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_UINT | FLUF_DATA_TYPE_DOUBLE;
    case FLUF_CBOR_LL_VALUE_BOOL:
        return FLUF_DATA_TYPE_BOOL;
    case FLUF_CBOR_LL_VALUE_TIMESTAMP:
        return FLUF_DATA_TYPE_TIME;
    default:
        return FLUF_DATA_TYPE_NULL;
    }
}

int _fluf_cbor_extract_value(
        fluf_cbor_ll_decoder_t *ctx,
        fluf_cbor_ll_decoder_bytes_ctx_t **bytes_ctx_ptr,
        size_t *bytes_consumed_ptr,
        char (*objlnk_buf)[_FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE],
        fluf_data_type_t *inout_type_bitmask,
        fluf_res_value_t *out_value) {
    fluf_cbor_ll_value_type_t type;
    int result = fluf_cbor_ll_decoder_current_value_type(ctx, &type);
    if (result) {
        return result;
    }
    switch ((*inout_type_bitmask &= lwm2m_type_from_cbor_ll_type(type))) {
    case FLUF_DATA_TYPE_NULL: {
        result = FLUF_IO_ERR_FORMAT;
        break;
    }
    case FLUF_DATA_TYPE_BYTES:
    case FLUF_DATA_TYPE_STRING: {
        if (!*bytes_ctx_ptr) {
            ptrdiff_t total_size;
            if (!(result = fluf_cbor_ll_decoder_bytes(ctx, bytes_ctx_ptr,
                                                      &total_size))) {
                memset(&out_value->bytes_or_string, 0,
                       sizeof(out_value->bytes_or_string));
                if (total_size >= 0) {
                    out_value->bytes_or_string.full_length_hint =
                            (size_t) total_size;
                }
            }
        }
        bool message_finished;
        size_t chunk_length;
        if (!result
                && !(result = fluf_cbor_ll_decoder_bytes_get_some(
                             *bytes_ctx_ptr,
                             (const void **) (intptr_t) &out_value
                                     ->bytes_or_string.data,
                             &chunk_length, &message_finished))) {
            out_value->bytes_or_string.offset +=
                    out_value->bytes_or_string.chunk_length;
            out_value->bytes_or_string.chunk_length = chunk_length;
            if (message_finished) {
                *bytes_ctx_ptr = NULL;
                out_value->bytes_or_string.full_length_hint =
                        out_value->bytes_or_string.offset
                        + out_value->bytes_or_string.chunk_length;
            }
        }
        break;
    }
    case FLUF_DATA_TYPE_INT: {
        fluf_cbor_ll_number_t number;
        (void) ((result = fluf_cbor_ll_decoder_number(ctx, &number))
                || (result = _fluf_cbor_get_i64_from_ll_number(
                            &number, &out_value->int_value, false)));
        break;
    }
    case FLUF_DATA_TYPE_DOUBLE: {
        fluf_cbor_ll_number_t number;
        (void) ((result = fluf_cbor_ll_decoder_number(ctx, &number))
                || (result = _fluf_cbor_get_double_from_ll_number(
                            &number, &out_value->double_value)));
        break;
    }
    case FLUF_DATA_TYPE_BOOL: {
        result = fluf_cbor_ll_decoder_bool(ctx, &out_value->bool_value);
        break;
    }
    case FLUF_DATA_TYPE_OBJLNK: {
        if (!(result = _fluf_cbor_get_short_string(
                      ctx, bytes_ctx_ptr, bytes_consumed_ptr, *objlnk_buf,
                      sizeof(*objlnk_buf)))
                && fluf_string_to_objlnk_value(&out_value->objlnk,
                                               *objlnk_buf)) {
            result = FLUF_IO_ERR_FORMAT;
        }
        break;
    }
    case FLUF_DATA_TYPE_UINT: {
        fluf_cbor_ll_number_t number;
        (void) ((result = fluf_cbor_ll_decoder_number(ctx, &number))
                || (result = _fluf_cbor_get_u64_from_ll_number(
                            &number, &out_value->uint_value)));
        break;
    }
    case FLUF_DATA_TYPE_TIME: {
        fluf_cbor_ll_number_t number;
        (void) ((result = fluf_cbor_ll_decoder_number(ctx, &number))
                || (result = _fluf_cbor_get_i64_from_ll_number(
                            &number, &out_value->time_value, true)));
        break;
    }
    default:
        result = FLUF_IO_WANT_TYPE_DISAMBIGUATION;
    }
    return result;
}

#endif // defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)

#ifdef FLUF_WITH_CBOR

int _fluf_cbor_decoder_init(fluf_io_in_ctx_t *ctx,
                            const fluf_uri_path_t *base_path) {
    if (!base_path || !fluf_uri_path_has(base_path, FLUF_ID_RID)) {
        return FLUF_IO_ERR_FORMAT;
    }
    ctx->_out_path = *base_path;
    fluf_cbor_ll_decoder_init(&ctx->_decoder._cbor.ctx);
    return 0;
}

int _fluf_cbor_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                    void *buff,
                                    size_t buff_size,
                                    bool payload_finished) {
    return fluf_cbor_ll_decoder_feed_payload(&ctx->_decoder._cbor.ctx, buff,
                                             buff_size, payload_finished);
}

int _fluf_cbor_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                 fluf_data_type_t *inout_type_bitmask,
                                 const fluf_res_value_t **out_value,
                                 const fluf_uri_path_t **out_path) {
    fluf_internal_cbor_decoder_t *const cbor = &ctx->_decoder._cbor;
    *out_value = NULL;
    *out_path = NULL;
    int result = 0;
    if (!cbor->bytes_ctx) {
        result = fluf_cbor_ll_decoder_errno(&cbor->ctx);
    }
    if (result == FLUF_IO_EOF) {
        if (cbor->entry_parsed) {
            return FLUF_IO_EOF;
        } else {
            return FLUF_IO_ERR_FORMAT;
        }
    } else if (result) {
        return result;
    } else if (cbor->entry_parsed) {
        // More data in the input stream - this is unexpected
        return FLUF_IO_ERR_FORMAT;
    }
    *out_path = &ctx->_out_path;
    if ((result = _fluf_cbor_extract_value(
                 &cbor->ctx, &cbor->bytes_ctx, &cbor->bytes_consumed,
                 &cbor->objlnk_buf, inout_type_bitmask, &ctx->_out_value))) {
        return result;
    }
    if (!cbor->bytes_ctx) {
        cbor->entry_parsed = true;
    }
    *out_value = &ctx->_out_value;
    return 0;
}

int _fluf_cbor_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                       size_t *out_count) {
    (void) ctx;
    *out_count = 1;
    return 0;
}

#endif // FLUF_WITH_CBOR
