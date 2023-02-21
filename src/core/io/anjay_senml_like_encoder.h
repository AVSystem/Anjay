/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_SENML_LIKE_ENCODER_H
#define ANJAY_IO_SENML_LIKE_ENCODER_H

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_time.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_senml_like_encoder_struct anjay_senml_like_encoder_t;

/**
 * All <c>_anjay_senml_like_encode_*</c> functions below encode a pair of
 * automatically deduced label and value to stream and require that element was
 * started before.
 *
 * Return 0 in case of success and negative value otherwise.
 */

int _anjay_senml_like_encode_int(anjay_senml_like_encoder_t *ctx, int64_t data);

#ifdef ANJAY_WITH_LWM2M11
int _anjay_senml_like_encode_uint(anjay_senml_like_encoder_t *ctx,
                                  uint64_t data);
#endif // ANJAY_WITH_LWM2M11

int _anjay_senml_like_encode_double(anjay_senml_like_encoder_t *ctx,
                                    double data);

int _anjay_senml_like_encode_bool(anjay_senml_like_encoder_t *ctx, bool data);

int _anjay_senml_like_encode_string(anjay_senml_like_encoder_t *ctx,
                                    const char *data);

int _anjay_senml_like_encode_objlnk(anjay_senml_like_encoder_t *ctx,
                                    const char *data);

/**
 * Starts a map containing optional basename and/or name.
 * Only one value can be encoded to this map.
 *
 * @param ctx      Pointer to SenML-like encoder.
 * @param basename Zero-terminated string with basename; if NULL, basename is
 *                 not encoded.
 * @param name     Zero-terminated string with name; if NULL, name is not
 *                 encoded.
 * @param time_s   Time value in seconds to be encoded. NAN if it has to be
 *                 ommited.
 * @returns 0 in case of success, negative value otherwise.
 */
int _anjay_senml_like_element_begin(anjay_senml_like_encoder_t *ctx,
                                    const char *basename,
                                    const char *name,
                                    double time_s);

/**
 * @param ctx Pointer to SenML-like encoder.
 * @returns 0 in case of success, negative value otherwise.
 */
int _anjay_senml_like_element_end(anjay_senml_like_encoder_t *ctx);

/**
 * @param ctx  Pointer to SenML-like encoder.
 * @param size Size of data in bytes.
 * @returns 0 in case of success, negative value otherwise.
 */
int _anjay_senml_like_bytes_begin(anjay_senml_like_encoder_t *ctx, size_t size);

/**
 * Appends bytes to started bytes value.
 *
 * @param ctx  Pointer to SenML-like encoder.
 * @param data Pointer to data.
 * @param size Size of data.
 * @returns 0 in case of success, negative value otherwise.
 */
int _anjay_senml_like_bytes_append(anjay_senml_like_encoder_t *ctx,
                                   const void *data,
                                   size_t size);

/**
 * @param ctx Pointer to SenML-like encoder.
 * @returns 0 in case of success, negative value otherwise.
 */
int _anjay_senml_like_bytes_end(anjay_senml_like_encoder_t *ctx);

/**
 * Deletes SenML-like encoder. Performs validation if necessary.
 *
 * @param ctx Pointer to pointer to SenML-like encoder.
 * @returns 0 in case of success, negative value otherwise.
 */
int _anjay_senml_like_encoder_cleanup(anjay_senml_like_encoder_t **ctx);

#ifdef ANJAY_WITH_CBOR
/**
 * Creates SenML CBOR encoder (content format 112).
 *
 * @param stream Stream to encode data to. Encoder doesn't take ownership of
 *               stream.
 * @returns Pointer to encoder in case of success, NULL otherwise.
 *
 * In current implementation, all data are cached and written to stream during
 * call to <c>_anjay_senml_like_encoder_cleanup</c>.
 */
anjay_senml_like_encoder_t *_anjay_senml_cbor_encoder_new(avs_stream_t *stream);
#endif // ANJAY_WITH_CBOR

#ifdef ANJAY_WITH_SENML_JSON
/**
 * Creates SenML JSON encoder (content format 110).
 * Writes <c>[</c> to stream.
 *
 * @param stream Stream to encode data to. Encoder doesn't take ownership of
 *               stream.
 * @returns Pointer to encoder in case of success, NULL otherwise.
 */
anjay_senml_like_encoder_t *_anjay_senml_json_encoder_new(avs_stream_t *stream);
#endif // ANJAY_WITH_SENML_JSON

/**
 * Creates LwM2M 1.0 JSON encoder (content format 11543).
 * Writes <c>{"bn":basename,"e":[</c> to stream.
 *
 * @param stream   Stream to encode data to. Encoder doesn't take ownership of
 *                 stream.
 * @param basename Pointer to zero-terminated string with basename.
 * @returns Pointer to encoder in case of success, NULL otherwise.
 */
anjay_senml_like_encoder_t *_anjay_lwm2m_json_encoder_new(avs_stream_t *stream,
                                                          const char *basename);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_IO_SENML_LIKE_ENCODER_H
