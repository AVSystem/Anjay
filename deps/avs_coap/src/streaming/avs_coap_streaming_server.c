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

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_STREAMING_API

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_utils.h>

#    include <avsystem/coap/code.h>
#    include <avsystem/coap/streaming.h>

#    include "async/avs_coap_async_server.h"
#    include "avs_coap_observe.h"
#    include "streaming/avs_coap_streaming_server.h"

#    define MODULE_NAME coap
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_ctx.h"
#    include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static bool
has_received_request_chunk(const avs_coap_streaming_server_ctx_t *ctx) {
    return ctx->state == AVS_COAP_STREAMING_SERVER_RECEIVED_REQUEST_CHUNK
           || ctx->state
                      == AVS_COAP_STREAMING_SERVER_RECEIVED_LAST_REQUEST_CHUNK;
}

static bool
is_sending_response_chunk(const avs_coap_streaming_server_ctx_t *ctx) {
    return ctx->state == AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK
           || ctx->state == AVS_COAP_STREAMING_SERVER_SENDING_RESPONSE_CHUNK;
}

static int feed_payload_chunk(size_t payload_offset,
                              void *payload_buf,
                              size_t payload_buf_size,
                              size_t *out_payload_chunk_size,
                              void *streaming_server_ctx_) {
    (void) payload_offset;

    avs_coap_streaming_server_ctx_t *streaming_server_ctx =
            (avs_coap_streaming_server_ctx_t *) streaming_server_ctx_;
    AVS_ASSERT(streaming_server_ctx->expected_next_outgoing_chunk_offset
                       == payload_offset,
               "payload is supposed to be read sequentially");
    assert(is_sending_response_chunk(streaming_server_ctx));

    *out_payload_chunk_size =
            avs_buffer_data_size(streaming_server_ctx->chunk_buffer);
    if (payload_buf_size <= *out_payload_chunk_size) {
        *out_payload_chunk_size = payload_buf_size;
        streaming_server_ctx->state =
                AVS_COAP_STREAMING_SERVER_SENDING_RESPONSE_CHUNK;
    } else {
        streaming_server_ctx->state =
                AVS_COAP_STREAMING_SERVER_SENT_LAST_RESPONSE_CHUNK;
    }
    memcpy(payload_buf, avs_buffer_data(streaming_server_ctx->chunk_buffer),
           *out_payload_chunk_size);
    streaming_server_ctx->expected_next_outgoing_chunk_offset +=
            *out_payload_chunk_size;
    avs_buffer_consume_bytes(streaming_server_ctx->chunk_buffer,
                             *out_payload_chunk_size);

    return 0;
}

avs_stream_t *
avs_coap_streaming_setup_response(avs_coap_streaming_request_ctx_t *ctx,
                                  const avs_coap_response_header_t *response) {
    if (!ctx) {
        LOG(ERROR, _("no request to respond to"));
        return NULL;
    }
    if (!response) {
        LOG(ERROR, _("response must be provided"));
        return NULL;
    }
    if (!_avs_coap_response_header_valid(response)) {
        return NULL;
    }
    if (!has_received_request_chunk(&ctx->server_ctx)) {
        LOG(ERROR,
            _("Attempted to call avs_coap_streaming_setup_response() in "
              "an invalid state"));
        return NULL;
    }

    avs_coap_options_cleanup(&ctx->response_header.options);
    if (avs_is_err(_avs_coap_options_copy_as_dynamic(
                &ctx->response_header.options, &response->options))) {
        LOG(ERROR, _("Could not copy response options"));
        return NULL;
    }

    ctx->response_header.code = response->code;
    return (avs_stream_t *) ctx;
}

static avs_error_t
try_enter_sending_state(avs_coap_streaming_request_ctx_t *ctx) {
    if (!has_received_request_chunk(&ctx->server_ctx)) {
        return avs_errno(AVS_EINVAL);
    }
    if (!avs_coap_code_is_response(ctx->response_header.code)) {
        LOG(WARNING, _("Response not set up"));
        return avs_errno(AVS_EINVAL);
    }
    // Note: this is supposed to be called after the calls to
    // called _avs_coap_async_incoming_packet_handle() and
    // _avs_coap_async_incoming_packet_call_request_handler() (supposedly from
    // within handle_incoming_packet()), but before
    // _avs_coap_async_incoming_packet_send_response(). So effectively, we are
    // in the middle of the logic that would usually be handled through
    // _avs_coap_async_incoming_packet_simple_handle().
    // This function is called either from coap_write(), or after returning from
    // the user-provided request handler if the user has not called coap_write()
    // at all. So we know for sure that we're done with the receiving phase,
    // thus we're setting up the response - this code can be treated as the
    // continuation of request_handler(), now that the necessary data from the
    // user is available.
    // Note: _avs_coap_server_setup_async_response() does not call
    // feed_payload_chunk(). It will be called by the following call to
    // _avs_coap_async_incoming_packet_send_response().
    avs_error_t err = avs_coap_server_setup_async_response(
            &_avs_coap_get_base(ctx->server_ctx.coap_ctx)->request_ctx,
            &ctx->response_header, feed_payload_chunk, &ctx->server_ctx);
    if (avs_is_ok(err)) {
        if (avs_buffer_data_size(ctx->server_ctx.chunk_buffer) > 0) {
            LOG(WARNING,
                _("Ignoring ") "%s" _(" unread bytes of request"),
                AVS_UINT64_AS_STRING(
                        avs_buffer_data_size(ctx->server_ctx.chunk_buffer)));
            avs_buffer_reset(ctx->server_ctx.chunk_buffer);
        }
        ctx->server_ctx.state =
                AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK;
    }
    return err;
}

