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

#include <config.h>
#include <string.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include "../log.h"

#include "transfer_impl.h"
#include "response.h"
#include "../stream/common.h"
#include "../id_source/static.h"

VISIBILITY_SOURCE_BEGIN

static int handle_block_size_renegotiation(coap_block_transfer_ctx_t *ctx,
                                           const coap_block_info_t *block2) {
    assert(block2->size >= ANJAY_COAP_MSG_BLOCK_MIN_SIZE
            && block2->size <= ANJAY_COAP_MSG_BLOCK_MAX_SIZE);

    if (block2->size > ctx->block.size) {
        coap_log(WARNING, "client attempted to increase block size from %u to "
                 "%u B", ctx->block.size, block2->size);
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

static int block_recv_handler(const anjay_coap_msg_t *msg,
                              const anjay_coap_msg_t *last_response,
                              coap_block_transfer_ctx_t *ctx,
                              bool *out_wait_for_next,
                              uint8_t *out_error_code) {
    (void) last_response;

    *out_wait_for_next = false;

    anjay_coap_msg_identity_t id = _anjay_coap_common_identity_from_msg(msg);
    _anjay_coap_id_source_static_reset(ctx->id_source, &id);

    coap_block_info_t block1;
    if (_anjay_coap_common_get_block_info(msg, COAP_BLOCK1,
                                          &block1) || block1.valid) {
        *out_error_code = -ANJAY_ERR_BAD_OPTION;
        return -1;
    }
    coap_block_info_t block2;
    if (_anjay_coap_common_get_block_info(msg, COAP_BLOCK2, &block2)
            || !block2.valid) {
        *out_error_code = -ANJAY_ERR_BAD_REQUEST;
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

coap_block_transfer_ctx_t *
_anjay_coap_block_response_new(uint16_t max_block_size,
                               coap_input_buffer_t *in,
                               coap_output_buffer_t *out,
                               anjay_coap_socket_t *socket,
                               coap_id_source_t *id_source) {
    return _anjay_coap_block_transfer_new(max_block_size, in, out, socket,
                                          COAP_BLOCK2, id_source,
                                          block_recv_handler);
}

anjay_coap_msg_identity_t
_anjay_coap_block_response_last_request_id(coap_block_transfer_ctx_t *ctx) {
    return _anjay_coap_id_source_get(ctx->id_source);
}
