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

#define ANJAY_COAP_STREAM_INTERNALS

#include "in.h"
#include "common.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

#include "../utils.h"
#include "../log.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_coap_in_get_next_message(coap_input_buffer_t *in,
                                    anjay_coap_socket_t *socket) {
    int result = _anjay_coap_socket_recv(socket,
                                         (anjay_coap_msg_t *)in->buffer,
                                         in->buffer_size);
    if (result) {
        int error =
                avs_net_socket_errno(_anjay_coap_socket_get_backend(socket));
        if (error) {
            coap_log(ERROR, "recv returned %d (%s)", result, strerror(error));
        } else {
            coap_log(TRACE, "recv returned %d", result);
        }
        return result;
    }

    const anjay_coap_msg_t *msg = _anjay_coap_in_get_message(in);

    in->payload_off = 0;
    in->payload = (const uint8_t *)_anjay_coap_msg_payload(msg);
    in->payload_size = _anjay_coap_msg_payload_length(msg);

    return 0;
}

void _anjay_coap_in_read(coap_input_buffer_t *in,
                         size_t *out_bytes_read,
                         char *out_message_finished,
                         void *buffer,
                         size_t buffer_length) {
    size_t bytes_available = _anjay_coap_in_get_bytes_available(in);
    size_t bytes_to_copy = ANJAY_MIN(buffer_length, bytes_available);
    memcpy(buffer, in->payload + in->payload_off, bytes_to_copy);
    in->payload_off += bytes_to_copy;

    *out_bytes_read = bytes_to_copy;
    *out_message_finished = (in->payload_off >= in->payload_size);
}

