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

#ifndef AVSYSTEM_COAP_STREAMING_H
#define AVSYSTEM_COAP_STREAMING_H

#include <avsystem/coap/avs_coap_config.h>

#include <avsystem/commons/avs_stream.h>

#include <avsystem/coap/ctx.h>
#include <avsystem/coap/observe.h>
#include <avsystem/coap/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal CoAP context type.
 *
 * Note: this type exists only to ensure correct function call flow, i.e.:
 *
 * avs_coap_streaming_handle_incoming_packet
 * '-> handle_request (avs_coap_streaming_request_handler_t)
 *     '-> avs_coap_streaming_setup_response
 */
typedef struct avs_coap_streaming_request_ctx avs_coap_streaming_request_ctx_t;

/**
 * @param[in]  request        Incoming request to handle.
 *
 * @param[in]  payload_stream Stream that may be used to retrieve request
 *                            payload.
 *
 * @param[out] observe_id     If not NULL, indicates that the incoming request
 *                            establishes a CoAP Observe. In such case, it
 *                            should be passed to the
 *                            @ref avs_coap_observe_start function *before
 *                            starting to generate response payload*.
 *                            Not calling @ref avs_coap_observe_start will
 *                            cause the request to be interpreted as plain GET.
 * @param[in]  arg            Opaque user-defined data.
 *
 * @returns @li 0 on success. If @ref avs_coap_streaming_setup_response is
 *              called within the handler, such response is sent to the server.
 *              If it was NOT called by the handler, Internal Server Error is
 *              sent instead.
 *          @li a non-zero value in case of error. In case the returned value
 *              is one of AVS_COAP_CODE_* constants, an appropriate response
 *              will be sent. Otherwise, Internal Server Error response will
 *              be sent.
 */
typedef int
avs_coap_streaming_request_handler_t(avs_coap_streaming_request_ctx_t *ctx,
                                     const avs_coap_request_header_t *request,
                                     avs_stream_t *payload_stream,
                                     const avs_coap_observe_id_t *observe_id,
                                     void *arg);

#ifdef WITH_AVS_COAP_STREAMING_API

/**
 * Sends a CoAP request in a blocking way, returning when a response is
 * received or a network-layer error occurs.
 *
 * @param[in]  ctx                 CoAP context object to operate on. Indicates
 *                                 where should the request be sent to.
 *
 * @param[in]  request             CoAP request to send.
 *
 * @param[in]  write_payload       A callback that may be used to pass request
 *                                 payload. If NULL, a message with empty
 *                                 payload is sent.
 *
 * @param[in]  write_payload_arg   An opaque argument passed to
 *                                 @p write_payload .
 *
 * @param[out] out_response        On success, it is filled with details of
 *                                 received response.
 *
 *                                 MUST NOT be NULL.
 *
 *                                 On success, the caller MUST cleanup the
 *                                 options associated with response header (@p
 *                                 avs_coap_options_cleanup()). On error, it is
 *                                 valid to do so, but not required.
 *
 * @param[out] out_response_stream If not NULL, after a successful execution
 *                                 <c>*out_response_stream</c> is set to a
 *                                 non-NULL stream object that may be used to
 *                                 retrieve the response payload.
 *
 *                                 The stream object is owned by the @p ctx
 *                                 object and MUST NOT be deleted.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 *
 * Notes:
 *
 * - Using output methods (e.g. @ref avs_stream_write) on the payload stream
 *   associated with the returned response object is undefined.
 * - The function may return success even if @p write_payload failed, but some
 *   kind of valid response (e.g. to a partially sent payload) has been
 *   received.
 * - [UDP] Requests are always sent as Confirmable messages.
 * - [UDP] Transparently handles Separate Responses and BLOCK-wise requests
 *   as required. This means the call may block for extended periods of time
 *   in case of severe packet loss or a malicious server.
 */
avs_error_t
avs_coap_streaming_send_request(avs_coap_ctx_t *ctx,
                                const avs_coap_request_header_t *request,
                                avs_coap_streaming_writer_t *write_payload,
                                void *write_payload_arg,
                                avs_coap_response_header_t *out_response,
                                avs_stream_t **out_response_stream);

/**
 * Sets up a response that should be sent in response to a previously received
 * request.
 *
 * @param request  Request to respond to.
 *
 * @param response Response object to set up.
 *
 * @returns @li A non-NULL stream object that may be used to attach payload to
 *              sent response on success,
 *          @li NULL in case of error.
 */
avs_stream_t *
avs_coap_streaming_setup_response(avs_coap_streaming_request_ctx_t *ctx,
                                  const avs_coap_response_header_t *response);

/**
 * Receives a CoAP messages from the socket associated with @p ctx and handles
 * them as appropriate.
 *
 * Initially, the receive method on the underlying socket is called with receive
 * timeout set to zero. Subsequent receive requests may block with non-zero
 * timeout values when e.g. waiting for retransmissions or subsequent BLOCK
 * chunks - this is necessary to hide this complexity from the user callbacks in
 * streaming mode.
 *
 * This function may handle more than one request at once, possibly calling
 * @p handle_request multiple times. Upon successful return, it is guaranteed
 * that there is no more data to be received on the socket at the moment.
 *
 * If the packet is recognized as a response to an asynchronous request, such
 * message is handled internally without calling @p handle_request . Otherwise,
 * incoming message is passed to @p handle_request .
 *
 * @param ctx            CoAP context associated with the socket to receive
 *                       the message from.
 *
 * @param start_observe  If not NULL, the caller declares
 *
 * @param handle_request Callback used to handle incoming requests. May be
 *                       NULL, in which case it will only handle responses
 *                       to asynchronous requests and ignore incoming requests.
 *
 * @param handler_arg    An opaque argument passed to @p handle_request .
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t avs_coap_streaming_handle_incoming_packet(
        avs_coap_ctx_t *ctx,
        avs_coap_streaming_request_handler_t *handle_request,
        void *handler_arg);

#    ifdef WITH_AVS_COAP_OBSERVE

/**
 * Informs the CoAP context that an observation request was accepted and the
 * user will send resource value updates with @ref avs_coap_notify_async or
 * @ref avs_coap_notify_streaming .
 *
 * Should only be used along with
 * @ref avs_coap_server_streaming_setup_response , or when the return value of
 * @ref avs_coap_server_streaming_request_handler_t is one of AVS_COAP_CODE_*
 * constants representing a success.
 *
 * Not fulfilling that condition results in immediate cancellation of
 * established observation after
 * @ref avs_coap_server_streaming_request_handler_t returns.
 *
 * If an observation with the same @p id already exists, it is canceled and
 * replaced with a new observation.
 *
 * @param ctx            CoAP request context associated with the Observe
 *                       request.
 *
 * @param id             Observation ID, as passed to
 *                       @ref avs_coap_server_streaming_request_handler_t .
 *
 * @param cancel_handler Optional user-defined handler to be called whenever
 *                       the observation is canceled for any reason.
 *
 *                       After a successful call to this function,
 *                       @p cancel_handler is guaranteed to be called at some
 *                       point.
 *
 * @param handler_arg    Opaque argument to pass to @p cancel_handler.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed. On failure, any previously established observation
 *          with the same @p id is NOT canceled.
 */
avs_error_t avs_coap_observe_streaming_start(
        avs_coap_streaming_request_ctx_t *ctx,
        avs_coap_observe_id_t id,
        avs_coap_observe_cancel_handler_t *cancel_handler,
        void *handler_arg);

#    endif // WITH_AVS_COAP_OBSERVE

#endif // WITH_AVS_COAP_STREAMING_API

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_STREAMING_H
