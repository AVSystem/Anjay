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

#    include <inttypes.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_stream_v_table.h>
#    include <avsystem/commons/avs_utils.h>

#    include "async/avs_coap_async_client.h"
#    include "async/avs_coap_async_server.h"
#    include "avs_coap_code_utils.h"
#    include "avs_coap_streaming_client.h"

#    define MODULE_NAME coap_stream
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_ctx.h"
#    include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static const char *coap_stream_state_string(coap_stream_state_t state) {
    switch (state) {
    case COAP_STREAM_STATE_UNINITIALIZED:
        return "UNINITIALIZED";
    case COAP_STREAM_STATE_SENDING_REQUEST:
        return "SENDING_REQUEST";
    case COAP_STREAM_STATE_RECEIVING_RESPONSE:
        return "RECEIVING_RESPONSE";
    }
    AVS_UNREACHABLE("unexpected enum value");
    return "???";
}

static bool state_transition_allowed(coap_stream_state_t old_state,
                                     coap_stream_state_t new_state) {
    switch (old_state) {
    case COAP_STREAM_STATE_UNINITIALIZED:
        return true;

    case COAP_STREAM_STATE_SENDING_REQUEST:
        return (new_state == COAP_STREAM_STATE_UNINITIALIZED
                || new_state == COAP_STREAM_STATE_RECEIVING_RESPONSE);

    case COAP_STREAM_STATE_RECEIVING_RESPONSE:
        return new_state == COAP_STREAM_STATE_UNINITIALIZED;
    }

    AVS_UNREACHABLE("unexpected enum value");
    return false;
}

static inline bool coap_stream_valid(coap_stream_t *stream) {
    switch (stream->state) {
    case COAP_STREAM_STATE_UNINITIALIZED:
        return stream->chunk_buffer == NULL;

    default:
        return stream->chunk_buffer != NULL;
    }
}

static inline void coap_stream_set_state(coap_stream_t *stream,
                                         coap_stream_state_t new_state) {
    LOG(DEBUG, _("coap_stream state: ") "%s" _(" -> ") "%s",
        coap_stream_state_string(stream->state),
        coap_stream_state_string(new_state));

    if (state_transition_allowed(stream->state, new_state)) {
        stream->state = new_state;
        assert(coap_stream_valid(stream));
    } else {
        LOG(ERROR,
            _("unexpected coap_stream state change: ") "%s" _(" -> ") "%s",
            coap_stream_state_string(stream->state),
            coap_stream_state_string(new_state));
        AVS_UNREACHABLE("coap_stream misused");
    }
}

static void coap_stream_set_error(coap_stream_t *stream, avs_error_t err) {
    if (avs_is_ok(stream->err)) {
        stream->err = err;
    } else {
        LOG(DEBUG, _("Suppressing error: ") "%s", AVS_COAP_STRERROR(err));
    }
}

static int feed_payload_chunk(size_t payload_offset,
                              void *payload_buf,
                              size_t payload_buf_size,
                              size_t *out_payload_chunk_size,
                              void *stream_) {
    (void) payload_offset;

    coap_stream_t *stream = (coap_stream_t *) stream_;
    AVS_ASSERT(stream->next_outgoing_chunk.expected_offset == payload_offset,
               "payload is supposed to be read sequentially");
    assert(stream->state == COAP_STREAM_STATE_SENDING_REQUEST);

    *out_payload_chunk_size = avs_buffer_data_size(stream->chunk_buffer);
    if (payload_buf_size < *out_payload_chunk_size) {
        *out_payload_chunk_size = payload_buf_size;
    }
    memcpy(payload_buf, avs_buffer_data(stream->chunk_buffer),
           *out_payload_chunk_size);
    stream->next_outgoing_chunk.expected_offset += *out_payload_chunk_size;
    stream->next_outgoing_chunk.expected_payload_size = 0;
    avs_buffer_consume_bytes(stream->chunk_buffer, *out_payload_chunk_size);

    return 0;
}

