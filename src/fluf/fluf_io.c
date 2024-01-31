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

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf_cbor_encoder_ll.h>
#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include "fluf_cbor_encoder.h"
#include "fluf_internal.h"
#include "fluf_tlv_decoder.h"

AVS_STATIC_ASSERT(_FLUF_IO_CTX_BUFFER_LENGTH
                          >= FLUF_CBOR_LL_SINGLE_CALL_MAX_LEN,
                  CBOR_buffer_too_small);

static uint16_t supported_formats_list[] = {
#ifdef FLUF_WITH_PLAINTEXT_SUPPORT
    FLUF_COAP_FORMAT_PLAINTEXT,
#endif // FLUF_WITH_PLAINTEXT_SUPPORT
#ifdef FLUF_WITH_DISCOVERY_ENCODER_SUPPORT
    FLUF_COAP_FORMAT_LINK_FORMAT,
#endif // FLUF_WITH_DISCOVERY_ENCODER_SUPPORT
    FLUF_COAP_FORMAT_CBOR,
#ifdef FLUF_WITH_LWM2M12
    FLUF_COAP_FORMAT_OMA_LWM2M_CBOR,
#endif // FLUF_WITH_LWM2M12
#ifndef FLUF_WITHOUT_SENML_CBOR_SUPPORT
    FLUF_COAP_FORMAT_SENML_CBOR,     FLUF_COAP_FORMAT_SENML_ETCH_CBOR
#endif // FLUF_WITHOUT_SENML_CBOR_SUPPORT
};

static int copy_to_buffer(uint8_t *buffer,
                          size_t buffer_length,
                          const fluf_io_out_entry_t *entry,
                          fluf_io_buff_t *buff_ctx,
                          const char *bootstrap_uri) {
    size_t bytes_to_copy = buff_ctx->remaining_bytes < buffer_length
                                   ? buff_ctx->remaining_bytes
                                   : buffer_length;
    size_t copied_bytes = 0;
    // first copy from internal buffer
    if (buff_ctx->offset < buff_ctx->bytes_in_internal_buff) {
        size_t bytes_to_copy_from_int_buff =
                AVS_MIN(buff_ctx->bytes_in_internal_buff - buff_ctx->offset,
                        bytes_to_copy);
        memcpy(buffer,
               &(buff_ctx->internal_buff[buff_ctx->offset]),
               bytes_to_copy_from_int_buff);
        copied_bytes = bytes_to_copy_from_int_buff;
        bytes_to_copy -= copied_bytes;
    }
    // copy extended data
    assert(buff_ctx->is_extended_type || !bytes_to_copy);
    if (entry && buff_ctx->is_extended_type && bytes_to_copy) {
        size_t extended_offset =
                buff_ctx->offset > buff_ctx->bytes_in_internal_buff
                        ? (buff_ctx->offset - buff_ctx->bytes_in_internal_buff)
                        : 0;
        if (entry->type == FLUF_DATA_TYPE_BYTES
                || entry->type == FLUF_DATA_TYPE_STRING) {
            memcpy(&buffer[copied_bytes],
                   &((const uint8_t *) entry->value.bytes_or_string
                             .data)[extended_offset],
                   bytes_to_copy);
        } else {
            int res = entry->value.external_data.get_external_data(
                    &buffer[copied_bytes], bytes_to_copy, extended_offset,
                    entry->value.external_data.user_args);
            if (res) {
                return res;
            }
        }
        copied_bytes += bytes_to_copy;
    } // uri from Bootstrap-Discover
    else if (bootstrap_uri && buff_ctx->is_extended_type && bytes_to_copy) {
        size_t extended_offset =
                buff_ctx->offset > buff_ctx->bytes_in_internal_buff
                        ? (buff_ctx->offset - buff_ctx->bytes_in_internal_buff)
                        : 0;
        // copy last part of the message
        if (buff_ctx->remaining_bytes <= buffer_length) {
            memcpy(&buffer[copied_bytes], &bootstrap_uri[extended_offset],
                   bytes_to_copy - 1);
            buffer[copied_bytes + bytes_to_copy - 1] = '\"';
        } else {
            memcpy(&buffer[copied_bytes], &bootstrap_uri[extended_offset],
                   bytes_to_copy);
        }
        copied_bytes += bytes_to_copy;
    }

    buff_ctx->remaining_bytes -= copied_bytes;
    buff_ctx->offset += copied_bytes;

    return 0;
}

