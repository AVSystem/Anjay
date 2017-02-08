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

#ifndef ANJAY_COAP_BLOCK_REQUEST_H
#define ANJAY_COAP_BLOCK_REQUEST_H

#include "transfer.h"

#include "../stream/in.h"
#include "../stream/out.h"
#include "../socket.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_BLOCK_SEND

/**
 * Creates a block request object.
 *
 * @param max_block_size           Maximum block size the client is willing to
 *                                 handle.
 * @param in                       Input buffer to store incoming response.
 * @param out                      Output buffer containing the part of a
 *                                 request created so far. It is consumed in
 *                                 the process of creating the block_request
 *                                 object and MUST NOT be used without
 *                                 reintializing it after a successful call to
 *                                 this function.
 * @param socket                   The CoAP socket used to send/receive blocks.
 * @param id_source                CoAP message identity generator.
 *
 * @returns Created block_request object on success, NULL on failure.
 */
coap_block_transfer_ctx_t *
_anjay_coap_block_request_new(uint16_t max_block_size,
                              coap_input_buffer_t *in,
                              coap_output_buffer_t *out,
                              anjay_coap_socket_t *socket,
                              coap_id_source_t *id_source);

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif
