/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_BLOCK_RESPONSE_H
#define ANJAY_COAP_BLOCK_RESPONSE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <avsystem/commons/coap/ctx.h>
#include <avsystem/commons/coap/msg.h>

#include "transfer.h"

#include "../id_source/id_source.h"
#include "../stream/common.h"
#include "../stream/in.h"
#include "../stream/out.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_BLOCK_SEND

typedef struct {
    anjay_coap_block_request_validator_t *validator;
    void *validator_arg;
} anjay_coap_block_request_validator_ctx_t;

/**
 * Creates a block response object.
 *
 * @param max_block_size           Maximum block size the client is willing to
 *                                 handle.
 * @param stream_data              Internal structure of the CoAP stream for
 *                                 which the block request is performed. The
 *                                 <c>out</c> field MUST NOT be used without
 *                                 reinitializing after a successful call to
 *                                 this function.
 * @param socket                   The CoAP socket used to send/receive blocks.
 * @param id_source                CoAP id source for the block responses.
 * @param validator_ctx            Request relation validator context.
 *
 * @returns Created block_response object on success, NULL on failure.
 */
coap_block_transfer_ctx_t *_anjay_coap_block_response_new(
        uint16_t max_block_size,
        coap_stream_common_t *stream_data,
        coap_id_source_t *id_source,
        anjay_coap_block_request_validator_ctx_t *validator_ctx);

avs_coap_msg_identity_t
_anjay_coap_block_response_last_request_id(coap_block_transfer_ctx_t *ctx);

#else

#    define _anjay_coap_block_response_last_request_id(...) \
        (AVS_UNREACHABLE("should never happen"),            \
         (avs_coap_msg_identity_t) {                        \
             .msg_id = 0                                    \
         })

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif
