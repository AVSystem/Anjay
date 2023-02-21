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

#    include <avsystem/coap/tcp.h>
#    include <avsystem/commons/avs_buffer.h>
#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_socket.h>
#    include <avsystem/commons/avs_utils.h>

#    include "avs_coap_code_utils.h"
#    include "options/avs_coap_iterator.h"

#    define MODULE_NAME coap_tcp
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_tcp_ctx.h"

#    include "avs_coap_common_utils.h"
#    include "tcp/avs_coap_tcp_pending_requests.h"
#    include "tcp/avs_coap_tcp_signaling.h"

// Base value defined in RFC8323
#    define CSM_MAX_MESSAGE_SIZE_BASE_VALUE 1152

#    ifdef WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
#        define SET_DIAGNOSTIC_MESSAGE(Ctx, Message) \
            ((Ctx->err_details) = (Message))
#    else
#        define SET_DIAGNOSTIC_MESSAGE(Ctx, Message)
#    endif // WITH_AVS_COAP_DIAGNOSTIC_MESSAGES

#    ifdef WITH_AVS_COAP_DIAGNOSTIC_MESSAGES
#        define GET_DIAGNOSTIC_MESSAGE(Ctx) (Ctx->err_details)
#    else
#        define GET_DIAGNOSTIC_MESSAGE(Ctx) NULL
#    endif // WITH_AVS_COAP_DIAGNOSTIC_MESSAGES

VISIBILITY_SOURCE_BEGIN

// Maximum length of the entire message (all chunks received, etc.) we are able
// to receive, if we had enough memory (note that this is not related to input
// buffer size, because we can actually receive packets in chunks over TCP).
static const uint32_t INCOMING_MESSAGE_MAX_TOTAL_SIZE =
        (uint32_t) AVS_MIN(SIZE_MAX, UINT32_MAX);

static void log_tcp_msg_summary(const char *info,
                                const avs_coap_borrowed_msg_t *msg) {
    char observe_str[24] = "";
#    ifdef WITH_AVS_COAP_OBSERVE
    uint32_t observe;
    if (avs_coap_options_get_observe(&msg->options, &observe) == 0) {
        snprintf(observe_str, sizeof(observe_str), ", Observe %u", observe);
    }
#    endif // WITH_AVS_COAP_OBSERVE

#    ifdef WITH_AVS_COAP_BLOCK
    avs_coap_option_block_t block1;
    bool has_block1 =
            (avs_coap_options_get_block(&msg->options, AVS_COAP_BLOCK1, &block1)
             == 0);
    avs_coap_option_block_string_buf_t block1_str_buf = { "" };
    if (has_block1) {
        _avs_coap_option_block_string(&block1_str_buf, &block1);
    }

    avs_coap_option_block_t block2;
    bool has_block2 =
            (avs_coap_options_get_block(&msg->options, AVS_COAP_BLOCK2, &block2)
             == 0);
    avs_coap_option_block_string_buf_t block2_str_buf = { "" };
    if (has_block2) {
        _avs_coap_option_block_string(&block2_str_buf, &block2);
    }

    LOG(DEBUG, "%s: %s (token: %s)%s%s%s%s%s, payload: %u B", info,
        AVS_COAP_CODE_STRING(msg->code), AVS_COAP_TOKEN_HEX(&msg->token),
        has_block1 ? ", " : "", block1_str_buf.str, has_block2 ? ", " : "",
        block2_str_buf.str, observe_str, (unsigned) msg->total_payload_size);
#    else  // WITH_AVS_COAP_BLOCK
    LOG(DEBUG, "%s: %s (token: %s)%s, payload: %u B", info,
        AVS_COAP_CODE_STRING(msg->code), AVS_COAP_TOKEN_HEX(&msg->token),
        observe_str, (unsigned) msg->total_payload_size);
#    endif // WITH_AVS_COAP_BLOCK
}

avs_error_t _avs_coap_tcp_send_msg(avs_coap_tcp_ctx_t *ctx,
                                   const avs_coap_borrowed_msg_t *msg) {
    void *out_buf = avs_shared_buffer_acquire(ctx->base.out_buffer);
    size_t buf_size = ctx->base.out_buffer->capacity;
    size_t msg_size;
    avs_error_t err =
            _avs_coap_tcp_serialize_msg(msg, out_buf, buf_size, &msg_size);

    if (avs_is_ok(err)) {
        log_tcp_msg_summary("send", msg);
        if (avs_is_err((err = avs_net_socket_send(ctx->base.socket, out_buf,
                                                  msg_size)))) {
            LOG(DEBUG, _("send failed: ") "%s", AVS_COAP_STRERROR(err));
            SET_DIAGNOSTIC_MESSAGE(ctx, "send failed");
        }
    }

    avs_shared_buffer_release(ctx->base.out_buffer);
    return err;
}

