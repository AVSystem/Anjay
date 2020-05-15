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

#ifndef AVSYSTEM_COAP_ASYNC_SERVER_H
#define AVSYSTEM_COAP_ASYNC_SERVER_H

#include <avsystem/coap/avs_coap_config.h>

#include <stdint.h>

#include <avsystem/coap/async_exchange.h>
#include <avsystem/coap/ctx.h>
#include <avsystem/coap/observe.h>
#include <avsystem/coap/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal CoAP context types.
 *
 * Note: these types exist only to ensure correct function call flow, i.e.:
 *
 * avs_coap_async_handle_incoming_packet
 * '-> handle_new_request (avs_coap_server_new_async_request_handler_t)
 *     '-> avs_coap_server_accept_async_request
 *         '-> handle_request (avs_coap_server_async_request_handler_t)
 *             '-> avs_coap_server_setup_async_response
 *
 * It does not seem to make much sense to "accept a request" if there is none;
 * or to send a response if we aren't processing any request.
 */
typedef struct avs_coap_server_ctx avs_coap_server_ctx_t;
typedef struct avs_coap_request_ctx avs_coap_request_ctx_t;

typedef struct {
    /** Object that contains request code and options. */
    avs_coap_request_header_t header;

    /** Offset of the payload within a full request payload. */
    size_t payload_offset;

    /** Pointer to the request payload. Never NULL. */
    const void *payload;

    /**
     * Number of bytes available to read from
     * @ref avs_coap_exchange_request_t#payload .
     */
    size_t payload_size;
} avs_coap_server_async_request_t;

typedef enum {
    /** Non-final request block received. */
    AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT,

    /**
     * Full request was received: either a non-block one, or the last block of
     * a multi-block request. There will be no more payload blocks, but the
     * @ref avs_coap_request_handler_t will be called again with
     * AVS_COAP_REQUEST_CLEANUP state.
     */
    AVS_COAP_SERVER_REQUEST_RECEIVED,

    /**
     * Used in one of following cases:
     * * BLOCK request is incomplete, but no new blocks recevied for enough
     *   time to consider the request aborted,
     * * Request exchange handling finished,
     * * Exchange canceled on user request (by @ref avs_coap_exchange_cancel
     *   or the context is being cleaned up).
     *
     * The handler will not be called any more.
     */
    AVS_COAP_SERVER_REQUEST_CLEANUP,
} avs_coap_server_request_state_t;

/**
 * This handler is always called with @p state equal to
 * @ref AVS_COAP_REQUEST_CLEANUP at the end of exchange.
 *
 * @param ctx        Pointer to request context. NULL if @p state is
 *                   @ref AVS_COAP_REQUEST_CLEANUP .
 *
 * @param request_id ID that uniquely identifies incoming request.
 *
 * @param state      Reason to call this handler.
 *
 * @param request    Received request data.
 *
 * @param observe_id If not NULL, indicates that the incoming request
 *                   establishes a CoAP Observe. In such case, it should
 *                   be passed to the @ref avs_coap_observe_async_start
 *                   function <strong>before starting to generate response
 *                   payload</strong>. Not calling
 *                   @ref avs_coap_observe_async_start will cause the request
 *                   to be interpreted as plain GET.
 *
 * @param arg        Opaque argument, as passed to
 *                   @ref avs_coap_async_handle_incoming_packet
 *
 * @returns:
 *
 *   @li If @p state was @ref AVS_COAP_REQUEST_PARTIAL_CONTENT or
 *       @ref AVS_COAP_REQUEST_RECEIVED:
 *
 *       @li If one of @ref avs_coap_code_constants valid for responses is
 *           returned, a proper message is set up with this code and no payload.
 *           Response set up by calling @ref avs_coap_setup_response in handler
 *           is ignored.
 *
 *       @li Otherwise, if the return value is nonzero, an Internal Server
 *           Error response is sent with no payload. Response set up by calling
 *           @ref avs_coap_setup_response in handler is ignored.
 *
 *       @li Otherwise, if return value is 0 and @ref avs_coap_setup_response
 *           was called, that response is sent.
 *
 *       @li Otherwise, if @p state was AVS_COAP_REQUEST_PARTIAL_CONTENT and
 *           return value is 0 and message wasn't set up, a 2.31 Continue
 *           response is sent if neccessary.
 *
 *       @li Otherwise, if @p state was AVS_COAP_REQUEST_RECEIVED, return value
 *           is 0 and message wasn't set up, then Internal Server Error is sent.
 *
 *       If @p state was AVS_COAP_REQUEST_RECEIVED, response isn't set up and
 *       return value is 0, then this handler will be called again with
 *       more request payload chunks.
 *
 *   @li Otherwise (if @p state was @ref AVS_COAP_REQUEST_CLEANUP) the return
 *       value is ignored. No message will be sent and the exchange will be
 *       terminated. This handler will not be called again.
 */
