/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_OPAQUE_H
#define FLUF_OPAQUE_H

#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FLUF_WITH_OPAQUE

int _fluf_opaque_out_init(fluf_io_out_ctx_t *ctx);

int _fluf_opaque_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                   const fluf_io_out_entry_t *entry);

int _fluf_opaque_get_extended_data_payload(void *out_buff,
                                           size_t out_buff_len,
                                           size_t *out_copied_bytes,
                                           fluf_io_buff_t *ctx,
                                           const fluf_io_out_entry_t *entry);

int _fluf_opaque_decoder_init(fluf_io_in_ctx_t *ctx,
                              const fluf_uri_path_t *request_uri);

int _fluf_opaque_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                      void *buff,
                                      size_t buff_size,
                                      bool payload_finished);

int _fluf_opaque_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                   fluf_data_type_t *inout_type_bitmask,
                                   const fluf_res_value_t **out_value,
                                   const fluf_uri_path_t **out_path);

int _fluf_opaque_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                         size_t *out_count);

#endif // FLUF_WITH_OPAQUE

#ifdef __cplusplus
}
#endif

#endif // FLUF_OPAQUE_H
