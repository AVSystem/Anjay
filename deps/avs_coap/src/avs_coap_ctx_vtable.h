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

#ifndef AVS_COAP_SRC_CTX_VTABLE_H
#define AVS_COAP_SRC_CTX_VTABLE_H

#include <avsystem/coap/ctx.h>
#include <avsystem/coap/observe.h>
#include <avsystem/coap/option.h>
#include <avsystem/coap/token.h>

#include "avs_coap_observe.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct avs_coap_base avs_coap_base_t;

/**
 * A CoAP message whose options and payload point to storage not owned by the
 * object itself.
 *
 * It may contain only a part of the payload.
 */
typedef struct {
    uint8_t code;
    avs_coap_token_t token;
    avs_coap_options_t options;

    /**
     * Offset within the original CoAP message's payload that corresponds to
     * offset 0 of data pointed to by the <c>payload</c> field.
     */
    size_t payload_offset;

    /**
     * Pointer to memory that contains the part of the payload represented by
     * this object.
     */
    const void *payload;

    /**
     * Number of bytes of valid data at the location pointed to by the
     * <c>payload</c> field.
     */
    size_t payload_size;

    /**
     * Length of the entire payload in the original CoAP message.
     */
    size_t total_payload_size;
} avs_coap_borrowed_msg_t;

typedef enum {
    AVS_COAP_SEND_RESULT_PARTIAL_CONTENT,
    AVS_COAP_SEND_RESULT_OK,
    AVS_COAP_SEND_RESULT_FAIL,
    AVS_COAP_SEND_RESULT_CANCEL
} avs_coap_send_result_t;

typedef enum {
    AVS_COAP_RESPONSE_ACCEPTED,
    AVS_COAP_RESPONSE_NOT_ACCEPTED
} avs_coap_send_result_handler_result_t;

/**
 * Handler called whenever:
 *
 * - the context is sure a message was delivered:
 *
 *   - if the message was NOT a request, result is AVS_COAP_SEND_RESULT_OK and
 *     @p response is NULL.
 *
 *   - if the message was a request, @p response is not NULL and result may be:
 *
 *     - AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, if @p response contains a
 *       partial response payload. The handler will be called again later with
 *       further data.
 *
 *     - AVS_COAP_SEND_RESULT_OK, if @p response contains a full response or the
 *       last part of a response. The handler will not be called again later.
 *       Note: in case of a sequence of AVS_COAP_SEND_RESULT_PARTIAL_CONTENT
 *       + AVS_COAP_SEND_RESULT_OK calls, @p response payload contains
 *       consecutive chunks of data (i.e. no data will be passed to the handler
 *       twice).
 *
 * - the message was not delivered (AVS_COAP_SEND_RESULT_FAIL), in which case
 *   @p fail_errno is set to a specific error code.
 *
 * - @ref avs_coap_cancel_delivery_t was called (AVS_COAP_SEND_RESULT_CANCEL).
 *
 * If @p result is @ref AVS_COAP_SEND_RESULT_OK and @p response is not NULL,
 * this handler is supposed to return @ref AVS_COAP_RESPONSE_ACCEPTED if it
 * accepts the response or @ref AVS_COAP_RESPONSE_NOT_ACCEPTED if it doesn't. In
 * other cases return value is ignored.
 * If the response is not accepted, pending request will not be deleted in
 * CoAP ctx and further responses to the same request will be accepted.
 * This is used in OSCORE, to ignore unencrypted responses and prevent possible
 * attacks trying to make the CoAP client unusable.
 */
typedef avs_coap_send_result_handler_result_t
avs_coap_send_result_handler_t(avs_coap_ctx_t *ctx,
                               avs_coap_send_result_t result,
                               avs_error_t fail_err,
                               const avs_coap_borrowed_msg_t *response,
                               void *arg);

/**
 * Virtual method typedefs.
 *
 * These are CoAP context methods that need to be implemented for each
 * supported transport protocol.
 *
 * Vtable methods are supposed to only deal with:
 * - packet encoding/decoding
 * - retransmissions (if required)
 * - token-based request-response matching
 * - transport-specific stuff, e.g.:
 *   - UDP:
 *     - message IDs
 *     - Separate Responses
 *     - message types
 *     - Observe cancellation with Reset response
 *   - TCP:
 *     - CSM messages
 *
 * In particular, vtable methods SHOULD NOT:
 * - handle avs_coap_exchange_t objects,
 * - handle any CoAP options,
 * - handle BLOCK options or split messages into multiple separate packets,
 * - assign tokens
 *
 * @{
 */
typedef void avs_coap_cleanup_t(avs_coap_ctx_t *ctx);

/**
 * Returns base of @p ctx .
 */
typedef avs_coap_base_t *avs_coap_get_base_t(avs_coap_ctx_t *ctx);

/**
 * @returns Maximum number of bytes possible to include in a single CoAP packet
 *          with specified @p token_size, @p options and @p message_code .
 *
 *          Upper layers are supposed to split payload into BLOCK/BERT chunks
 *          in case the whole logical request/response payload is larger than
 *          the value returned by this function.
 *
 *          If this returns 0, @ref avs_coap_send_message_t may fail even if
 *          no payload is passed.
 *
 * Note: @p options may be NULL, which is interpreted as no options.
 *       @p message_code is used only in OSCORE context to properly determine
 *          actual options size. It's ignored if @p options is NULL.
 */
typedef size_t
avs_coap_max_outgoing_payload_size_t(avs_coap_ctx_t *ctx,
                                     size_t token_size,
                                     const avs_coap_options_t *options,
                                     uint8_t message_code);

