/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <math.h>
#include <string.h>

#include <fluf/fluf_cbor_encoder_ll.h>
#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#include "fluf_cbor_encoder.h"
#include "fluf_internal.h"

#ifdef FLUF_WITH_LWM2M_CBOR

static size_t uri_path_span(const fluf_uri_path_t *a,
                            const fluf_uri_path_t *b) {
    size_t equal_ids = 0;
    for (;
         equal_ids < AVS_MAX(fluf_uri_path_length(a), fluf_uri_path_length(b));
         equal_ids++) {
        if (a->ids[equal_ids] != b->ids[equal_ids]) {
            break;
        }
    }
    return equal_ids;
}

static void
end_maps(fluf_io_buff_t *buff_ctx, uint8_t *map_counter, size_t count) {
    for (size_t i = 0; i < count; i++) {
        size_t bytes_written = fluf_cbor_ll_indefinite_map_end(
                &buff_ctx->internal_buff[buff_ctx->bytes_in_internal_buff]);
        buff_ctx->bytes_in_internal_buff += bytes_written;
        assert(buff_ctx->bytes_in_internal_buff <= _FLUF_IO_CTX_BUFFER_LENGTH);
        (*map_counter)--;
    }
}

static void encode_subpath(fluf_io_buff_t *buff_ctx,
                           uint8_t *map_counter,
                           const fluf_uri_path_t *path,
                           size_t begin_idx) {
    for (size_t idx = begin_idx; idx < fluf_uri_path_length(path); idx++) {
        size_t bytes_written = 0;
        // for the first record fluf_cbor_ll_indefinite_map_begin() is
        // called in _fluf_lwm2m_cbor_encoder_init(), for the rest, the first ID
        // is a continuation of the open map
        if (idx != begin_idx) {
            bytes_written = fluf_cbor_ll_indefinite_map_begin(
                    &buff_ctx->internal_buff[buff_ctx->bytes_in_internal_buff]);
            (*map_counter)++;
        }
        bytes_written += fluf_cbor_ll_encode_uint(
                &buff_ctx->internal_buff[buff_ctx->bytes_in_internal_buff
                                         + bytes_written],
                path->ids[idx]);
        buff_ctx->bytes_in_internal_buff += bytes_written;
        assert(buff_ctx->bytes_in_internal_buff <= _FLUF_IO_CTX_BUFFER_LENGTH);
    }
}

static void encode_path(fluf_io_out_ctx_t *ctx, const fluf_uri_path_t *path) {
    assert(fluf_uri_path_has(path, FLUF_ID_RID));
    fluf_internal_lwm2m_cbor_encoder_t *lwm2m_cbor = &ctx->_encoder._lwm2m;

    size_t path_span = uri_path_span(&lwm2m_cbor->last_path, path);
    if (fluf_uri_path_length(&lwm2m_cbor->last_path)) {
        assert(path_span < fluf_uri_path_length(&lwm2m_cbor->last_path));
        // close open maps to the level where the paths do not differ
        end_maps(&ctx->_buff, &ctx->_encoder._lwm2m.maps_opened,
                 fluf_uri_path_length(&lwm2m_cbor->last_path)
                         - (path_span + 1));
    }
    // write path from the level where the path differs from the last one
    encode_subpath(&ctx->_buff, &ctx->_encoder._lwm2m.maps_opened, path,
                   path_span);

    lwm2m_cbor->last_path = *path;
}

static int prepare_payload(fluf_io_out_ctx_t *ctx,
                           const fluf_io_out_entry_t *entry) {
    encode_path(ctx, &entry->path);

    fluf_io_buff_t *buff_ctx = &ctx->_buff;
    int ret_val = _fluf_cbor_encode_value(buff_ctx, entry);
    if (ret_val) {
        return ret_val;
    }

    fluf_internal_lwm2m_cbor_encoder_t *lwm2m_cbor = &ctx->_encoder._lwm2m;
    lwm2m_cbor->items_count--;
    // last record, add map endings
    if (!lwm2m_cbor->items_count) {
        buff_ctx->is_extended_type = true;
        buff_ctx->remaining_bytes += lwm2m_cbor->maps_opened;
    }
    return 0;
}

int _fluf_lwm2m_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                       const fluf_io_out_entry_t *entry) {
    assert(ctx->_format == FLUF_COAP_FORMAT_OMA_LWM2M_CBOR);
    fluf_internal_lwm2m_cbor_encoder_t *lwm2m_cbor = &ctx->_encoder._lwm2m;

    if (ctx->_buff.remaining_bytes || !lwm2m_cbor->items_count) {
        return FLUF_IO_ERR_LOGIC;
    }
    if (fluf_uri_path_outside_base(&entry->path, &lwm2m_cbor->base_path)
            || !fluf_uri_path_has(&entry->path, FLUF_ID_RID)
            // There is no specification-compliant way to represent the same two
            // paths one after the other
            || fluf_uri_path_equal(&entry->path, &lwm2m_cbor->last_path)) {
        return FLUF_IO_ERR_INPUT_ARG;
    }

    return prepare_payload(ctx, entry);
}

int _fluf_lwm2m_cbor_encoder_init(fluf_io_out_ctx_t *ctx,
                                  const fluf_uri_path_t *base_path,
                                  size_t items_count) {
    assert(base_path);

    fluf_internal_lwm2m_cbor_encoder_t *lwm2m_cbor = &ctx->_encoder._lwm2m;

    lwm2m_cbor->items_count = items_count;
    lwm2m_cbor->last_path = FLUF_MAKE_ROOT_PATH();
    lwm2m_cbor->base_path = *base_path;

    lwm2m_cbor->maps_opened = 1;
    fluf_io_buff_t *buff_ctx = &ctx->_buff;
    buff_ctx->bytes_in_internal_buff =
            fluf_cbor_ll_indefinite_map_begin(buff_ctx->internal_buff);
    return 0;
}

int _fluf_get_lwm2m_cbor_map_ends(fluf_io_out_ctx_t *ctx,
                                  void *out_buff,
                                  size_t out_buff_len,
                                  size_t *inout_copied_bytes) {
    fluf_io_buff_t *buff_ctx = &ctx->_buff;
    fluf_internal_lwm2m_cbor_encoder_t *lwm2m_cbor = &ctx->_encoder._lwm2m;

    size_t maps_to_end = AVS_MIN(out_buff_len - *inout_copied_bytes,
                                 lwm2m_cbor->maps_opened);
    memset(&((uint8_t *) out_buff)[*inout_copied_bytes],
           CBOR_INDEFINITE_STRUCTURE_BREAK, maps_to_end);
    *inout_copied_bytes += maps_to_end;
    lwm2m_cbor->maps_opened -= (uint8_t) maps_to_end;
    buff_ctx->remaining_bytes -= maps_to_end;

    if (buff_ctx->remaining_bytes) {
        return FLUF_IO_NEED_NEXT_CALL;
    }
    return 0;
}

#endif // FLUF_WITH_LWM2M_CBOR
