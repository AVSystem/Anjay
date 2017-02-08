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

#ifndef SRC_COAP_STREAM_IN_H
#define SRC_COAP_STREAM_IN_H

#include <stdint.h>
#include <stddef.h>

#include "../../utils.h"
#include "../msg.h"
#include "../socket.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct coap_input_buffer {
    uint8_t *buffer;
    size_t buffer_size;
    const uint8_t *payload;
    size_t payload_off;
    size_t payload_size;

    coap_transmission_params_t transmission_params;
    anjay_rand_seed_t rand_seed;
} coap_input_buffer_t;

static inline void _anjay_coap_in_reset(coap_input_buffer_t *in) {
    in->payload = NULL;
}

static inline bool _anjay_coap_in_is_reset(const coap_input_buffer_t *in) {
    return in->payload == NULL;
}

static inline const anjay_coap_msg_t *
_anjay_coap_in_get_message(const coap_input_buffer_t *in) {
    return (const anjay_coap_msg_t *)in->buffer;
}

static inline size_t
_anjay_coap_in_get_bytes_available(const coap_input_buffer_t *in) {
    assert(in->payload_off <= in->payload_size);
    return in->payload_size - in->payload_off;
}

static inline size_t
_anjay_coap_in_get_payload_size(const coap_input_buffer_t *in) {
    return _anjay_coap_msg_payload_length(
            _anjay_coap_in_get_message(in));
}

/**
 * Attempts to receive next message.
 *
 * Note: If the message was truncated by the underlying networking API (i.e. due
 * to @p in buffer being too small), then it responds with 413 Request Entity
 * Too Large to the sender.
 *
 * @return 0 on success, one of ANJAY_COAP_SOCKET_RECV_ERR_* in case of failure
 */
int _anjay_coap_in_get_next_message(coap_input_buffer_t *in,
                                    anjay_coap_socket_t *socket);

void _anjay_coap_in_read(coap_input_buffer_t *in,
                         size_t *out_bytes_read,
                         char *out_message_finished,
                         void *buffer,
                         size_t buffer_length);

VISIBILITY_PRIVATE_HEADER_END

#endif // SRC_COAP_STREAM_IN_H
