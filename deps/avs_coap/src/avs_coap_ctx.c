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

#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/code.h>

#include "options/avs_coap_iterator.h"

#include "async/avs_coap_async_client.h"
#include "async/avs_coap_exchange.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

#include "avs_coap_ctx.h"
#include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

avs_error_t _avs_coap_in_buffer_acquire(avs_coap_ctx_t *ctx,
                                        uint8_t **out_in_buffer,
                                        size_t *out_in_buffer_size) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    if (coap_base->in_buffer_in_use) {
        LOG(WARNING,
            _("double use of shared buffer. Note: calling "
              "handle_incoming_packet from within request handler is not "
              "supported"));
        return _avs_coap_err(AVS_COAP_ERR_SHARED_BUFFER_IN_USE);
    }

    coap_base->in_buffer_in_use = true;
    *out_in_buffer = avs_shared_buffer_acquire(coap_base->in_buffer);
    *out_in_buffer_size = coap_base->in_buffer->capacity;
    return AVS_OK;
}

void avs_coap_ctx_cleanup(avs_coap_ctx_t **ctx) {
    if (ctx && *ctx) {
        assert((*ctx)->vtable);
        assert((*ctx)->vtable->cleanup);

        avs_coap_base_t *coap_base = _avs_coap_get_base(*ctx);

        while (coap_base->client_exchanges) {
            avs_coap_exchange_cancel(*ctx, coap_base->client_exchanges->id);
        }
        while (coap_base->server_exchanges) {
            avs_coap_exchange_cancel(*ctx, coap_base->server_exchanges->id);
        }
#ifdef WITH_AVS_COAP_OBSERVE
        while (coap_base->observes) {
            _avs_coap_observe_cancel(*ctx, &coap_base->observes->id);
        }
#endif // WITH_AVS_COAP_OBSERVE
#ifdef WITH_AVS_COAP_STREAMING_API
        _avs_coap_stream_cleanup(&coap_base->coap_stream);
#endif // WITH_AVS_COAP_STREAMING_API

        avs_sched_del(&coap_base->retry_or_request_expired_job);

        (*ctx)->vtable->cleanup(*ctx);
        *ctx = NULL;
    }
}

avs_error_t _avs_coap_ctx_generate_token(avs_crypto_prng_ctx_t *prng_ctx,
                                         avs_coap_token_t *out_token) {
    /**
     * One might be tempted to use sequential tokens to avoid collisions as
     * much as possible, but that is explicitly discouraged by the CoAP spec
     * (RFC 7252, 5.3.1 "Token"):
     *
     * > A client sending a request without using Transport Layer Security
     * > (Section 9) SHOULD use a nontrivial, randomized token to guard
     * > against spoofing of responses (Section 11.4).
     */

    if (avs_crypto_prng_bytes(prng_ctx, (unsigned char *) &out_token->bytes,
                              sizeof(out_token->bytes))) {
        LOG(ERROR, _("failed to generate token"));
        out_token->size = 0;
        return _avs_coap_err(AVS_COAP_ERR_PRNG_FAIL);
    }
    out_token->size = sizeof(out_token->bytes);
    return AVS_OK;
}

AVS_LIST(avs_coap_exchange_t) *
_avs_coap_find_exchange_ptr_by_id(AVS_LIST(avs_coap_exchange_t) *list_ptr,
                                  avs_coap_exchange_id_t id) {
    AVS_LIST(avs_coap_exchange_t) *it;
    AVS_LIST_FOREACH_PTR(it, list_ptr) {
        if (avs_coap_exchange_id_equal(id, (*it)->id)) {
            return it;
        }
    }
    return NULL;
}

void avs_coap_exchange_cancel(avs_coap_ctx_t *ctx, avs_coap_exchange_id_t id) {
    if (!avs_coap_exchange_id_valid(id)) {
        return;
    }

    AVS_LIST(avs_coap_exchange_t) *exchange_ptr;

    exchange_ptr = _avs_coap_find_client_exchange_ptr_by_id(ctx, id);
    if (exchange_ptr) {
        _avs_coap_client_exchange_cleanup(ctx, AVS_LIST_DETACH(exchange_ptr),
                                          AVS_OK);
        return;
    }

    exchange_ptr = _avs_coap_find_server_exchange_ptr_by_id(ctx, id);
    if (exchange_ptr) {
        _avs_coap_server_exchange_cleanup(
                ctx, AVS_LIST_DETACH(exchange_ptr),
                _avs_coap_err(AVS_COAP_ERR_EXCHANGE_CANCELED));
    }
}

