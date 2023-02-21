/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVSYSTEM_COAP_H
#define AVSYSTEM_COAP_H

#include <avsystem/coap/avs_coap_config.h>

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
 *       incoming request, see @ref avs_coap_async_handle_incoming_packet .
 *
 *   - functions:
 *
 *     - @ref avs_coap_streaming_send_request - sends a (possibly BLOCK-wise)
 *       request.
 *
 *     - @ref avs_coap_async_handle_incoming_packet - receives a packet from a
 *       socket, calls @ref avs_coap_streaming_request_handler_t if it's an
 *       incoming request (note: this function is common to streaming and
 *       async APIs).
 *
 * - async API:
 *
 *   - typedefs:
 *
 *     - @ref avs_coap_client_async_response_handler_t - handler called after
 *       async request delivery is confirmed and a response was received.
 *
 *   - functions:
 *
 *     - @ref avs_coap_client_send_async_request - sends an asynchronous request
 *       and possibly registers a function to be called when a response is
 *       received.
 *
 *     - @ref avs_coap_exchange_cancel - cancels an asynchronous request if it's
 *       still in progress.
 *
 *     - @ref avs_coap_async_handle_incoming_packet - receives a packet from a
 *       socket, calls appropriate @ref avs_coap_client_async_response_handler_t
 *       if a delivery confirmation was received (note: this function is common
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
 *      - @ref avs_coap_observe_streaming_start - function that may be called
 *        from within @ref avs_coap_streaming_request_handler_t to indicate that
 *        an Observe request was established and the user will proceed to send
 *        notifications.
 *
 *      - @ref avs_coap_observe_async_start - function that may be called from
 *        within @ref avs_coap_server_async_request_handler_t to indicate that
 *        an Observe request was established and the user will proceed to send
 *        notifications.
 *
 *      - @ref avs_coap_notify_streaming and
 *        @ref avs_coap_notify_async - functions that may be called at
 *        any time after an observation is established to send a notification.
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
 *   - message ID,
 *   - message type (CON/NON/ACK/RST),
 *   - retransmissions,
 *   - BLOCK
 *
 * - TCP:
 *   - Signaling options, including Capabilities and Settings Messages (CSM)
 *   - Block-wise transfers over Reliable Transports (BERT)
 */

#include <avsystem/coap/async.h>
#include <avsystem/coap/code.h>
#include <avsystem/coap/ctx.h>
#include <avsystem/coap/observe.h>
#include <avsystem/coap/option.h>
#include <avsystem/coap/streaming.h>
#include <avsystem/coap/tcp.h>
#include <avsystem/coap/token.h>
#include <avsystem/coap/udp.h>

#endif // AVSYSTEM_COAP_H
