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

#ifndef SRC_COAP_STREAM_OUT_H
#define SRC_COAP_STREAM_OUT_H

#include <stdint.h>
#include <stddef.h>

#include "../utils.h"
#include "../msg.h"
#include "../msg_builder.h"
#include "../socket.h"
#include "../stream.h"

#include "common.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum msg_state {
    MSG_STATE_RESET        = 0,
    MSG_STATE_HAS_DETAILS  = (1 << 0),
    MSG_STATE_HAS_ID       = (1 << 1),
    MSG_STATE_HAS_TOKEN    = (1 << 2),
    MSG_STATE_HAS_IDENTITY = MSG_STATE_HAS_ID | MSG_STATE_HAS_TOKEN,
    MSG_STATE_FINISHED     = (1 << 3),

    _MSG_STATE_MASK = (1 << 4) - 1
} msg_state_t;

typedef struct coap_output_buffer {
    uint8_t *buffer;
    size_t buffer_capacity;
    size_t dgram_layer_mtu;

    anjay_coap_msg_info_t info;
    anjay_coap_msg_builder_t builder;
} coap_output_buffer_t;

coap_output_buffer_t _anjay_coap_out_init(uint8_t *payload_buffer,
                                          size_t payload_capacity);

/**
 * Does the same as @ref _anjay_coap_out_reset_payload
 * plus it resets the details
 */
void _anjay_coap_out_reset(coap_output_buffer_t *out);

/**
 * Sets the limit of buffer size, adequate to the MTU of a specified socket.
 *
 * @param out    Buffer to operate on.
 * @param socket CoAP socket to query MTU on.
 */
void _anjay_coap_out_setup_mtu(coap_output_buffer_t *out,
                               anjay_coap_socket_t *socket);

/**
 * @param out Buffer to check.
 *
 * @returns true if the buffer does not contain any data yet, false otherwise.
 */
static inline bool _anjay_coap_out_is_reset(coap_output_buffer_t *out) {
    return !_anjay_coap_msg_builder_is_initialized(&out->builder);
}

/**
 * @param out     Buffer to operate on.
 * @param id      Message identity to set.
 * @param details Details of the message being constructed.
 * @param block   BLOCK option to include in the message.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int _anjay_coap_out_setup_msg(coap_output_buffer_t *out,
                              const anjay_coap_msg_identity_t *id,
                              const anjay_msg_details_t *details,
                              const coap_block_info_t *block);

/**
 * Resets message ID, token and acknowledged BLOCK option for the message being
 * constructed.
 *
 * @param out   Buffer to operate on.
 * @param id    Message identity.
 * @param block Block to acknowledge.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int _anjay_coap_out_update_msg_header(coap_output_buffer_t *out,
                                      const anjay_coap_msg_identity_t *id,
                                      const coap_block_info_t *block);

/**
 * Writes a message payload.
 *
 * NOTE: calling this one makes the buffer unable to set any header data.
 * In order to do that, the buffer must be reset.
 *
 * @param out         Buffer to operate on.
 * @param data        Payload data to write.
 * @param data_length Number of bytes to write.
 *
 * @returns Number of bytes successfully written.
 */
size_t _anjay_coap_out_write(coap_output_buffer_t *out,
                             const void *data,
                             size_t data_length);

static inline const anjay_coap_msg_t *
_anjay_coap_out_build_msg(coap_output_buffer_t *out) {
    return _anjay_coap_msg_builder_get_msg(&out->builder);
}

VISIBILITY_PRIVATE_HEADER_END

#endif /* SRC_COAP_STREAM_OUT_H */
