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

#ifndef AVS_COAP_SRC_CTX_H
#define AVS_COAP_SRC_CTX_H

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_prng.h>
#include <avsystem/commons/avs_shared_buffer.h>

#include <avsystem/coap/async.h>
#include <avsystem/coap/ctx.h>

#ifdef WITH_AVS_COAP_OBSERVE
#    include "avs_coap_observe.h"
#endif // WITH_AVS_COAP_OBSERVE

#ifdef WITH_AVS_COAP_STREAMING_API
#    include "streaming/avs_coap_streaming_client.h"
#endif // WITH_COAP_STREAMING_API

#include "async/avs_coap_async_server.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

static inline avs_error_t _avs_coap_err(avs_coap_error_t error) {
    assert(error > 0 && error <= UINT16_MAX);
    return (avs_error_t) {
        .category = AVS_COAP_ERR_CATEGORY,
        .code = (uint16_t) error
    };
}

struct avs_coap_ctx_vtable;

/**
 * Abstract CoAP context.
 */
struct avs_coap_ctx {
    const struct avs_coap_ctx_vtable *vtable;
};

/**
 * CoAP base, containing only stuff that's completely independent from the
 * transport protocol used.
 */
struct avs_coap_base {
    /** Last assigned exchange ID. */
    avs_coap_exchange_id_t last_exchange_id;

    /**
     * All unfinished asynchronous request exchanges initiated by us acting
     * as a CoAP client (outgoing requests/incoming responses).
     *
     * NOTE: Exchanges for which the initial request packet has not yet been
     * sent are always kept at the beginning of this list.
     */
    AVS_LIST(struct avs_coap_exchange) client_exchanges;

    /**
     * All unfinished asynchronous request exchanges initiated by remote CoAP
     * client (incoming requests/outgoing responses).
     */
    AVS_LIST(struct avs_coap_exchange) server_exchanges;

#ifdef WITH_AVS_COAP_OBSERVE
    /** Active observations. */
    AVS_LIST(avs_coap_observe_t) observes;
#endif // WITH_AVS_COAP_OBSERVE

    /** PRNG context. */
    avs_crypto_prng_ctx_t *prng_ctx;

#ifdef WITH_AVS_COAP_STREAMING_API
    /** Stream object used by streaming API. */
    coap_stream_t coap_stream;
#endif // WITH_AVS_COAP_STREAMING_API

    avs_net_socket_t *socket;
    avs_shared_buffer_t *in_buffer;
    avs_shared_buffer_t *out_buffer;

    avs_sched_t *sched;

    /**
     * Scheduler job used to detect cases where the remote host lost interest
     * in a block-wise request before it completed, or to handle any
     * time-dependent actions required by the transport (e.g. retransmissions
     * or request timeouts).
     */
    avs_sched_handle_t retry_or_request_expired_job;

    /* Used to ensure in_buffer is not used twice. */
    bool in_buffer_in_use;

    /* State necessary for handling incoming requests. */
    avs_coap_request_ctx_t request_ctx;
};

static inline avs_coap_base_t *_avs_coap_get_base(avs_coap_ctx_t *ctx) {
    return ctx->vtable->get_base(ctx);
}

avs_error_t _avs_coap_in_buffer_acquire(avs_coap_ctx_t *ctx,
                                        uint8_t **out_in_buffer,
                                        size_t *out_in_buffer_size);

static inline void _avs_coap_in_buffer_release(avs_coap_ctx_t *ctx) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    assert(coap_base->in_buffer_in_use);
    avs_shared_buffer_release(coap_base->in_buffer);
    coap_base->in_buffer_in_use = false;
}

#if defined(WITH_AVS_COAP_POISONING) && !defined(AVS_UNIT_TESTING)
// No CoAP context should use scheduler jobs other than
// @ref avs_coap_ctx_t#retry_or_request_expired_job ; this is necessary to
// properly support streaming API
#    pragma GCC poison avs_sched_handle_t
#endif

/**
 * Utility methods not specific to any particular protocol.
 * @{
 */

