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
#include <fluf/fluf_config.h>
#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include "fluf_cbor_decoder.h"
#include "fluf_cbor_encoder.h"
#include "fluf_internal.h"
#include "fluf_opaque.h"
#include "fluf_text_decoder.h"
#include "fluf_text_encoder.h"
#include "fluf_tlv_decoder.h"

AVS_STATIC_ASSERT(_FLUF_IO_CTX_BUFFER_LENGTH
                          >= FLUF_CBOR_LL_SINGLE_CALL_MAX_LEN,
                  CBOR_buffer_too_small);

static const uint16_t supported_formats_list[] = {
#ifdef FLUF_WITH_OPAQUE
    FLUF_COAP_FORMAT_OPAQUE_STREAM,
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_PLAINTEXT
    FLUF_COAP_FORMAT_PLAINTEXT,
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_CBOR
    FLUF_COAP_FORMAT_CBOR,
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    FLUF_COAP_FORMAT_OMA_LWM2M_CBOR,
#endif // FLUF_WITH_LWM2M_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    FLUF_COAP_FORMAT_SENML_CBOR,     FLUF_COAP_FORMAT_SENML_ETCH_CBOR
#endif // FLUF_WITH_SENML_CBOR
};

void _fluf_io_reset_internal_buff(fluf_io_buff_t *ctx) {
    assert(ctx);
    ctx->offset = 0;
    ctx->bytes_in_internal_buff = 0;
    ctx->is_extended_type = false;
}

static int
check_format(uint16_t given_format, size_t items_count, fluf_op_t op) {
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
    // OPAQUE, CBOR and PLAINTEXT formats are allowed only for single record and
    // for 3 types of requests
    if ((given_format == FLUF_COAP_FORMAT_OPAQUE_STREAM
         || given_format == FLUF_COAP_FORMAT_CBOR
         || given_format == FLUF_COAP_FORMAT_PLAINTEXT)
            && (items_count > 1
                || (op != FLUF_OP_DM_READ && op != FLUF_OP_INF_OBSERVE
                    && op != FLUF_OP_INF_CANCEL_OBSERVE))) {
        return -1;
    }
    return 0;
}

static uint16_t choose_format(uint16_t given_format) {
    if (given_format != FLUF_COAP_FORMAT_NOT_DEFINED) {
        return given_format;
    }
#ifdef FLUF_WITH_LWM2M_CBOR
    return FLUF_COAP_FORMAT_OMA_LWM2M_CBOR;
#else  // FLUF_WITH_LWM2M_CBOR
    return FLUF_COAP_FORMAT_SENML_CBOR;
#endif // FLUF_WITH_LWM2M_CBOR
}

static int get_cbor_extended_data(fluf_io_buff_t *buff_ctx,
                                  const fluf_io_out_entry_t *entry,
                                  void *out_buff,
                                  size_t out_buff_len,
                                  size_t *copied_bytes,
                                  size_t bytes_at_the_end_to_ignore) {
    if (bytes_at_the_end_to_ignore >= buff_ctx->remaining_bytes) {
        return 0;
    }
    size_t extended_offset =
            buff_ctx->offset - buff_ctx->bytes_in_internal_buff;
    size_t bytes_to_copy =
            AVS_MIN(buff_ctx->remaining_bytes - bytes_at_the_end_to_ignore,
                    out_buff_len - *copied_bytes);

    if (entry->type == FLUF_DATA_TYPE_BYTES
            || entry->type == FLUF_DATA_TYPE_STRING) {
        memcpy(&((uint8_t *) out_buff)[*copied_bytes],
               &((const uint8_t *)
                         entry->value.bytes_or_string.data)[extended_offset],
               bytes_to_copy);
    } else {
        int res = entry->value.external_data.get_external_data(
                &((uint8_t *) out_buff)[*copied_bytes], bytes_to_copy,
                extended_offset, entry->value.external_data.user_args);
        if (res) {
            return res;
        }
    }

    *copied_bytes += bytes_to_copy;
    buff_ctx->remaining_bytes -= bytes_to_copy;
    buff_ctx->offset += bytes_to_copy;

    // no more records
    if (!buff_ctx->remaining_bytes) {
        _fluf_io_reset_internal_buff(buff_ctx);
        return 0;
    }
    return FLUF_IO_NEED_NEXT_CALL;
}

