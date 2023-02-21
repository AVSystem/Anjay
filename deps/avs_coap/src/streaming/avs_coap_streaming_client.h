/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef COAP_SRC_STREAMING_CLIENT_H
#define COAP_SRC_STREAMING_CLIENT_H

#include <avsystem/coap/coap.h>

#include <avsystem/commons/avs_buffer.h>
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_stream_v_table.h>

#include "async/avs_coap_exchange.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    COAP_STREAM_STATE_UNINITIALIZED,
    COAP_STREAM_STATE_SENDING_REQUEST,
    COAP_STREAM_STATE_RECEIVING_RESPONSE
} coap_stream_state_t;

/**
 * NOTE: because of container_of usage, coap_stream_t can only be used as a
 * avs_coap_stream_t#coap_stream .
 */
typedef struct coap_stream {
    const avs_stream_v_table_t *vtable;

    avs_buffer_t *chunk_buffer;

    coap_stream_state_t state;
    avs_coap_exchange_id_t exchange_id;
    avs_error_t err;

    struct {
        size_t expected_offset;
        size_t expected_payload_size;
    } next_outgoing_chunk;

    avs_coap_request_header_t request_header;
    avs_coap_response_header_t response_header;

    avs_coap_ctx_t *coap_ctx;
} coap_stream_t;

extern const avs_stream_v_table_t _AVS_COAP_STREAM_VTABLE;

static inline void _avs_coap_stream_init(coap_stream_t *stream,
                                         avs_coap_ctx_t *ctx) {
    *stream = (coap_stream_t) {
        .vtable = &_AVS_COAP_STREAM_VTABLE,
        .coap_ctx = ctx
    };
}

void _avs_coap_stream_cleanup(coap_stream_t *stream);

VISIBILITY_PRIVATE_HEADER_END

#endif // COAP_SRC_STREAMING_CLIENT_H
