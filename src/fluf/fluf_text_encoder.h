/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_TEXT_ENCODER_H
#define FLUF_TEXT_ENCODER_H

#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FLUF_WITH_PLAINTEXT

int _fluf_text_encoder_init(fluf_io_out_ctx_t *ctx);

int _fluf_text_out_ctx_new_entry(fluf_io_out_ctx_t *ctx,
                                 const fluf_io_out_entry_t *entry);
int _fluf_text_get_extended_data_payload(void *out_buff,
                                         size_t out_buff_len,
                                         size_t *inout_copied_bytes,
                                         fluf_io_buff_t *ctx,
                                         const fluf_io_out_entry_t *entry);
#endif // FLUF_WITH_PLAINTEXT

#ifdef __cplusplus
}
#endif

#endif // FLUF_TEXT_ENCODER_H
