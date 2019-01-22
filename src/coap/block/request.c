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

#include <inttypes.h>
#include <string.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include "../coap_log.h"
#include "../id_source/auto.h"
#include "../stream/common.h"
#include "../stream/out.h"

#include "request.h"
#include "transfer_impl.h"

VISIBILITY_SOURCE_BEGIN

static bool is_separate_ack(const avs_coap_msg_t *msg,
                            const avs_coap_msg_t *request) {
    avs_coap_msg_type_t type = avs_coap_msg_get_type(msg);

    return type == AVS_COAP_MSG_ACKNOWLEDGEMENT
           && avs_coap_msg_get_code(msg) == AVS_COAP_CODE_EMPTY
           && avs_coap_msg_get_id(request) == avs_coap_msg_get_id(msg);
}

static bool response_token_matches(const avs_coap_msg_t *request,
                                   const avs_coap_msg_t *response) {
    avs_coap_token_t req_token = avs_coap_msg_get_token(request);
    avs_coap_token_t res_token = avs_coap_msg_get_token(response);

    return avs_coap_token_equal(&req_token, &res_token);
}

static bool is_matching_response(const avs_coap_msg_t *msg,
                                 const avs_coap_msg_t *request) {
    avs_coap_msg_type_t type = avs_coap_msg_get_type(msg);

    if (type == AVS_COAP_MSG_RESET) {
        return avs_coap_msg_get_id(request) == avs_coap_msg_get_id(msg);
    }

    // Message ID must match only in case of Piggybacked Response
    if (type == AVS_COAP_MSG_ACKNOWLEDGEMENT) {
        if (avs_coap_msg_get_id(request) != avs_coap_msg_get_id(msg)) {
            coap_log(DEBUG, "unexpected msg id %u in ACK message",
                     avs_coap_msg_get_id(msg));
            return false;
        }
    }

    if (!response_token_matches(request, msg)) {
        coap_log(DEBUG, "token mismatch");
        return false;
    }

    return true;
}

static int
block_request_update_block_option(coap_block_transfer_ctx_t *ctx,
                                  const avs_coap_block_info_t *block) {
    if (block->size == ctx->block.size) {
        ++ctx->block.seq_num;
        return 0;
    }

    coap_log(DEBUG, "server requested block size change: %u", block->size);

    if (block->seq_num != 0) {
        coap_log(WARNING, "server requested block size change in the middle of "
                          "a transfer");
        return -1;
    }

    if (block->size > ctx->block.size) {
        coap_log(WARNING,
                 "server requested block size bigger than original"
                 "(%u, was %u)",
                 block->size, ctx->block.size);
        return -1;
    }

    uint32_t size_ratio = ctx->block.size / block->size;
    ctx->block.seq_num = (ctx->block.seq_num + 1) * size_ratio;
    ctx->block.size = block->size;
    return 0;
}

static int handle_block_options(const avs_coap_msg_t *msg,
                                coap_block_transfer_ctx_t *ctx) {
    avs_coap_block_info_t block1;
    if (avs_coap_get_block_info(msg, AVS_COAP_BLOCK1, &block1)
            || !block1.valid) {
        coap_log(DEBUG, "BLOCK1 missing or invalid in response to block-wise "
                        "request");
        return -1;
    }
    avs_coap_block_info_t block2;
    if (avs_coap_get_block_info(msg, AVS_COAP_BLOCK2, &block2)
            || block2.valid) {
        coap_log(DEBUG, "block-wise responses to block-wise requests are not "
                        "supported");
        return -1;
    }

    if (block1.seq_num != ctx->block.seq_num) {
        coap_log(DEBUG,
                 "mismatched block number: got %" PRIu32 ", expected %" PRIu32,
                 block1.seq_num, ctx->block.seq_num);
        return -1;
    }

    return block_request_update_block_option(ctx, &block1);
}

static int handle_matching_block_response(const avs_coap_msg_t *msg,
                                          coap_block_transfer_ctx_t *ctx) {
    if (avs_coap_msg_code_is_client_error(avs_coap_msg_get_code(msg))
            || avs_coap_msg_code_is_server_error(avs_coap_msg_get_code(msg))) {
        coap_log(DEBUG, "block-wise transfer: error response");
        return -1;
    }

    return handle_block_options(msg, ctx);
}

static int handle_matching_response(const avs_coap_msg_t *msg,
                                    coap_block_transfer_ctx_t *ctx) {
    if (avs_coap_msg_get_type(msg) == AVS_COAP_MSG_RESET) {
        // Reset response to our request: abort the transfer
        coap_log(DEBUG, "block-wise transfer: Reset response");
        return -1;
    }

    int result = handle_matching_block_response(msg, ctx);

    if (avs_coap_msg_get_type(msg) == AVS_COAP_MSG_CONFIRMABLE) {
        // Confirmable Separate Response: we need to send ACK
        avs_coap_ctx_send_empty(ctx->coap_ctx, ctx->socket,
                                AVS_COAP_MSG_ACKNOWLEDGEMENT,
                                avs_coap_msg_get_id(msg));
    }

    return result;
}

static int continue_block_request(void *ignored,
                                  const avs_coap_msg_t *msg,
                                  const avs_coap_msg_t *request,
                                  coap_block_transfer_ctx_t *ctx,
                                  bool *out_wait_for_next,
                                  uint8_t *out_error_code) {
    (void) ignored;

    if (is_separate_ack(msg, request)) {
        // Empty ACK to a request: wait for Separate Response
        *out_wait_for_next = true;
        return 0;
    } else if (is_matching_response(msg, request)) {
        *out_wait_for_next = false;
        // matching response (Piggybacked, Separate or Reset)
        // handle or abort on error
        return handle_matching_response(msg, ctx);
    }

    // message unrelated to the block-wise transfer; reject and wait for next
    *out_wait_for_next = true;
    if (avs_coap_msg_get_type(msg) == AVS_COAP_MSG_CONFIRMABLE) {
        if (avs_coap_msg_is_request(msg)) {
            *out_error_code = AVS_COAP_CODE_SERVICE_UNAVAILABLE;
        }
    }

    return -1;
}

coap_block_transfer_ctx_t *
_anjay_coap_block_request_new(uint16_t max_block_size,
                              coap_stream_common_t *stream_data,
                              coap_id_source_t *id_source) {
    return _anjay_coap_block_transfer_new(max_block_size, stream_data,
                                          AVS_COAP_BLOCK1, id_source,
                                          continue_block_request, NULL);
}