static void handle_response(avs_coap_ctx_t *ctx,
                            avs_coap_exchange_id_t exchange_id,
                            avs_coap_client_request_state_t result,
                            const avs_coap_client_async_response_t *response,
                            avs_error_t err,
                            void *stream_) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(avs_coap_exchange_id_equal(exchange_id, stream->exchange_id));
    (void) exchange_id;

    if (stream->state != COAP_STREAM_STATE_RECEIVING_RESPONSE) {
        avs_buffer_reset(stream->chunk_buffer);
        coap_stream_set_state(stream, COAP_STREAM_STATE_RECEIVING_RESPONSE);
    }

    if (response) {
        avs_coap_options_cleanup(&stream->response_header.options);
        stream->response_header.code = response->header.code;
        if (avs_is_err((err = _avs_coap_options_copy_as_dynamic(
                                &stream->response_header.options,
                                &response->header.options)))) {
            LOG(ERROR, _("could not copy options: ") "%s",
                AVS_COAP_STRERROR(err));
            coap_stream_set_error(stream, err);
            // note that this will recursively call this handler
            avs_coap_exchange_cancel(ctx, stream->exchange_id);
        } else {
            assert(avs_buffer_data_size(stream->chunk_buffer) == 0);
            assert(response->payload_size
                   <= avs_buffer_capacity(stream->chunk_buffer));
            avs_buffer_append_bytes(stream->chunk_buffer, response->payload,
                                    response->payload_size);
        }
    }

    switch (result) {
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT:
        break;

    case AVS_COAP_CLIENT_REQUEST_FAIL:
        coap_stream_set_error(stream, err);
        // fall through
    case AVS_COAP_CLIENT_REQUEST_OK:
    case AVS_COAP_CLIENT_REQUEST_CANCEL:
        // exchange finished
        stream->exchange_id = AVS_COAP_EXCHANGE_ID_INVALID;
    }
}

static int reject_request(avs_coap_server_ctx_t *ctx,
                          const avs_coap_request_header_t *request,
                          void *arg) {
    (void) ctx;
    (void) arg;

    LOG(DEBUG,
        "%s" _(" received while handling a streaming CoAP transfer; sending "
               "Service Unavailable response"),
        AVS_COAP_CODE_STRING(request->code));

    return AVS_COAP_CODE_SERVICE_UNAVAILABLE;
}

static avs_coap_ctx_t *coap_stream_owner_ctx(coap_stream_t *stream) {
    return stream->coap_ctx;
}

static avs_error_t
acquire_in_buffer_and_handle_incoming_packet(coap_stream_t *stream) {
    avs_coap_ctx_t *ctx = coap_stream_owner_ctx(stream);
    uint8_t *acquired_in_buffer;
    size_t acquired_in_buffer_size;
    avs_error_t err = _avs_coap_in_buffer_acquire(ctx, &acquired_in_buffer,
                                                  &acquired_in_buffer_size);
    if (avs_is_err(err)) {
        return err;
    }
    err = _avs_coap_async_incoming_packet_simple_handle_single(
            ctx, acquired_in_buffer, acquired_in_buffer_size, reject_request,
            NULL);
    if (avs_is_ok(err) && !avs_coap_exchange_id_valid(stream->exchange_id)) {
        // We have just received a final response, the exchange is no longer
        // valid. We want to flush all the data that might be still buffered
        // in the socket before returning control to the user.
        // This might cause sending 5.03 Service Unavailable even though we'd
        // probably be capable of perfectly handling that request, but it's
        // lesser evil than requiring the end user to worry about multiple
        // layers of in-socket buffering.
        err = _avs_coap_async_incoming_packet_handle_while_possible_without_blocking(
                ctx, acquired_in_buffer, acquired_in_buffer_size,
                reject_request, NULL);
    }
    _avs_coap_in_buffer_release(ctx);
    return err;
}