int fluf_io_out_ctx_init(fluf_io_out_ctx_t *ctx,
                         fluf_op_t operation_type,
                         const fluf_uri_path_t *base_path,
                         size_t items_count,
                         uint16_t format) {
    assert(ctx);
    bool use_base_path = false;
#ifdef FLUF_WITH_SENML_CBOR
    bool encode_time = false;
#endif // FLUF_WITH_SENML_CBOR

    if (!items_count || !ctx) {
        return FLUF_IO_ERR_INPUT_ARG;
    }
    switch (operation_type) {
    case FLUF_OP_DM_READ:
    case FLUF_OP_INF_OBSERVE:
    case FLUF_OP_INF_CANCEL_OBSERVE:
        use_base_path = true;
        assert(base_path);
        break;
    case FLUF_OP_DM_READ_COMP:
    case FLUF_OP_INF_OBSERVE_COMP:
    case FLUF_OP_INF_CANCEL_OBSERVE_COMP:
        break;
    case FLUF_OP_INF_NON_CON_NOTIFY:
    case FLUF_OP_INF_CON_NOTIFY:
    case FLUF_OP_INF_CON_SEND:
    case FLUF_OP_INF_NON_CON_SEND:
#ifdef FLUF_WITH_SENML_CBOR
        encode_time = true;
#endif // FLUF_WITH_SENML_CBOR
        break;
    default:
        return FLUF_IO_ERR_INPUT_ARG;
    }
    if (check_format(format, items_count, operation_type)) {
        return FLUF_IO_ERR_FORMAT;
    }

    const fluf_uri_path_t path =
            use_base_path ? *base_path : FLUF_MAKE_ROOT_PATH();

    memset(ctx, 0, sizeof(fluf_io_out_ctx_t));
    ctx->_format = choose_format(format);

    switch (ctx->_format) {
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        return _fluf_text_encoder_init(ctx);
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        return _fluf_opaque_out_init(ctx);
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        return _fluf_cbor_encoder_init(ctx);
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
    case FLUF_COAP_FORMAT_SENML_ETCH_CBOR:
        return _fluf_senml_cbor_encoder_init(ctx, &path, items_count,
                                             encode_time);
#endif // FLUF_WITH_SENML_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    case FLUF_COAP_FORMAT_OMA_LWM2M_CBOR:
        return _fluf_lwm2m_cbor_encoder_init(ctx, &path, items_count);
#endif // FLUF_WITH_LWM2M_CBOR
    default:
        // not implemented yet
        return FLUF_IO_ERR_INPUT_ARG;
    }
}

int fluf_io_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                              const fluf_io_out_entry_t *entry) {
    assert(ctx && entry);
    int res = FLUF_IO_ERR_INPUT_ARG;

    switch (ctx->_format) {
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        res = _fluf_text_out_ctx_new_entry(ctx, entry);
        break;
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        res = _fluf_opaque_out_ctx_new_entry(ctx, entry);
        break;
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        res = _fluf_cbor_out_ctx_new_entry(ctx, entry);
        break;
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
    case FLUF_COAP_FORMAT_SENML_ETCH_CBOR:
        res = _fluf_senml_cbor_out_ctx_new_entry(ctx, entry);
        break;
#endif // FLUF_WITH_SENML_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    case FLUF_COAP_FORMAT_OMA_LWM2M_CBOR:
        res = _fluf_lwm2m_cbor_out_ctx_new_entry(ctx, entry);
        break;
#endif // FLUF_WITH_LWM2M_CBOR
    default:
        break;
    }
    if (!res) {
        ctx->_entry = entry;
    }
    return res;
}

void _fluf_io_get_payload_from_internal_buff(fluf_io_buff_t *buff_ctx,
                                             void *out_buff,
                                             size_t out_buff_len,
                                             size_t *copied_bytes) {
    if (buff_ctx->offset >= buff_ctx->bytes_in_internal_buff
            || !buff_ctx->bytes_in_internal_buff) {
        *copied_bytes = 0;
        return;
    }

    size_t bytes_to_copy =
            AVS_MIN(buff_ctx->bytes_in_internal_buff - buff_ctx->offset,
                    out_buff_len);
    memcpy(out_buff, &(buff_ctx->internal_buff[buff_ctx->offset]),
           bytes_to_copy);
    buff_ctx->remaining_bytes -= bytes_to_copy;
    buff_ctx->offset += bytes_to_copy;
    *copied_bytes = bytes_to_copy;
}

