/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#ifndef AVSYSTEM_COAP_H
#define AVSYSTEM_COAP_H

#include <avsystem/coap/config.h>

/**
 * The general idea is to provide a hybrid blocking (stream-based) + async API
 * so that we can extract most of CoAP handling logic into commons without
 * throwing the whole Anjay upside down.
 *
 * Brief overview:
 *
 * - streaming API:
 *
 *   - typedefs:
 *
 *     - @ref avs_coap_streaming_writer_t - callback used to pass
 *       payload to outgoing streaming requests, see
 *       @ref avs_coap_streaming_send_request .
 *
 *     - @ref avs_coap_streaming_request_handler_t - callback used to handle an
 *       incoming request, see @ref avs_coap_handle_incoming_packet .
 *
 *   - functions:
 *
 *     - @ref avs_coap_streaming_send_request - sends a (possibly BLOCK-wise)
 *       request.
 *
 *     - @ref avs_coap_handle_incoming_packet - receives a packet from a
 *       socket, calls @ref avs_coap_streaming_request_handler_t if it's an
 *       incoming request (note: this function is common to streaming and
 *       async APIs).
 *
 * - async API:
 *
 *   - typedefs:
 *
 *     - @ref avs_coap_async_response_handler_t - handler called after async
 *       request delivery is confirmed and a response was received.
 *
 *   - functions:
 *
 *     - @ref avs_coap_async_send_request - sends an asynchronous request and
 *       possibly registers a function to be called when a response is
 *       received.
 *
 *     - @ref avs_coap_exchange_abort - cancels an asynchronous request if it's
 *       still in progress.
 *
 *     - @ref avs_coap_handle_incoming_packet - receives a packet from a
 *       socket, calls appropriate @ref avs_coap_async_response_handler_t if
 *       a delivery confirmation was received (note: this function is common
 *       to streaming and async APIs).
 *
 * - notification API:
 *
 *   - typedefs:
 *
 *     - @ref avs_coap_observe_cancel_handler_t - handler called whenever the
 *       remote endpoint cancels an observation.
 *
 *   - functions:
 *
 *      - @ref avs_coap_observe_start - function that may be called from within
 *        @ref avs_coap_streaming_request_handler_t to indicate that an Observe
 *        request was established and the user will proceed to send
 *        notifications using @ref avs_coap_streaming_notify or
 *        @ref avs_coap_async_notify whenever required.
 *
 *      - @ref avs_coap_observe_notify - function that may be called at any
 *        time after an observation is established to send a notification.
 *
 *
 * The API is supposed to be independent from the underlying transport and
 * expose only common parts of CoAP:
 *
 * - message code,
 * - message token,
 * - options,
 * - payload.
 *
 * Transport-specific details are abstracted away:
 *
 * - UDP:
 *
 *   - message ID,
 *   - message type (CON/NON/ACK/RST),
 *   - retransmissions,
 *   - BLOCK
 *
 * - TCP:
 *
 *   - Signaling options, including Capabilities and Settings Messages (CSM)
 *   - Block-wise transfers over Reliable Transports (BERT)
 *
 *
 * The CoAP API would integrate into Anjay as follows:
 *
 * - Every server object has its own avs_coap_ctx_t object associated with
 *   the server socket.
 *
 * - Anjay is responsible for maintaining the socket connection and tearing
 *   down the CoAP context object whenever CoAP endpoint changes.
 *
 * - anjay_serve() calls avs_coap_handle_incoming_packet() for a CoAP context
 *   object associated with the socket that's ready.
 *
 * - Downloader uses async API. BLOCK-wise transfers are possible with the
 *   use of @ref AVS_COAP_EXCHANGE_PARTIAL_CONTENT exchange result. Download
 *   continuations require setting up the BLOCK2 option in initial request.
 *
 * - Send uses async API.
 *
 * - Register/Update and all server-originated reqests like Read/Write etc. use
 *   streaming API.
 *
 * - Observe uses avs_coap_observe_start to make the CoAP context keep track of
 *   the observation and become notified of observation cancellation.
 *
 * - Notify uses avs_coap_observe_notify to handle observations. Either
 *   streaming or async API may then be used to send the notification.
 *
 *   [UDP] using async API with no response handler maps to NON notifications.
 *
 *   [UDP] Observe cancellation via Reset will be detected by CoAP context,
 *   and result in a call to avs_coap_observe_cancel_handler_t .
 *
 *   TODO: this will break block-wise Notify on non-Confirmable messages, which
 *   we currently try to support.
 *
 * - CoAP statistics getters will need to aggregate stats from all CoAP context
 *   objects.
 *
 * TODO: Observe persistence will need to be reimplemented.
 *
 * Note: in the future, it could be possible to use async API for
 * Register/Update instead of the blocking one, at the cost of caching the
 * whole payload.
 */

#include <avsystem/coap/async.h>
#include <avsystem/coap/code.h>
#include <avsystem/coap/ctx.h>
#include <avsystem/coap/observe.h>
#include <avsystem/coap/option.h>
#include <avsystem/coap/streaming.h>
#include <avsystem/coap/token.h>
#include <avsystem/coap/udp.h>

#endif // AVSYSTEM_COAP_H