static inline void handle_response(avs_coap_tcp_ctx_t *ctx,
                                   const avs_coap_tcp_cached_msg_t *msg) {
    const avs_coap_tcp_pending_request_status_t result =
            msg->remaining_bytes ? PENDING_REQUEST_STATUS_PARTIAL_CONTENT
                                 : PENDING_REQUEST_STATUS_COMPLETED;
    _avs_coap_tcp_handle_pending_request(ctx, &msg->content, result, AVS_OK);
}

static avs_error_t send_simple_msg(avs_coap_tcp_ctx_t *ctx,
                                   uint8_t code,
                                   avs_coap_token_t *token,
                                   const char *payload) {
    assert(token);
    size_t payload_size = payload ? strlen(payload) : 0;
    avs_coap_borrowed_msg_t msg = {
        .code = code,
        .token = *token,
        .payload = payload,
        .payload_size = payload_size,
        .total_payload_size = payload_size
    };

    return _avs_coap_tcp_send_msg(ctx, &msg);
}

static void send_release(avs_coap_tcp_ctx_t *ctx) {
    avs_coap_borrowed_msg_t msg = {
        .code = AVS_COAP_CODE_RELEASE
    };

    (void) _avs_coap_ctx_generate_token(ctx->base.prng_ctx, &msg.token);
    (void) _avs_coap_tcp_send_msg(ctx, &msg);
}

static avs_error_t handle_cached_msg(avs_coap_tcp_ctx_t *ctx,
                                     avs_coap_borrowed_msg_t *out_request) {
    avs_coap_tcp_cached_msg_t *msg = &ctx->cached_msg;

    const uint8_t code = msg->content.code;

    LOG(DEBUG,
        _("handling incoming ") "%s" _(", token: ") "%s" _(
                ", payload: ") "%u" _(" B"),
        AVS_COAP_CODE_STRING(ctx->cached_msg.content.code),
        AVS_COAP_TOKEN_HEX(&ctx->cached_msg.content.token),
        (unsigned) ctx->cached_msg.content.payload_size);

    if (avs_coap_code_is_request(code)) {
        if (out_request && !msg->ignore_request) {
            AVS_ASSERT(msg->content.payload_offset + msg->content.payload_size
                               <= msg->content.total_payload_size,
                       "bug: sum of payload_offset and payload_size should not "
                       "be greater than total_payload_length");
            *out_request = ctx->cached_msg.content;
        }
    } else if (avs_coap_code_is_response(code)) {
        handle_response(ctx, msg);
        return AVS_OK;
    } else if (_avs_coap_code_is_signaling_message(code)) {
        return _avs_coap_tcp_handle_signaling_message(ctx, &ctx->peer_csm,
                                                      &msg->content);
    } else if (code == AVS_COAP_CODE_EMPTY) {
        // "Empty messages (Code 0.00) can always be sent and MUST be ignored by
        //  the recipient. This provides a basic keepalive function that can
        //  refresh NAT bindings."
        if (msg->content.options.size || msg->content.payload_size) {
            LOG(DEBUG, _("non-empty message with Code 0.00"));
        }
    } else {
        LOG(DEBUG, _("Unexpected CoAP code: ") "%s" _(", ignoring"),
            AVS_COAP_CODE_STRING(code));
    }
    return AVS_OK;
}

static avs_error_t set_recv_timeout(avs_net_socket_t *socket,
                                    avs_time_duration_t timeout) {
    avs_error_t err =
            avs_net_socket_set_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                   (avs_net_socket_opt_value_t) {
                                       .recv_timeout = timeout
                                   });
    if (avs_is_err(err)) {
        LOG(ERROR, _("failed to set recv timeout"));
    }
    return err;
}

static avs_error_t get_recv_timeout(avs_net_socket_t *socket,
                                    avs_time_duration_t *out_timeout) {
    avs_net_socket_opt_value_t socket_timeout;
    avs_error_t err =
            avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                   &socket_timeout);
    if (avs_is_err(err)) {
        LOG(ERROR, _("failed to get recv timeout"));
    } else {
        *out_timeout = socket_timeout.recv_timeout;
    }
    return err;
}

static avs_error_t coap_tcp_recv_data(avs_coap_tcp_ctx_t *ctx,
                                      void *buffer,
                                      size_t buffer_size,
                                      size_t *out_bytes_received) {
    if (!buffer_size) {
        LOG(ERROR, _("no space in input buffer"));
        return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
    }

    avs_error_t err =
            avs_net_socket_receive(ctx->base.socket, out_bytes_received, buffer,
                                   buffer_size);

    if (avs_is_err(err)) {
        LOG(TRACE, _("recv failed: ") "%s", AVS_COAP_STRERROR(err));
        return err;
    }

    if (avs_is_err(err = set_recv_timeout(ctx->base.socket,
                                          AVS_TIME_DURATION_ZERO))) {
        return err;
    }

    if (!*out_bytes_received) {
        return _avs_coap_err(AVS_COAP_ERR_TCP_CONN_CLOSED);
    }

    return AVS_OK;
}

