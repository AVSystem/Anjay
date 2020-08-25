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

/*
 * Implementation of server-side asynchronous operations on
 * @ref avs_coap_exchange_t .
 *
 *                                 handle_request
 *                                  |    |    |
 *                                  |    |    | request for next
 *                     new request? |    |    | response block?
 *                   .--------------'    |    '----------------------.
 *                   v                   |                           |
 *          call on_new_request          |  next                     |
 *             |   |   |   |             | request                   |
 *      not    |  (E) (R)  |             |  block?                   |
 *   accepted? |           | accepted?   |                           |
 *             v           |             |                           |
 *        send empty       |             |                           |
 *         5.00 ISE        v             |                           |
 *                       create          |                           |
 *                avs_coap_exchange_t    |                           |
 *                         |             |                           |
 *                         '--------.    |                           |
 *                                  v    v                           |
 *                            call request_handler                   |
 *                              |    |    |    | response set up and |
 *       response not set up    |   (E)  (R)   | handler returned 0  |
 *       and handler returned 0 |              '--------------.      |
 *                              |                             |      |
 *          request has_more=1? | request complete?           v      v
 *                    .---------'--------.              call payload_writer
 *                    |                  |                 |          |
 *                    v                  v                 v         (E)
 *                send empty         send empty      send response
 *               2.31 Continue        5.00 ISE        with payload
 *
 * (R) - user handler returned a valid CoAP response code.
 * - the response exchange object is deleted if one exists,
 * - a response with given response code and without payload is sent.
 *
 * (E) - user handler returned an unexpected result.
 * - the response exchange object is deleted if one exists,
 * - a response with 5.00 ISE code and without payload is sent.
 *
 * Note: payload_writer cannot trigger a non-error response by returning
 * a valid CoAP code.
 *
 * Other remarks:
 * - Exchange is deleted either after sending the last response block, or
 *   if no incoming packets are matched to the exchange for at least
 *   EXCHANGE_LIFETIME (see RFC 7959, 2.4 "Using the Block2 Option")
 */

#include <avs_coap_init.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/async_server.h>
#include <avsystem/coap/option.h>

#include "avs_coap_code_utils.h"
#include "avs_coap_exchange.h"
#include "avs_coap_observe.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

#include "avs_coap_ctx.h"
#include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static avs_time_monotonic_t get_exchange_deadline(void) {
    // arbitrarily defined interval - if an exchange is not updated within
    // that time, it is considered expired
    const avs_time_duration_t MAX_EXCHANGE_UPDATE_INTERVAL =
            avs_time_duration_from_scalar(5, AVS_TIME_MIN);

    return avs_time_monotonic_add(avs_time_monotonic_now(),
                                  MAX_EXCHANGE_UPDATE_INTERVAL);
}

typedef struct {
    avs_coap_exchange_id_t exchange_id;

    const avs_coap_borrowed_msg_t *request;

    uint8_t response_code;
    const avs_coap_options_t *response_options;

    avs_coap_payload_writer_t *response_writer;
    void *response_writer_arg;

    avs_coap_server_async_request_handler_t *request_handler;
    void *request_handler_arg;

    avs_coap_notify_reliability_hint_t reliability_hint;
    avs_coap_delivery_status_handler_t *delivery_handler;
    void *delivery_handler_arg;

    const uint32_t *observe_option_value;
} server_exchange_create_args_t;

static AVS_LIST(avs_coap_exchange_t)
server_exchange_create(const server_exchange_create_args_t *args) {
    assert(avs_coap_code_is_response(args->response_code));

    // Add a few extra bytes for BLOCK1/2 options in case request/response
    // turns out to be large
    const size_t request_key_options_capacity =
            _avs_coap_options_request_key_size(&args->request->options)
            + AVS_COAP_OPT_BLOCK_MAX_SIZE;
    // We may need to add both BLOCK1 and BLOCK2 to response options
    const size_t response_options_capacity =
            args->response_options->capacity + AVS_COAP_OPT_BLOCK_MAX_SIZE * 2
            + (args->observe_option_value ? AVS_COAP_OPT_OBSERVE_MAX_SIZE : 0);

    AVS_LIST(avs_coap_exchange_t) exchange =
            (AVS_LIST(avs_coap_exchange_t)) AVS_LIST_NEW_BUFFER(
                    sizeof(avs_coap_exchange_t) + request_key_options_capacity
                    + response_options_capacity);
    if (!exchange) {
        return NULL;
    }

    char *request_key_options_buffer =
            ((char *) exchange) + sizeof(avs_coap_exchange_t);
    char *response_options_buffer =
            request_key_options_buffer + request_key_options_capacity;

    *exchange = (avs_coap_exchange_t) {
        .id = args->exchange_id,
        .write_payload = args->response_writer,
        .write_payload_arg = args->response_writer_arg,
        .code = args->response_code,
        .token = args->request->token,
        .eof_cache = {
            .empty = true
        },
        .by_type = {
            .server = {
                .request_handler = args->request_handler,
                .request_handler_arg = args->request_handler_arg,
                .reliability_hint = args->reliability_hint,
                .delivery_handler = args->delivery_handler,
                .delivery_handler_arg = args->delivery_handler_arg,
                .exchange_deadline = get_exchange_deadline(),
                .request_code = args->request->code,
                .request_key_options = _avs_coap_options_copy_request_key(
                        &args->request->options, request_key_options_buffer,
                        request_key_options_capacity),
                .request_key_options_buffer = request_key_options_buffer
            }
        },
        .options_buffer_size =
                response_options_capacity + request_key_options_capacity
    };

    // DO NOT attempt to put this within the compound literal above.
    // See T2393 or [COMPOUND-LITERAL-FAM-ASSIGNMENT-TRAP]
    exchange->options = _avs_coap_options_copy(args->response_options,
                                               response_options_buffer,
                                               response_options_capacity);

#ifdef WITH_AVS_COAP_OBSERVE
    if (args->observe_option_value) {
        avs_coap_options_remove_by_number(&exchange->options,
                                          AVS_COAP_OPTION_OBSERVE);
        if (avs_is_err(avs_coap_options_add_observe(
                    &exchange->options,
                    (uint32_t) *args->observe_option_value))) {
            AVS_UNREACHABLE();
        }
    }
#endif // WITH_AVS_COAP_OBSERVE

    return exchange;
}

