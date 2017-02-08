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
#include "../block_builder.h"
#include "../stream/common.h"
#include "../stream/out.h"

#include "transfer.h"
#include "transfer_impl.h"

VISIBILITY_SOURCE_BEGIN

static size_t max_power_of_2_not_greater_than(size_t bound) {
    int exponent = -1;
    while (bound) {
        bound >>= 1;
        ++exponent;
    }
    return (exponent >= 0) ? ((size_t) 1 << exponent) : 0;
}

static size_t mtu_enforced_payload_capacity(const coap_output_buffer_t *out) {
    size_t headers_overhead =
           _anjay_coap_msg_info_get_max_mtu_overhead(&out->info)
           /* assume the header does not contain the BLOCK option */
           + ANJAY_COAP_OPT_BLOCK_MAX_SIZE
           + sizeof(ANJAY_COAP_PAYLOAD_MARKER);

    if (headers_overhead < out->dgram_layer_mtu) {
        return out->dgram_layer_mtu - headers_overhead;
    } else {
        return 0;
    }
}

static size_t
buffer_size_enforced_payload_capacity(const coap_output_buffer_t *out) {
   /* The flow assumes that the last block is only sent from finish_response.
    * Because of that, we MUST NOT flush the last block of a transfer even if
    * it's ready - we must wait for either finish_response call or another byte
    * (that would make current block not-the-last-one).
    * The max_block_size < payload_capacity condition causes that -1. */
    return out->buffer_capacity < 1 ? 0 : out->buffer_capacity - 1;
}

static uint16_t calculate_proposed_block_size(uint16_t original_block_size,
                                              const coap_output_buffer_t *out) {
    size_t payload_capacity_considering_mtu = ANJAY_MIN(
            mtu_enforced_payload_capacity(out),
            buffer_size_enforced_payload_capacity(out));

    size_t max_block_size = (payload_capacity_considering_mtu > 0)
            ? max_power_of_2_not_greater_than(payload_capacity_considering_mtu)
            : 0;

    if (max_block_size < ANJAY_COAP_MSG_BLOCK_MIN_SIZE) {
        coap_log(ERROR, "MTU is too low to send block response");
        return 0;
    }

    if (max_block_size < original_block_size) {
        assert(max_block_size <= ANJAY_COAP_MSG_BLOCK_MAX_SIZE);
        coap_log(INFO, "Lowering proposed block size to %u due to buffer size "
                       "or MTU constraints",
                 (unsigned) max_block_size);
        return (uint16_t) max_block_size;
    }

    return original_block_size;
}

coap_block_transfer_ctx_t *
_anjay_coap_block_transfer_new(uint16_t max_block_size,
                               coap_input_buffer_t *in,
                               coap_output_buffer_t *out,
                               anjay_coap_socket_t *socket,
                               coap_block_type_t block_type,
                               coap_id_source_t *id_source,
                               block_recv_handler_t *block_recv_handler) {
    assert(in);
    assert(out);
    assert(socket);
    assert(id_source);
    assert(block_recv_handler);

    uint16_t block_size_considering_mtu =
            calculate_proposed_block_size(max_block_size, out);
    if (block_size_considering_mtu == 0) {
        return NULL;
    }

    coap_block_transfer_ctx_t *ctx = (coap_block_transfer_ctx_t *)
            calloc(1, sizeof(coap_block_transfer_ctx_t));

    if (!ctx) {
        return NULL;
    }

    *ctx = (coap_block_transfer_ctx_t){
        .timed_out = false,
        .num_sent_blocks = 0,
        .socket = socket,
        .in = in,
        .block_builder = _anjay_coap_block_builder_init(&out->builder),
        .info = out->info,
        .block = {
            .type = block_type,
            .valid = true,
            .seq_num = 0,
            .size = block_size_considering_mtu
        },
        .id_source = id_source,
        .block_recv_handler = block_recv_handler
    };

    out->info = _anjay_coap_msg_info_init();
    return ctx;
}

void _anjay_coap_block_transfer_delete(coap_block_transfer_ctx_t **ctx) {
    if (ctx && *ctx) {
        _anjay_coap_msg_info_reset(&(*ctx)->info);
        free(*ctx);
        *ctx = NULL;
    }
}