static avs_error_t
init_chunk_buffer(avs_coap_ctx_t *ctx,
                  avs_buffer_t **out_buffer,
                  const avs_coap_server_async_request_t *request,
                  const avs_coap_response_header_t *response) {
    /**
     * In case of incoming requests, the buffer must be able to hold either
     * request or response payload, so we need to take maximum of:
     * - maximum estimated request chunk size - for BLOCK transfers, this will
     *   be the BLOCK1 size of a first request chunk (it can never grow during
     *   the transfer); for non-BLOCK transfers, we need to take in_buffer_size
     *   instead,
     * - maximum estimated response chunk size - here we use @ref
     *   _avs_coap_get_next_outgoing_chunk_payload_size , assuming an arbitrary
     *   response code and empty options list (effectively calculating the
     *   biggest possible response payload chunk size).
     *
     * In case of notifications, we know the response headers in advance, so
     * we use this information instead of dummy values. In this case, we will
     * never receive any request payload, so @p request is NULL.
     */
    size_t max_request_chunk_size = 0;
    if (request) {
        max_request_chunk_size = _avs_coap_get_base(ctx)->in_buffer->capacity;
#    ifdef WITH_AVS_COAP_BLOCK
        avs_coap_option_block_t req_block1;
        switch (avs_coap_options_get_block(&request->header.options,
                                           AVS_COAP_BLOCK1, &req_block1)) {
        case 0:
            max_request_chunk_size =
                    AVS_MIN(req_block1.size, max_request_chunk_size);
            break;
        case AVS_COAP_OPTION_MISSING:
            break;
        default:
            AVS_UNREACHABLE("malformed options got through packet validation");
            return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
        }
#    endif // WITH_AVS_COAP_BLOCK
    }

    size_t max_response_chunk_size;
    avs_coap_options_t empty_opts = avs_coap_options_create_empty(NULL, 0);
    avs_error_t err = _avs_coap_get_first_outgoing_chunk_payload_size(
            ctx,
            response ? response->code : AVS_COAP_CODE_CONTENT,
            response ? &response->options : &empty_opts,
            &max_response_chunk_size);
    if (avs_is_err(err)) {
        LOG(DEBUG, _("get_next_outgoing_chunk_payload_size failed: ") "%s",
            AVS_COAP_STRERROR(err));
        return err;
    }

    avs_buffer_free(out_buffer);
    if (avs_buffer_create(out_buffer, AVS_MAX(max_request_chunk_size,
                                              max_response_chunk_size))) {
        return avs_errno(AVS_ENOMEM);
    }

    return AVS_OK;
}

static int request_handler(avs_coap_request_ctx_t *request_ctx,
                           avs_coap_exchange_id_t request_id,
                           avs_coap_server_request_state_t state,
                           const avs_coap_server_async_request_t *request,
                           const avs_coap_observe_id_t *observe_id,
                           void *streaming_req_ctx_) {
    avs_coap_streaming_request_ctx_t *streaming_req_ctx =
            (avs_coap_streaming_request_ctx_t *) streaming_req_ctx_;

    (void) request_ctx;
    (void) request_id;

    if (state == AVS_COAP_SERVER_REQUEST_CLEANUP) {
        // NOTE: while this may be called on either success or failure, it's
        // the client that should be concerned about delivering the whole
        // request or receiving the whole response. It should be fine to handle
        // any kind of cleanup as success.
        avs_buffer_free(&streaming_req_ctx->server_ctx.chunk_buffer);
        avs_coap_options_cleanup(&streaming_req_ctx->request_header.options);
        avs_coap_options_cleanup(&streaming_req_ctx->response_header.options);
        streaming_req_ctx->server_ctx.exchange_id =
                AVS_COAP_EXCHANGE_ID_INVALID;
        streaming_req_ctx->server_ctx.state =
                AVS_COAP_STREAMING_SERVER_FINISHED;
        // return value is ignored for CLEANUP anyway
        return 0;
    }

    if (streaming_req_ctx->error_response_code) {
        return streaming_req_ctx->error_response_code;
    }

    if (request->payload_offset == 0) {
        // This means that it's the first chunk of the request.
        assert(!streaming_req_ctx->server_ctx.chunk_buffer);
        assert(!streaming_req_ctx->request_has_observe_id);
        if (observe_id) {
            streaming_req_ctx->request_has_observe_id = true;
            streaming_req_ctx->request_observe_id = *observe_id;
        }

        if (avs_is_err(init_chunk_buffer(
                    streaming_req_ctx->server_ctx.coap_ctx,
                    &streaming_req_ctx->server_ctx.chunk_buffer, request,
                    NULL))) {
            return AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
        }

        assert(!streaming_req_ctx->request_header.options.allocated);
        streaming_req_ctx->request_header.code = request->header.code;
        if (avs_is_err(_avs_coap_options_copy_as_dynamic(
                    &streaming_req_ctx->request_header.options,
                    &request->header.options))) {
            return AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
        }
    }

    if (!streaming_req_ctx->server_ctx.chunk_buffer) {
        // The buffer was never created, meaning that we never received a
        // request with payload_offset == 0.
        return AVS_COAP_CODE_REQUEST_ENTITY_INCOMPLETE;
    }
    assert(request->payload_size
           <= avs_buffer_space_left(
                      streaming_req_ctx->server_ctx.chunk_buffer));
    avs_buffer_append_bytes(streaming_req_ctx->server_ctx.chunk_buffer,
                            request->payload, request->payload_size);
    assert(request_ctx
           == &_avs_coap_get_base(streaming_req_ctx->server_ctx.coap_ctx)
                       ->request_ctx);
    assert(streaming_req_ctx->server_ctx.state
           == AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST);

    switch (state) {
    case AVS_COAP_SERVER_REQUEST_PARTIAL_CONTENT:
        streaming_req_ctx->server_ctx.state =
                AVS_COAP_STREAMING_SERVER_RECEIVED_REQUEST_CHUNK;
        // This will be continued in ensure_data_is_available_to_read()
        return 0;

    case AVS_COAP_SERVER_REQUEST_RECEIVED:
        streaming_req_ctx->server_ctx.state =
                AVS_COAP_STREAMING_SERVER_RECEIVED_LAST_REQUEST_CHUNK;
        // This will be continued in ensure_data_is_available_to_read()
        return 0;

    case AVS_COAP_SERVER_REQUEST_CLEANUP:;
    }

    AVS_UNREACHABLE("invalid enum value");
    return -1;
}

