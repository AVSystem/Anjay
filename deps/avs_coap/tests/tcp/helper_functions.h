/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/coap/coap.h>

#include <avsystem/commons/avs_errno.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>

#include "./env.h"
#include "./utils.h"

static inline avs_error_t
send_request(avs_coap_ctx_t *ctx,
             const test_msg_t *msg,
             avs_coap_send_result_handler_t *send_result_handler,
             void *send_result_handler_arg) {
    assert(ctx->vtable->send_message);
    return ctx->vtable->send_message(ctx, &msg->msg.content,
                                     send_result_handler,
                                     send_result_handler_arg);
}

static inline avs_error_t send_response(avs_coap_ctx_t *ctx,
                                        const avs_coap_borrowed_msg_t *msg) {
    assert(ctx->vtable->send_message);
    return ctx->vtable->send_message(ctx, msg, NULL, NULL);
}

static inline avs_error_t
receive_message(avs_coap_ctx_t *ctx, avs_coap_borrowed_msg_t *out_request) {
    assert(ctx->vtable->receive_message);
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    assert(!coap_base->in_buffer_in_use);
    coap_base->in_buffer_in_use = true;
    uint8_t *buf = avs_shared_buffer_acquire(coap_base->in_buffer);
    avs_error_t err = ctx->vtable->receive_message(
            ctx, buf, coap_base->in_buffer->capacity, out_request);
    avs_shared_buffer_release(coap_base->in_buffer);
    coap_base->in_buffer_in_use = false;
    return err;
}

static inline avs_error_t
receive_nonrequest_message(avs_coap_ctx_t *ctx,
                           avs_coap_borrowed_msg_t *out_request) {
    avs_error_t err = receive_message(ctx, out_request);
    ASSERT_FALSE(avs_coap_code_is_request(out_request->code));
    return err;
}

static inline avs_error_t
receive_request_message(avs_coap_ctx_t *ctx,
                        avs_coap_borrowed_msg_t *out_request) {
    avs_error_t err = receive_message(ctx, out_request);
    ASSERT_TRUE(avs_coap_code_is_request(out_request->code));
    return err;
}

static inline avs_error_t
handle_incoming_packet(avs_coap_ctx_t *ctx,
                       avs_coap_server_new_async_request_handler_t *handler,
                       void *handler_args) {
    return avs_coap_async_handle_incoming_packet(ctx, handler, handler_args);
}

static inline void cancel_delivery(avs_coap_ctx_t *ctx,
                                   const avs_coap_token_t *token) {
    assert(ctx->vtable->abort_delivery);
    ctx->vtable->abort_delivery(ctx, AVS_COAP_EXCHANGE_CLIENT_REQUEST, token,
                                AVS_COAP_SEND_RESULT_CANCEL,
                                _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED));
}

static inline void ignore_request(avs_coap_ctx_t *ctx,
                                  const avs_coap_token_t *token) {
    assert(ctx->vtable->ignore_current_request);
    ctx->vtable->ignore_current_request(ctx, token);
}

static inline void
expect_sliced_recv(test_env_t *env, const test_msg_t *msg, size_t slice_pos) {
    assert(slice_pos > 0 && slice_pos < msg->size);

    avs_unit_mocksock_input(env->mocksock, msg->data, slice_pos);
    expect_has_buffered_data_check(env, false);
    avs_unit_mocksock_input(env->mocksock, msg->data + slice_pos,
                            msg->size - slice_pos);
}