const char *avs_coap_strerror(avs_error_t error, char *buf, size_t buf_size) {
    if (avs_is_ok(error)) {
        return "no error";
    }
    switch (error.category) {
    case AVS_COAP_ERR_CATEGORY:
        switch ((avs_coap_error_t) error.code) {
        case AVS_COAP_ERR_SHARED_BUFFER_IN_USE:
            return "shared buffer in use";
        case AVS_COAP_ERR_SOCKET_ALREADY_SET:
            return "socket already set";
        case AVS_COAP_ERR_PAYLOAD_WRITER_FAILED:
            return "payload writer failed";
        case AVS_COAP_ERR_MESSAGE_TOO_BIG:
            return "message too big";
        case AVS_COAP_ERR_TIME_INVALID:
            return "time invalid";
        case AVS_COAP_ERR_EXCHANGE_CANCELED:
            return "exchange canceled";
        case AVS_COAP_ERR_UDP_RESET_RECEIVED:
            return "UDP Reset received";
        case AVS_COAP_ERR_MALFORMED_MESSAGE:
            return "malformed message";
        case AVS_COAP_ERR_MALFORMED_OPTIONS:
            return "malformed options list";
        case AVS_COAP_ERR_BLOCK_SIZE_RENEGOTIATION_INVALID:
            return "block size renegotiation invalid";
        case AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED:
            return "truncated message received";
        case AVS_COAP_ERR_BLOCK_SEQ_NUM_OVERFLOW:
            return "block seq num overflow";
        case AVS_COAP_ERR_ETAG_MISMATCH:
            return "ETag mismatch";
        case AVS_COAP_ERR_UNEXPECTED_CONTINUE_RESPONSE:
            return "unexpected Continue response";
        case AVS_COAP_ERR_TIMEOUT:
            return "timeout";
        case AVS_COAP_ERR_MORE_DATA_REQUIRED:
            return "more data required";
        case AVS_COAP_ERR_TCP_ABORT_SENT:
            return "TCP Abort sent";
        case AVS_COAP_ERR_TCP_ABORT_RECEIVED:
            return "TCP Abort received";
        case AVS_COAP_ERR_TCP_RELEASE_RECEIVED:
            return "TCP Release received";
        case AVS_COAP_ERR_TCP_CSM_NOT_RECEIVED:
            return "TCP CSM options not received";
        case AVS_COAP_ERR_TCP_MALFORMED_CSM_OPTIONS_RECEIVED:
            return "TCP malformed CSM options received";
        case AVS_COAP_ERR_TCP_UNKNOWN_CSM_CRITICAL_OPTION_RECEIVED:
            return "TCP unknown CSM critical option received";
        case AVS_COAP_ERR_TCP_CONN_CLOSED:
            return "TCP connection closed by peer";
        case AVS_COAP_ERR_ASSERT_FAILED:
            return "assert failed";
        case AVS_COAP_ERR_NOT_IMPLEMENTED:
            return "feature not implemented";
        case AVS_COAP_ERR_FEATURE_DISABLED:
            return "feature disabled";
        case AVS_COAP_ERR_OSCORE_DATA_TOO_BIG:
            return "OSCORE data too big";
        case AVS_COAP_ERR_OSCORE_NEEDS_RECREATE:
            return "OSCORE security context outdated";
        case AVS_COAP_ERR_OSCORE_OPTION_MISSING:
            return "OSCORE option missing in message received by OSCORE "
                   "context";
        case AVS_COAP_ERR_PRNG_FAIL:
            return "PRNG failure";
        }
        break;

    case AVS_ERRNO_CATEGORY:
        return avs_strerror((avs_errno_t) error.code);
    }

    if (buf_size
            && avs_simple_snprintf(buf, buf_size,
                                   "unknown error, category %" PRIu16
                                   ", code %" PRIu16,
                                   error.category, error.code)
                           >= 0) {
        return buf;
    }
    return "unknown error";
}

static void retry_or_request_expired_job(avs_sched_t *sched,
                                         const void *ctx_ptr) {
    (void) sched;

    avs_coap_ctx_t *ctx = *(avs_coap_ctx_t *const *) ctx_ptr;
    _avs_coap_retry_or_request_expired_job(ctx);
}

