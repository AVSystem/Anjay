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

#ifndef AVSYSTEM_COAP_OBSERVE_H
#define AVSYSTEM_COAP_OBSERVE_H

#include <avsystem/coap/avs_coap_config.h>

#include <avsystem/coap/async_exchange.h>
#include <avsystem/coap/ctx.h>
#include <avsystem/coap/token.h>
#include <avsystem/coap/writer.h>

#ifdef WITH_AVS_COAP_OBSERVE_PERSISTENCE
#    include <avsystem/commons/avs_persistence.h>
#endif // WITH_AVS_COAP_OBSERVE_PERSISTENCE

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ID uniquely identifying an observation.
 *
 * Note: using just the token should be unique enough if we assume separate
 * avs_coap_ctx_t per server.
 */
typedef struct {
    avs_coap_token_t token;
} avs_coap_observe_id_t;

typedef enum {
    /**
     * The caller does not care if the notification gets delivered successfully
     * or not. Implementation is free to send it as non-confirmable if such
     * messages are supported by underlying transport.
     */
    AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE,

    /**
     * The caller needs to reliably know if the notification was delivered
     * successfully or not.
     */
    AVS_COAP_NOTIFY_PREFER_CONFIRMABLE,
} avs_coap_notify_reliability_hint_t;

/**
 * A function called whenever the CoAP context object receives a request for
 * Observe cancellation.
 *
 * @param id Identifier of the cancelled observation.
 *
 * @param arg Opaque user-defined data, as passed to
 *            @ref avs_coap_observe_start.
 *
 * - [UDP] RST response to a Notify may also cancel a notification, not only
 *   GET with Observe=1
 */
typedef void avs_coap_observe_cancel_handler_t(avs_coap_observe_id_t id,
                                               void *arg);

/**
 * A function called whenever the confirmation of notification delivery is
 * received or notification is cancelled and known to be never sent again.
 *
 * @param result Result of sending notification.
 * @param err    AVS_OK if the delivery was successful, or the reason for which
 *               the delivery failed.
 * @param arg    Opaque user-defined data, as passed to
 *               @ref avs_coap_notify_async .
 */
typedef void avs_coap_delivery_status_handler_t(avs_coap_ctx_t *ctx,
                                                avs_error_t err,
                                                void *arg);

#ifdef WITH_AVS_COAP_OBSERVE_PERSISTENCE

/**
 * Stores Observe information (for the Observe entry as specified by @p id)
 * using @p persistence context.
 *
 * The information can be used later on with new CoAP context using function
 * @ref avs_coap_observe_restore().
 *
 * @param ctx           CoAP context to operate on.
 * @param id            Unique observation ID (request token).
 * @param persistence   Persistence context to operate on.
 *
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_EINVAL)</c> if there is no observation with such @p id
 *  - @ref AVS_COAP_ERR_NOT_IMPLEMENTED if observation options are too long
 *  - any I/O error forwarded from the underlying stream
 */
avs_error_t avs_coap_observe_persist(avs_coap_ctx_t *ctx,
                                     avs_coap_observe_id_t id,
                                     avs_persistence_context_t *persistence);

/**
 * Restores single Observe entry from the specified @p persistence context.
 *
 * Restoring observation with identifier that already exists in given CoAP
 * context, will result in error being returned.
 *
 * IMPORTANT: If CoAP context is already initialized with socket (see @ref
 * avs_coap_ctx_set_socket()), the restore operation is not possible and an
 * error will be returned.
 *
 * NOTE: In case of error, nothing in CoAP context is changed.
 *
 * @param ctx            CoAP context to operate on.
 *
 * @param cancel_handler Optional user-defined handler to be called whenever
 *                       the observation is cancelled for any reason.
 *
 *                       After a successful call to this function,
 *                       @p cancel_handler is guaranteed to be called at some
 *                       point.
 *
 * @param handler_arg    Opaque argument to pass to @p cancel_handler.
 *
 * @param persistence    Persistence context to operate on.
 *
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_EBADMSG)</c> for malformed stream data
 *  - <c>avs_errno(AVS_ENOMEM)</c> for an out-of-memory condition
 *  - <c>avs_errno(AVS_EINVAL)</c> if the CoAP context is already initialized
 *  - any I/O error forwarded from the underlying stream
 */
avs_error_t
avs_coap_observe_restore(avs_coap_ctx_t *ctx,
                         avs_coap_observe_cancel_handler_t *cancel_handler,
                         void *handler_arg,
                         avs_persistence_context_t *persistence);

#endif // WITH_AVS_COAP_OBSERVE_PERSISTENCE

#ifdef WITH_AVS_COAP_OBSERVE

/**
 * Informs the CoAP context that it should establish an observation without
 * explicit client request.
 *
 * This may happen in case of restoring observations from persistent storage.
 * In such case, one MUST make sure that @p req exactly matches the request
 * object used for an original observation. Using different request options
 * than these included in original Observe request are not allowed by CoAP
 * Observe RFC and may cause CoAP clients to react in unexpected ways.
 *
 * Request details passed to this function are copied for later use by
 * notification sending functions. The copy is released whenever the
 * observation gets invalidated.
 *
 * If an observation with the same @p id already exists, it is canceled and
 * replaced with a new observation.
 *
 * @param ctx            CoAP context to operate on.
 *
 * @param id             Unique observation ID (request token).
 *
 * @param req            Header of the original observation request.
 *                       MUST NOT be NULL.
 *
 * @param cancel_handler Optional user-defined handler to be called whenever
 *                       the observation is cancelled for any reason.
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
 *
 * On failure, any previously established observation with the same @p id is NOT
 * canceled.
 */
