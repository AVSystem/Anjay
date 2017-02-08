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
#include "../stream/common.h"
#include "../stream/out.h"
#include "../id_source/auto.h"

#include "transfer_impl.h"
#include "request.h"

VISIBILITY_SOURCE_BEGIN

static bool is_separate_ack(const anjay_coap_msg_t *msg,
                            const anjay_coap_msg_t *request) {
    anjay_coap_msg_type_t type = _anjay_coap_msg_header_get_type(&msg->header);

    return type == ANJAY_COAP_MSG_ACKNOWLEDGEMENT
            && msg->header.code == ANJAY_COAP_CODE_EMPTY
            && _anjay_coap_msg_get_id(request) == _anjay_coap_msg_get_id(msg);
}

static bool response_token_matches(const anjay_coap_msg_t *request,
                                   const anjay_coap_msg_t *response) {
    anjay_coap_token_t req_token;
    size_t req_token_size = _anjay_coap_msg_get_token(request, &req_token);

    anjay_coap_token_t res_token;
    size_t res_token_size = _anjay_coap_msg_get_token(response, &res_token);

    return _anjay_coap_common_tokens_equal(&req_token, req_token_size,
                                           &res_token, res_token_size);
}

static bool is_matching_response(const anjay_coap_msg_t *msg,
                                 const anjay_coap_msg_t *request) {
    anjay_coap_msg_type_t type = _anjay_coap_msg_header_get_type(&msg->header);

    if (type == ANJAY_COAP_MSG_RESET) {
        return _anjay_coap_msg_get_id(request) == _anjay_coap_msg_get_id(msg);
    }

    // Message ID must match only in case of Piggybacked Response
    if (type == ANJAY_COAP_MSG_ACKNOWLEDGEMENT) {
        if (_anjay_coap_msg_get_id(request) != _anjay_coap_msg_get_id(msg)) {
            coap_log(DEBUG, "unexpected msg id %u in ACK message",
                     _anjay_coap_msg_get_id(msg));
            return false;
        }
    }

    if (!response_token_matches(request, msg)) {
        coap_log(DEBUG, "token mismatch");
        return false;
    }

    return true;
}

static int block_request_update_block_option(coap_block_transfer_ctx_t *ctx,
                                             const coap_block_info_t *block) {
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
        coap_log(WARNING, "server requested block size bigger than original"
                 "(%u, was %u)", block->size, ctx->block.size);
        return -1;
    }

    uint32_t size_ratio = ctx->block.size / block->size;
    ctx->block.seq_num = (ctx->block.seq_num + 1) * size_ratio;
    ctx->block.size = block->size;
    return 0;
}

static int handle_block_options(const anjay_coap_msg_t *msg,
                                coap_block_transfer_ctx_t *ctx) {
    coap_block_info_t block1;
    if (_anjay_coap_common_get_block_info(msg, COAP_BLOCK1,
                                          &block1) || !block1.valid) {
        coap_log(DEBUG, "BLOCK1 missing or invalid in response to block-wise "
                 "request");
        return -1;
    }
    coap_block_info_t block2;
    if (_anjay_coap_common_get_block_info(msg, COAP_BLOCK2,
                                          &block2) || block2.valid) {
        coap_log(DEBUG, "block-wise responses to block-wise requests are not "
                 "supported");
        return -1;
    }

    if (block1.seq_num != ctx->block.seq_num) {
        coap_log(DEBUG, "mismatched block number: got %u, expected %u",
                 block1.seq_num, ctx->block.seq_num);
        return -1;
    }

    return block_request_update_block_option(ctx, &block1);
}

static int handle_matching_block_response(const anjay_coap_msg_t *msg,
                                          coap_block_transfer_ctx_t *ctx) {
    if (_anjay_coap_msg_code_is_client_error(msg->header.code)
            || _anjay_coap_msg_code_is_server_error(msg->header.code)) {
        coap_log(DEBUG, "block-wise transfer: error response");
        return -1;
    }

    return handle_block_options(msg, ctx);
}

static int handle_matching_response(const anjay_coap_msg_t *msg,
                                    coap_block_transfer_ctx_t *ctx) {
    if (_anjay_coap_msg_header_get_type(&msg->header) == ANJAY_COAP_MSG_RESET) {
        // Reset response to our request: abort the transfer
        coap_log(DEBUG, "block-wise transfer: Reset response");
        return -1;
    }

    int result = handle_matching_block_response(msg, ctx);

    if (_anjay_coap_msg_header_get_type(&msg->header)
            == ANJAY_COAP_MSG_CONFIRMABLE) {
        // Confirmable Separate Response: we need to send ACK
        _anjay_coap_common_send_empty(ctx->socket,
                                      ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                                      _anjay_coap_msg_get_id(msg));
    }

    return result;
}

static int continue_block_request(const anjay_coap_msg_t *msg,
                                  const anjay_coap_msg_t *request,
                                  coap_block_transfer_ctx_t *ctx,
                                  bool *out_wait_for_next,
                                  uint8_t *out_error_code) {
    (void) out_error_code;

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
    return -1;
}

coap_block_transfer_ctx_t *
_anjay_coap_block_request_new(uint16_t max_block_size,
                              coap_input_buffer_t *in,
                              coap_output_buffer_t *out,
                              anjay_coap_socket_t *socket,
                              coap_id_source_t *id_source) {
    return _anjay_coap_block_transfer_new(max_block_size, in, out, socket,
                                          COAP_BLOCK1, id_source,
                                          continue_block_request);
}
