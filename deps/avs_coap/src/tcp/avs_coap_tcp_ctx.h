/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "avs_coap_ctx.h"

#include "tcp/avs_coap_tcp_msg.h"
#include "tcp/avs_coap_tcp_pending_requests.h"
#include "tcp/avs_coap_tcp_signaling.h"

#ifndef AVS_COAP_SRC_TCP_CTX_H
#    define AVS_COAP_SRC_TCP_CTX_H

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_HEADER = 0,
    AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_TOKEN,
    AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_OPTIONS,
    AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_PAYLOAD,
    AVS_COAP_TCP_OPT_CACHE_STATE_IGNORING
} avs_coap_tcp_opt_cache_state_t;

typedef struct {
    avs_buffer_t *buffer;
    avs_coap_tcp_opt_cache_state_t state;
} avs_coap_tcp_opt_cache_t;

typedef struct avs_coap_tcp_ctx_struct {
    const struct avs_coap_ctx_vtable *vtable;

    avs_coap_base_t base;
    avs_coap_tcp_opt_cache_t opt_cache;
    avs_coap_tcp_cached_msg_t cached_msg;
    avs_coap_tcp_csm_t peer_csm;
    // Sorted by @ref avs_coap_tcp_pending_request_t#expire_time
    AVS_LIST(avs_coap_tcp_pending_request_t) pending_requests;
    // Timeout defined during creation of CoAP TCP context.
    avs_time_duration_t request_timeout;

#    ifdef WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
    const char *err_details;
#    endif // WITH_AVS_COAP_DIAGNOSTIC_MESSAGES

    // Indicating that Abort message was sent to prevent sending Release
    // message in cleanup.
    bool aborted;

    // Error set when incoming message is set up to be ignored, returned to user
    // when message is finished. It has to be stored, because we want to delay
    // reporting the error until the whole message is received.
    avs_error_t ignoring_error;
} avs_coap_tcp_ctx_t;

/** Sends previously constructed message over CoAP/TCP. */
avs_error_t _avs_coap_tcp_send_msg(avs_coap_tcp_ctx_t *ctx,
                                   const avs_coap_borrowed_msg_t *msg);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_TCP_CTX_H