void _avs_coap_reschedule_retry_or_request_expired_job(
        avs_coap_ctx_t *ctx, avs_time_monotonic_t target_time) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    if (avs_time_monotonic_before(
                avs_sched_time(&coap_base->retry_or_request_expired_job),
                target_time)) {
        return;
    }

    if (AVS_SCHED_AT(coap_base->sched, &coap_base->retry_or_request_expired_job,
                     target_time, retry_or_request_expired_job, &ctx,
                     sizeof(ctx))) {
        LOG(ERROR, _("unable to reschedule timeout job"));
    }
}

avs_time_monotonic_t
_avs_coap_retry_or_request_expired_job(avs_coap_ctx_t *ctx) {
    avs_sched_del(&_avs_coap_get_base(ctx)->retry_or_request_expired_job);

    AVS_LIST(avs_coap_exchange_t) *exchange_head =
            &_avs_coap_get_base(ctx)->client_exchanges;
    AVS_LIST(avs_coap_exchange_t) *exchange_ptr = exchange_head;
    while (*exchange_ptr
           && !_avs_coap_client_exchange_request_sent(*exchange_ptr)) {
        AVS_LIST(avs_coap_exchange_t) *exchange_ptr_copy = exchange_ptr;
        avs_error_t err =
                _avs_coap_client_exchange_send_first_chunk(ctx,
                                                           &exchange_ptr_copy);
        if (avs_is_err(err) && exchange_ptr_copy) {
            _avs_coap_client_exchange_cleanup(
                    ctx, AVS_LIST_DETACH(exchange_ptr_copy), err);
            exchange_ptr_copy = NULL;
        }
        if (exchange_ptr_copy) {
            exchange_ptr = exchange_ptr_copy;
            AVS_LIST_ADVANCE_PTR(&exchange_ptr);
        } else {
            // ANY exchange pointers may have been invalidated. We need to find
            // our iteration pointer anew. Please note that if we have already
            // sent some packets during this iteration, the portion of the list
            // with unsent messages may be temporarily in the middle.
            exchange_ptr = exchange_head;
            while (*exchange_ptr
                   && _avs_coap_client_exchange_request_sent(*exchange_ptr)) {
                AVS_LIST_ADVANCE_PTR(&exchange_ptr);
            }
        }
    }

    avs_time_monotonic_t next_timeout =
            _avs_coap_async_server_abort_timedout_exchanges(ctx);

    if (ctx->vtable->on_timeout) {
        avs_time_monotonic_t transport_timeout = ctx->vtable->on_timeout(ctx);
        if (!avs_time_monotonic_valid(next_timeout)
                || avs_time_monotonic_before(transport_timeout, next_timeout)) {
            next_timeout = transport_timeout;
        }
    }

    if (avs_time_monotonic_valid(next_timeout)) {
        _avs_coap_reschedule_retry_or_request_expired_job(ctx, next_timeout);
    }

    return next_timeout;
}

avs_error_t avs_coap_ctx_set_socket(avs_coap_ctx_t *ctx,
                                    avs_net_socket_t *socket) {
    if (!ctx->vtable->setsock) {
        return _avs_coap_err(AVS_COAP_ERR_NOT_IMPLEMENTED);
    }
    return ctx->vtable->setsock(ctx, socket);
}

bool avs_coap_ctx_has_socket(avs_coap_ctx_t *ctx) {
    return _avs_coap_get_base(ctx)->socket != NULL;
}

size_t avs_coap_max_incoming_message_payload(avs_coap_ctx_t *ctx,
                                             const avs_coap_options_t *options,
                                             uint8_t code) {
    return ctx->vtable->max_incoming_payload_size(
            ctx, AVS_COAP_MAX_TOKEN_LENGTH, options, code);
}

