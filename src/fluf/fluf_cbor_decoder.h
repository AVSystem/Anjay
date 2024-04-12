/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_CBOR_DECODER_H
#define FLUF_CBOR_DECODER_H

#include <fluf/fluf_cbor_decoder_ll.h>
#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_SENML_CBOR) \
        || defined(FLUF_WITH_LWM2M_CBOR)
int _fluf_cbor_get_i64_from_ll_number(const fluf_cbor_ll_number_t *number,
                                      int64_t *out_value,
                                      bool allow_convert_fractions);

int _fluf_cbor_get_u64_from_ll_number(const fluf_cbor_ll_number_t *number,
                                      uint64_t *out_value);

int _fluf_cbor_get_double_from_ll_number(const fluf_cbor_ll_number_t *number,
                                         double *out_value);

int _fluf_cbor_get_short_string(
        fluf_cbor_ll_decoder_t *ctx,
        fluf_cbor_ll_decoder_bytes_ctx_t **bytes_ctx_ptr,
        size_t *bytes_consumed_ptr,
        char *out_string_buf,
        size_t out_string_buf_size);
#endif /* defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_SENML_CBOR) || \
          defined(FLUF_WITH_LWM2M_CBOR) */

#if defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)
int _fluf_cbor_extract_value(
        fluf_cbor_ll_decoder_t *ctx,
        fluf_cbor_ll_decoder_bytes_ctx_t **bytes_ctx_ptr,
        size_t *bytes_consumed_ptr,
        char (*objlnk_buf)[_FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE],
        fluf_data_type_t *inout_type_bitmask,
        fluf_res_value_t *out_value);
#endif // defined(FLUF_WITH_CBOR) || defined(FLUF_WITH_LWM2M_CBOR)

#ifdef FLUF_WITH_CBOR
int _fluf_cbor_decoder_init(fluf_io_in_ctx_t *ctx,
                            const fluf_uri_path_t *base_path);

int _fluf_cbor_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                    void *buff,
                                    size_t buff_size,
                                    bool payload_finished);

int _fluf_cbor_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                 fluf_data_type_t *inout_type_bitmask,
                                 const fluf_res_value_t **out_value,
                                 const fluf_uri_path_t **out_path);

int _fluf_cbor_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                       size_t *out_count);
#endif // FLUF_WITH_CBOR

#ifdef FLUF_WITH_SENML_CBOR
int _fluf_senml_cbor_decoder_init(fluf_io_in_ctx_t *ctx,
                                  fluf_op_t operation_type,
                                  const fluf_uri_path_t *base_path);

int _fluf_senml_cbor_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                          void *buff,
                                          size_t buff_size,
                                          bool payload_finished);

int _fluf_senml_cbor_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                       fluf_data_type_t *inout_type_bitmask,
                                       const fluf_res_value_t **out_value,
                                       const fluf_uri_path_t **out_path);

int _fluf_senml_cbor_decoder_get_entry_count(fluf_io_in_ctx_t *ctx,
                                             size_t *out_count);
#endif // FLUF_WITH_SENML_CBOR

#ifdef FLUF_WITH_LWM2M_CBOR
int _fluf_lwm2m_cbor_decoder_init(fluf_io_in_ctx_t *ctx,
                                  const fluf_uri_path_t *base_path);

int _fluf_lwm2m_cbor_decoder_feed_payload(fluf_io_in_ctx_t *ctx,
                                          void *buff,
                                          size_t buff_size,
                                          bool payload_finished);

int _fluf_lwm2m_cbor_decoder_get_entry(fluf_io_in_ctx_t *ctx,
                                       fluf_data_type_t *inout_type_bitmask,
                                       const fluf_res_value_t **out_value,
                                       const fluf_uri_path_t **out_path);
#endif // FLUF_WITH_LWM2M_CBOR

#ifdef __cplusplus
}
#endif

#endif // FLUF_CBOR_ENCODER_H