typedef struct {
    coap_block_transfer_ctx_t *ctx;
    const anjay_coap_msg_t *sent_msg;
} block_recv_data_t;

static int block_recv(const anjay_coap_msg_t *msg,
                      void *data_,
                      bool *out_wait_for_next,
                      uint8_t *out_error_code) {
    block_recv_data_t *data = (block_recv_data_t *)data_;
    return data->ctx->block_recv_handler(msg, data->sent_msg, data->ctx,
                                         out_wait_for_next, out_error_code);
}


static bool should_wait_for_response(coap_block_transfer_ctx_t *ctx) {
    /* for intermediate blocks, transfer direction does not matter - we need
     * to wait until we receive a response*/
    return ctx->block.has_more
        /* For the last response block, we do not expect more requests.
         * In case of requests, we still need to receive an actual response. */
        || ctx->block.type == COAP_BLOCK1;
}

static int accept_response_with_timeout(coap_block_transfer_ctx_t *ctx,
                                        const anjay_coap_msg_t *sent_msg,
                                        int32_t recv_timeout_ms) {
    coap_log(TRACE, "waiting %d ms for response", recv_timeout_ms);

    block_recv_data_t block_recv_data = {
        .ctx = ctx,
        .sent_msg = sent_msg
    };

    _anjay_coap_in_reset(ctx->in);

    int handler_retval;
    int result = _anjay_coap_common_recv_msg_with_timeout(
            ctx->socket, ctx->in, &recv_timeout_ms,
            block_recv, &block_recv_data, &handler_retval);

    if (result == COAP_RECV_MSG_WITH_TIMEOUT_EXPIRED) {
        ctx->timed_out = true;
    }

    return result ? result : handler_retval;
}

static int send_block_msg(coap_block_transfer_ctx_t *ctx,
                          const anjay_coap_msg_t *msg) {
    coap_log(TRACE, "sending block %u (size %u, payload size %lu), has_more=%d\n",
             ctx->block.seq_num, ctx->block.size,
             (unsigned long)_anjay_coap_msg_payload_length(msg),
             ctx->block.has_more);

    coap_retry_state_t retry_state = { .retry_count = 0, .recv_timeout_ms = 0 };
    int result = 0;
    do {
        _anjay_coap_common_update_retry_state(&retry_state,
                                              &ctx->in->transmission_params,
                                              &ctx->in->rand_seed);

        if ((result = _anjay_coap_socket_send(ctx->socket, msg))) {
            coap_log(ERROR, "cannot send block message");
            break;
        }

        if (!should_wait_for_response(ctx)) {
            break;
        } else {
            result = accept_response_with_timeout(ctx, msg,
                                                  retry_state.recv_timeout_ms);
        }

        if (result != COAP_RECV_MSG_WITH_TIMEOUT_EXPIRED) {
            break;
        }

        coap_log(DEBUG, "timeout reached, next: %d ms",
                 retry_state.recv_timeout_ms);
    } while (retry_state.retry_count
                < ctx->in->transmission_params.max_retransmit);

    if (!result) {
        ctx->timed_out = false;
        ctx->num_sent_blocks++;
    }

    return result;
}

static int overwrite_block_option(anjay_coap_msg_info_t *info,
                                  const coap_block_info_t *block) {
    uint16_t opt_num = _anjay_coap_opt_num_from_block_type(block->type);

    _anjay_coap_msg_info_opt_remove_by_number(info, opt_num);
    return _anjay_coap_msg_info_opt_block(info, block);
}

static int prepare_block(coap_block_transfer_ctx_t *ctx,
                         anjay_coap_aligned_msg_buffer_t *buffer,
                         size_t buffer_size,
                         const anjay_coap_msg_t **out_msg) {
    ctx->info.identity = _anjay_coap_id_source_get(ctx->id_source);
    int result = overwrite_block_option(&ctx->info, &ctx->block);
    if (result) {
        return result;
    }

    *out_msg = _anjay_coap_block_builder_build(&ctx->block_builder, &ctx->info,
                                               ctx->block.size,
                                               buffer, buffer_size);
    return 0;
}

