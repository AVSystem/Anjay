/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_CBOR_ENCODER_H
#define FLUF_CBOR_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FLUF_WITH_CBOR
int _fluf_cbor_encoder_init(fluf_io_out_ctx_t *ctx);

int _fluf_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                 const fluf_io_out_entry_t *entry);
#endif // FLUF_WITH_CBOR

#ifdef FLUF_WITH_SENML_CBOR
int _fluf_senml_cbor_encoder_init(fluf_io_out_ctx_t *ctx,
                                  const fluf_uri_path_t *base_path,
                                  size_t items_count,
                                  bool encode_time);

int _fluf_senml_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                       const fluf_io_out_entry_t *entry);
#endif // FLUF_WITH_SENML_CBOR

#ifdef FLUF_WITH_LWM2M_CBOR
int _fluf_lwm2m_cbor_encoder_init(fluf_io_out_ctx_t *ctx,
                                  const fluf_uri_path_t *base_path,
                                  size_t items_count);

int _fluf_lwm2m_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                       const fluf_io_out_entry_t *entry);

int _fluf_get_lwm2m_cbor_map_ends(fluf_io_out_ctx_t *ctx,
                                  void *out_buff,
                                  size_t out_buff_len,
                                  size_t *inout_copied_bytes);
#endif // FLUF_WITH_LWM2M_CBOR

#if defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)
int _fluf_cbor_encode_value(fluf_io_buff_t *buff_ctx,
                            const fluf_io_out_entry_t *entry);
#endif // defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)

#ifdef __cplusplus
}
#endif

#endif // FLUF_CBOR_ENCODER_H
