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

#ifndef AVS_COAP_SRC_ASYNC_SERVER_H
#define AVS_COAP_SRC_ASYNC_SERVER_H

#include <avsystem/coap/async_server.h>

#include "avs_coap_ctx_vtable.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Additional exchange data required by incoming requests currently being
 * processed by us (acting as a CoAP server).
 */
typedef struct avs_coap_server_exchange_data {
    /**
     * Internal handler used by async_server to handle incoming requests.
     */
    avs_coap_server_async_request_handler_t *request_handler;
    void *request_handler_arg;

    /** Flag indicating whether NON messages may be used if supported. */
    avs_coap_notify_reliability_hint_t reliability_hint;

    /**
     * User-defined response delivery handler. May be non-NULL for Observe
     * notifications.
     */
    avs_coap_delivery_status_handler_t *delivery_handler;
    void *delivery_handler_arg;

    /**
     * A time point at which the exchange should be considered failed.
     *
     * Used for response exchanges to detect when the remote client stops
     * requesting further response blocks.
     */
    avs_time_monotonic_t exchange_deadline;

    /**
     * CoAP code of the last received request message.
     * Used to match individual request blocks to a specific exchange.
     */
    uint8_t request_code;
    /**
     * CoAP options of the last received request message.
     * Used to match individual request blocks to a specific exchange.
     */
    avs_coap_options_t request_key_options;

    /** avs_coap_server_exchange_t#request_key_options storage. */
    char *request_key_options_buffer;

    /**
     * Used to check if requests' BLOCK1s are received sequentially. This is
     * required because BERT may make the offset increment by more than a single
     * block size.
     */
    size_t expected_request_payload_offset;
} avs_coap_server_exchange_data_t;

struct avs_coap_server_ctx {
    avs_coap_ctx_t *coap_ctx;

    /**
     * ID of the server exchange created by user call to
     * @ref avs_coap_server_accept_async_request .
     *
     * Stored within this context to:
     * - prevent the user from accepting the same request more than once (i.e.
     *   allocating more than 1 exchange for processing the same request),
     * - delay sending the Continue response until after
     *   @ref avs_coap_server_new_async_request_handler_t finishes. This allows
     *   us to handle all nonzero return values from that handler as error
     *   response codes; otherwise we'd end up sending two responses to the
     *   same request (2.31 Continue first and then an error response later).
     */
    avs_coap_exchange_id_t exchange_id;

    /** Incoming request message we're currently processing. */
    const avs_coap_borrowed_msg_t *request;
};

struct avs_coap_request_ctx {
    /** Incoming request message we're currently processing. */
    avs_coap_borrowed_msg_t request;

    /** ID of the exchange that we're currently processing. */
    avs_coap_exchange_id_t exchange_id;

    /** Associated CoAP context. */
    avs_coap_ctx_t *coap_ctx;

    /**
     * Set to true after the user calls
     * @ref avs_coap_server_setup_async_response . Used for:
     * - preventing further calls to @ref avs_coap_async_request_handler after
     *   user decides no more request payload is necessary to determine final
     *   operation result,
     * - detect the case where the user does not setup any response despite
     *   having received complete request.
     */
    bool response_setup;

    /**
     * Set to true after the user calls @ref avs_coap_observe_async_start
     * successfully. Used to determine whether @ref avs_coap_observe_t object
     * has to be deleted after a failed call to user request_handler.
     */
    bool observe_established;
};

/**
 * @returns time at which next exchange timeout occurs, or
 *          @ref AVS_TIME_MONOTONIC_INVALID if there are no more exchanges
 *          that could time out.
 */
avs_time_monotonic_t
_avs_coap_async_server_abort_timedout_exchanges(avs_coap_ctx_t *ctx);

struct avs_coap_exchange;

void _avs_coap_server_exchange_cleanup(avs_coap_ctx_t *ctx,
                                       struct avs_coap_exchange *exchange,
                                       avs_error_t error);

bool _avs_coap_response_header_valid(const avs_coap_response_header_t *res);

