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

#ifndef SRC_COAP_STREAM_COMMON_H
#define SRC_COAP_STREAM_COMMON_H

#include "../stream.h"
#include "../msg_builder.h"

#include "in.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Attempts to obtain block info of given block @p type. Possible return values
 * along with @p out_info->valid values are shown in the table below.
 *
 * +-----------------------+----------------+-----------------+
 * |        Option         |  Return value  | out_info->valid |
 * +-----------------------+----------------+-----------------+
 * |   Present and valid   |       0        |      true       |
 * +-----------------------+----------------+-----------------+
 * | Present and malformed |      -1        |      false      |
 * +-----------------------+----------------+-----------------+
 * |        Doubled        |      -1        |      false      |
 * +-----------------------+----------------+-----------------+
 * |      Not present      |       0        |      false      |
 * +-----------------------+----------------+-----------------+
 */
int _anjay_coap_common_get_block_info(const anjay_coap_msg_t *msg,
                                      coap_block_type_t type,
                                      coap_block_info_t *out_info);

int _anjay_coap_common_fill_msg_info(anjay_coap_msg_info_t *info,
                                     const anjay_msg_details_t *details,
                                     const anjay_coap_msg_identity_t *identity,
                                     const coap_block_info_t *block_info);

static inline bool
_anjay_coap_common_tokens_equal(const anjay_coap_token_t *first,
                                size_t first_size,
                                const anjay_coap_token_t *second,
                                size_t second_size) {
    return first_size == second_size
        && !memcmp(first->bytes, second->bytes, first_size);
}

bool _anjay_coap_common_token_matches(const anjay_coap_msg_t *msg,
                                      const anjay_coap_msg_identity_t *id);

static inline anjay_coap_msg_identity_t
_anjay_coap_common_identity_from_msg(const anjay_coap_msg_t *msg) {
    anjay_coap_msg_identity_t id;
    memset(&id, 0, sizeof(id));
    id.msg_id = _anjay_coap_msg_get_id(msg);
    id.token_size = _anjay_coap_msg_get_token(msg, &id.token);
    return id;
}

static inline
bool _anjay_coap_common_identity_equal(const anjay_coap_msg_identity_t *a,
                                       const anjay_coap_msg_identity_t *b) {
    return a->msg_id == b->msg_id
        && a->token_size == b->token_size
        && !memcmp(&a->token, &b->token, a->token_size);
}

/**
 * Sends an Empty message with given values of @p msg_type and @p msg_id.
 */
int _anjay_coap_common_send_empty(anjay_coap_socket_t *socket,
                                  anjay_coap_msg_type_t msg_type,
                                  uint16_t msg_id);

/**
 * Responds with error specified as @p error_code to the message @p msg.
 */
void _anjay_coap_common_send_error(anjay_coap_socket_t *socket,
                                   const anjay_coap_msg_t *msg,
                                   uint8_t error_code);

/**
 * Rejects a message by either ignoring it (if it's not a Confirmable one) or
 * by sending a Reset response.
 */
void _anjay_coap_common_reject_message(anjay_coap_socket_t *socket,
                                       const anjay_coap_msg_t *msg);

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
 *                               be send instead of RESET. Not setting it (or setting
 *                               to 0) will result in usual RESET message.
 *
 * @return If @p out_wait_for_next is not set to true, the return value of this
 *         function will be propagated up by the
 *         @ref _coap_common_with_recv_timeout .
 */
typedef int recv_msg_handler_t(const anjay_coap_msg_t *msg,
                               void *data,
                               bool *out_wait_for_next,
                               uint8_t *out_error_code);

#define COAP_RECV_MSG_WITH_TIMEOUT_EXPIRED (-0xE0)

/**
 * @param        socket             Socket to wait on.
 * @param        in                 Input buffer for the incoming message.
 * @param[inout] timeout_ms         Maximum time to wait for a message. Will be
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
int _anjay_coap_common_recv_msg_with_timeout(anjay_coap_socket_t *socket,
                                             coap_input_buffer_t *in,
                                             int32_t *inout_timeout_ms,
                                             recv_msg_handler_t *handle_msg,
                                             void *handle_msg_data,
                                             int *out_handler_result);

typedef struct {
    unsigned retry_count;
    int32_t recv_timeout_ms;
} coap_retry_state_t;

void _anjay_coap_common_update_retry_state(
        coap_retry_state_t *retry_state,
        const coap_transmission_params_t *tx_params,
        anjay_rand_seed_t *rand_seed);

VISIBILITY_PRIVATE_HEADER_END

#endif // SRC_COAP_STREAM_COMMON_H