#ifdef WITH_AVS_COAP_BLOCK
static bool is_last_response_block_sent(const avs_coap_exchange_t *exchange) {
    avs_coap_option_block_t block;

    // BLOCK response? Exchange is done after sending the last response block
    int result = avs_coap_options_get_block(&exchange->options, AVS_COAP_BLOCK2,
                                            &block);
    if (result == 0 && block.has_more) {
        return false;
    }

    result = avs_coap_options_get_block(&exchange->options, AVS_COAP_BLOCK1,
                                        &block);
    // Non-BLOCK response to a non-BLOCK request? Exchange is done after
    // sending the response
    if (result != 0) {
        return true;
    }

    // Non-BLOCK response to a BLOCK request? Exchange is done after sending
    // a response to last request block
    return !block.has_more;
}
#endif // WITH_AVS_COAP_BLOCK

static bool is_exchange_done(const avs_coap_exchange_t *exchange) {
    switch (exchange->by_type.server.reliability_hint) {
    case AVS_COAP_NOTIFY_PREFER_CONFIRMABLE:
        // CON response? we're not done until the delivery handler is called.
        // send_result_handler will take care of cleanup
        return false;
    case AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE:
        break;
    }

#ifdef WITH_AVS_COAP_BLOCK
    return is_last_response_block_sent(exchange);
#else  // WITH_AVS_COAP_BLOCK
    return true;
#endif // WITH_AVS_COAP_BLOCK
}

static void cancel_notification_on_error(avs_coap_ctx_t *ctx,
                                         avs_coap_observe_id_t observe_id,
                                         uint8_t response_code) {
    if (!avs_coap_code_is_success(response_code)) {
        LOG(DEBUG,
            _("Non-success notification code (") "%s" _(
                    "): cancelling observation"),
            AVS_COAP_CODE_STRING(response_code));

#ifdef WITH_AVS_COAP_OBSERVE
        _avs_coap_observe_cancel(ctx, &observe_id);
#endif // WITH_AVS_COAP_OBSERVE
        (void) ctx;
        (void) observe_id;
    }
}

static avs_coap_send_result_handler_result_t
send_result_handler(avs_coap_ctx_t *ctx,
                    avs_coap_send_result_t send_result,
                    avs_error_t fail_err,
                    const avs_coap_borrowed_msg_t *response,
                    void *exchange_) {
    AVS_ASSERT(response == NULL, "response to a response makes no sense; this "
                                 "should be detected by lower layers");
    (void) response;

    avs_coap_exchange_t *exchange = (avs_coap_exchange_t *) exchange_;
    avs_coap_exchange_id_t exchange_id = exchange->id;

    if (!_avs_coap_find_server_exchange_ptr_by_id(ctx, exchange_id)) {
        // This might happen if we sent a notification, the observation got
        // already cancelled and only now is the transport layer giving up on
        // transmissions.
        assert(send_result == AVS_COAP_SEND_RESULT_CANCEL);
        return AVS_COAP_RESPONSE_ACCEPTED;
    }

    avs_coap_server_exchange_data_t *server = &exchange->by_type.server;
    AVS_ASSERT(server->delivery_handler,
               "send_result_handler called for an exchange without "
               "user-defined delivery handler; this should not happen");

    switch (send_result) {
    case AVS_COAP_SEND_RESULT_PARTIAL_CONTENT:
    case AVS_COAP_SEND_RESULT_OK:
        AVS_ASSERT(avs_is_ok(fail_err),
                   "Error code passed for successful send");
        break;
    default:
        AVS_ASSERT(avs_is_err(fail_err),
                   "No error code passed for failed send");
        break;
    }

#ifdef WITH_AVS_COAP_BLOCK
    if (avs_is_ok(fail_err) && !is_last_response_block_sent(exchange)) {
        return AVS_COAP_RESPONSE_ACCEPTED;
    }
#endif // WITH_AVS_COAP_BLOCK

    if (avs_is_ok(fail_err)) {
        cancel_notification_on_error(ctx,
                                     (avs_coap_observe_id_t) {
                                         .token = exchange->token
                                     },
                                     exchange->code);
    }

    // exchange may have been canceled by user handler
    if (!_avs_coap_find_server_exchange_ptr_by_id(ctx, exchange_id)) {
        return AVS_COAP_RESPONSE_ACCEPTED;
    }

    server->delivery_handler(ctx, fail_err, server->delivery_handler_arg);

    // delivery status handler might have canceled the exchange as well
    AVS_LIST(avs_coap_exchange_t) *exchange_ptr =
            _avs_coap_find_server_exchange_ptr_by_id(ctx, exchange_id);
    if (!exchange_ptr) {
        return AVS_COAP_RESPONSE_ACCEPTED;
    }

    // make sure we won't call the handler again during exchange cleanup
    (*exchange_ptr)->by_type.server.delivery_handler = NULL;
    _avs_coap_server_exchange_cleanup(ctx, AVS_LIST_DETACH(exchange_ptr),
                                      fail_err);

    return AVS_COAP_RESPONSE_ACCEPTED;
}

static avs_error_t send_ise(avs_coap_ctx_t *ctx,
                            const avs_coap_token_t *token,
                            avs_coap_send_result_handler_t *result_handler,
                            void *result_handler_arg) {
    avs_coap_borrowed_msg_t msg = {
        .code = AVS_COAP_CODE_INTERNAL_SERVER_ERROR,
        .token = *token
    };

    return ctx->vtable->send_message(ctx, &msg, result_handler,
                                     result_handler_arg);
}

static avs_error_t
server_exchange_send_next_chunk(avs_coap_ctx_t *ctx,
                                AVS_LIST(avs_coap_exchange_t) *exchange_ptr) {
    AVS_ASSERT(AVS_LIST_FIND_PTR(&_avs_coap_get_base(ctx)->server_exchanges,
                                 *exchange_ptr)
                       != NULL,
               "not a started server exchange");

    avs_coap_exchange_id_t id = (*exchange_ptr)->id;
    avs_coap_send_result_handler_t *handler = NULL;
    if ((*exchange_ptr)->by_type.server.reliability_hint
            == AVS_COAP_NOTIFY_PREFER_CONFIRMABLE) {
        handler = send_result_handler;
    }

    // store token, exchange may be cancelled by user
    avs_coap_token_t token = (*exchange_ptr)->token;

    // token not changed: responses MUST echo token of the request
    avs_error_t err =
            _avs_coap_exchange_send_next_chunk(ctx, *exchange_ptr, handler,
                                               *exchange_ptr);

    if (avs_is_err(err)) {
        if (err.category == AVS_COAP_ERR_CATEGORY
                && (err.code == AVS_COAP_ERR_MESSAGE_TOO_BIG
                    || err.code == AVS_COAP_ERR_PAYLOAD_WRITER_FAILED)) {
            err = send_ise(ctx, &token, handler, *exchange_ptr);
        } else if (err.category == AVS_COAP_ERR_CATEGORY
                   && err.code == AVS_COAP_ERR_EXCHANGE_CANCELED) {
            err = send_ise(ctx, &token, NULL, NULL);
        } else {
            return err;
        }
    }

    // If the element *before* current exchange gets canceled as a result of
    // calling user-defined payload_writer, exchange_ptr is not valid any more
    // even though _avs_coap_exchange_send_next_chunk does not detect it was
    // canceled - because it wasn't
    exchange_ptr = _avs_coap_find_server_exchange_ptr_by_id(ctx, id);

    if (exchange_ptr && is_exchange_done(*exchange_ptr)) {
        _avs_coap_server_exchange_cleanup(ctx, AVS_LIST_DETACH(exchange_ptr),
                                          AVS_OK);
    }
    return err;
}

avs_time_monotonic_t
_avs_coap_async_server_abort_timedout_exchanges(avs_coap_ctx_t *ctx) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    while (coap_base->server_exchanges
           && avs_time_monotonic_before(coap_base->server_exchanges->by_type
                                                .server.exchange_deadline,
                                        avs_time_monotonic_now())) {
        LOG(DEBUG, _("exchange ") "%s" _(" timed out"),
            AVS_UINT64_AS_STRING(coap_base->server_exchanges->id.value));

        _avs_coap_server_exchange_cleanup(
                ctx, AVS_LIST_DETACH(&coap_base->server_exchanges),
                _avs_coap_err(AVS_COAP_ERR_TIMEOUT));
    }

    if (coap_base->server_exchanges) {
        return coap_base->server_exchanges->by_type.server.exchange_deadline;
    } else {
        return AVS_TIME_MONOTONIC_INVALID;
    }
}

static AVS_LIST(avs_coap_exchange_t) *
insert_server_exchange(avs_coap_ctx_t *ctx, avs_coap_exchange_t *new_exchange) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    AVS_LIST(avs_coap_exchange_t) *insert_ptr = &coap_base->server_exchanges;
    AVS_LIST_ITERATE_PTR(insert_ptr) {
        if (avs_time_monotonic_before(
                    new_exchange->by_type.server.exchange_deadline,
                    (*insert_ptr)->by_type.server.exchange_deadline)) {
            break;
        }
    }

    AVS_LIST_INSERT(insert_ptr, new_exchange);
    _avs_coap_reschedule_retry_or_request_expired_job(
            ctx, coap_base->server_exchanges->by_type.server.exchange_deadline);

    return insert_ptr;
}

static AVS_LIST(avs_coap_exchange_t) *
refresh_exchange(avs_coap_ctx_t *ctx,
                 AVS_LIST(avs_coap_exchange_t) *exchange_ptr) {
    (*exchange_ptr)->by_type.server.exchange_deadline = get_exchange_deadline();
    return insert_server_exchange(ctx, AVS_LIST_DETACH(exchange_ptr));
}

avs_coap_exchange_id_t avs_coap_server_accept_async_request(
        avs_coap_server_ctx_t *ctx,
        avs_coap_server_async_request_handler_t *request_handler,
        void *request_handler_arg) {
    if (!ctx) {
        LOG(ERROR, _("server_ctx must not be NULL"));
        return AVS_COAP_EXCHANGE_ID_INVALID;
    }
    if (!request_handler) {
        LOG(ERROR, _("request_handler must not be NULL"));
        return AVS_COAP_EXCHANGE_ID_INVALID;
    }
    if (avs_coap_exchange_id_valid(ctx->exchange_id)) {
        LOG(ERROR, _("cannot accept a request twice"));
        return AVS_COAP_EXCHANGE_ID_INVALID;
    }

    // ID assigned here will be kept through the _avs_coap_exchange_start call
    // to allow referring to this exchange for its whole lifetime
    avs_coap_exchange_id_t id = _avs_coap_generate_exchange_id(ctx->coap_ctx);

    // NOTE: we create a temporary exchange with empty response options,
    // because we don't know how much space we'll need for them - it will
    // only become known after the user calls
    // @ref avs_coap_async_server_setup_response . We still need some space
    // for storing request options in order to match future requests that are
    // a part of the same exchange.
    //
    // Each call to @ref server_exchange_send_next_chunk sets up a CoAP message
    // based on current contents of the exchange, so we use 2.31 Continue code
    // here to ensure exactly that kind of response.
    avs_coap_options_t empty_options = avs_coap_options_create_empty(NULL, 0);

    server_exchange_create_args_t exchange_create_args = {
        .exchange_id = id,
        .request = ctx->request,
        .response_code = AVS_COAP_CODE_CONTINUE,
        .response_options = &empty_options,
        .request_handler = request_handler,
        .request_handler_arg = request_handler_arg
    };
    AVS_LIST(avs_coap_exchange_t) response_exchange =
            server_exchange_create(&exchange_create_args);
    if (!response_exchange) {
        return AVS_COAP_EXCHANGE_ID_INVALID;
    }

    ctx->exchange_id = id;
    insert_server_exchange(ctx->coap_ctx, response_exchange);

    return ctx->exchange_id;
}

bool _avs_coap_response_header_valid(const avs_coap_response_header_t *res) {
    if (!avs_coap_code_is_response(res->code)) {
        LOG(WARNING, _("non-response code ") "%s" _(" used in response header"),
            AVS_COAP_CODE_STRING(res->code));
        return false;
    }
    if (res->code == AVS_COAP_CODE_CONTINUE) {
        LOG(WARNING,
            "%s" _(" responses are handled internally and not allowed "
                   "in avs_coap_server_setup_async_response"),
            AVS_COAP_CODE_STRING(res->code));
        return false;
    }

    return _avs_coap_options_valid(&res->options);
}

avs_error_t
avs_coap_server_setup_async_response(avs_coap_request_ctx_t *ctx,
                                     const avs_coap_response_header_t *response,
                                     avs_coap_payload_writer_t *response_writer,
                                     void *response_writer_arg) {
    if (!ctx) {
        LOG(ERROR, _("no request to respond to"));
        return avs_errno(AVS_EINVAL);
    }
    if (!response) {
        LOG(ERROR, _("response must be provided"));
        return avs_errno(AVS_EINVAL);
    }

    avs_coap_ctx_t *coap_ctx = _avs_coap_ctx_from_request_ctx(ctx);
    AVS_LIST(avs_coap_exchange_t) *response_exchange_ptr =
            _avs_coap_find_server_exchange_ptr_by_id(coap_ctx,
                                                     ctx->exchange_id);
    if (!response_exchange_ptr) {
        LOG(ERROR, _("invalid exchange ID: ") "%s",
            AVS_UINT64_AS_STRING(ctx->exchange_id.value));
        return avs_errno(AVS_EINVAL);
    }

    if (!_avs_coap_response_header_valid(response)) {
        return avs_errno(AVS_EINVAL);
    }

    if (!ctx->observe_established
            && _avs_coap_option_exists(&response->options,
                                       AVS_COAP_OPTION_OBSERVE)) {
        LOG(ERROR,
            _("Observe option in response, but observe is not established"));
        return avs_errno(AVS_EINVAL);
    }

    // Now that we actually know what options are included in the response,
    // recreate the exchange object with the same ID.
    const uint32_t *observe_option_value =
            ctx->observe_established
                    ? &(uint32_t) { _avs_coap_observe_initial_option_value() }
                    : NULL;
    server_exchange_create_args_t exchange_create_args = {
        .exchange_id = ctx->exchange_id,
        .request = &ctx->request,
        .response_code = response->code,
        .response_options = &response->options,
        .response_writer = response_writer,
        .response_writer_arg = response_writer_arg,
        .request_handler =
                (*response_exchange_ptr)->by_type.server.request_handler,
        .request_handler_arg =
                (*response_exchange_ptr)->by_type.server.request_handler_arg,
        .observe_option_value = observe_option_value
    };
    AVS_LIST(avs_coap_exchange_t) new_exchange =
            server_exchange_create(&exchange_create_args);

    if (!new_exchange) {
        return avs_errno(AVS_ENOMEM);
    }

    // NOTE: it might seem tempting to delete the exchange before recreating
    // it, but that is problematic. If we delete the old exchange object, and
    // fail to create a new one, the caller doesn't know if this function
    // failed because of invalid input, or because of out-of-memory condition,
    // so it is impossible for them to know whether to free
    // request_handler_arg or not. And if we call request_handler with CLEANUP
    // status, this results in a recursive call, and the original caller will
    // have to be wary of use-after-free.
    //
    // Deleting the old exchange only if we're sure we have a new copy seems
    // the most robust solution.
    AVS_LIST_DELETE(response_exchange_ptr);
    insert_server_exchange(coap_ctx, new_exchange);

    ctx->response_setup = true;
    return AVS_OK;
}

#ifdef WITH_AVS_COAP_BLOCK
static int get_request_block_option(const avs_coap_borrowed_msg_t *request,
                                    avs_coap_option_block_t *out_block1) {
    switch (avs_coap_options_get_block(&request->options, AVS_COAP_BLOCK1,
                                       out_block1)) {
    case 0:
        return 0;
    case AVS_COAP_OPTION_MISSING:
        return -1;
    default:
        AVS_UNREACHABLE("malformed option got through packet validation");
        return -1;
    }
}

static size_t
get_request_payload_offset(const avs_coap_borrowed_msg_t *request) {
    avs_coap_option_block_t block1;
    // request->payload_offset refers to payload offset in a single CoAP message
    // payload if it's received in chunks, which can happen if CoAP/TCP is used.
    if (get_request_block_option(request, &block1)) {
        return request->payload_offset;
    }
    return block1.seq_num * block1.size + request->payload_offset;
}
#else  // WITH_AVS_COAP_BLOCK
static size_t
get_request_payload_offset(const avs_coap_borrowed_msg_t *request) {
    return request->payload_offset;
}
#endif // WITH_AVS_COAP_BLOCK

static bool
is_request_message_finished(const avs_coap_borrowed_msg_t *request) {
    return request->payload_offset + request->payload_size
           == request->total_payload_size;
}

static bool is_entire_request_finished(const avs_coap_borrowed_msg_t *request) {
    if (!is_request_message_finished(request)) {
        return false;
    }
#ifdef WITH_AVS_COAP_BLOCK
    avs_coap_option_block_t block1;
    if (get_request_block_option(request, &block1)) {
        return true;
    }
    return !block1.has_more;
#else  // WITH_AVS_COAP_BLOCK
    return true;
#endif // WITH_AVS_COAP_BLOCK
}

static uint8_t response_code_from_result(int result) {
    static const uint8_t DEFAULT_CODE = AVS_COAP_CODE_INTERNAL_SERVER_ERROR;

    if (_avs_coap_code_in_range(result)) {
        if (avs_coap_code_is_response((uint8_t) result)) {
            return (uint8_t) result;
        }

        LOG(WARNING,
            "%s" _(" is not a valid response code, sending ") "%s" _(
                    " instead"),
            AVS_COAP_CODE_STRING((uint8_t) result),
            AVS_COAP_CODE_STRING(DEFAULT_CODE));
    } else {
        LOG(DEBUG,
            "%d" _(" does not represent a correct CoAP code, sending ") "%s" _(
                    " instead"),
            result, AVS_COAP_CODE_STRING(DEFAULT_CODE));
    }

    return DEFAULT_CODE;
}

static avs_error_t send_empty_response(avs_coap_ctx_t *ctx,
                                       const avs_coap_token_t *request_token,
                                       uint8_t code) {
    avs_coap_borrowed_msg_t msg = {
        .code = code,
        .token = *request_token
    };
    return ctx->vtable->send_message(ctx, &msg, NULL, NULL);
}

typedef enum {
    OBSERVE_MISSING = -1,
    OBSERVE_REGISTER = 0,
    OBSERVE_DEREGISTER = 1
} observe_value_t;

#ifdef WITH_AVS_COAP_OBSERVE
static observe_value_t
get_observe_option(const avs_coap_borrowed_msg_t *request) {
    uint32_t observe_value;
    if (avs_coap_options_get_observe(&request->options, &observe_value) != 0) {
        return OBSERVE_MISSING;
    }

    switch (observe_value) {
    case 0:
        return OBSERVE_REGISTER;
    case 1:
        return OBSERVE_DEREGISTER;
    default:
        LOG(DEBUG, _("invalid Observe value: ") "%" PRIu32, observe_value);
        return OBSERVE_MISSING;
    }
}

static observe_value_t
handle_observe_option(avs_coap_ctx_t *ctx,
                      const avs_coap_borrowed_msg_t *request) {
    observe_value_t observe_value = get_observe_option(request);
    if (observe_value == OBSERVE_MISSING) {
        return OBSERVE_MISSING;
    }

    if (request->payload_offset == 0) {
        // Cancel observe, if already exists. This ensure that the user
        // request-handler always is in a position where an old observation
        // state is removed.
        //
        // Make sure to only do this for the first chunk of the message, to
        // not cancel observe multiple times unnecessarily.
        //
        // TODO: this should probably only be called for the first request
        // block; currently this is repeated for every single one.
        const avs_coap_observe_id_t observe_id = {
            .token = request->token
        };
        _avs_coap_observe_cancel(ctx, &observe_id);
    }
    return observe_value;
}
#endif // WITH_AVS_COAP_OBSERVE

int _avs_coap_async_incoming_packet_call_request_handler(
        avs_coap_ctx_t *ctx, avs_coap_exchange_t *exchange) {
    assert(ctx);
    assert(exchange->by_type.server.request_handler);
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    assert(avs_coap_exchange_id_equal(coap_base->request_ctx.exchange_id,
                                      exchange->id));

    const avs_coap_observe_id_t *observe_id = NULL;
#ifdef WITH_AVS_COAP_OBSERVE
    if (handle_observe_option(ctx, &coap_base->request_ctx.request)
            == OBSERVE_REGISTER) {
        // avs_coap_observe_id_t is basically just a blessed
        // avs_coap_token_t, so let's pretend the token is actually an
        // observe ID, it should be safe
        AVS_STATIC_ASSERT(sizeof(coap_base->request_ctx.request.token)
                                  == sizeof(*observe_id),
                          observe_id_and_token_have_the_same_size);
        observe_id = AVS_CONTAINER_OF(&coap_base->request_ctx.request.token,
                                      const avs_coap_observe_id_t, token);
    }
#endif // WITH_AVS_COAP_OBSERVE
    bool entire_request_finished =
            is_entire_request_finished(&coap_base->request_ctx.request);
    avs_coap_server_request_state_t state =
            entire_request_finished ? AVS_COAP_SERVER_REQUEST_RECEIVED
                                    : AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT;

    const avs_coap_server_async_request_t async_request = {
        .header = {
            .code = coap_base->request_ctx.request.code,
            .options = coap_base->request_ctx.request.options,
        },
        .payload_offset =
                get_request_payload_offset(&coap_base->request_ctx.request),
        .payload = coap_base->request_ctx.request.payload,
        .payload_size = coap_base->request_ctx.request.payload_size
    };

    LOG(DEBUG, _("exchange ") "%s" _(": request_handler, ") "%s",
        AVS_UINT64_AS_STRING(exchange->id.value),
        entire_request_finished ? "full request" : "partial content");

    return exchange->by_type.server.request_handler(
            &coap_base->request_ctx, exchange->id, state, &async_request,
            observe_id, exchange->by_type.server.request_handler_arg);
}

static bool matching_request_options(uint16_t opt_number) {
    return _avs_coap_option_is_critical(opt_number)
           || opt_number == AVS_COAP_OPTION_CONTENT_FORMAT;
}

static bool request_matches_exchange(const avs_coap_borrowed_msg_t *request,
                                     const avs_coap_exchange_t *exchange) {
    if (exchange->by_type.server.request_code != request->code) {
        LOG(TRACE, _("looking for CoAP code ") "%s" _(", got ") "%s",
            AVS_COAP_CODE_STRING(exchange->by_type.server.request_code),
            AVS_COAP_CODE_STRING(request->code));
        return false;
    }
    /* Check if token, critical options, and content-format match, as the
     * request may be the non-blockwise continuation [TCP] */
    if (request->payload_offset > 0
            && avs_coap_token_equal(&request->token, &exchange->token)
            && _avs_coap_selected_options_equal(
                       &request->options,
                       &exchange->by_type.server.request_key_options,
                       matching_request_options)) {
        return true;
    }
#ifdef WITH_AVS_COAP_BLOCK
    /* If that failed, the request may still be a blockwise transfer
     * continuation */
    return _avs_coap_options_is_sequential_block_request(
            &exchange->options,
            &exchange->by_type.server.request_key_options,
            &request->options,
            exchange->by_type.server.expected_request_payload_offset);
#else  // WITH_AVS_COAP_BLOCK
    return false;
#endif // WITH_AVS_COAP_BLOCK
}

static AVS_LIST(avs_coap_exchange_t) *
find_existing_response_exchange_ptr(avs_coap_ctx_t *ctx,
                                    const avs_coap_borrowed_msg_t *request) {
    AVS_LIST(avs_coap_exchange_t) *it;
    AVS_LIST_FOREACH_PTR(it, &_avs_coap_get_base(ctx)->server_exchanges) {
        if (avs_coap_code_is_response((*it)->code)
                && request_matches_exchange(request, *it)) {
            return it;
        }
    }
    return NULL;
}

#ifdef WITH_AVS_COAP_BLOCK
static void
update_exchange_block1_option(avs_coap_exchange_t *exchange,
                              const avs_coap_borrowed_msg_t *request,
                              bool response_set_up) {
    // Echo request BLOCK1 option, if any. Remove it if not present.
    avs_coap_option_block_t block1;
    int result = get_request_block_option(request, &block1);
    avs_coap_options_remove_by_number(&exchange->options,
                                      AVS_COAP_OPTION_BLOCK1);

    if (result) {
        return;
    }

    // Set has_more flag to false if response is set up. If user already started
    // to respond to the message, then probably more request payload chunks
    // aren't required.
    block1.has_more = !response_set_up;

    if (avs_is_err(avs_coap_options_add_block(&exchange->options, &block1))) {
        AVS_UNREACHABLE("options buffer too small");
    }
    exchange->by_type.server.expected_request_payload_offset +=
            request->total_payload_size;
}

static void
update_exchange_block2_option(avs_coap_exchange_t *exchange,
                              const avs_coap_borrowed_msg_t *request) {
    avs_coap_option_block_t block2;
    int result = avs_coap_options_get_block(&request->options, AVS_COAP_BLOCK2,
                                            &block2);
    avs_coap_options_remove_by_number(&exchange->options,
                                      AVS_COAP_OPTION_BLOCK2);

    if (result == AVS_COAP_OPTION_MISSING) {
        return;
    }

    // this will be set to false later when EOF is encountered when calling
    // payload_writer
    block2.has_more = true;

    if (avs_is_err(avs_coap_options_add_block(&exchange->options, &block2))) {
        AVS_UNREACHABLE("options buffer too small");
    }
}
#endif // WITH_AVS_COAP_BLOCK

static avs_error_t
setup_response_from_nonzero_result(avs_coap_request_ctx_t *request_ctx,
                                   int result) {
    return avs_coap_server_setup_async_response(
            request_ctx,
            &(avs_coap_response_header_t) {
                .code = response_code_from_result(result)
            },
            NULL, NULL);
}

static int validate_request_exchange_state(avs_coap_ctx_t *ctx) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    if (!_avs_coap_find_server_exchange_ptr_by_id(
                ctx, coap_base->request_ctx.exchange_id)) {
        LOG(DEBUG, _("exchange ") "%s" _(" canceled by user handler"),
            AVS_UINT64_AS_STRING(coap_base->request_ctx.exchange_id.value));
        return AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    if (!coap_base->request_ctx.response_setup
            && is_entire_request_finished(&coap_base->request_ctx.request)) {
        LOG(DEBUG,
            _("request ") "%s" _(" finished, but response still not set up and "
                                 "request handler returned 0"),
            AVS_UINT64_AS_STRING(coap_base->request_ctx.exchange_id.value));
        return AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
    }

    return 0;
}

static avs_error_t continue_request_exchange(avs_coap_ctx_t *ctx) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    AVS_LIST(avs_coap_exchange_t) *exchange_ptr =
            _avs_coap_find_server_exchange_ptr_by_id(
                    ctx, coap_base->request_ctx.exchange_id);
    AVS_ASSERT(exchange_ptr, "exchange cancellation by user handler should be "
                             "detected in validate_exchange_state");

#ifdef WITH_AVS_COAP_BLOCK
    update_exchange_block1_option(*exchange_ptr,
                                  &coap_base->request_ctx.request,
                                  coap_base->request_ctx.response_setup);
    update_exchange_block2_option(*exchange_ptr,
                                  &coap_base->request_ctx.request);
#endif // WITH_AVS_COAP_BLOCK
    return server_exchange_send_next_chunk(ctx, exchange_ptr);
}

static void cleanup_exchange_by_id(avs_coap_ctx_t *ctx,
                                   const avs_coap_exchange_id_t exchange_id,
                                   avs_error_t err) {
    AVS_LIST(avs_coap_exchange_t) *exchange_ptr =
            _avs_coap_find_server_exchange_ptr_by_id(ctx, exchange_id);
    if (exchange_ptr) {
        _avs_coap_server_exchange_cleanup(ctx, AVS_LIST_DETACH(exchange_ptr),
                                          err);
    }
}

static avs_error_t
send_response_to_request_chunk(avs_coap_ctx_t *ctx,
                               int exchange_request_handler_result) {
    int result = exchange_request_handler_result;
    avs_error_t err = AVS_OK;

    if (!result) {
        result = validate_request_exchange_state(ctx);
    }

    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    if (result) {
        err = setup_response_from_nonzero_result(&coap_base->request_ctx,
                                                 result);
    }

    if (avs_is_err(err)) {
        // setup_response_from_nonzero_result() failed because exchange was
        // canceled or another error occured during setting up a response.
        // Exchange cannot be continued.
        cleanup_exchange_by_id(ctx, coap_base->request_ctx.exchange_id, err);
        return send_empty_response(ctx, &coap_base->request_ctx.request.token,
                                   AVS_COAP_CODE_INTERNAL_SERVER_ERROR);
    }

    if (is_request_message_finished(&coap_base->request_ctx.request)
            || coap_base->request_ctx.response_setup) {
        return continue_request_exchange(ctx);
    }

    AVS_ASSERT(!is_entire_request_finished(&coap_base->request_ctx.request),
               "finished request without response setup is supposed to be "
               "handled by validate_exchange_state");
    // There will be more payload for this message.
    return AVS_OK;
}

typedef struct {
    avs_coap_server_new_async_request_handler_t *on_new_request;
    void *on_new_request_arg;
} user_defined_request_handler_t;

static avs_error_t
handle_new_request(avs_coap_ctx_t *ctx,
                   const avs_coap_borrowed_msg_t *request,
                   user_defined_request_handler_t *user_handler,
                   AVS_LIST(avs_coap_exchange_t) *out_exchange) {
    if (!user_handler->on_new_request) {
        LOG(ERROR, _("rejecting incoming ") "%s" _(": on_new_request NULL"),
            AVS_COAP_CODE_STRING(request->code));
        return send_empty_response(ctx, &request->token,
                                   AVS_COAP_CODE_INTERNAL_SERVER_ERROR);
    }

    avs_coap_server_ctx_t server_ctx = {
        .coap_ctx = ctx,
        .request = request,
        .exchange_id = AVS_COAP_EXCHANGE_ID_INVALID
    };
    const avs_coap_request_header_t request_header = {
        .code = request->code,
        .options = request->options
    };

    // NOTE: this function should never be called if any error happens on our
    // way here, so we pass 0 as the error code.
    int result = user_handler->on_new_request(&server_ctx, &request_header,
                                              user_handler->on_new_request_arg);
    if (result) {
        avs_coap_exchange_cancel(ctx, server_ctx.exchange_id);
        return send_empty_response(ctx, &request->token,
                                   response_code_from_result(result));
    }

    if (!avs_coap_exchange_id_valid(server_ctx.exchange_id)) {
        LOG(WARNING, _("on_new_request suceeded, but ") "%s" _(" not accepted"),
            AVS_COAP_CODE_STRING(request->code));

        return send_empty_response(ctx, &request->token,
                                   AVS_COAP_CODE_INTERNAL_SERVER_ERROR);
    }

    if (!(*out_exchange = _avs_coap_find_server_exchange_by_id(
                  ctx, server_ctx.exchange_id))) {
        LOG(DEBUG,
            _("on_new_request handler canceled exchange ") "%s" _(
                    " immediately after accepting it"),
            AVS_UINT64_AS_STRING(server_ctx.exchange_id.value));
    }
    return AVS_OK;
}