static int handle_new_request(avs_coap_server_ctx_t *server_ctx,
                              const avs_coap_request_header_t *request,
                              void *streaming_req_ctx_) {
    (void) request;

    avs_coap_streaming_request_ctx_t *streaming_req_ctx =
            (avs_coap_streaming_request_ctx_t *) streaming_req_ctx_;

    assert(avs_coap_exchange_id_valid(streaming_req_ctx->server_ctx.exchange_id)
           == !!streaming_req_ctx->server_ctx.chunk_buffer);
    if (!streaming_req_ctx->server_ctx.chunk_buffer) {
        if (!avs_coap_exchange_id_valid(
                    (streaming_req_ctx->server_ctx.exchange_id =
                             avs_coap_server_accept_async_request(
                                     server_ctx, request_handler,
                                     streaming_req_ctx)))) {
            LOG(ERROR, _("accept_async_request failed"));
            return AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
        }
        return 0;
    } else {
        // another request is being handled
        return AVS_COAP_CODE_SERVICE_UNAVAILABLE;
    }
}

static int reject_new_request(avs_coap_server_ctx_t *server_ctx,
                              const avs_coap_request_header_t *request,
                              void *args_) {
    (void) server_ctx;
    (void) request;
    (void) args_;
    return AVS_COAP_CODE_SERVICE_UNAVAILABLE;
}

static avs_error_t
update_recv_timeout(avs_net_socket_t *socket,
                    avs_time_duration_t next_timeout,
                    avs_net_socket_opt_value_t *orig_recv_timeout) {
    avs_net_socket_opt_value_t recv_timeout = {
        .recv_timeout = next_timeout
    };
    avs_error_t err;
    if (avs_is_err((err = avs_net_socket_get_opt(
                            socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                            orig_recv_timeout)))
            || avs_is_err((err = avs_net_socket_set_opt(
                                   socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                   recv_timeout)))) {
        LOG(ERROR, _("could not set socket timeout"));
    }
    return err;
}

static avs_error_t
try_wait_for_next_chunk_request(const avs_coap_streaming_server_ctx_t *ctx,
                                const avs_error_t *abort_request_reason) {
    avs_time_monotonic_t next_deadline =
            _avs_coap_retry_or_request_expired_job(ctx->coap_ctx);

    if (abort_request_reason && avs_is_err(*abort_request_reason)) {
        return *abort_request_reason;
    }

    if (!avs_coap_exchange_id_valid(ctx->exchange_id)) {
        // exchange failed e.g. due to not receiving request for another block
        assert(ctx->state == AVS_COAP_STREAMING_SERVER_FINISHED);
        return _avs_coap_err(AVS_COAP_ERR_TIMEOUT);
    }

    avs_net_socket_t *socket = _avs_coap_get_base(ctx->coap_ctx)->socket;
    avs_time_duration_t recv_timeout =
            avs_time_monotonic_diff(next_deadline, avs_time_monotonic_now());
    assert(avs_time_duration_valid(recv_timeout));
    avs_net_socket_opt_value_t orig_recv_timeout;
    avs_error_t err =
            update_recv_timeout(socket, recv_timeout, &orig_recv_timeout);
    if (avs_is_ok(err)) {
        // In a normal flow, this will receive the request for another BLOCK2
        // chunk, and send the response. This does not require interaction with
        // user code in the middle, so
        // _avs_coap_async_incoming_packet_simple_handle() can be used, unlike
        // handle_incoming_packet().
        err = _avs_coap_async_incoming_packet_simple_handle_single(
                ctx->coap_ctx, ctx->acquired_in_buffer,
                ctx->acquired_in_buffer_size, reject_new_request, NULL);
        if (err.category == AVS_ERRNO_CATEGORY && err.code == AVS_ETIMEDOUT) {
            // timeout is expected; ignore
            err = AVS_OK;
        }

        if (avs_is_err(avs_net_socket_set_opt(socket,
                                              AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                              orig_recv_timeout))) {
            LOG(ERROR, _("could not restore socket timeout"));
        }
    }
    return err;
}

