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

#ifndef AVSYSTEM_COAP_ASYNC_CLIENT_H
#define AVSYSTEM_COAP_ASYNC_CLIENT_H

#include <avsystem/coap/avs_coap_config.h>

#include <stdint.h>

#include <avsystem/coap/async_exchange.h>
#include <avsystem/coap/ctx.h>
#include <avsystem/coap/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Response to an asynchronous request. */
typedef struct {
    /** Object that contains response code and options. */
    avs_coap_response_header_t header;

    /** Offset of the payload within a full response payload. */
    size_t payload_offset;

    /** Pointer to the response payload. Never NULL. */
    const void *payload;

    /**
     * Number of bytes available to read from
     * @ref avs_coap_client_async_response_t#payload .
     */
    size_t payload_size;
} avs_coap_client_async_response_t;

typedef enum {
    /**
     * Reception of the async request was acknowledged by the remote host. Full
     * response payload was received.
     */
    AVS_COAP_CLIENT_REQUEST_OK,

    /**
     * A response was received, but the available payload is not complete yet.
     *
     * This may mean a BLOCK [UDP] or BERT [TCP] download is in progress, and
     * there is still more data to be requested. In such case, a sequence of
     * @ref AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT calls will yield sequential
     * chunks of data, and will be followed by @ref AVS_COAP_CLIENT_REQUEST_OK
     * call, which means "this is the last block of data being downloaded".
     */
    AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,

    /**
     * The library was unable to successfully deliver the request.
     *
     * - [UDP] All retransmissions were sent, but no response was received
     *   on time (either because of a timeout or a network layer error).
     *
     * - [UDP] A Reset response to a request was received.
     *
     * - [TCP] No response was received in time defined during creation of
     *   CoAP/TCP context.
     */
    AVS_COAP_CLIENT_REQUEST_FAIL,

    /**
     * The application requests cancellation of the exchange, either
     * explicitly (@ref avs_coap_async_cancel) or by deleting the CoAP
     * context.
     */
    AVS_COAP_CLIENT_REQUEST_CANCEL
} avs_coap_client_request_state_t;

/**
 * @param[in] exchange_id ID of the asynchronous request this function is
 *                        being called for.
 *
 * @param[in] result      The result of the asynchronous packet exchange, as
 *                        established by the library.
 *
 * @param[in] response    Asynchronous message response. Not NULL only when
 *                        @p result is AVS_COAP_CLIENT_REQUEST_OK or
 *                        AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT.
 *
 * @param[in] err         Specific error code for which delivering the request
 *                        failed. Valid only if @p result is
 *                        AVS_COAP_CLIENT_REQUEST_FAIL.
 *
 * @param[in] arg         Opaque user-defined data.
 */
typedef void avs_coap_client_async_response_handler_t(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t exchange_id,
        avs_coap_client_request_state_t result,
        const avs_coap_client_async_response_t *response,
        avs_error_t err,
        void *arg);

