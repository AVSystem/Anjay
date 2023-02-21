/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_TCP_SIGNALING_H
#define AVS_COAP_SRC_TCP_SIGNALING_H

#include <avsystem/coap/option.h>
#include <avsystem/coap/token.h>

#include "tcp/avs_coap_tcp_msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

struct avs_coap_tcp_ctx_struct;

typedef struct {
    bool received;
    // max_message_size is a maximum single message size (starting from first
    // byte of the header and ending at the end of the message payload) which
    // peer can receive.
    size_t max_message_size;
    bool block_wise_transfer_capable;
} avs_coap_tcp_csm_t;

/**
 * CoAP Signaling option codes, as defined in RFC 8323.
 * Codes reused between different options. Meaning depends on message code.
 */
// clang-format off
#define _AVS_COAP_OPTION_MAX_MESSAGE_SIZE               2
#define _AVS_COAP_OPTION_BLOCK_WISE_TRANSFER_CAPABILITY 4
#define _AVS_COAP_OPTION_CUSTODY                        2
#define _AVS_COAP_OPTION_ALTERNATIVE_ADDRESS            2
#define _AVS_COAP_OPTION_HOLD_OFF                       4
#define _AVS_COAP_OPTION_BAD_CSM_OPTION                 2

#define AVS_COAP_CODE_CSM     AVS_COAP_CODE(7, 1)
#define AVS_COAP_CODE_PING    AVS_COAP_CODE(7, 2)
#define AVS_COAP_CODE_PONG    AVS_COAP_CODE(7, 3)
#define AVS_COAP_CODE_RELEASE AVS_COAP_CODE(7, 4)
#define AVS_COAP_CODE_ABORT   AVS_COAP_CODE(7, 5)
// clang-format on

avs_error_t
_avs_coap_tcp_handle_signaling_message(struct avs_coap_tcp_ctx_struct *ctx,
                                       avs_coap_tcp_csm_t *peer_csm,
                                       const avs_coap_borrowed_msg_t *msg);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_TCP_SIGNALING_H