#ifdef WITH_AVS_COAP_BLOCK
static avs_error_t get_payload_chunk_size(avs_coap_ctx_t *ctx,
                                          uint8_t code,
                                          const avs_coap_option_block_t *block,
                                          const avs_coap_options_t *options,
                                          size_t *out_payload_chunk_size) {
    assert(options);

    if (block) {
        const size_t max_payload_size = ctx->vtable->max_outgoing_payload_size(
                ctx, AVS_COAP_MAX_TOKEN_LENGTH, options, code);

        *out_payload_chunk_size = avs_max_power_of_2_not_greater_than(
                AVS_MIN(AVS_COAP_BLOCK_MAX_SIZE,
                        AVS_MIN(max_payload_size, block->size)));
        if (*out_payload_chunk_size < block->size) {
            return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
        }
        return AVS_OK;
    } else {
        size_t max_payload_size = ctx->vtable->max_outgoing_payload_size(
                ctx, AVS_COAP_MAX_TOKEN_LENGTH, options, code);

        /**
         * We're sending the first block of a request, or a response for which
         * the requester indicated no block size preference. The transfer may
         * not even need BLOCK. We can freely choose any payload size.
         *
         * When calculating max_payload_size, take into account that we may
         * need to add a BLOCK option if the payload turns out to be large.
         */
        if (max_payload_size > AVS_COAP_OPT_BLOCK_MAX_SIZE) {
            max_payload_size -= AVS_COAP_OPT_BLOCK_MAX_SIZE;
        } else {
            max_payload_size = 0;
        }

        *out_payload_chunk_size = avs_max_power_of_2_not_greater_than(
                AVS_MIN(max_payload_size, AVS_COAP_BLOCK_MAX_SIZE));
        if (*out_payload_chunk_size < AVS_COAP_BLOCK_MIN_SIZE) {
            return _avs_coap_err(AVS_COAP_ERR_MESSAGE_TOO_BIG);
        }

        return AVS_OK;
    }
}
#else  // WITH_AVS_COAP_BLOCK
static avs_error_t
get_payload_chunk_size(avs_coap_ctx_t *ctx,
                       uint8_t code,
                       const avs_coap_option_block_t *block_opt,
                       const avs_coap_options_t *options,
                       size_t *out_payload_chunk_size) {
    (void) block_opt;

    const size_t max_payload_size = ctx->vtable->max_outgoing_payload_size(
            ctx, AVS_COAP_MAX_TOKEN_LENGTH, options, code);
    *out_payload_chunk_size =
            AVS_MIN(max_payload_size,
                    AVS_COAP_EXCHANGE_OUTGOING_CHUNK_PAYLOAD_MAX_SIZE - 1);
    return AVS_OK;
}
#endif // WITH_AVS_COAP_BLOCK

avs_error_t _avs_coap_get_max_block_size(avs_coap_ctx_t *ctx,
                                         uint8_t code,
                                         const avs_coap_options_t *options,
                                         size_t *out_payload_chunk_size) {
    assert(options);

    avs_coap_option_block_t block;
    bool has_block = false;

#ifdef WITH_AVS_COAP_BLOCK
    avs_error_t err = _avs_coap_options_get_block_by_code(options, code, &block,
                                                          &has_block);
    if (avs_is_err(err)) {
        return err;
    }
#endif // WITH_AVS_COAP_BLOCK

    return get_payload_chunk_size(ctx, code, has_block ? &block : NULL, options,
                                  out_payload_chunk_size);
}

avs_coap_stats_t avs_coap_get_stats(avs_coap_ctx_t *ctx) {
    if (ctx->vtable->get_stats) {
        return ctx->vtable->get_stats(ctx);
    }
    return (avs_coap_stats_t) { 0 };
}

static bool
is_critical_opt_valid(uint8_t msg_code,
                      uint32_t opt_number,
                      avs_coap_critical_option_validator_t fallback_validator) {
    switch (opt_number) {
    case AVS_COAP_OPTION_BLOCK1:
        return msg_code == AVS_COAP_CODE_PUT || msg_code == AVS_COAP_CODE_POST
               || msg_code == AVS_COAP_CODE_FETCH
               || msg_code == AVS_COAP_CODE_IPATCH;
    case AVS_COAP_OPTION_BLOCK2:
        return msg_code == AVS_COAP_CODE_GET || msg_code == AVS_COAP_CODE_PUT
               || msg_code == AVS_COAP_CODE_POST
               || msg_code == AVS_COAP_CODE_FETCH
               || msg_code == AVS_COAP_CODE_IPATCH;
    default:
        return fallback_validator(msg_code, opt_number);
    }
}

int avs_coap_options_validate_critical(
        const avs_coap_request_header_t *request_header,
        avs_coap_critical_option_validator_t validator) {
    avs_coap_request_header_t *const_cast_request_header =
            (avs_coap_request_header_t *) (intptr_t) request_header;

    avs_coap_option_iterator_t it;
    for (it = _avs_coap_optit_begin(&const_cast_request_header->options);
         !_avs_coap_optit_end(&it);
         _avs_coap_optit_next(&it)) {
        const uint32_t opt_number = _avs_coap_optit_number(&it);
        if (_avs_coap_option_is_critical((uint16_t) opt_number)
                && !is_critical_opt_valid(request_header->code, opt_number,
                                          validator)) {
            LOG(DEBUG,
                _("warning: invalid critical option in query ") "%s" _(
                        ": ") "%" PRIu32,
                AVS_COAP_CODE_STRING(request_header->code), opt_number);
            return -1;
        }
    }
    return 0;
}
