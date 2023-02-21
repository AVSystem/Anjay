/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_TCP_PENDING_REQUESTS_H
#define AVS_COAP_SRC_TCP_PENDING_REQUESTS_H

#include <avsystem/commons/avs_list.h>

#include "avs_coap_ctx.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

struct avs_coap_tcp_ctx_struct;

typedef struct avs_coap_tcp_pending_request_struct
        avs_coap_tcp_pending_request_t;

typedef enum {
    PENDING_REQUEST_STATUS_COMPLETED = 0,
    PENDING_REQUEST_STATUS_PARTIAL_CONTENT,
    PENDING_REQUEST_STATUS_IGNORE,
    PENDING_REQUEST_STATUS_FINISH_IGNORE
} avs_coap_tcp_pending_request_status_t;

typedef struct {
    avs_coap_send_result_handler_t *handle_result;
    void *handle_result_arg;
} avs_coap_tcp_response_handler_t;

AVS_LIST(avs_coap_tcp_pending_request_t) *_avs_coap_tcp_create_pending_request(
        struct avs_coap_tcp_ctx_struct *ctx,
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_requests,
        const avs_coap_token_t *token,
        avs_coap_send_result_handler_t *handler,
        void *handler_arg);

void _avs_coap_tcp_handle_pending_request(
        struct avs_coap_tcp_ctx_struct *ctx,
        const avs_coap_borrowed_msg_t *msg,
        avs_coap_tcp_pending_request_status_t result,
        avs_error_t err);

/**
 * Cancels pending request without calling user's handler.
 */
void _avs_coap_tcp_remove_pending_request(
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_request_ptr);

void _avs_coap_tcp_abort_pending_request_by_token(
        struct avs_coap_tcp_ctx_struct *ctx,
        const avs_coap_token_t *token,
        avs_coap_send_result_t result,
        avs_error_t fail_err);

void _avs_coap_tcp_cancel_all_pending_requests(
        struct avs_coap_tcp_ctx_struct *ctx);

avs_time_monotonic_t _avs_coap_tcp_fail_expired_pending_requests(
        struct avs_coap_tcp_ctx_struct *ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_TCP_PENDING_REQUESTS_H