int fluf_io_out_ctx_get_payload(fluf_io_out_ctx_t *ctx,
                                void *out_buff,
                                size_t out_buff_len,
                                size_t *out_copied_bytes) {
    assert(ctx && out_buff && out_buff_len && out_copied_bytes);
    fluf_io_buff_t *buff_ctx = &ctx->_buff;

    /* Empty packets are illegal for all types apart from extended strings and
     * extended bytes in plain text format or opaque stream */
    if (!buff_ctx->remaining_bytes
            && !((ctx->_format == FLUF_COAP_FORMAT_PLAINTEXT
                  || ctx->_format == FLUF_COAP_FORMAT_OPAQUE_STREAM)
                 && buff_ctx->is_extended_type)) {
        return FLUF_IO_ERR_LOGIC;
    }
    _fluf_io_get_payload_from_internal_buff(&ctx->_buff, out_buff, out_buff_len,
                                            out_copied_bytes);

    if (!buff_ctx->remaining_bytes && !buff_ctx->b64_cache.cache_offset) {
        _fluf_io_reset_internal_buff(buff_ctx);
        return 0;
    } else if (!buff_ctx->is_extended_type
               || out_buff_len == *out_copied_bytes) {
        return FLUF_IO_NEED_NEXT_CALL;
    }

    switch (ctx->_format) {
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        return _fluf_text_get_extended_data_payload(out_buff, out_buff_len,
                                                    out_copied_bytes, buff_ctx,
                                                    ctx->_entry);
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        return _fluf_opaque_get_extended_data_payload(out_buff, out_buff_len,
                                                      out_copied_bytes,
                                                      buff_ctx, ctx->_entry);
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        return get_cbor_extended_data(&ctx->_buff, ctx->_entry, out_buff,
                                      out_buff_len, out_copied_bytes, 0);
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
        return get_cbor_extended_data(&ctx->_buff, ctx->_entry, out_buff,
                                      out_buff_len, out_copied_bytes, 0);
#endif // FLUF_WITH_SENML_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    case FLUF_COAP_FORMAT_OMA_LWM2M_CBOR: {
        fluf_internal_lwm2m_cbor_encoder_t *lwm2m = &ctx->_encoder._lwm2m;
        /**
         * For the last record, the additional data are also refers to the ends
         * of indefinite maps, get_cbor_extended_data will ignore last
         * lwm2m->maps_opened bytes, and they will be copied to out_buff in the
         * _fluf_get_lwm2m_cbor_map_ends.
         */
        int ret_val = get_cbor_extended_data(
                &ctx->_buff, ctx->_entry, out_buff, out_buff_len,
                out_copied_bytes, lwm2m->items_count ? 0 : lwm2m->maps_opened);
        // ret_val == FLUF_IO_NEED_NEXT_CALL means that there are still bytes to
        // be copied from internal buffer but maybe
        // _fluf_get_lwm2m_cbor_map_ends will copy more bytes
        if (ret_val && ret_val != FLUF_IO_NEED_NEXT_CALL) {
            return ret_val;
        }
        if (!lwm2m->items_count
                && ctx->_buff.remaining_bytes <= lwm2m->maps_opened) {
            ret_val = _fluf_get_lwm2m_cbor_map_ends(ctx, out_buff, out_buff_len,
                                                    out_copied_bytes);
        }
        if (!ctx->_buff.remaining_bytes) {
            _fluf_io_reset_internal_buff(buff_ctx);
        }
        return ret_val;
    }
#endif // FLUF_WITH_LWM2M_CBOR
    default:
        return FLUF_IO_ERR_LOGIC;
    }
}

uint16_t fluf_io_out_ctx_get_format(fluf_io_out_ctx_t *ctx) {
    return ctx->_format;
}