static avs_error_t try_wait_for_response(coap_stream_t *stream) {
    LOG(TRACE,
        _("waiting for response to ") "%s" _(" (exchange ID ") "%s" _(")"),
        AVS_COAP_CODE_STRING(stream->request_header.code),
        AVS_UINT64_AS_STRING(stream->exchange_id.value));

    avs_coap_ctx_t *ctx = coap_stream_owner_ctx(stream);
    // We are outside of the event loop, so we need to call the timeout handlers
    // manually. This may include handling timeouts for our own exchange, but
    // also for any other that might be ongoing.
    avs_time_monotonic_t next_timeout =
            _avs_coap_retry_or_request_expired_job(ctx);

    avs_error_t err = AVS_OK;
    if (!avs_coap_exchange_id_valid(stream->exchange_id)) {
        // exchange failed e.g. due to reaching MAX_RETRANSMIT number of
        // retransmissions
        assert(stream->state == COAP_STREAM_STATE_RECEIVING_RESPONSE);
        assert(avs_is_err(stream->err));
    } else if (avs_is_ok(stream->err)) {
        // next_timeout is the time until the next time
        // _avs_coap_retry_or_request_expired_job() is supposed to be called,
        // so we use that as the socket timeout.
        assert(avs_time_monotonic_valid(next_timeout));

        avs_net_socket_opt_value_t recv_timeout;
        recv_timeout.recv_timeout =
                avs_time_monotonic_diff(next_timeout, avs_time_monotonic_now());

        avs_net_socket_opt_value_t orig_recv_timeout;

        avs_net_socket_t *socket = _avs_coap_get_base(ctx)->socket;
        if (avs_is_err((err = avs_net_socket_get_opt(
                                socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                &orig_recv_timeout)))
                || avs_is_err((err = avs_net_socket_set_opt(
                                       socket,
                                       AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                       recv_timeout)))) {
            LOG(ERROR, _("could not set socket timeout"));
        } else {
            // _avs_coap_async_incoming_packet_acquire_in_buffer_and_handle_multiple()
            // cannot be used here, because we want to receive precisely one
            // packet here. The possible cases to be handled here:
            // - If we're called from flush_chunk(), the goal is to receive the
            //   2.31 Continue response, send the next request chunk (note that
            //   _avs_coap_async_incoming_packet_simple_handle() calls
            //   handle_response() and feed_payload_chunk() and sends that) and
            //   return the control - if that's the last chunk of request we
            //   just sent, we shall now proceed to receiving the response,
            //   which requires us to return control to the user so that they
            //   get the stream to read the response from, so we cannot receive
            //   actual response here - so we cannot receive more than one
            //   packet.
            // - If we're called from the end of perform_request() or from
            //   ensure_data_is_available_to_read(), the goal is to receive a
            //   chunk of the actual response. handle_response() will cache it
            //   in the buffer, and the async layer will send a request for the
            //   next BLOCK2 chunk if applicable, or finish the exchange
            //   otherwise.
            err = acquire_in_buffer_and_handle_incoming_packet(stream);
            if (err.category == AVS_ERRNO_CATEGORY
                    && err.code == AVS_ETIMEDOUT) {
                err = AVS_OK;
            }
            if (avs_is_err(
                        avs_net_socket_set_opt(socket,
                                               AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                                               orig_recv_timeout))) {
                LOG(ERROR, _("could not restore socket timeout"));
            }
        }
        if (avs_is_err(err)) {
            avs_coap_exchange_cancel(ctx, stream->exchange_id);
            coap_stream_set_error(stream, err);
        }
    }

    return stream->err;
}

static avs_error_t flush_chunk(coap_stream_t *stream) {
    assert(stream->state == COAP_STREAM_STATE_SENDING_REQUEST);

    avs_coap_ctx_t *ctx = coap_stream_owner_ctx(stream);
    if (!avs_coap_exchange_id_valid(stream->exchange_id)) {
        // We need to send the first (or only) request chunk, so we need to
        // create the underlying async exchange. feed_payload_chunk() will be
        // called during _avs_coap_retry_or_request_expired_job();
        // handle_response() is just configured, but not called just yet - the
        // response is received and handled later, within
        // try_wait_for_response() - see comments there for details.
        avs_error_t err = avs_coap_client_send_async_request(
                ctx, &stream->exchange_id, &stream->request_header,
                feed_payload_chunk, stream, handle_response, stream);
        if (avs_is_err(err)) {
            coap_stream_set_error(stream, err);
        } else {
            _avs_coap_retry_or_request_expired_job(ctx);
        }
        return stream->err;
    }

    // This is done in a loop, because try_wait_for_response() intentionally
    // returns success on timeout, and also might return success if it handled
    // something unrelated to this exchange (other async exchanges might be
    // handled "in the background").
    while (stream->state == COAP_STREAM_STATE_SENDING_REQUEST
           && stream->next_outgoing_chunk.expected_payload_size > 0) {
        // We need to send some non-first request chunk. We are being called
        // either from coap_write(), or just after payload writer; anyway, the
        // logic we are in is all about writing. To send another chunk, we need
        // to first receive the 2.31 Continue that we expect in response to the
        // previously sent chunk.
        // try_wait_for_response() will actually also call feed_payload_chunk()
        // and send that chunk. See comments inside for details.
        avs_error_t err = try_wait_for_response(stream);
        if (avs_is_err(err)) {
            return err;
        }
    }
    return AVS_OK;
}

static avs_error_t
get_next_outgoing_chunk_payload_size(coap_stream_t *stream,
                                     size_t *out_payload_size) {
    if (!stream->next_outgoing_chunk.expected_payload_size) {
        avs_error_t err;

        if (avs_coap_exchange_id_valid(stream->exchange_id)) {
            err = _avs_coap_exchange_get_next_outgoing_chunk_payload_size(
                    coap_stream_owner_ctx(stream), stream->exchange_id,
                    &stream->next_outgoing_chunk.expected_payload_size);
        } else {
            err = _avs_coap_get_first_outgoing_chunk_payload_size(
                    coap_stream_owner_ctx(stream),
                    stream->request_header.code,
                    &stream->request_header.options,
                    &stream->next_outgoing_chunk.expected_payload_size);
        }

        if (avs_is_err(err)) {
            return err;
        }
    }
    *out_payload_size = stream->next_outgoing_chunk.expected_payload_size;
    return AVS_OK;
}

static avs_error_t
coap_write(avs_stream_t *stream_, const void *data, size_t *data_length) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    if (stream->state != COAP_STREAM_STATE_SENDING_REQUEST) {
        LOG(ERROR,
            _("Could not write to CoAP stream: exchange already processed"));
        return avs_errno(AVS_EBADF);
    }

    size_t bytes_written = 0;
    while (bytes_written < *data_length) {
        size_t bytes_to_write =
                AVS_MIN(*data_length - bytes_written,
                        avs_buffer_space_left(stream->chunk_buffer));
        avs_buffer_append_bytes(stream->chunk_buffer,
                                (const char *) data + bytes_written,
                                bytes_to_write);
        bytes_written += bytes_to_write;
        size_t next_outgoing_chunk_payload_size;
        avs_error_t err = AVS_OK;
        while (avs_is_ok(err)
               && stream->state == COAP_STREAM_STATE_SENDING_REQUEST
               && avs_is_ok((err = get_next_outgoing_chunk_payload_size(
                                     stream,
                                     &next_outgoing_chunk_payload_size)))) {
            assert(avs_buffer_capacity(stream->chunk_buffer)
                   >= next_outgoing_chunk_payload_size);
            if (avs_buffer_data_size(stream->chunk_buffer)
                    < next_outgoing_chunk_payload_size) {
                break;
            }
            // Buffer filled, let's send the request packet
            err = flush_chunk(stream);
        }
        if (avs_is_err(err)) {
            return err;
        }
    }
    return AVS_OK;
}

static void
move_dynamic_response_header(coap_stream_t *stream,
                             avs_coap_response_header_t *out_header) {
    assert(stream->state == COAP_STREAM_STATE_RECEIVING_RESPONSE);
    assert(stream->response_header.options.allocated
           || !stream->response_header.options.capacity);
    // move the response header
    *out_header = stream->response_header;
    // reset the internal variable, so that there is only one copy of the
    // dynamically allocated options data
    stream->response_header.options = avs_coap_options_create_empty(NULL, 0);
}

static avs_error_t ensure_data_is_available_to_read(coap_stream_t *stream) {
    AVS_ASSERT(stream->state == COAP_STREAM_STATE_RECEIVING_RESPONSE,
               "coap_stream misused");

    // The purpose of this function is to ensure that at least one byte can be
    // read from the chunk_buffer.
    avs_error_t err = AVS_OK;
    while (avs_is_ok(err) && avs_buffer_data_size(stream->chunk_buffer) == 0) {
        if (!avs_coap_exchange_id_valid(stream->exchange_id)) {
            return stream->err;
        }
        // If the buffer is empty and if the exchange is still ongoing, it means
        // that we need to receive next BLOCK2 chunk of response
        err = try_wait_for_response(stream);
    }
    return err;
}

static avs_error_t coap_read(avs_stream_t *stream_,
                             size_t *out_bytes_read,
                             bool *out_message_finished,
                             void *buffer,
                             size_t buffer_length) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    avs_error_t err = ensure_data_is_available_to_read(stream);
    if (avs_is_err(err)) {
        return err;
    }

    size_t bytes_to_read =
            AVS_MIN(buffer_length, avs_buffer_data_size(stream->chunk_buffer));
    memcpy(buffer, avs_buffer_data(stream->chunk_buffer), bytes_to_read);
    avs_buffer_consume_bytes(stream->chunk_buffer, bytes_to_read);
    if (out_bytes_read) {
        *out_bytes_read = bytes_to_read;
    }
    if (out_message_finished) {
        *out_message_finished =
                (avs_buffer_data_size(stream->chunk_buffer) == 0
                 && !avs_coap_exchange_id_valid(stream->exchange_id));
    }
    return AVS_OK;
}

static avs_error_t
coap_peek(avs_stream_t *stream_, size_t offset, char *out_value) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    avs_error_t err = ensure_data_is_available_to_read(stream);
    if (avs_is_err(err)) {
        return err;
    }

    if (offset >= avs_buffer_data_size(stream->chunk_buffer)) {
        return AVS_EOF;
    }
    *out_value = avs_buffer_data(stream->chunk_buffer)[offset];
    return AVS_OK;
}

const avs_stream_v_table_t _AVS_COAP_STREAM_VTABLE = {
    .write_some = coap_write,
    .read = coap_read,
    .peek = coap_peek,
    .extension_list = AVS_STREAM_V_TABLE_NO_EXTENSIONS
};

static avs_error_t perform_request(coap_stream_t *coap_stream,
                                   const avs_coap_request_header_t *req,
                                   avs_coap_streaming_writer_t *write_payload,
                                   void *write_payload_arg) {
    if (coap_stream->state != COAP_STREAM_STATE_UNINITIALIZED) {
        LOG(DEBUG, _("discarding unread response data"));
        _avs_coap_stream_cleanup(coap_stream);
    }

    assert(coap_stream->vtable == &_AVS_COAP_STREAM_VTABLE);
    AVS_ASSERT(coap_stream->chunk_buffer == NULL,
               "chunk_buffer is not supposed to exist in UNINITIALIZED state");

    coap_stream->err = AVS_OK;
    coap_stream->request_header.code = req->code;
    avs_error_t err = _avs_coap_options_copy_as_dynamic(
            &coap_stream->request_header.options, &req->options);
    if (avs_is_err(err)) {
        LOG(ERROR, _("could not copy options: ") "%s", AVS_COAP_STRERROR(err));
        return err;
    }

    size_t buffer_size;
    if (avs_is_err((err = get_next_outgoing_chunk_payload_size(
                            coap_stream, &buffer_size)))) {
        return err;
    }
    avs_coap_ctx_t *coap_ctx = coap_stream_owner_ctx(coap_stream);
    const size_t in_buffer_capacity =
            _avs_coap_get_base(coap_ctx)->in_buffer->capacity;
    if (in_buffer_capacity > buffer_size) {
        buffer_size = in_buffer_capacity;
    }
    if (avs_buffer_create(&coap_stream->chunk_buffer, buffer_size)) {
        LOG(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }

    coap_stream_set_state(coap_stream, COAP_STREAM_STATE_SENDING_REQUEST);
    memset(&coap_stream->next_outgoing_chunk, 0,
           sizeof(coap_stream->next_outgoing_chunk));
    // write_payload() is expected to call coap_write(),
    // so see there for what happens next.
    if (write_payload
            && write_payload((avs_stream_t *) coap_stream, write_payload_arg)) {
        err = _avs_coap_err(AVS_COAP_ERR_PAYLOAD_WRITER_FAILED);
    }

    if (coap_stream->state != COAP_STREAM_STATE_SENDING_REQUEST) {
        // We have already received some kind of response. This might happen
        // even if write_payload() failed, e.g. if we received something else
        // than 2.31 Continue in response to a Block1 request. This might also
        // be an error (e.g. after receiving a UDP Reset message).
        err = coap_stream->err;
    } else if (avs_is_ok(err)) {
        // If we end up here, it means that coap_write() has either not been
        // called at all, or its calls have not filled the buffer enough to
        // send a BLOCK1 request - so let's send a non-BLOCK request now.
        err = flush_chunk(coap_stream);
        assert(avs_is_err(err)
               || avs_buffer_data_size(coap_stream->chunk_buffer) == 0);
    }
    // Now we ensure that we have at least one chunk of response data actually
    // buffered in the buffer - this will indirectly call handle_response().
    while (avs_is_ok(err)
           && avs_buffer_data_size(coap_stream->chunk_buffer) == 0
           && avs_coap_exchange_id_valid(coap_stream->exchange_id)) {
        err = try_wait_for_response(coap_stream);
    }
    return err;
}

avs_error_t
avs_coap_streaming_send_request(avs_coap_ctx_t *ctx,
                                const avs_coap_request_header_t *request,
                                avs_coap_streaming_writer_t *write_payload,
                                void *write_payload_arg,
                                avs_coap_response_header_t *out_response,
                                avs_stream_t **out_response_stream) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    out_response->options = avs_coap_options_create_empty(NULL, 0);
    avs_error_t err = perform_request(&coap_base->coap_stream, request,
                                      write_payload, write_payload_arg);
    if (avs_is_ok(err)) {
        // We have the first (possibly, but not necessarily, only) chunk of
        // response buffered. Let's return control to the user so that they can
        // read the response through the stream. If there are more chunks to be
        // received, coap_read() or coap_peek() will call
        // ensure_data_is_available_to_read(), so see there for what happens
        // next.
        move_dynamic_response_header(&coap_base->coap_stream, out_response);
        if (out_response_stream) {
            *out_response_stream = (avs_stream_t *) &coap_base->coap_stream;
            return AVS_OK;
        }
    }
    _avs_coap_stream_cleanup(&coap_base->coap_stream);
    return err;
}

void _avs_coap_stream_cleanup(coap_stream_t *stream) {
    avs_coap_exchange_cancel(coap_stream_owner_ctx(stream),
                             stream->exchange_id);
    avs_buffer_free(&stream->chunk_buffer);
    avs_coap_options_cleanup(&stream->request_header.options);
    avs_coap_options_cleanup(&stream->response_header.options);
    coap_stream_set_state(stream, COAP_STREAM_STATE_UNINITIALIZED);
}

#endif // WITH_AVS_COAP_STREAMING_API