static bool has_full_intermediate_block(const coap_block_transfer_ctx_t *ctx) {
    /* strong inequality is deliberate - makes sure it is NOT the last block
     * of the whole transfer */
    return _anjay_coap_block_builder_payload_remaining(&ctx->block_builder)
           > ctx->block.size;
}

static int send_next_block(coap_block_transfer_ctx_t *ctx,
                           anjay_coap_aligned_msg_buffer_t *buffer,
                           size_t buffer_size) {
    ctx->info.identity = _anjay_coap_id_source_get(ctx->id_source);
    ctx->block.seq_num = ctx->block.seq_num;

    const anjay_coap_msg_t *msg = NULL;
    int result;

    do {
        result = prepare_block(ctx, buffer, buffer_size, &msg);
        if (result) {
            return result;
        }

        assert(msg);
        result = send_block_msg(ctx, msg);
    } while (result == BLOCK_TRANSFER_RESULT_RETRY);

    if (!result) {
        _anjay_coap_block_builder_next(&ctx->block_builder,
                                       _anjay_coap_msg_payload_length(msg));
    }

    return result;
}

typedef enum final_block_action {
    FINAL_BLOCK_DONT_SEND = 0,
    FINAL_BLOCK_SEND = 1
} final_block_action_t;

static int flush_blocks_with_buffer(coap_block_transfer_ctx_t *ctx,
                                    anjay_coap_aligned_msg_buffer_t *buffer,
                                    size_t buffer_size,
                                    final_block_action_t final_block_action) {
    int result = 0;

    while (!result && has_full_intermediate_block(ctx)) {
        ctx->block.has_more = true;
        result = send_next_block(ctx, buffer, buffer_size);
    }

    if (!result && final_block_action == FINAL_BLOCK_SEND) {
        ctx->block.has_more = false;
        result = send_next_block(ctx, buffer, buffer_size);
    }

    return result;
}

static int get_block_packet_total_size(anjay_coap_msg_info_t *info,
                                       const coap_block_info_t *block,
                                       size_t *out_storage_size) {
    coap_block_info_t temporary_block = *block;
    temporary_block.seq_num = ANJAY_COAP_BLOCK_MAX_SEQ_NUMBER;
    int result = overwrite_block_option(info, &temporary_block);
    if (result) {
        return result;
    }

    *out_storage_size =
            _anjay_coap_msg_info_get_packet_storage_size(info, block->size);

    return overwrite_block_option(info, block);
}

static int flush_blocks(coap_block_transfer_ctx_t *ctx,
                        final_block_action_t final_block_action) {
    size_t storage_size;
    int result = get_block_packet_total_size(&ctx->info, &ctx->block,
                                             &storage_size);
    if (result) {
        return result;
    }

    // TODO: consider failing if storage_size too big to limit stack usage
    AVS_ALIGNED_STACK_BUF(aligned_storage, storage_size);

    return flush_blocks_with_buffer(
            ctx, _anjay_coap_ensure_aligned_buffer(aligned_storage),
            storage_size, final_block_action);
}

int _anjay_coap_block_transfer_write(coap_block_transfer_ctx_t *ctx,
                                     const void *data,
                                     size_t data_length) {
    size_t bytes_written = 0;

    while (!ctx->timed_out) {
        bytes_written += _anjay_coap_block_builder_append_payload(
                &ctx->block_builder,
                (const uint8_t*)data + bytes_written,
                data_length - bytes_written);

        if (bytes_written >= data_length) {
            break;
        } else {
            coap_log(TRACE, "short write: flushing intermediate blocks");

            int result = flush_blocks(ctx, FINAL_BLOCK_DONT_SEND);
            if (result < 0) {
                return result;
            }
        }
    }

    assert(ctx->timed_out || bytes_written == data_length);
    if (ctx->timed_out) {
        return ANJAY_COAP_SOCKET_RECV_ERR_TIMEOUT;
    }
    return 0;
}

int _anjay_coap_block_transfer_finish(coap_block_transfer_ctx_t *ctx) {
    if (ctx->timed_out) {
        return 0;
    }

    return flush_blocks(ctx, FINAL_BLOCK_SEND);
}