static inline void _avs_coap_base_init(avs_coap_base_t *base,
                                       avs_coap_ctx_t *coap_ctx,
                                       avs_shared_buffer_t *in_buffer,
                                       avs_shared_buffer_t *out_buffer,
                                       avs_sched_t *sched,
                                       avs_crypto_prng_ctx_t *prng_ctx) {
    base->last_exchange_id = AVS_COAP_EXCHANGE_ID_INVALID;
    base->client_exchanges = NULL;
    base->server_exchanges = NULL;
    base->prng_ctx = prng_ctx;
    base->socket = NULL;
    base->in_buffer = in_buffer;
    base->out_buffer = out_buffer;
    base->sched = sched;
#ifdef WITH_AVS_COAP_STREAMING_API
    _avs_coap_stream_init(&base->coap_stream, coap_ctx);
#else  // WITH_AVS_COAP_STREAMING_API
    (void) coap_ctx;
#endif // WITH_AVS_COAP_STREAMING_API
}

static inline avs_coap_ctx_t *
_avs_coap_ctx_from_request_ctx(avs_coap_request_ctx_t *request_ctx) {
    return request_ctx->coap_ctx;
}

static inline avs_error_t
_avs_coap_ctx_set_socket_base(avs_coap_ctx_t *ctx, avs_net_socket_t *socket) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    if (coap_base->socket != NULL) {
        LOG(ERROR, _("cannot set socket: it was already set"));
        return _avs_coap_err(AVS_COAP_ERR_SOCKET_ALREADY_SET);
    }
    coap_base->socket = socket;
    return AVS_OK;
}

static inline avs_coap_exchange_id_t
_avs_coap_generate_exchange_id(avs_coap_ctx_t *ctx) {
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    // TODO: handle 64bit value overflow?
    ++coap_base->last_exchange_id.value;
    return coap_base->last_exchange_id;
}

AVS_LIST(struct avs_coap_exchange) *
_avs_coap_find_exchange_ptr_by_id(AVS_LIST(struct avs_coap_exchange) *list_ptr,
                                  avs_coap_exchange_id_t id);

AVS_LIST(struct avs_coap_exchange) *_avs_coap_find_exchange_ptr_by_token(
        AVS_LIST(struct avs_coap_exchange) *list_ptr,
        const avs_coap_token_t *token);

static inline AVS_LIST(struct avs_coap_exchange) *
_avs_coap_find_client_exchange_ptr_by_id(avs_coap_ctx_t *ctx,
                                         avs_coap_exchange_id_t id) {
    return _avs_coap_find_exchange_ptr_by_id(
            &_avs_coap_get_base(ctx)->client_exchanges, id);
}

static inline AVS_LIST(struct avs_coap_exchange) *
_avs_coap_find_server_exchange_ptr_by_id(avs_coap_ctx_t *ctx,
                                         avs_coap_exchange_id_t id) {
    return _avs_coap_find_exchange_ptr_by_id(
            &_avs_coap_get_base(ctx)->server_exchanges, id);
}

static inline AVS_LIST(struct avs_coap_exchange)
_avs_coap_find_client_exchange_by_id(avs_coap_ctx_t *ctx,
                                     avs_coap_exchange_id_t id) {
    AVS_LIST(struct avs_coap_exchange) *ptr =
            _avs_coap_find_client_exchange_ptr_by_id(ctx, id);
    if (ptr) {
        return *ptr;
    }
    return NULL;
}

static inline AVS_LIST(struct avs_coap_exchange)
_avs_coap_find_server_exchange_by_id(avs_coap_ctx_t *ctx,
                                     avs_coap_exchange_id_t id) {
    AVS_LIST(struct avs_coap_exchange) *ptr =
            _avs_coap_find_server_exchange_ptr_by_id(ctx, id);
    if (ptr) {
        return *ptr;
    }
    return NULL;
}

#ifdef WITH_AVS_COAP_OBSERVE
static inline bool _avs_coap_is_observe(avs_coap_ctx_t *ctx,
                                        const avs_coap_token_t *token) {
    AVS_LIST(avs_coap_observe_t) it;
    AVS_LIST_FOREACH(it, _avs_coap_get_base(ctx)->observes) {
        if (avs_coap_token_equal(&it->id.token, token)) {
            return true;
        }
    }
    return false;
}
#endif // WITH_AVS_COAP_OBSERVE

static inline AVS_LIST(struct avs_coap_exchange)
_avs_coap_find_exchange_by_id(avs_coap_ctx_t *ctx, avs_coap_exchange_id_t id) {
    AVS_LIST(struct avs_coap_exchange) exchange =
            _avs_coap_find_client_exchange_by_id(ctx, id);
    if (!exchange) {
        exchange = _avs_coap_find_server_exchange_by_id(ctx, id);
    }
    return exchange;
}

avs_error_t
_avs_coap_ctx_generate_token(avs_crypto_prng_ctx_t *prng_ctx,
                             avs_coap_token_t *out_token) WEAK_IN_TESTS;

/**
 * Reschedules @ref _avs_coap_retry_or_request_expired_job to be called no
 * later than at @p target_time. If it is already scheduled for an earlier
 * time point, this call does nothing.
 */
void _avs_coap_reschedule_retry_or_request_expired_job(
        avs_coap_ctx_t *ctx, avs_time_monotonic_t target_time);

/**
 * Calls @ref avs_coap_ctx_vtable_t#on_retry_or_request_expired handler and
 * handles any transport-agnostic timeouts as necessary.
 *
 * Reschedules itself for execution at appropriate time.
 *
 * Note: the streaming API uses this function outside the scheduler to be able
 * to handle any retransmissions required for synchronous request processing.
 *
 * @returns Time point at which next execution of this job was scheduled
 */
avs_time_monotonic_t
_avs_coap_retry_or_request_expired_job(avs_coap_ctx_t *ctx);

/**
 * Queries the maximum number of payload bytes possible to include in a CoAP
 * message with given @p code and @p options when sending it using @p ctx .
 *
 * @param ctx
 * CoAP context to operate on.
 *
 * @param code
 * CoAP code of the message to calculate size for. Used to determine if the
 * message is a request or response, necessary for inspecting the BLOCK option.
 *
 * @param options
 * CoAP options list that will be included in sent message.
 *
 * @param[out] out_payload_chunk_size
 * On successful call, it is set to the calculated number of payload bytes.
 *
 * @returns
 *
 * @li @ref AVS_OK when the size was calculated correctly,
 * @li @ref AVS_COAP_ERR_MESSAGE_TOO_BIG when either:
 *
 *     @li calculated payload size is smaller than smallest possible block size,
 *         which means that payload size is limited to just a few bytes and
 *         BLOCK-wise transfer is impossible,
 *     @li @p options contains a BLOCK option with size larger than the
 *         calculated one, making it impossible to include that much data in
 *         a packet sent using @p ctx . The caller may attempt to lower that
 *         BLOCK size to be able to continue.
 *
 *     In either case, @p out_payload_chunk_size is set to the calculated
 *     payload size.
 *
 * @li other error code, in which case @p out_payload_chunk_size value is
 *     undefined.
 */
avs_error_t _avs_coap_get_max_block_size(avs_coap_ctx_t *ctx,
                                         uint8_t code,
                                         const avs_coap_options_t *options,
                                         size_t *out_payload_chunk_size);

/*
 * Queries the expected size of the chunk that will be requested during the
 * first call to @ref avs_coap_payload_writer_t for an newly created exchange
 * with given @p code and @p options .
 *
 * NOTE: this accounts for the BLOCK size *and* extra byte required for EOF
 * detection.
 */
static inline avs_error_t _avs_coap_get_first_outgoing_chunk_payload_size(
        avs_coap_ctx_t *ctx,
        uint8_t code,
        const avs_coap_options_t *options,
        size_t *out_payload_chunk_size) {
    avs_error_t err = _avs_coap_get_max_block_size(ctx, code, options,
                                                   out_payload_chunk_size);
    if (avs_is_ok(err)) {
        // +1 for EOF detection
        ++*out_payload_chunk_size;
    }
    return err;
}

/** @} */

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_CTX_H
