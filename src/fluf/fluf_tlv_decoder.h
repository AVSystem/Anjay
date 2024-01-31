/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_IN_CTX_FLUF_TLV_DECODER_H
#define FLUF_IN_CTX_FLUF_TLV_DECODER_H

#include <fluf/fluf_io.h>

int _fluf_tlv_decoder_init(fluf_io_in_ctx_t *ctx,
                           const fluf_uri_path_t *request_uri);

int _fluf_tlv_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                   void *buff,
                                   size_t buff_size,
                                   bool payload_finished);

int _fluf_tlv_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                fluf_data_type_t *inout_type_bitmask,
                                const fluf_res_value_t **out_value,
                                const fluf_uri_path_t **out_path);

#endif // FLUF_IN_CTX_FLUF_TLV_DECODER_H