static int check_format(uint16_t given_format, size_t items_count) {
    if (given_format == FLUF_COAP_FORMAT_NOT_DEFINED) {
        return 0;
    }
    bool is_present = false;
    for (size_t i = 0; i < AVS_ARRAY_SIZE(supported_formats_list); i++) {
        if (given_format == supported_formats_list[i]) {
            is_present = true;
            break;
        }
    }
    if (!is_present) {
        return -1;
    }
    if ((given_format == FLUF_COAP_FORMAT_CBOR
#ifdef FLUF_WITH_PLAINTEXT_SUPPORT
         || given_format == FLUF_WITH_PLAINTEXT_SUPPORT
#endif // FLUF_WITH_PLAINTEXT_SUPPORT
         ) && items_count > 1) {
        return -1;
    }
    return 0;
}

static uint16_t choose_format(size_t items_count, uint16_t given_format) {
    if (given_format != FLUF_COAP_FORMAT_NOT_DEFINED) {
        return given_format;
    }
    uint16_t format;

    if (items_count > 1) {
#ifdef FLUF_WITH_LWM2M12
        // TODO: FLUF_COAP_FORMAT_OMA_LWM2M_CBOR
        format = FLUF_COAP_FORMAT_SENML_CBOR;
#else  // FLUF_WITH_LWM2M12
        format = FLUF_COAP_FORMAT_SENML_CBOR;
#endif // FLUF_WITH_LWM2M12
    } else {
        format = FLUF_COAP_FORMAT_CBOR;
    }

    return format;
}

int fluf_io_out_ctx_init(fluf_io_out_ctx_t *ctx,
                         fluf_op_t operation_type,
                         const fluf_uri_path_t *base_path,
                         size_t items_count,
                         uint16_t format) {
    assert(ctx);
    bool use_base_path = false;
    bool encode_time = false;

    if (!items_count || !ctx) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    switch (operation_type) {
    case FLUF_OP_DM_READ:
    case FLUF_OP_INF_OBSERVE:
    case FLUF_OP_INF_CANCEL_OBSERVE:
        use_base_path = true;
        break;
    case FLUF_OP_DM_READ_COMP:
    case FLUF_OP_INF_OBSERVE_COMP:
    case FLUF_OP_INF_CANCEL_OBSERVE_COMP:
        break;
    case FLUF_OP_INF_NON_CON_NOTIFY:
    case FLUF_OP_INF_CON_NOTIFY:
    case FLUF_OP_INF_SEND:
        encode_time = true;
        break;
    default:
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (check_format(format, items_count)) {
        return FLUF_IO_ERR_FORMAT;
    }

    memset(ctx, 0, sizeof(fluf_io_out_ctx_t));
    ctx->_format = choose_format(items_count, format);

    if (ctx->_format == FLUF_COAP_FORMAT_CBOR) {
        return _fluf_cbor_encoder_init(ctx);
    } else if (ctx->_format == FLUF_COAP_FORMAT_SENML_CBOR
               || ctx->_format == FLUF_COAP_FORMAT_SENML_ETCH_CBOR) {
        const fluf_uri_path_t path =
                use_base_path ? *base_path : FLUF_MAKE_ROOT_PATH();
        return _fluf_senml_cbor_encoder_init(ctx, &path, items_count,
                                             encode_time);
    } else {
        // not implemented yet
        return -1;
    }
}

int fluf_io_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                              const fluf_io_out_entry_t *entry) {
    assert(ctx && entry);
    int res = FLUF_IO_ERR_INPUT_ARG;

    if (ctx->_format == FLUF_COAP_FORMAT_CBOR) {
        res = _fluf_cbor_out_ctx_new_entry(ctx, entry);
    } else if (ctx->_format == FLUF_COAP_FORMAT_SENML_CBOR
               || ctx->_format == FLUF_COAP_FORMAT_SENML_ETCH_CBOR) {
        res = _fluf_senml_cbor_out_ctx_new_entry(ctx, entry);
    }
    if (!res) {
        ctx->_entry = entry;
    }
    return res;
}

