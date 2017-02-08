/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANJAY_COAP_BLOCK_TRANSFER_H
#define ANJAY_COAP_BLOCK_TRANSFER_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_BLOCK_SEND

typedef struct coap_block_transfer_ctx coap_block_transfer_ctx_t;

void _anjay_coap_block_transfer_delete(coap_block_transfer_ctx_t **ctx);

int _anjay_coap_block_transfer_write(coap_block_transfer_ctx_t *ctx,
                                     const void *data,
                                     size_t data_length);

int _anjay_coap_block_transfer_finish(coap_block_transfer_ctx_t *ctx);

#else

#define _anjay_coap_block_transfer_delete(ctx) ((void) 0)

#define _anjay_coap_block_transfer_finish(ctx) \
        (assert(0 && "should never happen"), -1)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif
