/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_TCP

#    include <avsystem/coap/token.h>
#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_list.h>
#    include <avsystem/commons/avs_sched.h>

#    define MODULE_NAME coap_tcp
#    include <avs_coap_x_log_config.h>

#    include "tcp/avs_coap_tcp_ctx.h"

VISIBILITY_SOURCE_BEGIN

struct avs_coap_tcp_pending_request_struct {
    avs_coap_tcp_response_handler_t handler;
    avs_coap_token_t token;
    avs_time_monotonic_t expire_time;
};

static avs_coap_send_result_handler_result_t
call_pending_request_response_handler(
        avs_coap_tcp_ctx_t *ctx,
        avs_coap_tcp_pending_request_t *pending_request,
        const avs_coap_borrowed_msg_t *response_msg,
        avs_coap_send_result_t result,
        avs_error_t err) {
    assert(pending_request);

    AVS_ASSERT(pending_request->handler.handle_result,
               "pending request with no response handler shouldn't be created");

    return pending_request->handler.handle_result(
            (avs_coap_ctx_t *) ctx, result, err, response_msg,
            pending_request->handler.handle_result_arg);
}

static inline bool
is_list_ordered_by_expire_time(AVS_LIST(avs_coap_tcp_pending_request_t) list) {
    AVS_LIST_ITERATE(list) {
        AVS_LIST(avs_coap_tcp_pending_request_t) next = list;
        AVS_LIST_ADVANCE(&next);
        if (next
                && avs_time_monotonic_before(next->expire_time,
                                             list->expire_time)) {
            return false;
        }
    }
    return true;
}

static void
insert_pending_request(AVS_LIST(avs_coap_tcp_pending_request_t) *list_ptr,
                       AVS_LIST(avs_coap_tcp_pending_request_t) req) {
    AVS_LIST_ITERATE_PTR(list_ptr) {
        if (avs_time_monotonic_before(req->expire_time,
                                      (*list_ptr)->expire_time)) {
            break;
        }
    }
    AVS_LIST_INSERT(list_ptr, req);

    AVS_ASSERT(is_list_ordered_by_expire_time(*list_ptr),
               "pending request list must be ordered by expire_time");
}

static avs_coap_tcp_pending_request_t *detach_pending_request(
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_request_ptr) {
    assert(pending_request_ptr);
    assert(*pending_request_ptr);
    return AVS_LIST_DETACH(pending_request_ptr);
}

static void finish_pending_request_with_error(
        avs_coap_tcp_ctx_t *ctx,
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_request_ptr,
        avs_coap_send_result_t result,
        avs_error_t err) {
    AVS_ASSERT(result == AVS_COAP_SEND_RESULT_CANCEL
                       || result == AVS_COAP_SEND_RESULT_FAIL,
               "use try_finish_pending_request instead");
    // Element must be detached to avoid finishing the timed-out request twice
    // when sched_run() is called in response handler.
    avs_coap_tcp_pending_request_t *detached_request =
            detach_pending_request(pending_request_ptr);
    LOG(TRACE, _("finishing pending request, token ") "%s",
        AVS_COAP_TOKEN_HEX(&detached_request->token));
    (void) call_pending_request_response_handler(ctx, detached_request, NULL,
                                                 result, err);
    AVS_LIST_DELETE(&detached_request);
}

static void try_finish_pending_request(
        avs_coap_tcp_ctx_t *ctx,
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_request_ptr,
        const avs_coap_borrowed_msg_t *msg,
        avs_coap_send_result_t result,
        avs_error_t err) {
    // Element must be detached to avoid finishing the timed-out request twice
    // when sched_run() is called in response handler.
    avs_coap_tcp_pending_request_t *detached_request =
            detach_pending_request(pending_request_ptr);
    LOG(TRACE, _("finishing pending request, token ") "%s",
        AVS_COAP_TOKEN_HEX(&detached_request->token));
    avs_coap_send_result_handler_result_t handler_result =
            call_pending_request_response_handler(ctx, detached_request, msg,
                                                  result, err);
    if (msg && result == AVS_COAP_SEND_RESULT_OK
            && handler_result != AVS_COAP_RESPONSE_ACCEPTED) {
        insert_pending_request(&ctx->pending_requests, detached_request);
    } else {
        AVS_LIST_DELETE(&detached_request);
    }
}

static AVS_LIST(avs_coap_tcp_pending_request_t) *
find_pending_request_ptr_by_token(
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_requests,
        const avs_coap_token_t *token) {
    AVS_LIST_ITERATE_PTR(pending_requests) {
        if (avs_coap_token_equal(&(*pending_requests)->token, token)) {
            return pending_requests;
        }
    }
    return NULL;
}

avs_time_monotonic_t
_avs_coap_tcp_fail_expired_pending_requests(avs_coap_tcp_ctx_t *ctx) {
    while (ctx->pending_requests
           && avs_time_monotonic_before(ctx->pending_requests->expire_time,
                                        avs_time_monotonic_now())) {
        finish_pending_request_with_error(ctx, &ctx->pending_requests,
                                          AVS_COAP_SEND_RESULT_FAIL,
                                          _avs_coap_err(AVS_COAP_ERR_TIMEOUT));
    }

    if (ctx->pending_requests) {
        return ctx->pending_requests->expire_time;
    } else {
        return AVS_TIME_MONOTONIC_INVALID;
    }
}

static inline void
refresh_timeout(avs_coap_tcp_ctx_t *ctx,
                AVS_LIST(avs_coap_tcp_pending_request_t) *req_ptr) {
    assert(ctx);
    assert(req_ptr);
    assert(*req_ptr);
    assert(avs_time_monotonic_valid((*req_ptr)->expire_time));

    (*req_ptr)->expire_time = avs_time_monotonic_add(avs_time_monotonic_now(),
                                                     ctx->request_timeout);
    insert_pending_request(&ctx->pending_requests, AVS_LIST_DETACH(req_ptr));

    _avs_coap_reschedule_retry_or_request_expired_job((avs_coap_ctx_t *) ctx,
                                                      (*req_ptr)->expire_time);
}

void _avs_coap_tcp_handle_pending_request(
        avs_coap_tcp_ctx_t *ctx,
        const avs_coap_borrowed_msg_t *msg,
        avs_coap_tcp_pending_request_status_t status,
        avs_error_t err) {
    AVS_LIST(avs_coap_tcp_pending_request_t) *pending_req_ptr =
            find_pending_request_ptr_by_token(&ctx->pending_requests,
                                              &msg->token);
    if (!pending_req_ptr) {
        LOG(DEBUG,
            _("received response does not match any known request, ignoring"));
        return;
    }

    switch (status) {
    case PENDING_REQUEST_STATUS_COMPLETED:
        try_finish_pending_request(ctx, pending_req_ptr, msg,
                                   AVS_COAP_SEND_RESULT_OK, AVS_OK);
        return;

    case PENDING_REQUEST_STATUS_PARTIAL_CONTENT:
        call_pending_request_response_handler(
                ctx, *pending_req_ptr, msg,
                AVS_COAP_SEND_RESULT_PARTIAL_CONTENT, AVS_OK);
        // Request may be canceled in call above - not directly, but by
        // calling avs_sched_run() in user's handler for example.
        pending_req_ptr =
                find_pending_request_ptr_by_token(&ctx->pending_requests,
                                                  &msg->token);
        if (pending_req_ptr) {
            refresh_timeout(ctx, pending_req_ptr);
        }
        return;

    case PENDING_REQUEST_STATUS_IGNORE:
        refresh_timeout(ctx, pending_req_ptr);
        return;

    case PENDING_REQUEST_STATUS_FINISH_IGNORE:
        finish_pending_request_with_error(ctx, pending_req_ptr,
                                          AVS_COAP_SEND_RESULT_FAIL, err);
        // error already reported; do not propagate it further
        return;
    }

    AVS_UNREACHABLE("invalid enum value");
}

AVS_LIST(avs_coap_tcp_pending_request_t) *_avs_coap_tcp_create_pending_request(
        avs_coap_tcp_ctx_t *ctx,
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_requests,
        const avs_coap_token_t *token,
        avs_coap_send_result_handler_t *handler,
        void *handler_arg) {
    AVS_LIST(avs_coap_tcp_pending_request_t) req =
            AVS_LIST_NEW_ELEMENT(avs_coap_tcp_pending_request_t);
    if (req) {
        *req = (avs_coap_tcp_pending_request_t) {
            .handler = (avs_coap_tcp_response_handler_t) {
                .handle_result = handler,
                .handle_result_arg = handler_arg
            },
            .token = *token,
            .expire_time = avs_time_monotonic_add(avs_time_monotonic_now(),
                                                  ctx->request_timeout)
        };
        insert_pending_request(pending_requests, req);
        _avs_coap_reschedule_retry_or_request_expired_job(
                (avs_coap_ctx_t *) ctx, req->expire_time);
    } else {
        LOG(DEBUG, _("failed to create pending request - out of memory"));
        return NULL;
    }

    return pending_requests;
}

void _avs_coap_tcp_remove_pending_request(
        AVS_LIST(avs_coap_tcp_pending_request_t) *pending_request_ptr) {
    assert(pending_request_ptr);
    assert(*pending_request_ptr);
    LOG(TRACE, _("removing request with token ") "%s",
        AVS_COAP_TOKEN_HEX(&(*pending_request_ptr)->token));
    AVS_LIST_DELETE(pending_request_ptr);
}

void _avs_coap_tcp_abort_pending_request_by_token(avs_coap_tcp_ctx_t *ctx,
                                                  const avs_coap_token_t *token,
                                                  avs_coap_send_result_t result,
                                                  avs_error_t fail_err) {
    AVS_ASSERT(result == AVS_COAP_SEND_RESULT_CANCEL
                       || result == AVS_COAP_SEND_RESULT_FAIL,
               "abort called with success result");
    AVS_LIST(avs_coap_tcp_pending_request_t) *pending_request_ptr =
            find_pending_request_ptr_by_token(&ctx->pending_requests, token);
    if (pending_request_ptr) {
        LOG(TRACE, _("aborting request with token ") "%s",
            AVS_COAP_TOKEN_HEX(&(*pending_request_ptr)->token));
        finish_pending_request_with_error(ctx, pending_request_ptr, result,
                                          fail_err);
    }
}

void _avs_coap_tcp_cancel_all_pending_requests(avs_coap_tcp_ctx_t *ctx) {
    while (ctx->pending_requests) {
        finish_pending_request_with_error(
                ctx, &ctx->pending_requests, AVS_COAP_SEND_RESULT_CANCEL,
                _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED));
    }
}

#endif // WITH_AVS_COAP_TCP
