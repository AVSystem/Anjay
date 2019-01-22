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

#include <anjay_config.h>
#include <string.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include "../coap_log.h"

#include "../id_source/static.h"
#include "../stream/common.h"
#include "response.h"
#include "transfer_impl.h"

VISIBILITY_SOURCE_BEGIN

static int
handle_block_size_renegotiation(coap_block_transfer_ctx_t *ctx,
                                const avs_coap_block_info_t *block2) {
    assert(block2->size >= AVS_COAP_MSG_BLOCK_MIN_SIZE
           && block2->size <= AVS_COAP_MSG_BLOCK_MAX_SIZE);

    if (block2->size > ctx->block.size) {
        coap_log(WARNING,
                 "client attempted to increase block size from %u to "
                 "%u B",
                 ctx->block.size, block2->size);
        return -1;
    } else if (block2->size < ctx->block.size) {
        if (block2->seq_num != 0 || ctx->num_sent_blocks != 0) {
            coap_log(ERROR, "client changed block size in the middle of block "
                            "transfer");
            return -1;
        } else {
            coap_log(TRACE, "lowering block size to %u B on client request",
                     block2->size);
            ctx->block.size = block2->size;
        }
    }

    return 0;
}

static int block_recv_handler(void *validator_ctx_,
                              const avs_coap_msg_t *msg,
                              const avs_coap_msg_t *last_response,
                              coap_block_transfer_ctx_t *ctx,
                              bool *out_wait_for_next,
                              uint8_t *out_error_code) {
    anjay_coap_block_request_validator_ctx_t *validator_ctx =
            (anjay_coap_block_request_validator_ctx_t *) validator_ctx_;

    (void) last_response;

    *out_wait_for_next = false;

    avs_coap_msg_identity_t id = avs_coap_msg_get_identity(msg);
    avs_coap_msg_identity_t prev_id = avs_coap_msg_get_identity(last_response);

    // Message identity matches last response, it means it must be a duplicate
    // of the previous request.
    if (avs_coap_identity_equal(&id, &prev_id)) {
        return BLOCK_TRANSFER_RESULT_RETRY;
    }

    _anjay_coap_id_source_static_reset(ctx->id_source, &id);

    avs_coap_block_info_t block1;
    if (avs_coap_get_block_info(msg, AVS_COAP_BLOCK1, &block1)) {
        // Malformed BLOCK1 option, or multiple BLOCK1 options found.
        *out_error_code = -ANJAY_ERR_BAD_REQUEST;
        return -1;
    } else if (block1.valid) {
        // BLOCK1 option present: we do not expect it to be set, as
        // block-wise responses to block-wise requests are not supported.
        // It must be a part of an unrelated BLOCK-wise request.
        *out_wait_for_next = true;
        *out_error_code = -ANJAY_ERR_SERVICE_UNAVAILABLE;
        return -1;
    }

    avs_coap_block_info_t block2;
    if (avs_coap_get_block_info(msg, AVS_COAP_BLOCK2, &block2)) {
        // Malformed BLOCK2 option, or multiple BLOCK2 options found.
        *out_error_code = -ANJAY_ERR_BAD_REQUEST;
        return -1;
    } else if (!block2.valid // no BLOCK2 option - must be an unrelated request
               || (validator_ctx && validator_ctx->validator
                   && validator_ctx->validator(msg,
                                               validator_ctx->validator_arg))) {
        *out_wait_for_next = true;
        *out_error_code = -ANJAY_ERR_SERVICE_UNAVAILABLE;
        return -1;
    }

    if (handle_block_size_renegotiation(ctx, &block2)) {
        *out_error_code = -ANJAY_ERR_BAD_REQUEST;
        return -1;
    }

    if (block2.seq_num < ctx->block.seq_num
            || block2.seq_num > ctx->block.seq_num + 1) {
        coap_log(WARNING, "expected BLOCK2 seq numbers to be consecutive");
        *out_wait_for_next = true;
        return -1;
    }

    if (block2.seq_num == ctx->block.seq_num) {
        return BLOCK_TRANSFER_RESULT_RETRY;
    }

    ctx->block.seq_num = block2.seq_num;
    return BLOCK_TRANSFER_RESULT_OK;
}

coap_block_transfer_ctx_t *_anjay_coap_block_response_new(
        uint16_t max_block_size,
        coap_stream_common_t *stream_data,
        coap_id_source_t *id_source,
        anjay_coap_block_request_validator_ctx_t *validator_ctx) {
    return _anjay_coap_block_transfer_new(max_block_size, stream_data,
                                          AVS_COAP_BLOCK2, id_source,
                                          block_recv_handler, validator_ctx);
}

avs_coap_msg_identity_t
_anjay_coap_block_response_last_request_id(coap_block_transfer_ctx_t *ctx) {
    return _anjay_coap_id_source_get(ctx->id_source);
}
