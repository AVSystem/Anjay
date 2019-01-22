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

#ifndef ANJAY_COAP_STREAM_SERVER_H
#define ANJAY_COAP_STREAM_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include "../block/response.h"
#include "../coap_stream.h"
#include "../id_source/id_source.h"
#include "common.h"
#include "in.h"
#include "out.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#    error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    uint32_t optnum;
    uint32_t length;
    uint8_t content[];
} coap_block_optbuf_t;

typedef enum coap_server_state {
    // waiting for incoming request
    COAP_SERVER_STATE_RESET,

    // received a basic (non-BLOCK) request
    COAP_SERVER_STATE_HAS_REQUEST,

    // got a BLOCK1 request
    COAP_SERVER_STATE_HAS_BLOCK1_REQUEST,

    // got a BLOCK2 request
    COAP_SERVER_STATE_HAS_BLOCK2_REQUEST,

    // last read() call finished reading the packet, another one needs to be
    // received on subsequent read() call
    COAP_SERVER_STATE_NEEDS_NEXT_BLOCK
} coap_server_state_t;

typedef struct coap_server {
    coap_stream_common_t common;

    coap_server_state_t state;

    // only valid if state != COAP_SERVER_STATE_RESET
    avs_coap_msg_identity_t request_identity;

#ifdef WITH_BLOCK_SEND
    coap_block_transfer_ctx_t *block_ctx;
    anjay_coap_block_request_validator_ctx_t block_relation_validator;
#endif
    coap_id_source_t *static_id_source;

    // only valid if state == COAP_SERVER_STATE_HAS_BLOCK1_REQUEST or
    // state == COAP_SERVER_STATE_HAS_BLOCK2_REQUEST
    avs_coap_block_info_t curr_block;

    uint32_t expected_block_offset;
    AVS_LIST(coap_block_optbuf_t) expected_block_opts;

    uint8_t last_error_code;
} coap_server_t;

void _anjay_coap_server_reset(coap_server_t *server);

#ifdef WITH_BLOCK_SEND
void _anjay_coap_server_set_block_request_relation_validator(
        coap_server_t *server,
        anjay_coap_block_request_validator_t *validator,
        void *validator_arg);
#else // WITH_BLOCK_SEND
#    define _anjay_coap_server_set_block_request_relation_validator( \
            Server, Validator, Arg)                                  \
        ((void) (Server), (void) (Validator), (void) (Arg))
#endif // WITH_BLOCK_SEND

/**
 * @returns identity of the current request or NULL if there is no request.
 */
const avs_coap_msg_identity_t *
_anjay_coap_server_get_request_identity(const coap_server_t *server);

int _anjay_coap_server_setup_response(coap_server_t *server,
                                      const anjay_msg_details_t *details);

/**
 * Sets the error code to be sent on the next call to finish_response instead
 * of the previously set up response.
 */
void _anjay_coap_server_set_error(coap_server_t *server, uint8_t code);

/**
 * Sends the response prepared in the <c>server->common.out</c> buffer, unless
 * the error code was set using the set_error call. In that case, the error
 * response is sent.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int _anjay_coap_server_finish_response(coap_server_t *server);

/**
 * Returns the currently handled request. If there is none, attempts to receive
 * one from the configured socket into the input buffer.
 *
 * NOTE: this function succeeds if a Reset message is received, allowing it to
 * be handled by the upper layer.
 *
 * @param      server  Server state object.
 * @param[out] out_msg Filled with the current request.
 *
 * @returns:
 * - 0 if @p out_msg was filled with a correct CoAP request,
 * - a negative value on error. In that case @p out_msg is set to NULL.
 */
int _anjay_coap_server_get_or_receive_msg(coap_server_t *server,
                                          const avs_coap_msg_t **out_msg);

/**
 * Reads the request payload, requesting and receiving additional blocks if
 * required. May wait for more packets if block-wise request is being handled.
 * In that case the call may send packets through the socket to acknowledge or
 * reject incoming packets.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int _anjay_coap_server_read(coap_server_t *server,
                            size_t *out_bytes_read,
                            char *out_message_finished,
                            void *buffer,
                            size_t buffer_length);

int _anjay_coap_server_write(coap_server_t *server,
                             const void *data,
                             size_t data_length);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_STREAM_SERVER_H