static inline void opt_cache_finish_message(avs_coap_tcp_opt_cache_t *cache) {
    avs_buffer_reset(cache->buffer);
    cache->state = AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_HEADER;
}

static void finish_message_handling(avs_coap_tcp_ctx_t *ctx) {
    memset(&ctx->cached_msg, 0, sizeof(ctx->cached_msg));
    opt_cache_finish_message(&ctx->opt_cache);
}

static inline void send_abort(avs_coap_tcp_ctx_t *ctx) {
    ctx->aborted = true;
    (void) send_simple_msg(ctx, AVS_COAP_CODE_ABORT,
                           &ctx->cached_msg.content.token,
                           GET_DIAGNOSTIC_MESSAGE(ctx));
}

static void coap_tcp_cleanup(avs_coap_ctx_t *ctx_) {
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;

    if (!ctx->aborted && ctx->base.socket) {
        (void) send_release(ctx);
    }
    // TODO T2262
    // Wait for completion of pending requests after sending release message.
    // "The peer responding to the Release message SHOULD delay the closing of
    //  the connection until it has responded to all requests received by it
    //  before the Release message."
    _avs_coap_tcp_cancel_all_pending_requests(ctx);
    avs_buffer_free(&ctx->opt_cache.buffer);
    avs_free(ctx);
}

static size_t max_payload_size(size_t buffer_capacity,
                               size_t csm_max_message_size,
                               size_t token_size,
                               size_t options_size) {
    // TODO: use actual header length
    size_t length_until_payload = token_size + options_size
                                  + _AVS_COAP_TCP_MAX_HEADER_LENGTH
                                  + sizeof(AVS_COAP_PAYLOAD_MARKER);
    if (buffer_capacity <= length_until_payload
            || csm_max_message_size <= length_until_payload) {
        return 0;
    }
    size_t buffer_space = buffer_capacity - length_until_payload;
    size_t peer_capability = csm_max_message_size - length_until_payload;
    return AVS_MIN(buffer_space, peer_capability);
}

static size_t
coap_tcp_max_outgoing_payload_size(avs_coap_ctx_t *ctx_,
                                   size_t token_size,
                                   const avs_coap_options_t *options,
                                   uint8_t code) {
    (void) code;
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
    return max_payload_size(ctx->base.out_buffer->capacity,
                            ctx->peer_csm.max_message_size, token_size,
                            options ? options->size : 0);
}

static size_t
coap_tcp_max_incoming_payload_size(avs_coap_ctx_t *ctx_,
                                   size_t token_size,
                                   const avs_coap_options_t *options,
                                   uint8_t code) {
    (void) code;
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
    return max_payload_size(ctx->base.in_buffer->capacity,
                            INCOMING_MESSAGE_MAX_TOTAL_SIZE, token_size,
                            options ? options->size : 0);
}

static void coap_tcp_ignore_current_request(avs_coap_ctx_t *ctx_,
                                            const avs_coap_token_t *token) {
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;

    // Ensure that it's currently processed request.
    if (avs_coap_token_equal(&ctx->cached_msg.content.token, token)
            && avs_coap_code_is_request(ctx->cached_msg.content.code)) {
        ctx->cached_msg.ignore_request = true;
    }
}

/**
 * Note: tries to send Abort message if network error occured. It may not be
 *       successfully sent though.
 */
static avs_error_t
coap_tcp_send_message(avs_coap_ctx_t *ctx_,
                      const avs_coap_borrowed_msg_t *msg,
                      avs_coap_send_result_handler_t *send_result_handler,
                      void *send_result_handler_arg) {
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
    if (ctx->aborted) {
        LOG(ERROR,
            _("Abort message was sent and context shouldn't be used anymore"));
        return _avs_coap_err(AVS_COAP_ERR_TCP_ABORT_SENT);
    }
    SET_DIAGNOSTIC_MESSAGE(ctx, NULL);

    AVS_LIST(avs_coap_tcp_pending_request_t) *req = NULL;
    if (send_result_handler && avs_coap_code_is_request(msg->code)) {
        req = _avs_coap_tcp_create_pending_request(ctx,
                                                   &ctx->pending_requests,
                                                   &msg->token,
                                                   send_result_handler,
                                                   send_result_handler_arg);
        if (!req) {
            return avs_errno(AVS_ENOMEM);
        }
    } else if (avs_coap_code_is_response(msg->code)) {
        // Response may be sent before receiving the entire request, don't pass
        // more payload chunks to the upper layer.
        coap_tcp_ignore_current_request(ctx_, &msg->token);
    }

    avs_error_t err = _avs_coap_tcp_send_msg(ctx, msg);
    if (avs_is_ok(err)) {
        if (send_result_handler && !avs_coap_code_is_request(msg->code)) {
            send_result_handler(ctx_, AVS_COAP_SEND_RESULT_OK, AVS_OK, NULL,
                                send_result_handler_arg);
        }
    } else {
        if (req) {
            _avs_coap_tcp_remove_pending_request(req);
        }
        send_abort(ctx);
    }
    return err;
}

static void coap_tcp_abort_delivery(avs_coap_ctx_t *ctx_,
                                    avs_coap_exchange_direction_t direction,
                                    const avs_coap_token_t *token,
                                    avs_coap_send_result_t result,
                                    avs_error_t fail_err) {
    // notifications are never explicitly confirmed over TCP
    if (direction != AVS_COAP_EXCHANGE_SERVER_NOTIFICATION) {
        avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
        _avs_coap_tcp_abort_pending_request_by_token(ctx, token, result,
                                                     fail_err);
    }
}

static avs_error_t coap_tcp_accept_observation(avs_coap_ctx_t *ctx_,
                                               avs_coap_observe_t *observe) {
    (void) ctx_;
    (void) observe;

#    ifdef WITH_AVS_COAP_OBSERVE
    return AVS_OK;
#    else  // WITH_AVS_COAP_OBSERVE
    LOG(WARNING, _("Observes support disabled"));
    return _avs_coap_err(AVS_COAP_ERR_FEATURE_DISABLED);
#    endif // WITH_AVS_COAP_OBSERVE
}

static inline avs_error_t receive_missing_payload(avs_coap_tcp_ctx_t *ctx,
                                                  uint8_t *buf,
                                                  size_t buf_size,
                                                  size_t *out_bytes_received) {
    assert(ctx->cached_msg.options_cached);
    size_t bytes_to_read = AVS_MIN(buf_size, ctx->cached_msg.remaining_bytes);
    avs_error_t err =
            coap_tcp_recv_data(ctx, buf, bytes_to_read, out_bytes_received);
    if (avs_is_err(err)) {
        SET_DIAGNOSTIC_MESSAGE(ctx, "recv failed");
    }
    return err;
}

static inline void
pack_payload_from_opts_buffer(avs_coap_tcp_cached_msg_t *inout_msg,
                              avs_buffer_t *data,
                              size_t payload_offset) {
    const uint8_t *payload_ptr =
            (const uint8_t *) avs_buffer_data(data) + payload_offset;
    size_t data_size = avs_buffer_data_size(data) - payload_offset;
    AVS_ASSERT(data_size <= inout_msg->remaining_bytes,
               "bug: more than one message in buffer");
    _avs_coap_tcp_pack_payload(inout_msg, payload_ptr, data_size);
}

static avs_error_t recv_to_internal_buffer_with_bytes_limit(
        avs_coap_tcp_ctx_t *ctx, size_t limit, size_t *out_bytes_read) {
    size_t bytes_read = 0;
    const size_t space_left = avs_buffer_space_left(ctx->opt_cache.buffer);
    const size_t bytes_to_read = AVS_MIN(space_left, limit);
    avs_error_t err =
            coap_tcp_recv_data(ctx,
                               avs_buffer_raw_insert_ptr(ctx->opt_cache.buffer),
                               bytes_to_read, &bytes_read);
    if (avs_is_err(err)) {
        return err;
    }

    avs_buffer_advance_ptr(ctx->opt_cache.buffer, bytes_read);
    if (out_bytes_read) {
        *out_bytes_read = bytes_read;
    }
    return AVS_OK;
}

static void ignore_data_for_current_msg(avs_coap_tcp_ctx_t *ctx) {
    const size_t bytes_in_buffer = avs_buffer_data_size(ctx->opt_cache.buffer);
    const size_t bytes_to_ignore =
            AVS_MIN(ctx->cached_msg.remaining_bytes, bytes_in_buffer);

    avs_buffer_consume_bytes(ctx->opt_cache.buffer, bytes_to_ignore);
    ctx->cached_msg.remaining_bytes -= bytes_to_ignore;
}

static avs_error_t receive_header(avs_coap_tcp_ctx_t *ctx) {
    if (!ctx->cached_msg.remaining_header_bytes) {
        ctx->cached_msg.remaining_header_bytes =
                _AVS_COAP_TCP_MIN_HEADER_LENGTH;
    }

    avs_error_t err;
    avs_coap_tcp_header_t header;

    // Stops if:
    // - less bytes than required were received from the socket,
    // - header is invalid,
    // - header was parsed successfully.
    //
    // Note: in the first iteration, it tries to receive just two bytes of
    // header and parse them. If header is longer than 2 bytes, then
    // ctx->cached_msg.remaining_header_bytes is updated and recv function
    // is called again to obtain remaining bytes.
    while (ctx->cached_msg.remaining_header_bytes) {
        size_t bytes_read;
        err = recv_to_internal_buffer_with_bytes_limit(
                ctx, ctx->cached_msg.remaining_header_bytes, &bytes_read);
        if (avs_is_err(err)) {
            return err;
        }

        ctx->cached_msg.remaining_header_bytes -= bytes_read;
        if (ctx->cached_msg.remaining_header_bytes) {
            return _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED);
        }

        bytes_dispenser_t dispenser = {
            .read_ptr =
                    (const uint8_t *) avs_buffer_data(ctx->opt_cache.buffer),
            .bytes_left = avs_buffer_data_size(ctx->opt_cache.buffer)
        };

        err = _avs_coap_tcp_header_parse(
                &header, &dispenser, &ctx->cached_msg.remaining_header_bytes);
        if (err.category == AVS_COAP_ERR_CATEGORY
                && err.code == AVS_COAP_ERR_MALFORMED_MESSAGE) {
            return err;
        }
    }

    ctx->cached_msg = (avs_coap_tcp_cached_msg_t) {
        .content = (avs_coap_borrowed_msg_t) {
            .code = header.code,
            .token = (avs_coap_token_t) {
                .size = header.token_len
            },
            .options = { 0 }
        },
        .remaining_bytes = header.opts_and_payload_len + header.token_len
    };
    avs_buffer_reset(ctx->opt_cache.buffer);
    return AVS_OK;
}

static avs_error_t receive_token(avs_coap_tcp_ctx_t *ctx) {
    size_t bytes_read;

    size_t remaining_token_bytes =
            ctx->cached_msg.content.token.size
            - avs_buffer_data_size(ctx->opt_cache.buffer);

    if (remaining_token_bytes) {
        avs_error_t err = recv_to_internal_buffer_with_bytes_limit(
                ctx, remaining_token_bytes, &bytes_read);
        if (avs_is_err(err)) {
            return err;
        }
        if (remaining_token_bytes != bytes_read) {
            return _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED);
        }
    }

    memcpy(ctx->cached_msg.content.token.bytes,
           avs_buffer_data(ctx->opt_cache.buffer),
           ctx->cached_msg.content.token.size);
    avs_buffer_reset(ctx->opt_cache.buffer);
    ctx->cached_msg.remaining_bytes -= ctx->cached_msg.content.token.size;
    return AVS_OK;
}

static avs_error_t receive_options(avs_coap_tcp_ctx_t *ctx) {
    // cached_msg.remaining_bytes indicates how many bytes of the message wasn't
    // parsed, but some of them may be already received and present in
    // opt_cache.buffer.
    assert(ctx->cached_msg.remaining_bytes
           > avs_buffer_data_size(ctx->opt_cache.buffer));
    size_t bytes_to_receive = ctx->cached_msg.remaining_bytes
                              - avs_buffer_data_size(ctx->opt_cache.buffer);
    avs_error_t err =
            recv_to_internal_buffer_with_bytes_limit(ctx, bytes_to_receive,
                                                     NULL);
    if (avs_is_err(err)) {
        return err;
    }

    err = _avs_coap_tcp_pack_options(&ctx->cached_msg, ctx->opt_cache.buffer);
    if (avs_is_ok(err)) {
        ctx->cached_msg.options_cached = true;
        return AVS_OK;
    }
    if (err.category == AVS_COAP_ERR_CATEGORY) {
        switch ((avs_coap_error_t) err.code) {
        case AVS_COAP_ERR_MORE_DATA_REQUIRED:
            /**
             * If options are truncated and entire buffer is filled with data,
             * we'll not be able to receive remaining options and message has to
             * be ignored.
             */
            if (avs_buffer_data_size(ctx->opt_cache.buffer)
                    == avs_buffer_capacity(ctx->opt_cache.buffer)) {
                return _avs_coap_err(AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED);
            }
            return err;

        case AVS_COAP_ERR_MALFORMED_OPTIONS:
            LOG(DEBUG, _("invalid or malformed options"));
            return err;

        case AVS_COAP_ERR_MALFORMED_MESSAGE:
            LOG(DEBUG, _("malformed message"));
            return err;

        default:;
        }
    }

    AVS_UNREACHABLE("bug: some case not handled");
    return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
}

static avs_error_t pack_payload_from_internal_buffer_and_handle_msg(
        avs_coap_tcp_ctx_t *ctx, avs_coap_borrowed_msg_t *out_request) {
    avs_error_t err = AVS_OK;

    const size_t data_size = avs_buffer_data_size(ctx->opt_cache.buffer);

    if (ctx->cached_msg.content.total_payload_size && data_size) {
        const size_t options_size = ctx->cached_msg.content.options.size;
        const size_t payload_offset =
                options_size + sizeof(AVS_COAP_PAYLOAD_MARKER);

        AVS_ASSERT(data_size - payload_offset
                           <= ctx->cached_msg.content.total_payload_size,
                   "bug: more than one message in buffer");

        if (payload_offset < data_size) {
            pack_payload_from_opts_buffer(
                    &ctx->cached_msg, ctx->opt_cache.buffer, payload_offset);
        }
    }

    if (ctx->cached_msg.content.payload_size
            || !ctx->cached_msg.remaining_bytes) {
        err = handle_cached_msg(ctx, out_request);
    }

    if (avs_is_err(err)) {
        return err;
    }
    return ctx->cached_msg.remaining_bytes
                   ? _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED)
                   : AVS_OK;
}

static avs_error_t ignore_invalid_msg(avs_coap_tcp_ctx_t *ctx) {
    avs_error_t err = _avs_coap_err(AVS_COAP_ERR_MORE_DATA_REQUIRED);
    ignore_data_for_current_msg(ctx);

    if (avs_coap_code_is_response(ctx->cached_msg.content.code)) {
        _avs_coap_tcp_handle_pending_request(
                ctx,
                &ctx->cached_msg.content,
                ctx->cached_msg.remaining_bytes
                        ? PENDING_REQUEST_STATUS_IGNORE
                        : PENDING_REQUEST_STATUS_FINISH_IGNORE,
                ctx->ignoring_error);
    } else if (!ctx->cached_msg.remaining_bytes) {
        assert(avs_is_err(ctx->ignoring_error));
        if (ctx->ignoring_error.category == AVS_COAP_ERR_CATEGORY
                && ctx->ignoring_error.code
                               == AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED) {
            SET_DIAGNOSTIC_MESSAGE(ctx, "options too big");
        }
        err = ctx->ignoring_error;
    }

    return err;
}

static avs_error_t
receive_to_internal_buffer_and_handle(avs_coap_tcp_ctx_t *ctx,
                                      avs_coap_borrowed_msg_t *out_request) {
    avs_error_t err = AVS_OK;
    switch (ctx->opt_cache.state) {
    case AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_HEADER:
        err = receive_header(ctx);
        if (avs_is_err(err)) {
            return err;
        }
        ctx->opt_cache.state = AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_TOKEN;
    // fall-through
    case AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_TOKEN:
        err = receive_token(ctx);
        if (avs_is_err(err)) {
            return err;
        }
        ctx->opt_cache.state = AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_OPTIONS;
    // fall-through
    case AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_OPTIONS:
        if (ctx->cached_msg.remaining_bytes) {
            err = receive_options(ctx);
            if (err.category == AVS_COAP_ERR_CATEGORY
                    && (err.code == AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED
                        || err.code == AVS_COAP_ERR_MALFORMED_OPTIONS
                        || err.code == AVS_COAP_ERR_MALFORMED_MESSAGE)) {
                ctx->ignoring_error = err;
                ctx->opt_cache.state = AVS_COAP_TCP_OPT_CACHE_STATE_IGNORING;
                return ignore_invalid_msg(ctx);
            }
            if (avs_is_err(err)) {
                return err;
            }
        }
        ctx->opt_cache.state = AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_PAYLOAD;
        log_tcp_msg_summary("recv", &ctx->cached_msg.content);
    // fall-through
    case AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_PAYLOAD:
        err = pack_payload_from_internal_buffer_and_handle_msg(ctx,
                                                               out_request);
        break;
    case AVS_COAP_TCP_OPT_CACHE_STATE_IGNORING:
        err = recv_to_internal_buffer_with_bytes_limit(
                ctx, ctx->cached_msg.remaining_bytes, NULL);
        if (avs_is_err(err)) {
            return err;
        }
        err = ignore_invalid_msg(ctx);
        break;
    }
    return err;
}

static avs_error_t
receive_to_shared_buffer_and_handle(avs_coap_tcp_ctx_t *ctx,
                                    uint8_t *in_buffer,
                                    size_t in_buffer_capacity,
                                    avs_coap_borrowed_msg_t *out_request) {
    size_t bytes_read = 0;
    avs_error_t err = receive_missing_payload(ctx, in_buffer,
                                              in_buffer_capacity, &bytes_read);
    if (avs_is_ok(err)) {
        _avs_coap_tcp_pack_payload(&ctx->cached_msg, in_buffer, bytes_read);
        err = handle_cached_msg(ctx, out_request);
    }
    return err;
}

static avs_error_t handle_error(avs_coap_tcp_ctx_t *ctx, avs_error_t err) {
    if (avs_is_err(err)) {
        if (err.category == AVS_COAP_ERR_CATEGORY) {
            switch ((avs_coap_error_t) err.code) {
            case AVS_COAP_ERR_MORE_DATA_REQUIRED:
            case AVS_COAP_ERR_TIMEOUT:
                return AVS_OK;

            case AVS_COAP_ERR_MALFORMED_OPTIONS:
                return send_simple_msg(ctx, AVS_COAP_CODE_BAD_OPTION,
                                       &ctx->cached_msg.content.token, NULL);

            case AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED:
                return send_simple_msg(ctx, AVS_COAP_CODE_INTERNAL_SERVER_ERROR,
                                       &ctx->cached_msg.content.token,
                                       GET_DIAGNOSTIC_MESSAGE(ctx));

            default:;
            }
        }
        if (err.category != AVS_ERRNO_CATEGORY || err.code != AVS_ETIMEDOUT) {
            LOG(ERROR, _("failure (") "%s" _("), aborting"),
                AVS_COAP_STRERROR(err));
            send_abort(ctx);
        }
    }
    return err;
}

static avs_error_t
receive_and_handle_message(avs_coap_tcp_ctx_t *ctx,
                           uint8_t *in_buffer,
                           size_t in_buffer_capacity,
                           avs_coap_borrowed_msg_t *out_request) {
    avs_error_t err;
    if (!ctx->cached_msg.options_cached) {
        // Use internal buffer to cache options. If some payload is received,
        // then handle it.
        err = receive_to_internal_buffer_and_handle(ctx, out_request);
    } else {
        // Use shared buffer to receive only remaining payload.
        err = receive_to_shared_buffer_and_handle(
                ctx, in_buffer, in_buffer_capacity, out_request);
    }
    return handle_error(ctx, err);
}

static avs_error_t
coap_tcp_receive_message(avs_coap_ctx_t *ctx_,
                         uint8_t *in_buffer,
                         size_t in_buffer_capacity,
                         avs_coap_borrowed_msg_t *out_request) {
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
    memset(out_request, 0, sizeof(*out_request));
    if (ctx->aborted) {
        LOG(ERROR,
            _("Abort message was sent and context shouldn't be used anymore"));
        return _avs_coap_err(AVS_COAP_ERR_TCP_ABORT_SENT);
    }
    SET_DIAGNOSTIC_MESSAGE(ctx, NULL);

    avs_time_duration_t timeout;
    avs_error_t err = get_recv_timeout(ctx->base.socket, &timeout);
    if (avs_is_err(err)) {
        return err;
    }

    if (ctx->cached_msg.remaining_bytes == 0
            && ctx->cached_msg.remaining_header_bytes == 0) {
        finish_message_handling(ctx);
        AVS_ASSERT(avs_buffer_data_size(ctx->opt_cache.buffer) == 0,
                   "bug: data in buffer after finishing message handling");
    }

    err = receive_and_handle_message(ctx, in_buffer, in_buffer_capacity,
                                     out_request);

    avs_error_t restore_err = set_recv_timeout(ctx->base.socket, timeout);
    return avs_is_ok(err) ? restore_err : err;
}

static avs_time_monotonic_t coap_tcp_on_timeout(avs_coap_ctx_t *ctx_) {
    return _avs_coap_tcp_fail_expired_pending_requests(
            (avs_coap_tcp_ctx_t *) ctx_);
}

static avs_error_t receive_csm(avs_coap_tcp_ctx_t *ctx) {
    const avs_time_monotonic_t start = avs_time_monotonic_now();

    avs_time_duration_t timeout;
    avs_error_t err = get_recv_timeout(ctx->base.socket, &timeout);
    if (avs_is_err(err)) {
        return err;
    }

    do {
        const avs_time_monotonic_t now = avs_time_monotonic_now();
        const avs_time_duration_t time_passed =
                avs_time_monotonic_diff(now, start);
        const avs_time_duration_t new_timeout =
                avs_time_duration_diff(ctx->request_timeout, time_passed);
        if (avs_time_duration_less(new_timeout, AVS_TIME_DURATION_ZERO)) {
            LOG(ERROR, _("timeout reached while receiving CSM"));
            err = _avs_coap_err(AVS_COAP_ERR_TIMEOUT);
            break;
        }

        if (avs_is_err(
                    (err = set_recv_timeout(ctx->base.socket, new_timeout)))) {
            break;
        }

        // Used to receive possible chunks of payload, which are ignored anyway.
        uint8_t temp[16];
        err = receive_and_handle_message(ctx, temp, sizeof(temp), NULL);
    } while (avs_is_ok(err) && ctx->cached_msg.remaining_bytes);

    if (avs_is_err(err)) {
        return err;
    } else if (!ctx->peer_csm.received) {
        return _avs_coap_err(AVS_COAP_ERR_TCP_CSM_NOT_RECEIVED);
    }

    AVS_ASSERT(ctx->cached_msg.remaining_bytes == 0
                       && ctx->cached_msg.remaining_header_bytes == 0,
               "bug: message seems to be unfinished after handling CSM");
    finish_message_handling(ctx);
    AVS_ASSERT(avs_buffer_data_size(ctx->opt_cache.buffer) == 0,
               "bug: data in buffer after finishing message handling");
    AVS_ASSERT(ctx->opt_cache.state
                       == AVS_COAP_TCP_OPT_CACHE_STATE_RECEIVING_HEADER,
               "bug: invalid state after handling CSM");

    if (avs_is_err((err = set_recv_timeout(ctx->base.socket, timeout)))) {
        return err;
    }

    return AVS_OK;
}