static avs_error_t flush_response_chunk(avs_coap_streaming_request_ctx_t *ctx) {
    if (avs_is_ok(ctx->err)) {
        switch (ctx->server_ctx.state) {
        case AVS_COAP_STREAMING_SERVER_RECEIVED_REQUEST_CHUNK:
        case AVS_COAP_STREAMING_SERVER_RECEIVED_LAST_REQUEST_CHUNK:
        case AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK:
            // This call concludes the replication of
            // _avs_coap_async_incoming_packet_simple_handle(). Note that in the
            // AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK case,
            // feed_payload_chunk() will be called here.
            return _avs_coap_async_incoming_packet_send_response(
                    ctx->server_ctx.coap_ctx, ctx->error_response_code);

        case AVS_COAP_STREAMING_SERVER_SENDING_RESPONSE_CHUNK:
            // For the non-first chunk, we are not in the middle of
            // incoming_packet_handle logic, so we need to handle this case
            // differently.
            return try_wait_for_next_chunk_request(&ctx->server_ctx, NULL);

        default:;
        }
    }

    assert(avs_is_err(ctx->err));
    LOG(ERROR, _("invalid state for flush_request_chunk(), aborting exchange"));
    avs_coap_exchange_cancel(ctx->server_ctx.coap_ctx,
                             ctx->server_ctx.exchange_id);
    return avs_is_err(ctx->err) ? ctx->err
                                : _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
}

static avs_error_t
coap_write(avs_stream_t *stream_, const void *data, size_t *data_length) {
    avs_coap_streaming_request_ctx_t *streaming_req_ctx =
            (avs_coap_streaming_request_ctx_t *) stream_;
    avs_error_t err = streaming_req_ctx->err;
    if (avs_is_ok(err)
            && !is_sending_response_chunk(&streaming_req_ctx->server_ctx)) {
        err = try_enter_sending_state(streaming_req_ctx);
    }
    if (avs_is_err(err)) {
        LOG(ERROR, _("CoAP server stream not ready for writing"));
        return err;
    }

    size_t bytes_written = 0;
    while (bytes_written < *data_length) {
        size_t bytes_to_write =
                AVS_MIN(*data_length - bytes_written,
                        avs_buffer_space_left(
                                streaming_req_ctx->server_ctx.chunk_buffer));
        avs_buffer_append_bytes(streaming_req_ctx->server_ctx.chunk_buffer,
                                (const char *) data + bytes_written,
                                bytes_to_write);
        bytes_written += bytes_to_write;
        // Once the buffer is full, flush_response_chunk() needs to be called.
        if (!is_sending_response_chunk(&streaming_req_ctx->server_ctx)) {
            return avs_errno(AVS_ENOBUFS);
        }
        if (avs_buffer_space_left(streaming_req_ctx->server_ctx.chunk_buffer)
                        == 0
                && avs_is_err((streaming_req_ctx->err = flush_response_chunk(
                                       streaming_req_ctx)))) {
            return streaming_req_ctx->err;
        }
    }
    return AVS_OK;
}

