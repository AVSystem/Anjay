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

#ifndef ANJAY_COAP_BLOCK_TRANSFERIMPL_H
#define ANJAY_COAP_BLOCK_TRANSFERIMPL_H

#include <stdbool.h>

#include "transfer.h"

#include "../socket.h"
#include "../stream/in.h"
#include "../stream/out.h"

#include "../msg_info.h"
#include "../msg_builder.h"
#include "../block_builder.h"

#include "../id_source/id_source.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#define BLOCK_TRANSFER_RESULT_OK    0
#define BLOCK_TRANSFER_RESULT_RETRY 1

/**
 * @param      msg               Received message. It is guaranteed to never
 *                               be NULL.
 * @param      sent_msg          Last message sent by the library that was a
 *                               part of the block-wise transfer.
 * @param      ctx               Block transfer context used.
 * @param[out] out_wait_for_next When set to true (default) when the handler
 *                               exits, @p msg is considered to not be relevant
 *                               to a block-wise transfer and will be rejected.
 *                               The library will then call this handler with
 *                               next received message.
 *                               If set to false: depending on the return value
 *                               the transfer will either continue (retval == 0)
 *                               or abort (retval != 0).
 * @param[out] out_error_code    Allows to specify custom error code that will
 *                               be send instead of RESET when rejecting the
 *                               message. Not setting it (or setting to 0) will
 *                               result in usual RESET message.
 *
 * @return @li BLOCK_TRANSFER_RESULT_OK when @p msg allows for block transfer
 *             continuation,
 *         @li BLOCK_TRANSFER_RESULT_RETRY after receiving retransmission of the
 *             last correct message,
*          @li a negative value in case of error.
 *         NOTE: returning a negative value from this handler is NOT equivalent
 *         to aborting the transfer. For that, use @p out_wait_for_next .
 */
typedef int block_recv_handler_t(const anjay_coap_msg_t *msg,
                                 const anjay_coap_msg_t *sent_msg,
                                 coap_block_transfer_ctx_t *ctx,
                                 bool *out_wait_for_next,
                                 uint8_t *out_error_code);

struct coap_block_transfer_ctx {
    bool timed_out;
    uint32_t num_sent_blocks;

    anjay_coap_socket_t *socket;
    coap_input_buffer_t *in;
    anjay_coap_msg_info_t info;
    anjay_coap_block_builder_t block_builder;
    coap_block_info_t block;

    coap_id_source_t *id_source;

    block_recv_handler_t *block_recv_handler;
};

coap_block_transfer_ctx_t *
_anjay_coap_block_transfer_new(uint16_t max_block_size,
                               coap_input_buffer_t *in,
                               coap_output_buffer_t *out,
                               anjay_coap_socket_t *socket,
                               coap_block_type_t block_type,
                               coap_id_source_t *id_source,
                               block_recv_handler_t *recv_msg_handler);

VISIBILITY_PRIVATE_HEADER_END

#endif
