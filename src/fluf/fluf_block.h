/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_BLOCK_H
#define FLUF_BLOCK_H

#include <fluf/fluf.h>
#include <fluf/fluf_config.h>

#include "fluf_options.h"

#ifdef __cplusplus
extern "C" {
#endif

int _fluf_block_decode(fluf_coap_options_t *opts, fluf_block_t *block);

int _fluf_block_prepare(fluf_coap_options_t *opts, fluf_block_t *block);

#ifdef __cplusplus
}
#endif

#endif // FLUF_BLOCK_H