static avs_error_t
handle_incoming_packet(avs_coap_streaming_request_ctx_t *streaming_req_ctx,
                       avs_time_duration_t recv_timeout) {
    assert(streaming_req_ctx->server_ctx.state
           == AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST);
    avs_net_socket_t *socket =
            _avs_coap_get_base(streaming_req_ctx->server_ctx.coap_ctx)->socket;
    avs_net_socket_opt_value_t orig_recv_timeout;
    avs_error_t err =
            update_recv_timeout(socket, recv_timeout, &orig_recv_timeout);
    if (avs_is_err(err)) {
        return err;
    }
    avs_coap_exchange_t *exchange = NULL;
    // The possible cases to be handled here:
    // - The first packet of the incoming request is received. In this case,
    //   streaming_req_ctx->server_ctx.exchange_id is invalid,
    //   handle_new_request() will actually call
    //   _avs_coap_server_accept_async_request().
    // - Any following packet of the incoming request is received.
    // - Concurrect incoming request is received while another one is already
    //   being handled. 5.03 Service Unavailable will be sent.
    err = _avs_coap_async_incoming_packet_handle_single(
            streaming_req_ctx->server_ctx.coap_ctx,
            streaming_req_ctx->server_ctx.acquired_in_buffer,
            streaming_req_ctx->server_ctx.acquired_in_buffer_size,
            handle_new_request, streaming_req_ctx, &exchange);
    if (err.category == AVS_ERRNO_CATEGORY && err.code == AVS_ETIMEDOUT) {
        // timeout is expected; ignore
        err = AVS_OK;
    }
    if (exchange) {
        // Note that we've just called _avs_coap_async_incoming_packet_handle(),
        // not _avs_coap_async_incoming_packet_simple_handle(). That function
        // was created in D8245 by splitting the old
        // _avs_coap_async_handle_incoming_packet(), which was equivalent to the
        // modern "simple" version, i.e. always called the request handler and
        // sent the response immediately after receiving the incoming response.
        // The whole reason why we needed the "non-simple" version is that in
        // this streaming server API, we sometimes want to defer calling
        // _avs_coap_async_incoming_packet_send_response() until we actually
        // get the contents of that response from the user. And since we might
        // be called from _within_ the user code (via coap_read()), we cannot
        // _call_ user code, we need to _return_, which makes the whole logic
        // somewhat complicated.
        // Note: is_streaming_request will be false if a message pertaining to
        // another exchange (some "background" async exchange) is received.
        bool is_streaming_request =
                (exchange->by_type.server.request_handler == request_handler);
        // If is_streaming_request == true, this will call request_handler().
        int call_result = _avs_coap_async_incoming_packet_call_request_handler(
                streaming_req_ctx->server_ctx.coap_ctx, exchange);
        if (!call_result && is_streaming_request
                && has_received_request_chunk(&streaming_req_ctx->server_ctx)) {
            // This is supposed to correspond with the "This will be continued
            // in ensure_data_is_available_to_read()" cases, as commented in
            // request_handler(). Note that this mean that the whole request
            // handling logic is not finished yet, we're just waiting for
            // interaction with the user. ensure_data_is_available_to_read() may
            // be called from coap_read() or coap_peek() - see there for more
            // information on what it does.
            err = AVS_OK;
        } else {
            // Otherwise, we just replicate the logic of
            // _avs_coap_async_incoming_packet_simple_handle().
            err = _avs_coap_async_incoming_packet_send_response(
                    streaming_req_ctx->server_ctx.coap_ctx, call_result);
        }
    }
    if (avs_is_err(avs_net_socket_set_opt(
                socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT, orig_recv_timeout))) {
        LOG(ERROR, _("could not restore socket timeout"));
    }
    return err;
}

static avs_error_t ensure_data_is_available_to_read(
        avs_coap_streaming_request_ctx_t *streaming_req_ctx) {
    if (avs_is_err(streaming_req_ctx->err)) {
        return streaming_req_ctx->err;
    }
    // Note: there is a distinct
    // AVS_COAP_STREAMING_SERVER_RECEIVED_LAST_REQUEST_CHUNK state, so if we
    // enter the condition below, we know that the next packet to receive is
    // supposed to be another chunk of request.
    if (streaming_req_ctx->server_ctx.state
                    == AVS_COAP_STREAMING_SERVER_RECEIVED_REQUEST_CHUNK
            && avs_buffer_data_size(streaming_req_ctx->server_ctx.chunk_buffer)
                           == 0) {
        // All data from the previously received chunk has been consumed by the
        // user. We now can send the response, concluding the replication of
        // _avs_coap_async_incoming_packet_simple_handle() logic. We could do it
        // earlier, but that would require further differentiating logic between
        // the RECEIVED_REQUEST_CHUNK and RECEIVED_LAST_REQUEST_CHUNK cases.
        // flush_response_chunk() may be called instead of this function, and we
        // want the states in which the two functions may be called to be
        // equivalent.
        if (avs_is_err((streaming_req_ctx->err =
                                _avs_coap_async_incoming_packet_send_response(
                                        streaming_req_ctx->server_ctx.coap_ctx,
                                        0)))) {
            return streaming_req_ctx->err;
        }
        // Now we need to receive the next chunk of the request. This replicates
        // the logic in handle_incoming_packet_with_acquired_in_buffer().
        streaming_req_ctx->server_ctx.state =
                AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST;
        while (streaming_req_ctx->server_ctx.state
               == AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST) {
            avs_time_monotonic_t next_deadline =
                    _avs_coap_retry_or_request_expired_job(
                            streaming_req_ctx->server_ctx.coap_ctx);
            if (streaming_req_ctx->server_ctx.state
                    != AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST) {
                // The exchange has been cleaned up by
                // _avs_coap_retry_or_request_expired_job()
                assert(streaming_req_ctx->server_ctx.state
                       == AVS_COAP_STREAMING_SERVER_FINISHED);
                streaming_req_ctx->err = _avs_coap_err(AVS_COAP_ERR_TIMEOUT);
                return streaming_req_ctx->err;
            }
            avs_time_duration_t recv_timeout =
                    avs_time_monotonic_diff(next_deadline,
                                            avs_time_monotonic_now());
            assert(avs_time_duration_valid(recv_timeout));
            if (avs_is_err((streaming_req_ctx->err = handle_incoming_packet(
                                    streaming_req_ctx, recv_timeout)))) {
                return streaming_req_ctx->err;
            }
        }
    }

    if (avs_is_err(streaming_req_ctx->err)) {
        return streaming_req_ctx->err;
    } else if (!has_received_request_chunk(&streaming_req_ctx->server_ctx)) {
        LOG(ERROR, _("CoAP streaming_server read called in invalid state"));
        return avs_errno(AVS_EBADF);
    }
    return AVS_OK;
}

