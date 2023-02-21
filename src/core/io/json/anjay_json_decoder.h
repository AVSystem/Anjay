/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_IO_JSON_DECODER_H
#define ANJAY_IO_JSON_DECODER_H

#include "../anjay_json_like_decoder.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_json_like_decoder_t *_anjay_json_decoder_new(avs_stream_t *stream);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_IO_JSON_DECODER_H