static avs_error_t send_csm(avs_coap_tcp_ctx_t *ctx) {
    uint8_t opts[16];
    avs_coap_borrowed_msg_t msg = {
        .code = AVS_COAP_CODE_CSM,
        .options = avs_coap_options_create_empty(opts, sizeof(opts))
    };

    avs_error_t err =
            _avs_coap_ctx_generate_token(ctx->base.prng_ctx, &msg.token);
    if (avs_is_err(err)) {
        return err;
    }

#    ifdef WITH_AVS_COAP_BLOCK
    // From RFC 8323: "If a Max-Message-Size Option is indicated with a value
    // that is greater than 1152 (in the same CSM or a different CSM), the
    // Block-Wise-Transfer Option also indicates support for BERT"
    (void) avs_coap_options_add_empty(
            &msg.options, _AVS_COAP_OPTION_BLOCK_WISE_TRANSFER_CAPABILITY);
#    endif // WITH_AVS_COAP_BLOCK
    (void) avs_coap_options_add_u32(&msg.options,
                                    _AVS_COAP_OPTION_MAX_MESSAGE_SIZE,
                                    INCOMING_MESSAGE_MAX_TOTAL_SIZE);

    err = _avs_coap_tcp_send_msg(ctx, &msg);
    if (avs_is_err(err)) {
        LOG(ERROR, _("failed to send CSM"));
    }
    return err;
}

static avs_error_t coap_tcp_setsock(avs_coap_ctx_t *ctx_,
                                    avs_net_socket_t *socket) {
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
    avs_error_t err = _avs_coap_ctx_set_socket_base(ctx_, socket);
    if (avs_is_err(err)) {
        return err;
    }

    if (avs_is_err((err = send_csm(ctx)))
            || avs_is_err((err = receive_csm(ctx)))) {
        SET_DIAGNOSTIC_MESSAGE(ctx, "failed to send/receive CSM");
        send_abort(ctx);
        return err;
    }
    return AVS_OK;
}

static avs_coap_base_t *coap_tcp_get_base(avs_coap_ctx_t *ctx_) {
    avs_coap_tcp_ctx_t *ctx = (avs_coap_tcp_ctx_t *) ctx_;
    return &ctx->base;
}

static uint32_t coap_tcp_next_observe_option_value(avs_coap_ctx_t *ctx,
                                                   uint32_t last_value) {
    (void) ctx;
    (void) last_value;
    return 0;
}

static const avs_coap_ctx_vtable_t COAP_TCP_VTABLE = {
    .cleanup = coap_tcp_cleanup,
    .get_base = coap_tcp_get_base,
    .setsock = coap_tcp_setsock,
    .max_outgoing_payload_size = coap_tcp_max_outgoing_payload_size,
    .max_incoming_payload_size = coap_tcp_max_incoming_payload_size,
    .send_message = coap_tcp_send_message,
    .abort_delivery = coap_tcp_abort_delivery,
    .accept_observation = coap_tcp_accept_observation,
    .ignore_current_request = coap_tcp_ignore_current_request,
    .receive_message = coap_tcp_receive_message,
    .on_timeout = coap_tcp_on_timeout,
    .next_observe_option_value = coap_tcp_next_observe_option_value
};

avs_coap_ctx_t *avs_coap_tcp_ctx_create(avs_sched_t *sched,
                                        avs_shared_buffer_t *in_buffer,
                                        avs_shared_buffer_t *out_buffer,
                                        size_t max_opts_size,
                                        avs_time_duration_t request_timeout,
                                        avs_crypto_prng_ctx_t *prng_ctx) {
    assert(in_buffer);
    assert(out_buffer);
    assert(prng_ctx);

    if (out_buffer->capacity < _AVS_COAP_TCP_MAX_HEADER_LENGTH) {
        LOG(ERROR,
            _("output buffer capacity must be at least ") "%u" _(" bytes"),
            (unsigned) _AVS_COAP_TCP_MAX_HEADER_LENGTH);
        return NULL;
    }
    if (max_opts_size < AVS_COAP_MAX_TOKEN_LENGTH) {
        LOG(ERROR, _("max_opts_size must be at least ") "%u",
            (unsigned) AVS_COAP_MAX_TOKEN_LENGTH);
        return NULL;
    }
    if (!avs_time_duration_valid(request_timeout)) {
        LOG(ERROR, _("invalid timeout specified"));
        return NULL;
    }

    avs_coap_tcp_ctx_t *ctx =
            (avs_coap_tcp_ctx_t *) avs_calloc(1, sizeof(avs_coap_tcp_ctx_t));
    if (!ctx) {
        return NULL;
    }

    const size_t buf_size = max_opts_size + sizeof(AVS_COAP_PAYLOAD_MARKER);

    if (avs_buffer_create(&ctx->opt_cache.buffer, buf_size)) {
        avs_free(ctx);
        return NULL;
    }

    _avs_coap_base_init(&ctx->base, (avs_coap_ctx_t *) ctx, in_buffer,
                        out_buffer, sched, prng_ctx);

    ctx->vtable = &COAP_TCP_VTABLE;
    ctx->peer_csm.max_message_size = CSM_MAX_MESSAGE_SIZE_BASE_VALUE;
    ctx->request_timeout = request_timeout;

    return (avs_coap_ctx_t *) ctx;
}

#endif // WITH_AVS_COAP_TCP