int _fluf_io_get_payload(void *out_buff,
                         size_t out_buff_len,
                         size_t *copied_bytes,
                         fluf_io_buff_t *ctx,
                         const fluf_io_out_entry_t *entry,
                         const char *bootstrap_uri) {
    assert(out_buff && out_buff_len && copied_bytes && ctx);

    if (!ctx->remaining_bytes) {
        return FLUF_IO_ERR_LOGIC;
    }

    size_t bytes_to_copy = ctx->remaining_bytes;
    int res = copy_to_buffer((uint8_t *) out_buff, out_buff_len, entry, ctx,
                             bootstrap_uri);
    if (res) {
        return res;
    }

    if (!ctx->remaining_bytes) {
        ctx->offset = 0;
        ctx->bytes_in_internal_buff = 0;
        ctx->is_extended_type = false;
    } else {
        res = FLUF_IO_NEED_NEXT_CALL;
    }
    *copied_bytes = bytes_to_copy - ctx->remaining_bytes;

    return res;
}

int fluf_io_out_ctx_get_payload(fluf_io_out_ctx_t *ctx,
                                void *out_buff,
                                size_t out_buff_len,
                                size_t *out_copied_bytes) {
    assert(ctx);
    return _fluf_io_get_payload(out_buff, out_buff_len, out_copied_bytes,
                                &ctx->_buff, ctx->_entry, NULL);
}

uint16_t fluf_io_out_ctx_get_format(fluf_io_out_ctx_t *ctx) {
    return ctx->_format;
}

size_t _fluf_io_out_add_objlink(fluf_io_buff_t *buff_ctx,
                                size_t buf_pos,
                                fluf_oid_t oid,
                                fluf_iid_t iid) {
    char buffer[_FLUF_IO_CBOR_SIMPLE_RECORD_MAX_LENGTH] = { 0 };

    size_t str_size = fluf_uint16_to_string_value(oid, buffer);
    buffer[str_size++] = ':';
    str_size += fluf_uint16_to_string_value(iid, &buffer[str_size]);

    size_t header_size =
            fluf_cbor_ll_string_begin(&buff_ctx->internal_buff[buf_pos],
                                      str_size);
    memcpy(&((uint8_t *) buff_ctx->internal_buff)[buf_pos + header_size],
           buffer, str_size);
    return header_size + str_size;
}

