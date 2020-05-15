/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#ifndef COAP_SRC_STREAMING_SERVER_H
#define COAP_SRC_STREAMING_SERVER_H

#include <avsystem/commons/avs_buffer.h>
#include <avsystem/commons/avs_stream_v_table.h>

#include <avsystem/coap/async_server.h>
#include <avsystem/coap/observe.h>
#include <avsystem/coap/streaming.h>

#include "async/avs_coap_async_server.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST,
    AVS_COAP_STREAMING_SERVER_RECEIVED_REQUEST_CHUNK,
    AVS_COAP_STREAMING_SERVER_RECEIVED_LAST_REQUEST_CHUNK,
    AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK,
    AVS_COAP_STREAMING_SERVER_SENDING_RESPONSE_CHUNK,
    AVS_COAP_STREAMING_SERVER_SENT_LAST_RESPONSE_CHUNK,
    AVS_COAP_STREAMING_SERVER_FINISHED
} avs_coap_streaming_server_state_t;

typedef struct {
    avs_coap_ctx_t *coap_ctx;
    uint8_t *acquired_in_buffer;
    size_t acquired_in_buffer_size;

    avs_coap_exchange_id_t exchange_id;
    avs_coap_streaming_server_state_t state;
    size_t expected_next_outgoing_chunk_offset;

    /**
     * Depending on stream state, this buffer may be used either for *request*
     * payload (RECEIVING_REQUEST, RECEIVING_REQUEST_CHUNK,
     * RECEIVED_LAST_REQUEST_CHUNK) or *response* payload
     * (SENDING_FIRST_RESPONSE_CHUNK, SENDING_RESPONSE_CHUNK,
     * SENT_LAST_RESPONSE_CHUNK)
     */
    avs_buffer_t *chunk_buffer;
} avs_coap_streaming_server_ctx_t;

struct avs_coap_streaming_request_ctx {
    const avs_stream_v_table_t *vtable;

    avs_coap_streaming_server_ctx_t server_ctx;

    /**
     * CoAP code directly returned from the user handler, to be used in an
     * empty response.
     */
    int error_response_code;
    avs_error_t err;

    bool request_has_observe_id;
    avs_coap_observe_id_t request_observe_id;

    avs_coap_request_header_t request_header;
    avs_coap_response_header_t response_header;
};

VISIBILITY_PRIVATE_HEADER_END

#endif // COAP_SRC_STREAMING_SERVER_H
