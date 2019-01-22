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

#ifndef ANJAY_COAP_STREAM_CLIENT_H
#define ANJAY_COAP_STREAM_CLIENT_H

#include <stddef.h>

#include <avsystem/commons/coap/ctx.h>

#include "../block/transfer.h"
#include "../coap_stream.h"
#include "../id_source/id_source.h"

#include "common.h"
#include "in.h"
#include "out.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#    error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum coap_client_state {
    // request not yet set up
    COAP_CLIENT_STATE_RESET,

    // setup_request was called and message is ready to be sent
    // it's still possible to write payload data
    COAP_CLIENT_STATE_HAS_REQUEST_HEADER,

    // the request was sent, but response is not yet received
    COAP_CLIENT_STATE_REQUEST_SENT,

    // server responded with an empty ACK, actual response is yet to be received
    // read() call may block until the response is received
    COAP_CLIENT_STATE_HAS_SEPARATE_ACK,

    // the response is ready to read
    COAP_CLIENT_STATE_HAS_RESPONSE_CONTENT
} coap_client_state_t;

typedef struct coap_client {
    coap_stream_common_t common;

    coap_client_state_t state;

#ifdef WITH_BLOCK_SEND
    coap_block_transfer_ctx_t *block_ctx;
#endif

    // following are only valid if state != COAP_CLIENT_STATE_RESET
    avs_coap_msg_identity_t last_request_identity;
} coap_client_t;

void _anjay_coap_client_reset(coap_client_t *client);

/**
 * @returns identity of the prepared request or NULL if there is none.
 */
const avs_coap_msg_identity_t *
_anjay_coap_client_get_request_identity(const coap_client_t *client);

/**
 * @param client     CoAP client state.
 * @param details    Details of the message to be sent.
 * @param identity   Identity of the message to be sent.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error (e.g. if the @p client is not reset or
 *   the token is invalid).
 */
int _anjay_coap_client_setup_request(coap_client_t *client,
                                     const anjay_msg_details_t *details,
                                     const avs_coap_msg_identity_t *identity);

#define COAP_CLIENT_RECEIVE_RESET 1

/**
 * Loops until an actual response is received or timeout expires.
 *
 * @returns
 * - 0 on success,
 * - COAP_CLIENT_RECEIVE_RESET if the server responds with Reset,
 * - a negative value on error.
 */
int _anjay_coap_client_get_or_receive_msg(coap_client_t *client,
                                          const avs_coap_msg_t **out_msg);

/**
 * Sends the prepared request. If it's a Confirmable message, waits until
 * the server acknowledges it or retransmission limit is reached.
 *
 * @returns:
 * - 0 on success,
 * - COAP_CLIENT_RECEIVE_RESET if the server responds with Reset message,
 * - a negative value in case of other error.
 *
 */
int _anjay_coap_client_finish_request(coap_client_t *client);

int _anjay_coap_client_read(coap_client_t *client,
                            size_t *out_bytes_read,
                            char *out_message_finished,
                            void *buffer,
                            size_t buffer_length);

int _anjay_coap_client_write(coap_client_t *client,
                             coap_id_source_t *id_source,
                             const void *data,
                             size_t data_length);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_STREAM_CLIENT_H