static avs_error_t coap_read(avs_stream_t *stream_,
                             size_t *out_bytes_read,
                             bool *out_message_finished,
                             void *buffer,
                             size_t buffer_length) {
    avs_coap_streaming_request_ctx_t *streaming_req_ctx =
            (avs_coap_streaming_request_ctx_t *) stream_;
    avs_error_t err = ensure_data_is_available_to_read(streaming_req_ctx);
    if (avs_is_err(err)) {
        return err;
    }

    size_t bytes_to_read = AVS_MIN(
            buffer_length,
            avs_buffer_data_size(streaming_req_ctx->server_ctx.chunk_buffer));
    memcpy(buffer, avs_buffer_data(streaming_req_ctx->server_ctx.chunk_buffer),
           bytes_to_read);
    avs_buffer_consume_bytes(streaming_req_ctx->server_ctx.chunk_buffer,
                             bytes_to_read);
    if (out_bytes_read) {
        *out_bytes_read = bytes_to_read;
    }
    if (out_message_finished) {
        *out_message_finished =
                (avs_buffer_data_size(
                         streaming_req_ctx->server_ctx.chunk_buffer)
                         == 0
                 && streaming_req_ctx->server_ctx.state
                            == AVS_COAP_STREAMING_SERVER_RECEIVED_LAST_REQUEST_CHUNK);
    }
    return AVS_OK;
}

static avs_error_t
coap_peek(avs_stream_t *stream_, size_t offset, char *out_value) {
    avs_coap_streaming_request_ctx_t *streaming_req_ctx =
            (avs_coap_streaming_request_ctx_t *) stream_;
    avs_error_t err = ensure_data_is_available_to_read(streaming_req_ctx);
    if (avs_is_err(err)) {
        return err;
    }

    if (offset >= avs_buffer_data_size(
                          streaming_req_ctx->server_ctx.chunk_buffer)) {
        return AVS_EOF;
    }
    *out_value =
            avs_buffer_data(streaming_req_ctx->server_ctx.chunk_buffer)[offset];
    return AVS_OK;
}

static const avs_stream_v_table_t _AVS_COAP_STREAMING_REQUEST_CTX_VTABLE = {
    .write_some = coap_write,
    .read = coap_read,
    .peek = coap_peek,
    .extension_list = AVS_STREAM_V_TABLE_NO_EXTENSIONS
};

static avs_error_t handle_incoming_packet_with_acquired_in_buffer(
        avs_coap_ctx_t *coap_ctx,
        uint8_t *acquired_in_buffer,
        size_t acquired_in_buffer_size,
        avs_coap_streaming_request_handler_t *handle_request,
        void *handler_arg) {
    while (true) {
        avs_coap_streaming_request_ctx_t streaming_req_ctx = {
            .vtable = &_AVS_COAP_STREAMING_REQUEST_CTX_VTABLE,
            .server_ctx = {
                .coap_ctx = coap_ctx,
                .acquired_in_buffer = acquired_in_buffer,
                .acquired_in_buffer_size = acquired_in_buffer_size,
                .state = AVS_COAP_STREAMING_SERVER_RECEIVING_REQUEST
            }
        };
        // While this function "handles incoming packet" in a generic way, the
        // only case it handles that actually requires some interaction with the
        // user code is handling an incoming _request_. See inside for more
        // details.
        if (avs_is_ok((streaming_req_ctx.err = handle_incoming_packet(
                               &streaming_req_ctx, AVS_TIME_DURATION_ZERO)))) {
            if (!streaming_req_ctx.server_ctx.chunk_buffer) {
                // Timeout - as the contract of this function does not mandate
                // that we must always receive anything, we just return success.
                // Also, because we loop, wanting to flush internal socket
                // buffers, this is actually the only success return point of
                // this function.
                return AVS_OK;
            }
            if (has_received_request_chunk(&streaming_req_ctx.server_ctx)) {
                // We have successfully received some data, so passing
                // AVS_OK as error code makes sense here.
                // The user-provided request handler is supposed to call
                // coap_read(), possibly followed by coap_write().
                streaming_req_ctx.error_response_code = handle_request(
                        &streaming_req_ctx,
                        &streaming_req_ctx.request_header,
                        (avs_stream_t *) &streaming_req_ctx,
                        streaming_req_ctx.request_has_observe_id
                                ? &streaming_req_ctx.request_observe_id
                                : NULL,
                        handler_arg);
                // Update state if the response has been set up, but
                // coap_write() has not been called
                try_enter_sending_state(&streaming_req_ctx);
                if (!streaming_req_ctx.error_response_code
                        && has_received_request_chunk(
                                   &streaming_req_ctx.server_ctx)) {
                    // request handler returned success, but
                    // _avs_coap_streaming_setup_response() has not been
                    // successfully called
                    streaming_req_ctx.error_response_code =
                            AVS_COAP_CODE_INTERNAL_SERVER_ERROR;
                }
            }
            // We might have some data buffered to be sent, but not send yet -
            // send it here. This is done in a loop, because we might end up
            // receiving messages unrelated to this exchange in between.
            while (streaming_req_ctx.server_ctx.state
                   != AVS_COAP_STREAMING_SERVER_FINISHED) {
                streaming_req_ctx.err =
                        flush_response_chunk(&streaming_req_ctx);
            }
        }

        // make sure everything is cleaned up
        assert(!streaming_req_ctx.server_ctx.chunk_buffer);
        assert(!streaming_req_ctx.request_header.options.allocated);
        assert(!streaming_req_ctx.response_header.options.allocated);
        if (avs_is_err(streaming_req_ctx.err)) {
            // error
            return streaming_req_ctx.err;
        }
    }
}