/**
 * @returns Maximum number of bytes possible to receive in a single CoAP packet
 *          with specified @p token_size, @p options and @p message_code .
 *
 *          If this returns 0, @ref avs_coap_recv_message_t may fail even if
 *          no payload is passed.
 */
typedef size_t
avs_coap_max_incoming_payload_size_t(avs_coap_ctx_t *ctx,
                                     size_t token_size,
                                     const avs_coap_options_t *options,
                                     uint8_t message_code);

/**
 * Sends a single CoAP message and optionally registers a callback to be
 * executed when a response is received.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
typedef avs_error_t
avs_coap_send_message_t(avs_coap_ctx_t *ctx,
                        const avs_coap_borrowed_msg_t *msg,
                        avs_coap_send_result_handler_t *send_result_handler,
                        void *send_result_handler_arg);

typedef enum {
    AVS_COAP_EXCHANGE_CLIENT_REQUEST,
    AVS_COAP_EXCHANGE_SERVER_NOTIFICATION
} avs_coap_exchange_direction_t;

/**
 * Unregisters a callback configured to run when a response to message with
 * @p token is received, and aborts its retransmissions if any.
 *
 * @p result and @p fail_errno are passed to the appropriate
 * @ref avs_coap_send_result_handler_t .
 */
typedef void avs_coap_abort_delivery_t(avs_coap_ctx_t *ctx,
                                       avs_coap_exchange_direction_t direction,
                                       const avs_coap_token_t *token,
                                       avs_coap_send_result_t result,
                                       avs_error_t fail_err);

/**
 * Forces current request's incoming payload chunks to be ignored. User handler
 * for this request won't be called again.
 *
 * If currently processed message is not a request or @p token doesn't match the
 * token of it, then this function is a no-op.
 *
 * Note:
 * This operation is a noop for transports which receive entire message in a
 * single call to receive_message method.
 */
typedef void avs_coap_ignore_current_request_t(avs_coap_ctx_t *ctx,
                                               const avs_coap_token_t *token);

/**
 * Receives data from the socket associated with @p ctx .
 *
 * If received data is a response to a message previously sent by
 * @ref avs_coap_send_message_t , handles it internally, calling its
 * send_result_handler if it exists.
 *
 * If received data includes a request (with complete options and at least
 * partial payload), or if more payload data is received for a request that
 * was not fully received yet, @p out_request (which shall NEVER be NULL) is
 * filled with information about that request; the options and payload pointers
 * may point to either of:
 *
 * - inside @p in_buffer - in that case the caller is responsible for managing
 *   the lifetime of the buffer passed; @p in_buffer shall NEVER be NULL
 * - internal buffers allocated within the transport-specific part of @p ctx -
 *   in that case the data shall remain valid until the next call to this
 *   handler
 *
 * No request to handle is indicated by out_request->code value that is not
 * a valid request code.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
typedef avs_error_t
avs_coap_receive_message_t(avs_coap_ctx_t *ctx,
                           uint8_t *in_buffer,
                           size_t in_buffer_size,
                           avs_coap_borrowed_msg_t *out_request);

/**
 * Function called whenever a new observation request is accepted.
 *
 * @p observation MUST be initialized valid observation before call to this
 * function.
 */
typedef avs_error_t
avs_coap_accept_observation_t(avs_coap_ctx_t *ctx,
                              avs_coap_observe_t *observation);

/**
 * Function called whenever a scheduler job associated with @p ctx is run.
 *
 * The transport-specific backend MUST NOT allocate any scheduler jobs on
 * @p ctx scheduler object for retransmission/timeout purposes. This function
 * MUST be used instead.
 *
 * The implementation should use @ref _avs_coap_reschedule_timeout_job whenever
 * it knows when it should be notified.
 *
 * Note: spurious calls to this function may occur.
 *
 * @returns Next time this function should be called, or
 *          @ref AVS_TIME_MONOTONIC_INVALID if at the point of calling this
 *          the implementation does not need to perform any scheduled actions.
 */
typedef avs_time_monotonic_t avs_coap_on_timeout_t(avs_coap_ctx_t *ctx);

/**
 * Getter for context's statistics. May be not implemented if context does not
 * want to report any.
 */
typedef avs_coap_stats_t avs_coap_get_stats_t(avs_coap_ctx_t *ctx);

/**
 * Function called whenever someone executes @ref avs_coap_ctx_set_socket on a
 * context @p ctx.
 *
 * If the call fails, the underlying context state @p ctx MUST NOT be modified.
 * The only exception is context errno, which MAY be set by this function on
 * failure.
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
typedef avs_error_t avs_coap_setsock_t(avs_coap_ctx_t *ctx,
                                       avs_net_socket_t *socket);

/** @} */

typedef struct avs_coap_ctx_vtable {
    avs_coap_get_base_t *get_base;
    avs_coap_cleanup_t *cleanup;
    avs_coap_setsock_t *setsock;
    avs_coap_max_outgoing_payload_size_t *max_outgoing_payload_size;
    avs_coap_max_incoming_payload_size_t *max_incoming_payload_size;
    avs_coap_send_message_t *send_message;
    avs_coap_abort_delivery_t *abort_delivery;
    avs_coap_ignore_current_request_t *ignore_current_request;
    avs_coap_receive_message_t *receive_message;
    avs_coap_accept_observation_t *accept_observation;
    avs_coap_on_timeout_t *on_timeout;
    avs_coap_get_stats_t *get_stats;
} avs_coap_ctx_vtable_t;

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_CTX_VTABLE_H