avs_error_t
avs_coap_observe_start(avs_coap_ctx_t *ctx,
                       avs_coap_observe_id_t id,
                       const avs_coap_request_header_t *req,
                       avs_coap_observe_cancel_handler_t *cancel_handler,
                       void *handler_arg);

/**
 * Sends a CoAP Notification in an asynchronous mode. This function returns
 * immediately.
 *
 * @param ctx                  CoAP context object to operate on. Indicates
 *                             where should the notification be sent.
 *
 * @param[out] out_exchange_id On success, set to an ID that may be used to
 *                             identify a specific asynchronous notification,
 *                             or to AVS_COAP_EXCHANGE_ID_INVALID if
 *                             no response is expected.
 *
 * @param observe_id           Unique observation ID for which a notification
 *                             is being sent.
 *
 * @param response_header      CoAP code and options to include in sent
 *                             notification.
 *
 *                             Note: sending a notification with an error code
 *                             (4.xx or 5.xx) implicitly cancels the
 *                             observation.
 *
 * @param reliability_hint     Indicates whether the implementation should make
 *                             sure to deliver the notification reliably or is
 *                             allowed to use non-reliable messages if
 *                             supported.
 *
 *                             [UDP] @ref AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE
 *                             causes the library to use NON message.
 *
 * @param write_payload        A callback used to pass notification payload.
 *                             If NULL, no payload is included in the
 *                             notification.
 *
 * @param write_payload_arg    An opaque argument passed to @p write_payload .
 *
 * @param delivery_handler     Handler called after success or failure of
 *                             sending notification. It is guaranteed to be
 *                             called exactly once, after any @p write_payload
 *                             calls the library may need to do.
 *
 *                             If @p reliability_hint is
 *                             @ref AVS_COAP_PREFER_NOTIFY_NON_CONFIRMABLE , the
 *                             <c>result</c> argument passed to this handler
 *                             will have a value of @ref AVS_COAP_NOTIFY_FAIL
 *                             only if there was a definite failure. Otherwise,
 *                             @ref AVS_COAP_NOTIFY_SUCCESS will be passed, even
 *                             if actual success or failure of the delivery
 *                             cannot be determined.
 *
 *                             If @c NULL, @p reliability_hint MUST be set to
 *                             @ref AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE, and
 *                             @p write_payload_arg MUST NOT require any
 *                             cleanup.
 *
 * @param delivery_handler_arg An opaque argument passed to @p delivery_handler
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 *
 *          In case of an error, @p delivery_handler is NEVER called.
 *
 * Notes:
 *
 * - Successful result of this function doesn't guarantee arrival of the
 *   notification. In case reliable delivery is necessary, @p delivery_handler
 *   should be used.
 *
 * - It is not guaranteed that the @p write_payload will be called until the
 *   payload is read to end. If @p write_payload_arg requires any cleanup,
 *   it should be performed in @p delivery_handler .
 */
avs_error_t
avs_coap_notify_async(avs_coap_ctx_t *ctx,
                      avs_coap_exchange_id_t *out_exchange_id,
                      avs_coap_observe_id_t observe_id,
                      const avs_coap_response_header_t *response_header,
                      avs_coap_notify_reliability_hint_t reliability_hint,
                      avs_coap_payload_writer_t *write_payload,
                      void *write_payload_arg,
                      avs_coap_delivery_status_handler_t *delivery_handler,
                      void *delivery_handler_arg);

#    ifdef WITH_AVS_COAP_STREAMING_API

/**
 * Sends a Confirmable CoAP Notification in a blocking mode. Does not return
 * until the full notification is sent, or until an error happens.
 *
 * - [UDP] setting @p reliability_hint to
 *   @ref AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE causes the library to use NON
 *   message for the first notification block and ACK for following ones. The
 *   function will return immediately after writing the payload finishes and
 *   indicate success if a request for the last payload block was received.
 *
 * - [UDP] setting @p reliability_hint to
 *   @ref AVS_COAP_NOTIFY_PREFER_CONFIRMABLE forces the use of Separate
 *   Responses for *all* notification blocks; the function will then succeed
 *   only after receiving an ACK to the last payload block.
 *
 * - [TCP] the function returns immediately after passing the whole
 *   notification payload to the socket.
 *
 * @param ctx               CoAP context object to operate on. Indicates where
 *                          should the notification be sent to.
 *
 * @param observe_id        Unique observation ID for which a notification is
 *                          being sent.
 *
 * @param response_header   CoAP code and options to include in sent
 *                          notification.
 *
 *                          Note: sending a notification with an error code
 *                          (4.xx or 5.xx) implicitly cancels the observation.
 *
 * @param reliability_hint  Indicates whether the implementation should make
 *                          sure to deliver the notification reliably or is
 *                          allowed to use non-reliable messages if
 *                          supported.
 *
 * @param write_payload     A callback used to pass notification payload.
 *                          If NULL, no payload is included in the notification.
 *
 * @param write_payload_arg An opaque argument passed to @p write_payload .
 *
 * @returns @ref AVS_OK for success, or an error condition for which the
 *          operation failed.
 */
avs_error_t
avs_coap_notify_streaming(avs_coap_ctx_t *ctx,
                          avs_coap_observe_id_t observe_id,
                          const avs_coap_response_header_t *response_header,
                          avs_coap_notify_reliability_hint_t reliability_hint,
                          avs_coap_streaming_writer_t *write_payload,
                          void *write_payload_arg);

#    endif // WITH_AVS_COAP_STREAMING_API

#endif // WITH_AVS_COAP_OBSERVE

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_OBSERVE_H