int _fluf_io_add_link_format_record(const fluf_uri_path_t *uri_path,
                                    const char *version,
                                    const uint16_t *dim,
                                    bool first_record,
                                    fluf_io_buff_t *ctx) {
    assert(ctx->remaining_bytes == ctx->bytes_in_internal_buff);
    size_t write_pointer = ctx->bytes_in_internal_buff;

    if (!first_record) {
        ctx->internal_buff[write_pointer++] = ',';
    }
    ctx->internal_buff[write_pointer++] = '<';
    for (uint16_t i = 0; i < uri_path->uri_len; i++) {
        ctx->internal_buff[write_pointer++] = '/';
        write_pointer += fluf_uint16_to_string_value(
                uri_path->ids[i], (char *) &ctx->internal_buff[write_pointer]);
    }
    ctx->internal_buff[write_pointer++] = '>';

    if (dim) {
        if (!fluf_uri_path_is(uri_path, FLUF_ID_RID)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        memcpy(&ctx->internal_buff[write_pointer], ";dim=", 5);
        write_pointer += 5;
        write_pointer += fluf_uint16_to_string_value(
                *dim, (char *) &ctx->internal_buff[write_pointer]);
    }
    if (version) {
        if (fluf_validate_obj_version(version)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        memcpy(&ctx->internal_buff[write_pointer], ";ver=", 5);
        write_pointer += 5;
        size_t ver_len = strlen(version);
        memcpy(&ctx->internal_buff[write_pointer], version, ver_len);
        write_pointer += ver_len;
    }

    ctx->bytes_in_internal_buff = write_pointer;
    ctx->remaining_bytes = write_pointer;
    return 0;
}

int fluf_io_in_ctx_init(fluf_io_in_ctx_t *ctx,
                        fluf_op_t operation_type,
                        const fluf_uri_path_t *base_path,
                        uint16_t format) {
    (void) operation_type;
    assert(ctx);
    memset(ctx, 0, sizeof(*ctx));
    ctx->_format = format;
    switch (format) {
    case FLUF_COAP_FORMAT_OMA_LWM2M_TLV:
        return _fluf_tlv_decoder_init(ctx, base_path);
    default:
        return FLUF_IO_ERR_INPUT_ARG;
    }
}

int fluf_io_in_ctx_feed_payload(fluf_io_in_ctx_t *ctx,
                                void *buff,
                                size_t buff_size,
                                bool payload_finished) {
    assert(ctx);
    switch (ctx->_format) {
    case FLUF_COAP_FORMAT_OMA_LWM2M_TLV:
        return _fluf_tlv_decoder_feed_payload(ctx, buff, buff_size,
                                              payload_finished);
    default:
        return FLUF_IO_ERR_LOGIC;
    }
}

int fluf_io_in_ctx_get_entry(fluf_io_in_ctx_t *ctx,
                             fluf_data_type_t *inout_type_bitmask,
                             const fluf_res_value_t **out_value,
                             const fluf_uri_path_t **out_path) {
    assert(ctx);
    switch (ctx->_format) {
    case FLUF_COAP_FORMAT_OMA_LWM2M_TLV:
        return _fluf_tlv_decoder_get_entry(ctx, inout_type_bitmask, out_value,
                                           out_path);
    default:
        return FLUF_IO_ERR_LOGIC;
    }
}

int fluf_io_in_ctx_get_entry_count(fluf_io_in_ctx_t *ctx, size_t *out_count) {
    assert(ctx);
    assert(out_count);
    switch (ctx->_format) {
    default:
        return FLUF_IO_ERR_FORMAT;
    }
}

#ifndef FLUF_WITHOUT_REGISTER_CTX

int fluf_io_register_ctx_new_entry(fluf_io_register_ctx_t *ctx,
                                   const fluf_uri_path_t *path,
                                   const char *version) {
    assert(ctx);
    if (ctx->buff.bytes_in_internal_buff) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (!(fluf_uri_path_is(path, FLUF_ID_OID)
          || fluf_uri_path_is(path, FLUF_ID_IID))
            || !fluf_uri_path_increasing(&ctx->last_path, path)) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (path->ids[FLUF_ID_OID] == FLUF_OBJ_ID_SECURITY
            || path->ids[FLUF_ID_OID] == FLUF_OBJ_ID_OSCORE) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (fluf_uri_path_is(path, FLUF_ID_IID) && version) {
        return FLUF_IO_ERR_INPUT_ARG;
    }

    int res = _fluf_io_add_link_format_record(
            path, version, NULL, !ctx->first_record_added, &ctx->buff);
    if (res) {
        return res;
    }

    ctx->last_path = *path;
    ctx->first_record_added = true;
    return 0;
}

int fluf_io_register_ctx_get_payload(fluf_io_register_ctx_t *ctx,
                                     void *out_buff,
                                     size_t out_buff_len,
                                     size_t *out_copied_bytes) {
    assert(ctx);
    return _fluf_io_get_payload(out_buff, out_buff_len, out_copied_bytes,
                                &ctx->buff, NULL, NULL);
}

void fluf_io_register_ctx_init(fluf_io_register_ctx_t *ctx) {
    assert(ctx);
    memset(ctx, 0, sizeof(fluf_io_register_ctx_t));
}

#endif // FLUF_WITHOUT_REGISTER_CTX