static avs_error_t handle_request(avs_coap_ctx_t *ctx,
                                  const avs_coap_borrowed_msg_t *request,
                                  user_defined_request_handler_t *user_handler,
                                  AVS_LIST(avs_coap_exchange_t) *out_exchange) {
    assert(avs_coap_code_is_request(request->code));
    AVS_ASSERT(request->payload_offset + request->payload_size
                       <= request->total_payload_size,
               "bug: payload_offset + payload_size > total_payload_size");

    AVS_LIST(avs_coap_exchange_t) *existing_exchange_ptr =
            find_existing_response_exchange_ptr(ctx, request);

    if (!existing_exchange_ptr) {
        return handle_new_request(ctx, request, user_handler, out_exchange);
    }

    // Getting here means that incoming request was successfully matched to an
    // existing response, and that it either contains more request payload, or
    // requests more response payload.
    (*existing_exchange_ptr)->token = request->token;
    existing_exchange_ptr = refresh_exchange(ctx, existing_exchange_ptr);

    avs_coap_server_exchange_data_t *server_data =
            &(*existing_exchange_ptr)->by_type.server;

    // scan-build for some reason assumes server_data may be NULL
    assert(server_data);
    assert(server_data->request_key_options.capacity
           >= _avs_coap_options_request_key_size(&request->options));

    server_data->request_key_options = _avs_coap_options_copy_request_key(
            &request->options,
            server_data->request_key_options.begin,
            server_data->request_key_options.capacity);

#ifdef WITH_AVS_COAP_BLOCK
    // If the user didn't setup a final response yet, exchange code is set to
    // Continue as initialized in @ref avs_coap_server_accept_async_request .
    // That means we haven't finished receiving request payload yet, and the
    // user needs more to decide what to do with the request.
    if ((*existing_exchange_ptr)->code == AVS_COAP_CODE_CONTINUE) {
        *out_exchange = *existing_exchange_ptr;
        return AVS_OK;
    } else {
        // If exchange code is not Continue, it means the user called
        // @ref avs_coap_server_setup_async_response , and we're currently
        // sending response blocks (second or later BLOCK2), so we can drop
        // BLOCK1 option completely.
        avs_coap_options_remove_by_number(&(*existing_exchange_ptr)->options,
                                          AVS_COAP_OPTION_BLOCK1);
        update_exchange_block2_option(*existing_exchange_ptr, request);
        // In case of Observe notifications, only the first block
        // is supposed to have the Observe option; see RFC 7959,
        // Figure 12: "Observe Sequence with Block-Wise Response"
        avs_coap_options_remove_by_number(&(*existing_exchange_ptr)->options,
                                          AVS_COAP_OPTION_OBSERVE);
        return server_exchange_send_next_chunk(ctx, existing_exchange_ptr);
    }
#else  // WITH_AVS_COAP_BLOCK
    *out_exchange = *existing_exchange_ptr;
    return AVS_OK;
#endif // WITH_AVS_COAP_BLOCK
}

avs_error_t _avs_coap_async_incoming_packet_handle_single(
        avs_coap_ctx_t *ctx,
        uint8_t *in_buffer,
        size_t in_buffer_size,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg,
        AVS_LIST(struct avs_coap_exchange) *out_exchange) {
    assert(ctx);
    assert(ctx->vtable);
    assert(ctx->vtable->receive_message);

    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);

    memset(&coap_base->request_ctx, 0, sizeof(coap_base->request_ctx));
    coap_base->request_ctx.coap_ctx = ctx;
    avs_error_t err =
            ctx->vtable->receive_message(ctx, in_buffer, in_buffer_size,
                                         &coap_base->request_ctx.request);
    if (avs_is_ok(err)
            && avs_coap_code_is_request(coap_base->request_ctx.request.code)) {
        assert(coap_base->request_ctx.request.payload_size > 0
               || coap_base->request_ctx.request.payload_offset
                          == coap_base->request_ctx.request.total_payload_size);
        avs_coap_exchange_t *exchange = NULL;
        user_defined_request_handler_t args = {
            .on_new_request = on_new_request,
            .on_new_request_arg = on_new_request_arg
        };
        err = handle_request(ctx, &coap_base->request_ctx.request, &args,
                             &exchange);
        if (exchange) {
            assert(avs_is_ok(err));
            coap_base->request_ctx.exchange_id = exchange->id;
            *out_exchange = exchange;
            return AVS_OK;
        }
    }

    *out_exchange = NULL;
    memset(&coap_base->request_ctx, 0, sizeof(coap_base->request_ctx));
    return err;
}

avs_error_t _avs_coap_async_incoming_packet_send_response(avs_coap_ctx_t *ctx,
                                                          int call_result) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    assert(avs_coap_exchange_id_valid(coap_base->request_ctx.exchange_id));
    avs_error_t err = send_response_to_request_chunk(ctx, call_result);
    memset(&coap_base->request_ctx, 0, sizeof(coap_base->request_ctx));
    return err;
}

avs_error_t _avs_coap_async_incoming_packet_simple_handle_single(
        avs_coap_ctx_t *ctx,
        uint8_t *in_buffer,
        size_t in_buffer_size,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg) {
    avs_coap_exchange_t *exchange;
    avs_error_t err = _avs_coap_async_incoming_packet_handle_single(
            ctx, in_buffer, in_buffer_size, on_new_request, on_new_request_arg,
            &exchange);
    if (exchange) {
        assert(avs_is_ok(err));
        err = _avs_coap_async_incoming_packet_send_response(
                ctx, _avs_coap_async_incoming_packet_call_request_handler(
                             ctx, exchange));
    }
    return err;
}

avs_error_t
_avs_coap_async_incoming_packet_handle_while_possible_without_blocking(
        avs_coap_ctx_t *ctx,
        uint8_t *in_buffer,
        size_t in_buffer_size,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg) {
    avs_error_t err;
    avs_net_socket_t *socket = _avs_coap_get_base(ctx)->socket;
    avs_net_socket_opt_value_t socket_timeout;
    if (avs_is_err((err = avs_net_socket_get_opt(
                            socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                            &socket_timeout)))
            || avs_is_err((err = avs_net_socket_set_opt(
                                   socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                   (avs_net_socket_opt_value_t) {
                                       .recv_timeout = AVS_TIME_DURATION_ZERO
                                   })))) {
        return err;
    }
    while (avs_is_ok(err)) {
        err = _avs_coap_async_incoming_packet_simple_handle_single(
                ctx, in_buffer, in_buffer_size, on_new_request,
                on_new_request_arg);
    }
    if (err.category == AVS_ERRNO_CATEGORY && err.code == AVS_ETIMEDOUT) {
        // No more data possible to receive in a non-blocking way,
        // it's not an error
        err = AVS_OK;
    }
    avs_error_t restore_err =
            avs_net_socket_set_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                   socket_timeout);
    return avs_is_ok(err) ? restore_err : err;
}

avs_error_t avs_coap_async_handle_incoming_packet(
        avs_coap_ctx_t *ctx,
        avs_coap_server_new_async_request_handler_t *on_new_request,
        void *on_new_request_arg) {
    uint8_t *acquired_in_buffer;
    size_t acquired_in_buffer_size;
    avs_error_t err = _avs_coap_in_buffer_acquire(ctx, &acquired_in_buffer,
                                                  &acquired_in_buffer_size);
    if (avs_is_err(err)) {
        return err;
    }

    err = _avs_coap_async_incoming_packet_handle_while_possible_without_blocking(
            ctx, acquired_in_buffer, acquired_in_buffer_size, on_new_request,
            on_new_request_arg);
    _avs_coap_in_buffer_release(ctx);
    return err;
}

