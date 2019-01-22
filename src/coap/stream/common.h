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

#ifndef SRC_COAP_STREAM_COMMON_H
#define SRC_COAP_STREAM_COMMON_H

#include <avsystem/commons/coap/msg_builder.h>

#include "../coap_stream.h"
#include "in.h"
#include "out.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#    error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct coap_stream_common {
    avs_coap_ctx_t *coap_ctx;
    avs_net_abstract_socket_t *socket;

    coap_input_buffer_t in;
    coap_output_buffer_t out;
} coap_stream_common_t;

int _anjay_coap_common_fill_msg_info(avs_coap_msg_info_t *info,
                                     const anjay_msg_details_t *details,
                                     const avs_coap_msg_identity_t *identity,
                                     const avs_coap_block_info_t *block_info);

/**
 * @param      msg               Received message. It is guaranteed to never
 *                               be NULL.
 * @param      data              Opaque data as passwd to
 *                               @ref _coap_common_with_recv_timeout .
 * @param[out] out_wait_for_next When set to false, the
 *                               @ref _coap_common_recv_msg_with_timeout will
 *                               stop the message receive loop and return
 *                               without waiting for the timeout. If set to
 *                               true (default), the handler will be called
 *                               again with next message
 * @param[out] out_error_code    Allows to specify custom error code that will
 *                               be send instead of RESET. Not setting it (or
 *                               setting to 0) will result in usual RESET
 *                               message.
 *
 * @return If @p out_wait_for_next is not set to true, the return value of this
 *         function will be propagated up by the
 *         @ref _coap_common_with_recv_timeout .
 */
typedef int recv_msg_handler_t(const avs_coap_msg_t *msg,
                               void *data,
                               bool *out_wait_for_next,
                               uint8_t *out_error_code);

/**
 * @param        coap_ctx           Context to use for CoAP message handling.
 * @param        socket             Socket to wait on.
 * @param        in                 Input buffer for the incoming message.
 * @param[inout] inout_timeout      Maximum time to wait for a message. Will be
 *                                  decremented by the time spent waiting on the
 *                                  message.
 * @param        handle_msg         Function to call after successfully
 *                                  receiving the message.
 * @param        handle_msg_data    Opaque pointer passed to @p handle_msg.
 * @param[out]   out_handler_result Set to the return value of @p handle_msg .
 *                                  Only valid if the function succeeds.
 *
 * @returns:
 * - 0 if the @p handle_msg handles the incoming message without setting the
 *   out_continue flag,
 * - COAP_RECV_MSG_WITH_TIMEOUT_EXPIRED if the timeout expires,
 * - a negative value in case of error.
 */
int _anjay_coap_common_recv_msg_with_timeout(avs_coap_ctx_t *ctx,
                                             avs_net_abstract_socket_t *socket,
                                             coap_input_buffer_t *in,
                                             avs_time_duration_t *inout_timeout,
                                             recv_msg_handler_t *handle_msg,
                                             void *handle_msg_data,
                                             int *out_handler_result);

uint32_t _anjay_coap_common_timestamp(void);

VISIBILITY_PRIVATE_HEADER_END

#endif // SRC_COAP_STREAM_COMMON_H