typedef int avs_coap_server_async_request_handler_t(
        avs_coap_request_ctx_t *ctx,
        avs_coap_exchange_id_t request_id,
        avs_coap_server_request_state_t state,
        const avs_coap_server_async_request_t *request,
        const avs_coap_observe_id_t *observe_id,
        void *arg);

/**
 * Creates an exchange object representing a single request handled by the
 * server.
 *
 * @param ctx
 *
 * @param request_handler     Callback that should be used to handle request
 *                            payload.
 *
 * @param request_handler_arg Opaque argument that is going to be passed to
 *                            @p request_handler .
 *
 * @returns ID of created exchange object that may later be used to identify it,
 *          or @ref AVS_COAP_EXCHANGE_ID_INVALID in case of error.
 */
avs_coap_exchange_id_t avs_coap_server_accept_async_request(
        avs_coap_server_ctx_t *ctx,
        avs_coap_server_async_request_handler_t *request_handler,
        void *request_handler_arg);

/**
 * Called from @ref avs_coap_async_handle_incoming_packet whenever a new
 * request is received.
 *
 * If the request is going to be handled,
 * @ref avs_coap_server_accept_async_request shall be called.
 *
 * @param ctx
 *
 * @param request Received CoAP request.
 *
 * @param arg     Opaque argument, as passed to
 *                @ref avs_coap_async_handle_incoming_packet .
 *
 * @returns:
 *
 *   @li 0 if the application is willing to handle the request. Note: if
 *       @ref avs_coap_server_accept_async_request was not called, an Internal
 *       Server Error is sent to the client.
 *
 *   @li One of @ref avs_coap_code_constants related to response to be sent to
 *       the client otherwise. If this value is neither a client or server
 *       error, an Internal Server Error response is sent instead.
 */
typedef int avs_coap_server_new_async_request_handler_t(
        avs_coap_server_ctx_t *ctx,
        const avs_coap_request_header_t *request,
        void *arg);

/**
 * Sets up a response that should be sent in response to a request being
 * currently handled.
 *
 * @param ctx
 *
 * @param response            Header of the response to use.
 *
 * @param response_writer     Function to call when library is ready to send a
 *                            chunk of payload data. See
 *                            @ref avs_coap_payload_writer_t for details.
 *
 * @param response_writer_arg An opaque argument passed to @p response_writer .
 *
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_EINVAL)</c> if an invalid header has been passed
 *  - <c>avs_errno(AVS_ENOMEM)</c> for an out-of-memory condition
 *
 * TODO: should calling this function outside
 * @ref avs_coap_server_async_request_handler_t be allowed? That could allow us
 * to implement Separate Responses for UDP, and asynchronous out-of-order
 * responses for both UDP and TCP - if only we had the ability to prevent the
 * library from sending a response after
 * @ref avs_coap_server_async_request_handler_t returns.
 */
avs_error_t
avs_coap_server_setup_async_response(avs_coap_request_ctx_t *ctx,
                                     const avs_coap_response_header_t *response,
                                     avs_coap_payload_writer_t *response_writer,
                                     void *response_writer_arg);

#ifdef WITH_AVS_COAP_OBSERVE
/**
 * Informs the CoAP context that an observation request was accepted and the
 * user will send resource value updates with @ref avs_coap_notify_async or
 * @ref avs_coap_notify_streaming .
 *
 * Should only be used along with @ref avs_coap_server_setup_async_response ,
 * or when the return value of @ref avs_coap_server_async_request_handler_t
 * is one of AVS_COAP_CODE_* constants representing a success.
 *
 * Not fulfilling that condition results in immediate cancellation of
 * established observation after @ref avs_coap_server_async_request_handler_t
 * returns.
 *
 * If an observation with the same @p id already exists, it is canceled and
 * replaced with a new observation.
 *
 * @param ctx            CoAP request context associated with the Observe
 *                       request.
 *
 * @param id             Observation ID, as passed to
 *                       @ref avs_coap_server_async_request_handler_t .
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
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_EINVAL)</c> if an invalid @p ctx has been passed
 *  - <c>avs_errno(AVS_ENOMEM)</c> for an out-of-memory condition
 *  - @ref AVS_COAP_ERR_FEATURE_DISABLED if Observe support is not available in
 *    this build of the library
 */
avs_error_t
avs_coap_observe_async_start(avs_coap_request_ctx_t *ctx,
                             avs_coap_observe_id_t id,
                             avs_coap_observe_cancel_handler_t *cancel_handler,
                             void *handler_arg);
#endif // WITH_AVS_COAP_OBSERVE

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_ASYNC_SERVER_H