avs_error_t avs_coap_streaming_handle_incoming_packet(
        avs_coap_ctx_t *coap_ctx,
        avs_coap_streaming_request_handler_t *handle_request,
        void *handler_arg) {
    uint8_t *acquired_in_buffer;
    size_t acquired_in_buffer_size;
    avs_error_t err = _avs_coap_in_buffer_acquire(coap_ctx, &acquired_in_buffer,
                                                  &acquired_in_buffer_size);
    if (avs_is_ok(err)) {
        err = handle_incoming_packet_with_acquired_in_buffer(
                coap_ctx, acquired_in_buffer, acquired_in_buffer_size,
                handle_request, handler_arg);
        _avs_coap_in_buffer_release(coap_ctx);
    }
    return err;
}

#    ifdef WITH_AVS_COAP_OBSERVE
avs_error_t avs_coap_observe_streaming_start(
        avs_coap_streaming_request_ctx_t *ctx,
        avs_coap_observe_id_t id,
        avs_coap_observe_cancel_handler_t *cancel_handler,
        void *handler_arg) {
    return avs_coap_observe_async_start(
            &_avs_coap_get_base(ctx->server_ctx.coap_ctx)->request_ctx, id,
            cancel_handler, handler_arg);
}

typedef struct {
    const avs_stream_v_table_t *vtable;
    avs_coap_streaming_server_ctx_t server_ctx;
    avs_coap_observe_id_t observe_id;
    const avs_coap_response_header_t *response_header;
    avs_coap_notify_reliability_hint_t reliability_hint;
    bool required_receiving;
    avs_error_t err;
} notify_streaming_ctx_t;

static void notify_delivery_status_handler(avs_coap_ctx_t *ctx,
                                           avs_error_t err,
                                           void *arg) {
    (void) ctx;

    notify_streaming_ctx_t *notify_streaming_ctx =
            (notify_streaming_ctx_t *) arg;
    notify_streaming_ctx->server_ctx.state = AVS_COAP_STREAMING_SERVER_FINISHED;
    if (avs_is_ok(notify_streaming_ctx->err) && avs_is_err(err)) {
        notify_streaming_ctx->err = err;
    }
}

static avs_error_t flush_notify_chunk(notify_streaming_ctx_t *ctx) {
    switch (ctx->server_ctx.state) {
    case AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK: {
        // We need to send the first (or only) notification chunk, so we need
        // to create the underlying async exchange.
        // feed_payload_chunk() will be called during this call;
        // notify_delivery_status_handler() may be called if this is a
        // single-block, non-confirmable notification.
        avs_error_t err = avs_coap_notify_async(
                ctx->server_ctx.coap_ctx, &ctx->server_ctx.exchange_id,
                ctx->observe_id, ctx->response_header, ctx->reliability_hint,
                feed_payload_chunk, &ctx->server_ctx,
                notify_delivery_status_handler, ctx);
        if (avs_is_err(err)) {
            ctx->server_ctx.state = AVS_COAP_STREAMING_SERVER_FINISHED;
            ctx->err = err;
        } else if (avs_coap_exchange_id_valid(ctx->server_ctx.exchange_id)) {
            // If we have the exchange ID here, it means that it is either a
            // Confirmable notification, and/or requires a blockwise transfer.
            // Either way, we'll need to flush the socket buffer afterwards.
            ctx->required_receiving = true;
        }
        return err;
    }
    case AVS_COAP_STREAMING_SERVER_SENDING_RESPONSE_CHUNK:
    case AVS_COAP_STREAMING_SERVER_SENT_LAST_RESPONSE_CHUNK:
        // We need to send some non-first notification chunk. We are being
        // called either from notify_write(), or just after payload writer;
        // anyway, the logic we are in is all about writing. To send another
        // chunk, we need to first receive a BLOCK2 request for the next block.
        // try_wait_for_next_chunk_request() will actually also call
        // feed_payload_chunk() and send that chunk. See comments inside for
        // details.
        ctx->err = try_wait_for_next_chunk_request(&ctx->server_ctx, &ctx->err);
        return ctx->err;

    default:
        AVS_UNREACHABLE("invalid state for flush_notify_chunk()");
        return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
    }
}

