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

#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

int _fluf_cbor_encoder_init(fluf_io_out_ctx_t *ctx);

int _fluf_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                 const fluf_io_out_entry_t *entry);

int _fluf_senml_cbor_encoder_init(fluf_io_out_ctx_t *ctx,
                                  const fluf_uri_path_t *base_path,
                                  size_t items_count,
                                  bool encode_time);

int _fluf_senml_cbor_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                       const fluf_io_out_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif // FLUF_CBOR_ENCODER_H
