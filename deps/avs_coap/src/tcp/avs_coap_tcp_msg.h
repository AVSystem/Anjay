/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_TCP_MSG_H
#define AVS_COAP_SRC_TCP_MSG_H

#include <avsystem/commons/avs_buffer.h>

#include "avs_coap_common_utils.h"
#include "avs_coap_ctx_vtable.h"
#include "options/avs_coap_option.h"
#include "tcp/avs_coap_tcp_header.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    // Message which will be passed to user's handlers. It may contain an
    // entire payload or just a part of it (consecutive chunks).
    avs_coap_borrowed_msg_t content;

    // Remaining bytes to receive entire message. Includes options, payload
    // marker and payload.
    size_t remaining_bytes;

    // Indicates how many bytes should be received in receive_header() in
    // current call.
    size_t remaining_header_bytes;

    // True if options were parsed and are available in the content field.
    // Indicates that message is ready to be passed to user's handler.
    bool options_cached;

    // Indicating that message should be ignored if it's a request.
    bool ignore_request;
} avs_coap_tcp_cached_msg_t;

/**
 * Serializes given CoAP message to @p buf of size @p buf_size .
 *
 * @p msg          Pointer to message to serialize.
 * @p buf          Buffer to write data to.
 * @p buf_size     Size of the buffer.
 * @p out_msg_size Size of serialized message.
 */
avs_error_t _avs_coap_tcp_serialize_msg(const avs_coap_borrowed_msg_t *msg,
                                        void *buf,
                                        size_t buf_size,
                                        size_t *out_msg_size);

/**
 * Packs options from buffer to @p inout_msg .
 *
 * @p inout_msg        Pointer to message to pack options to.
 * @p data             Buffer to parse data from.
 *
 * Imporant note:
 * Bytes from @p data buffer are not consumed. @p data buffer MUST NOT be used
 * between call to this function and passing @p inout_msg to user, because
 * options are not copied.
 */
avs_error_t _avs_coap_tcp_pack_options(avs_coap_tcp_cached_msg_t *inout_msg,
                                       const avs_buffer_t *data);

/**
 * Packs payload to @p inout_msg.
 *
 * @p inout_msg Pointer to message which will contain pointer to payload after
 *              successful call.
 * @p data      Pointer to payload.
 * @p size      Size of the data in buffer.
 *
 * Note: passed data shouldn't contain anything else than payload for current
 * message.
 */
void _avs_coap_tcp_pack_payload(avs_coap_tcp_cached_msg_t *inout_msg,
                                const uint8_t *data,
                                size_t size);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_TCP_MSG_H