void _avs_coap_server_exchange_cleanup(avs_coap_ctx_t *ctx,
                                       avs_coap_exchange_t *exchange,
                                       avs_error_t err) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    AVS_ASSERT(!AVS_LIST_FIND_PTR(&coap_base->server_exchanges, exchange),
               "exchange must be detached");
    assert(avs_coap_code_is_response(exchange->code));

    avs_coap_server_exchange_data_t *server = &exchange->by_type.server;

    if (server->request_handler) {
        LOG(DEBUG, _("exchange ") "%s" _(": request_handler, cleanup"),
            AVS_UINT64_AS_STRING(exchange->id.value));

        server->request_handler(NULL, exchange->id,
                                AVS_COAP_SERVER_REQUEST_CLEANUP, NULL, NULL,
                                server->request_handler_arg);
    } else {
        avs_coap_send_result_t send_result;
        avs_error_t abort_err;
        if (avs_is_ok(err)) {
            send_result = AVS_COAP_SEND_RESULT_OK;
            abort_err = AVS_OK;
        } else if (err.category == AVS_COAP_ERR_CATEGORY
                   && err.code == AVS_COAP_ERR_EXCHANGE_CANCELED) {
            send_result = AVS_COAP_SEND_RESULT_CANCEL;
            abort_err = AVS_OK;
        } else {
            send_result = AVS_COAP_SEND_RESULT_FAIL;
            abort_err = err;
        }
        // Notify exchanges don't have a request handler
        ctx->vtable->abort_delivery(ctx, AVS_COAP_EXCHANGE_SERVER_NOTIFICATION,
                                    &exchange->token, send_result, abort_err);
    }

    if (server->delivery_handler) {
        server->delivery_handler(ctx, err, server->delivery_handler_arg);
    }

    AVS_LIST_DELETE(&exchange);
    if (coap_base->server_exchanges) {
        _avs_coap_reschedule_retry_or_request_expired_job(
                ctx,
                coap_base->server_exchanges->by_type.server.exchange_deadline);
    }
}

#ifdef WITH_AVS_COAP_OBSERVE
avs_error_t
avs_coap_observe_async_start(avs_coap_request_ctx_t *ctx,
                             avs_coap_observe_id_t id,
                             avs_coap_observe_cancel_handler_t *cancel_handler,
                             void *handler_arg) {
    const avs_coap_request_header_t request_header = {
        .code = ctx->request.code,
        .options = ctx->request.options
    };

    avs_error_t err =
            avs_coap_observe_start(_avs_coap_ctx_from_request_ctx(ctx), id,
                                   &request_header, cancel_handler,
                                   handler_arg);

    ctx->observe_established = avs_is_ok(err);
    return err;
}

avs_error_t
avs_coap_notify_async(avs_coap_ctx_t *ctx,
                      avs_coap_exchange_id_t *out_exchange_id,
                      avs_coap_observe_id_t observe_id,
                      const avs_coap_response_header_t *response_header,
                      avs_coap_notify_reliability_hint_t reliability_hint,
                      avs_coap_payload_writer_t *write_payload,
                      void *write_payload_arg,
                      avs_coap_delivery_status_handler_t *delivery_handler,
                      void *delivery_handler_arg) {
    if (!avs_coap_code_is_response(response_header->code)) {
        LOG(ERROR, "%s" _(" is not a valid response code"),
            AVS_COAP_CODE_STRING(response_header->code));
        avs_errno(AVS_EINVAL);
    }

    if (!delivery_handler
            && reliability_hint != AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE) {
        LOG(ERROR,
            _("delivery_handler is mandatory for reliable notifications"));
        avs_errno(AVS_EINVAL);
    }

    avs_coap_observe_notify_t notify;

    avs_error_t err = _avs_coap_observe_setup_notify(ctx, &observe_id, &notify);
    if (avs_is_err(err)) {
        return err;
    }

    // Create a server exchange in a "receiving request payload finished,
    // response not sent yet" state
    server_exchange_create_args_t exchange_create_args = {
        .exchange_id = _avs_coap_generate_exchange_id(ctx),
        // setup a fake request similar to the original Observe
        .request = &(avs_coap_borrowed_msg_t) {
            .code = notify.request_code,
            .token = observe_id.token,
            .options = notify.request_key
        },
        .response_code = response_header->code,
        .response_options = &response_header->options,
        .response_writer = write_payload,
        .response_writer_arg = write_payload_arg,
        .reliability_hint = reliability_hint,
        .delivery_handler = delivery_handler,
        .delivery_handler_arg = delivery_handler_arg,
        // RFC 7641, 3.2. Notifications:
        // "Non-2.xx responses do not include an Observe Option."
        .observe_option_value = avs_coap_code_is_success(response_header->code)
                                        ? &notify.observe_option_value
                                        : NULL
    };
    AVS_LIST(avs_coap_exchange_t) exchange =
            server_exchange_create(&exchange_create_args);
    if (!exchange) {
        return avs_errno(AVS_ENOMEM);
    }

    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);

    AVS_LIST_INSERT(&coap_base->server_exchanges, exchange);

    if (reliability_hint == AVS_COAP_NOTIFY_PREFER_NON_CONFIRMABLE) {
        cancel_notification_on_error(ctx, observe_id, response_header->code);
    }

#    ifdef WITH_AVS_COAP_BLOCK
    update_exchange_block2_option(exchange, exchange_create_args.request);
#    endif // WITH_AVS_COAP_BLOCK
    err = server_exchange_send_next_chunk(ctx, &coap_base->server_exchanges);
    AVS_LIST(avs_coap_exchange_t) *exchange_ptr =
            _avs_coap_find_server_exchange_ptr_by_id(
                    ctx, exchange_create_args.exchange_id);
    if (avs_is_err(err)) {
        if (exchange_ptr) {
            // Not using _avs_coap_server_exchange_cleanup(), because this
            // function's docs say that delivery_handler is not called on error.
            AVS_LIST_DELETE(exchange_ptr);
        }
        return err;
    }

    if (out_exchange_id) {
        *out_exchange_id = (exchange_ptr ? (*exchange_ptr)->id
                                         : AVS_COAP_EXCHANGE_ID_INVALID);
    }
    return AVS_OK;
}
#endif // WITH_AVS_COAP_OBSERVE