/**
 * Sends a request in an asynchronous way.
 *
 * @param ctx                  CoAP context to use for determining the request
 *                             recipient.
 *
 * @param[out] out_exchange_id On success, set to an ID that may be used to
 *                             identify a specific asynchronous request,
 *                             or to AVS_COAP_EXCHANGE_ID_INVALID if
 *                             no response is expected.
 *
 * @param req                  Request to send. If its options include a
 *                             BLOCK2 option (BLOCK [UDP] / BERT [TCP]),
 *                             it is assumed that the request is a continuation
 *                             of a partially complete download, and the
 *                             response will yield all payload chunks starting
 *                             from the one indicated by the BLOCK2 option.
 *
 *                             In case the intention was to download just a
 *                             single block of data, the @p response_handler
 *                             should cancel the exchange using
 *                             @ref avs_coap_async_cancel .
 *                             to avoid downloading following blocks.
 *
 *                             NOTE: A deep-copy of this parameter is made,
 *                             meaning that one may safely free any resources
 *                             associated with @p req when this function
 *                             returns.
 *
 * @param request_writer       Function to call when the library is ready to
 *                             send a chunk of payload data. See
 *                             @ref avs_coap_payload_writer_t for details.
 *
 * @param request_writer_arg   An opaque argument passed to @p request_writer
 *
 * @param response_handler     Function to call when the request is delivered
 *                             (and the remote host provides some kind of
 *                             response) or an error occurs. May be NULL (see
 *                             notes).
 *
 *                             IMPORTANT: The operation of receiving a response
 *                             is realized by @ref
 *                             avs_coap_async_handle_incoming_packet , refer to
 *                             its documentation for more details.
 *
 * @param response_handler_arg An opaque argument passed to
 *                             @p response_handler .
 *
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_EINVAL)</c> if an invalid header has been passed
 *  - <c>avs_errno(AVS_ENOMEM)</c> for an out-of-memory condition
 *  - error code cause by network communication error
 *
 * In case of an error, @p response_handler is NEVER called.
 *
 * Notes:
 *
 * - [UDP] Retransmissions for unconfirmed requests are sent by the scheduler
 *   associated with @p ctx .
 *
 * - If @p response_handler is NULL, the packet is considered nonessential. For
 *   unreliable transports, this means @p ctx will not attempt to retransmit
 *   such packets.
 *
 *   - [UDP] the packet will be sent as Non-Confirmable request. Any response
 *     to such packet will be ignored.
 *   - [UDP] in this case, Block-wise transfers are not supported. If request
 *     payload does not fit in a single datagram, this function fails.
 *   - [TCP] it's guaranteed by TCP stack that message will arrive, but any
 *     response to such packet will be ignored.
 *
 * - If @p response_handler is not NULL and @p payload needs to be split into
 *   multiple message exchanges, the handler is called whenever the server
 *   acknowledges the entire request, or when an error happens.
 *
 *   - [UDP] The library will attempt to send consecutive BLOCK packets
 *     sequentially, waiting for a confirmation after sending each one. The
 *     request is considered delivered when the server responds with code other
 *     than 2.31 Continue.
 */
avs_error_t avs_coap_client_send_async_request(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t *out_exchange_id,
        const avs_coap_request_header_t *req,
        avs_coap_payload_writer_t *request_writer,
        void *request_writer_arg,
        avs_coap_client_async_response_handler_t *response_handler,
        void *response_handler_arg);

/**
 * Changes the offset of the remote resource that the user wants to receive the
 * next response data chunk from.
 *
 * This function is only intended to be called from within an implementation
 * of @ref avs_coap_client_async_response_handler_t, or immediately after a
 * successful call to @ref avs_coap_client_send_async_request (before executing
 * any subsequent scheduler jobs).
 *
 * The offset can only be moved forward relative to the last known starting
 * offset. Attempting to set it to an offset of byte that was either already
 * received in a previously finished call to
 * @ref avs_coap_client_async_response_handler_t during this exchange, or is
 * smaller than an offset already passed to this function, will result in an
 * error.
 *
 * When called from within @ref avs_coap_client_async_response_handler_t, it is
 * permitted to set @p next_response_payload_offset to a position that lies
 * within the <c>response-&gt;payload</c> buffer passed to it (but further than
 * the current offset). If a position within the buffer is passed, the response
 * handler will be called again with a portion of the same buffer, starting at
 * the desired offset.
 *
 * If this function is never called during a call to
 * @ref avs_coap_client_async_response_handler_t, the pointer is implicitly
 * moved by the whole size of the buffer passed to it.
 *
 * As an additional exception, when called immediately after
 * @ref avs_coap_client_send_async_request, it is permitted to specify
 * @p next_response_payload_offset equal to zero. This is treated as a no-op.
 *
 * It is guaranteed that the next response chunk passed to the user code will
 * either start exactly on @p next_response_payload_offset, be empty (in case if
 * EOF is before the requested offset) or NULL (if no content is received from
 * the server).
 *
 * @param ctx                          CoAP context to operate on.
 *
 * @param exchange_id                  ID of the exchange to operate on. Note
 *                                     that passing an exchange other than the
 *                                     one currently handled may result in
 *                                     unexpected behavior.
 *
 * @param next_response_payload_offset Response payload offset to set.
 *
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_ENOENT)</c> if @p exchange_id is not an ID of existing
 *    client exchange
 *  - <c>avs_errno(AVS_EINVAL)</c> if @p next_response_payload_offset is smaller
 *    than the currently recognized value
 */
avs_error_t avs_coap_client_set_next_response_payload_offset(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t exchange_id,
        size_t next_response_payload_offset);

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_ASYNC_CLIENT_H