size_t _fluf_io_out_add_objlink(fluf_io_buff_t *buff_ctx,
                                size_t buf_pos,
                                fluf_oid_t oid,
                                fluf_iid_t iid) {
    char buffer[_FLUF_IO_CBOR_SIMPLE_RECORD_MAX_LENGTH] = { 0 };

    size_t str_size = fluf_uint16_to_string_value(buffer, oid);
    buffer[str_size++] = ':';
    str_size += fluf_uint16_to_string_value(&buffer[str_size], iid);

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
                                    fluf_io_buff_t *buff_ctx) {
    assert(buff_ctx->remaining_bytes == buff_ctx->bytes_in_internal_buff);
    size_t write_pointer = buff_ctx->bytes_in_internal_buff;

    if (!first_record) {
        buff_ctx->internal_buff[write_pointer++] = ',';
    }
    buff_ctx->internal_buff[write_pointer++] = '<';
    for (uint16_t i = 0; i < uri_path->uri_len; i++) {
        buff_ctx->internal_buff[write_pointer++] = '/';
        write_pointer += fluf_uint16_to_string_value(
                (char *) &buff_ctx->internal_buff[write_pointer],
                uri_path->ids[i]);
    }
    buff_ctx->internal_buff[write_pointer++] = '>';

    if (dim) {
        if (!fluf_uri_path_is(uri_path, FLUF_ID_RID)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        memcpy(&buff_ctx->internal_buff[write_pointer], ";dim=", 5);
        write_pointer += 5;
        write_pointer += fluf_uint16_to_string_value(
                (char *) &buff_ctx->internal_buff[write_pointer], *dim);
    }
    if (version) {
        if (fluf_validate_obj_version(version)) {
            return FLUF_IO_ERR_INPUT_ARG;
        }
        memcpy(&buff_ctx->internal_buff[write_pointer], ";ver=", 5);
        write_pointer += 5;
        size_t ver_len = strlen(version);
        memcpy(&buff_ctx->internal_buff[write_pointer], version, ver_len);
        write_pointer += ver_len;
    }

    buff_ctx->bytes_in_internal_buff = write_pointer;
    buff_ctx->remaining_bytes = write_pointer;
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
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        return _fluf_text_decoder_init(ctx, base_path);
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        return _fluf_opaque_decoder_init(ctx, base_path);
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        return _fluf_cbor_decoder_init(ctx, base_path);
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
    case FLUF_COAP_FORMAT_SENML_ETCH_CBOR:
        return _fluf_senml_cbor_decoder_init(ctx, operation_type, base_path);
#endif // FLUF_WITH_SENML_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    case FLUF_COAP_FORMAT_OMA_LWM2M_CBOR:
        return _fluf_lwm2m_cbor_decoder_init(ctx, base_path);
#endif // FLUF_WITH_LWM2M_CBOR
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
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        return _fluf_text_decoder_feed_payload(ctx, buff, buff_size,
                                               payload_finished);
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        return _fluf_opaque_decoder_feed_payload(ctx, buff, buff_size,
                                                 payload_finished);
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        return _fluf_cbor_decoder_feed_payload(ctx, buff, buff_size,
                                               payload_finished);
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
    case FLUF_COAP_FORMAT_SENML_ETCH_CBOR:
        return _fluf_senml_cbor_decoder_feed_payload(ctx, buff, buff_size,
                                                     payload_finished);
#endif // FLUF_WITH_SENML_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    case FLUF_COAP_FORMAT_OMA_LWM2M_CBOR:
        return _fluf_lwm2m_cbor_decoder_feed_payload(ctx, buff, buff_size,
                                                     payload_finished);
#endif // FLUF_WITH_LWM2M_CBOR
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
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        return _fluf_text_decoder_get_entry(ctx, inout_type_bitmask, out_value,
                                            out_path);
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        return _fluf_opaque_decoder_get_entry(ctx, inout_type_bitmask,
                                              out_value, out_path);
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        return _fluf_cbor_decoder_get_entry(ctx, inout_type_bitmask, out_value,
                                            out_path);
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
    case FLUF_COAP_FORMAT_SENML_ETCH_CBOR:
        return _fluf_senml_cbor_decoder_get_entry(ctx, inout_type_bitmask,
                                                  out_value, out_path);
#endif // FLUF_WITH_SENML_CBOR
#ifdef FLUF_WITH_LWM2M_CBOR
    case FLUF_COAP_FORMAT_OMA_LWM2M_CBOR:
        return _fluf_lwm2m_cbor_decoder_get_entry(ctx, inout_type_bitmask,
                                                  out_value, out_path);
#endif // FLUF_WITH_LWM2M_CBOR
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
#ifdef FLUF_WITH_PLAINTEXT
    case FLUF_COAP_FORMAT_PLAINTEXT:
        return _fluf_text_decoder_get_entry_count(ctx, out_count);
#endif // FLUF_WITH_PLAINTEXT
#ifdef FLUF_WITH_OPAQUE
    case FLUF_COAP_FORMAT_OPAQUE_STREAM:
        return _fluf_opaque_decoder_get_entry_count(ctx, out_count);
#endif // FLUF_WITH_OPAQUE
#ifdef FLUF_WITH_CBOR
    case FLUF_COAP_FORMAT_CBOR:
        return _fluf_cbor_decoder_get_entry_count(ctx, out_count);
#endif // FLUF_WITH_CBOR
#ifdef FLUF_WITH_SENML_CBOR
    case FLUF_COAP_FORMAT_SENML_CBOR:
    case FLUF_COAP_FORMAT_SENML_ETCH_CBOR:
        return _fluf_senml_cbor_decoder_get_entry_count(ctx, out_count);
#endif // FLUF_WITH_SENML_CBOR
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
    if (!ctx->buff.remaining_bytes) {
        return FLUF_IO_ERR_LOGIC;
    }
    _fluf_io_get_payload_from_internal_buff(&ctx->buff, out_buff, out_buff_len,
                                            out_copied_bytes);
    if (!ctx->buff.remaining_bytes) {
        ctx->buff.offset = 0;
        ctx->buff.bytes_in_internal_buff = 0;
    } else {
        return FLUF_IO_NEED_NEXT_CALL;
    }
    return 0;
}

void fluf_io_register_ctx_init(fluf_io_register_ctx_t *ctx) {
    assert(ctx);
    memset(ctx, 0, sizeof(fluf_io_register_ctx_t));
}

#endif // FLUF_WITHOUT_REGISTER_CTX