/**
 * Handles an incoming packet.
 *
 * If the incoming packet is either:
 * - an invalid message
 * - [UDP] a CoAP ping message
 * - [TCP] a CoAP signaling message
 * - a response to a client-side request
 * - a request for a next block of a response that is already being sent
 * then the packet is handled entirely within this call and the variable pointed
 * to by @p out_exchange is set to <c>NULL</c>.
 *
 * If the incoming packet is a new request that does not match any existing
 * exchange, then @p on_new_request is called. If it returns failure, then
 * failure response sending is handled entirely within this call and the
 * variable pointed to by @p out_exchange is set to <c>NULL</c> as well.
 *
 * If the @p on_new_request call was successful, or if the incoming packet is
 * further request payload block of an ongoing exchange, then the
 * <c>request_handler</c> needs to be called. It is not done within this call.
 * Instead, the variable pointed to by @p out_exchange is set to the matched
 * (or newly created) exchange. The caller then SHOULD call
 * @p _avs_coap_async_incoming_packet_call_request_handler to execute the
 * request handler and MUST call
 * @p _avs_coap_async_incoming_packet_send_response . Calling this function
 * without following with @p _avs_coap_async_incoming_packet_send_response
 * when <c>*out_exchange</c> has been set to non-NULL is undefined behaviour.
 *
 * @param coap_ctx           CoAP context to operate on.
 *
 * @param on_new_request     Handler to execute when a new request is to be
 *                           created. If <c>NULL</c>, then all such requests are
 *                           rejected with 5.00 Internal Server Error.
 *
 * @param on_new_request_arg Opaque argument to pass to @p on_new_request .
 *
 * @param out_exchange       Pointer to a variable that will be set to a pointer
 *                           to the exchange for which the request handler needs
 *                           to be called, or to <c>NULL</c> if there is no such
 *                           exchange.
 *
 *                           This function will never set <c>*out_exchange</c>
 *                           to non-NULL and return nonzero code at the same
 *                           time.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t _avs_coap_async_incoming_packet_handle_single(
        avs_coap_ctx_t *coap_ctx,
        uint8_t *in_buffer,
        size_t in_buffer_size,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg,
        AVS_LIST(struct avs_coap_exchange) *out_exchange);

/**
 * Calls the request handler for a given request context and exchange pointer.
 * May only be called with value returned from
 * @ref _avs_coap_async_incoming_packet_handle_single, see its documentation for
 * details.
 */
int _avs_coap_async_incoming_packet_call_request_handler(
        avs_coap_ctx_t *ctx, struct avs_coap_exchange *exchange);

/**
 * Sends a response to an incoming request that could not be handled entirely
 * within @ref _avs_coap_async_incoming_packet_handle_single; see its
 * documentation for details.
 *
 * @param coap_ctx    CoAP context to operate on.
 *
 * @param call_result Return value from
 *                    @ref _avs_coap_async_incoming_packet_call_request_handler
 *                    or any equivalent routine performed instead of or in
 *                    addition to it. If nonzero, then an error response will
 *                    be sent according to the rules documented for
 *                    @ref avs_coap_server_new_async_request_handler_t .
 *
 * @returns @ref AVS_OK for success (regardless of whether success or error was
 *          being sent), or an error condition for which the operation failed.
 */
avs_error_t _avs_coap_async_incoming_packet_send_response(avs_coap_ctx_t *ctx,
                                                          int call_result);

/**
 * Combines the calls to @ref _avs_coap_async_incoming_packet_handle_single and,
 * if applicable, @ref _avs_coap_async_incoming_packet_call_request_handler and
 * @ref _avs_coap_async_incoming_packet_send_response .
 */
avs_error_t _avs_coap_async_incoming_packet_simple_handle_single(
        avs_coap_ctx_t *ctx,
        uint8_t *in_buffer,
        size_t in_buffer_size,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg);

avs_error_t
_avs_coap_async_incoming_packet_handle_while_possible_without_blocking(
        avs_coap_ctx_t *ctx,
        uint8_t *in_buffer,
        size_t in_buffer_size,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_ASYNC_SERVER_H