static avs_error_t
notify_write(avs_stream_t *stream_, const void *data, size_t *data_length) {
    notify_streaming_ctx_t *notify_streaming_ctx =
            (notify_streaming_ctx_t *) stream_;
    if (!is_sending_response_chunk(&notify_streaming_ctx->server_ctx)) {
        LOG(ERROR, _("CoAP notification stream not ready for writing"));
        return avs_errno(AVS_EBADF);
    }
    if (avs_is_err(notify_streaming_ctx->err)) {
        LOG(ERROR, _("CoAP notification stream already in a failed state"));
        return notify_streaming_ctx->err;
    }

    size_t bytes_written = 0;
    while (bytes_written < *data_length) {
        size_t bytes_to_write =
                AVS_MIN(*data_length - bytes_written,
                        avs_buffer_space_left(
                                notify_streaming_ctx->server_ctx.chunk_buffer));
        avs_buffer_append_bytes(notify_streaming_ctx->server_ctx.chunk_buffer,
                                (const char *) data + bytes_written,
                                bytes_to_write);
        bytes_written += bytes_to_write;
        // Let's send the notification packet once the buffer is filled.
        if (!is_sending_response_chunk(&notify_streaming_ctx->server_ctx)) {
            return avs_errno(AVS_ENOBUFS);
        }
        if (avs_buffer_space_left(notify_streaming_ctx->server_ctx.chunk_buffer)
                == 0) {
            avs_error_t err = flush_notify_chunk(notify_streaming_ctx);
            if (avs_is_err(err)) {
                return err;
            }
        }
    }
    return AVS_OK;
}

static const avs_stream_v_table_t _AVS_COAP_STREAMING_NOTIFY_CTX_VTABLE = {
    .write_some = notify_write,
    .extension_list = AVS_STREAM_V_TABLE_NO_EXTENSIONS
};

avs_error_t
avs_coap_notify_streaming(avs_coap_ctx_t *ctx,
                          avs_coap_observe_id_t observe_id,
                          const avs_coap_response_header_t *response_header,
                          avs_coap_notify_reliability_hint_t reliability_hint,
                          avs_coap_streaming_writer_t *write_payload,
                          void *write_payload_arg) {
    notify_streaming_ctx_t notify_streaming_ctx = {
        .vtable = &_AVS_COAP_STREAMING_NOTIFY_CTX_VTABLE,
        .server_ctx = {
            .coap_ctx = ctx,
            .state = AVS_COAP_STREAMING_SERVER_SENDING_FIRST_RESPONSE_CHUNK
        },
        .observe_id = observe_id,
        .response_header = response_header,
        .reliability_hint = reliability_hint
    };
    notify_streaming_ctx.err = _avs_coap_in_buffer_acquire(
            ctx, &notify_streaming_ctx.server_ctx.acquired_in_buffer,
            &notify_streaming_ctx.server_ctx.acquired_in_buffer_size);
    if (avs_is_err(notify_streaming_ctx.err)) {
        return notify_streaming_ctx.err;
    }

    if (avs_is_err((notify_streaming_ctx.err = init_chunk_buffer(
                            ctx,
                            &notify_streaming_ctx.server_ctx.chunk_buffer,
                            NULL,
                            response_header)))) {
        goto finish;
    }

    if (write_payload) {
        // write_payload() is expected to call notify_write(),
        // so see there for what happens next.
        int write_result = write_payload((avs_stream_t *) &notify_streaming_ctx,
                                         write_payload_arg);
        if (write_result) {
            LOG(DEBUG,
                _("unable to write notification payload, result = ") "%d",
                write_result);
            if (avs_is_ok(notify_streaming_ctx.err)) {
                notify_streaming_ctx.err =
                        _avs_coap_err(AVS_COAP_ERR_PAYLOAD_WRITER_FAILED);
            }
        }
    }
    // If notify_write() has either not been called at all, or its calls have
    // not filled the buffer enough to send a BLOCK1 request, we need to
    // actually send the notification here.
    while (avs_is_ok(notify_streaming_ctx.err)
           && notify_streaming_ctx.server_ctx.state
                      != AVS_COAP_STREAMING_SERVER_FINISHED) {
        notify_streaming_ctx.err = flush_notify_chunk(&notify_streaming_ctx);
    }

    if (avs_is_err(notify_streaming_ctx.err)
            && avs_coap_exchange_id_valid(
                       notify_streaming_ctx.server_ctx.exchange_id)) {
        LOG(DEBUG, _("unable to send notification, result = ") "%s",
            AVS_COAP_STRERROR(notify_streaming_ctx.err));
        if (notify_streaming_ctx.server_ctx.state
                != AVS_COAP_STREAMING_SERVER_FINISHED) {
            avs_coap_exchange_cancel(
                    ctx, notify_streaming_ctx.server_ctx.exchange_id);
        }
    }
    assert(notify_streaming_ctx.server_ctx.state
           == AVS_COAP_STREAMING_SERVER_FINISHED);

finish:
    avs_buffer_free(&notify_streaming_ctx.server_ctx.chunk_buffer);
    if (avs_is_ok(notify_streaming_ctx.err)
            && notify_streaming_ctx.required_receiving) {
        notify_streaming_ctx.err =
                _avs_coap_async_incoming_packet_handle_while_possible_without_blocking(
                        ctx, notify_streaming_ctx.server_ctx.acquired_in_buffer,
                        notify_streaming_ctx.server_ctx.acquired_in_buffer_size,
                        reject_new_request, NULL);
    }
    _avs_coap_in_buffer_release(ctx);
    return notify_streaming_ctx.err;
}
#    endif // WITH_AVS_COAP_OBSERVE

#endif // WITH_AVS_COAP_STREAMING_API
