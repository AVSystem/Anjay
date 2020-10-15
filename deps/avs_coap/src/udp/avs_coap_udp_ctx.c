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

#ifdef WITH_AVS_COAP_UDP

#    include <assert.h>
#    include <inttypes.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_shared_buffer.h>
#    include <avsystem/commons/avs_socket.h>
#    include <avsystem/commons/avs_utils.h>

#    include <avsystem/coap/option.h>
#    include <avsystem/coap/udp.h>

#    include "avs_coap_code_utils.h"
#    include "avs_coap_ctx_vtable.h"
#    include "options/avs_coap_option.h"

#    include "udp/avs_coap_udp_header.h"
#    include "udp/avs_coap_udp_msg.h"
#    include "udp/avs_coap_udp_msg_cache.h"

#    define MODULE_NAME coap_udp
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_common_utils.h"
#    include "avs_coap_ctx.h"
#    include "options/avs_coap_options.h"
#    include "udp/avs_coap_udp_tx_params.h"

VISIBILITY_SOURCE_BEGIN

/**
 * Owning wrapper around an unconfirmed outgoing CoAP/UDP message.
 *
 * List of CoAP/UDP exchanges is kept sorted by (hold, next_retransmit) tuple:
 *
 * - up to NSTART first entries are "not held", i.e. are currently being
 *   retransmitted,
 *
 * - if more than NSTART exchanges were created, the rest is "held",
 *   i.e. not transmitted at all to honor NSTART defined by RFC7252.
 *
 * Whenever an exchange is retransmitted, next_retransmit is updated to the
 * time of a next retransmission, and the exchange entry moved to appropriate
 * place in the exchange list to keep described ordering.
 */
typedef struct {
    /** Handler to call when context is done with the message */
    avs_coap_send_result_handler_t *send_result_handler;
    /** Opaque argument to pass to send_result_handler */
    void *send_result_handler_arg;

    avs_coap_retry_state_t retry_state;

    /** If true, exchange retransmissions are disabled due to NSTART */
    bool hold;

    /** Time at which this packet has to be retransmitted next time. */
    avs_time_monotonic_t next_retransmit;

    /** CoAP message view. Points to @ref avs_coap_udp_exchange_t#packet . */
    avs_coap_udp_msg_t msg;

    /** Number of initialized bytes in @ref avs_coap_udp_exchange_t#packet . */
    size_t packet_size;

    /** Serialized packet data. */
    uint8_t packet[];
} avs_coap_udp_unconfirmed_msg_t;

#    ifdef WITH_AVS_COAP_OBSERVE
typedef struct {
    uint16_t msg_id;
    avs_coap_token_t token;
} avs_coap_udp_sent_notify_t;

/**
 * Fixed-size cache with queue semantics used to store (message ID, token)
 * pairs of recently sent notification messages.
 *
 * RFC 7641 defines Reset response to sent notification to be a preferred
 * method of cancelling an established observation. This cache allows us to
 * match incoming Reset messages to established observations so that we can
 * cancel them.
 *
 * Technically, entries in this cache should expire after MAX_TRANSMIT_WAIT
 * since the first retransmission, but we keep them around as long as there
 * is enough space and we don't try to reuse the same message ID. That means
 * some Reset messages may not cancel observations if notifications are
 * generated at a high rate, or that Reset messages that come later are still
 * handled as valid observe cancellation.
 *
 * This implementation trades correctness in all cases for simplicity.
 */
typedef struct {
    avs_coap_udp_sent_notify_t entries[AVS_COAP_UDP_NOTIFY_CACHE_SIZE];
    size_t size;
} avs_coap_udp_notify_cache_t;

AVS_STATIC_ASSERT(AVS_COAP_UDP_NOTIFY_CACHE_SIZE > 0,
                  notify_cache_must_have_at_least_one_element);

static inline const avs_coap_token_t *
coap_udp_notify_cache_get(const avs_coap_udp_notify_cache_t *cache,
                          uint16_t msg_id) {
    for (size_t i = 0; i < cache->size; ++i) {
        if (cache->entries[i].msg_id == msg_id) {
            return &cache->entries[i].token;
        }
    }

    return NULL;
}

static inline void
coap_udp_notify_cache_drop_entry(avs_coap_udp_notify_cache_t *cache,
                                 avs_coap_udp_sent_notify_t *entry) {
    const avs_coap_udp_sent_notify_t *end = cache->entries + cache->size;

    assert(cache->entries <= entry && entry < cache->entries + cache->size);
    assert(entry < end);

    memmove(entry, entry + 1,
            (size_t) (end - entry - 1) * sizeof(cache->entries[0]));
    --cache->size;
}

static inline void
coap_udp_notify_cache_drop(avs_coap_udp_notify_cache_t *cache,
                           uint16_t msg_id) {
    for (size_t i = 0; i < cache->size; ++i) {
        if (cache->entries[i].msg_id == msg_id) {
            coap_udp_notify_cache_drop_entry(cache, &cache->entries[i]);

            // cache is not supposed to have more than one entry with the same
            // ID at the same time
            assert(coap_udp_notify_cache_get(cache, msg_id) == NULL);
            return;
        }
    }
}

static inline void coap_udp_notify_cache_put(avs_coap_udp_notify_cache_t *cache,
                                             uint16_t msg_id,
                                             const avs_coap_token_t *token) {
    if (cache->size == AVS_ARRAY_SIZE(cache->entries)) {
        coap_udp_notify_cache_drop_entry(cache, &cache->entries[0]);
    }
    assert(cache->size < AVS_ARRAY_SIZE(cache->entries));

    cache->entries[cache->size] = (avs_coap_udp_sent_notify_t) {
        .msg_id = msg_id,
        .token = *token
    };

    ++cache->size;
}
#    endif // WITH_AVS_COAP_OBSERVE

typedef struct {
    const struct avs_coap_ctx_vtable *vtable;

    avs_coap_base_t base;

    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) unconfirmed_messages;

    avs_net_socket_t *socket;
    size_t last_mtu;
    size_t forced_incoming_mtu;
    avs_coap_udp_tx_params_t tx_params;

    avs_coap_stats_t stats;

    uint16_t last_msg_id;

    /**
     * Any Piggybacked response we send MUST echo message ID of received
     * request. Its ID/token pair is stored here to ensure that.
     */
    struct {
        /** true if we're currently processing a request */
        bool exists;
        uint16_t msg_id;
        avs_coap_token_t token;
    } current_request;

    avs_coap_udp_response_cache_t *response_cache;
#    ifdef WITH_AVS_COAP_OBSERVE
    avs_coap_udp_notify_cache_t notify_cache;
#    endif // WITH_AVS_COAP_OBSERVE
} avs_coap_udp_ctx_t;

AVS_STATIC_ASSERT(offsetof(avs_coap_udp_ctx_t, vtable) == 0,
                  vtable_field_must_be_first_in_udp_ctx_t);

static void update_last_mtu_from_socket(avs_coap_udp_ctx_t *ctx) {
    avs_net_socket_opt_value_t opt_value;

    if (avs_is_err(avs_net_socket_get_opt(
                ctx->base.socket, AVS_NET_SOCKET_OPT_INNER_MTU, &opt_value))) {
        LOG(DEBUG, _("socket MTU unknown"));
    } else if (opt_value.mtu <= 0) {
        LOG(DEBUG, _("socket MTU invalid: ") "%d", opt_value.mtu);
    } else {
        if ((size_t) opt_value.mtu != ctx->last_mtu) {
            LOG(DEBUG, _("socket MTU changed: ") "%u" _(" -> ") "%d",
                (unsigned) ctx->last_mtu, opt_value.mtu);
        } else {
            LOG(TRACE, _("socket MTU: ") "%d", opt_value.mtu);
        }

        ctx->last_mtu = (size_t) opt_value.mtu;
    }
}

static size_t udp_max_payload_size(size_t buffer_capacity,
                                   size_t mtu,
                                   size_t token_size,
                                   size_t options_size) {
    const size_t msg_size = (sizeof(avs_coap_udp_header_t) + token_size
                             + options_size + sizeof(AVS_COAP_PAYLOAD_MARKER));
    const size_t max_msg_size = AVS_MIN(mtu, buffer_capacity);

    if (msg_size > max_msg_size) {
        return 0;
    }
    return max_msg_size - msg_size;
}

static size_t
coap_udp_max_outgoing_payload_size(avs_coap_ctx_t *ctx_,
                                   size_t token_size,
                                   const avs_coap_options_t *options,
                                   uint8_t code) {
    (void) code;
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    update_last_mtu_from_socket(ctx);
    return udp_max_payload_size(ctx->base.out_buffer->capacity, ctx->last_mtu,
                                token_size, options ? options->size : 0);
}

static size_t
coap_udp_max_incoming_payload_size(avs_coap_ctx_t *ctx_,
                                   size_t token_size,
                                   const avs_coap_options_t *options,
                                   uint8_t code) {
    (void) code;
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    size_t incoming_mtu = ctx->forced_incoming_mtu;
    if (incoming_mtu <= 0) {
        update_last_mtu_from_socket(ctx);
        incoming_mtu = ctx->last_mtu;
    }
    return udp_max_payload_size(ctx->base.in_buffer->capacity, incoming_mtu,
                                token_size, options ? options->size : 0);
}

static uint16_t generate_id(avs_coap_udp_ctx_t *ctx) {
    return ctx->last_msg_id++;
}

static size_t current_nstart(const avs_coap_udp_ctx_t *ctx) {
    size_t started = 0;

    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) msg;
    AVS_LIST_FOREACH(msg, ctx->unconfirmed_messages) {
        if (!msg->hold) {
            ++started;
        } else {
            break;
        }
    }

    return started;
}

static size_t effective_nstart(const avs_coap_udp_ctx_t *ctx) {
    // equivalent to:
    // AVS_MIN(ctx->tx_params.nstart, AVS_LIST_SIZE(ctx->unconfirmed_messages))
    size_t result = 0;
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) msg;
    AVS_LIST_FOREACH(msg, ctx->unconfirmed_messages) {
        ++result;
        if (result >= ctx->tx_params.nstart) {
            break;
        }
    }
    return result;
}

static void _log_udp_msg_summary(const char *file,
                                 const int line,
                                 const char *info,
                                 const avs_coap_udp_msg_t *msg) {
    char observe_str[24] = "";
#    ifdef WITH_AVS_COAP_OBSERVE
    uint32_t observe;
    if (avs_coap_options_get_observe(&msg->options, &observe) == 0) {
        snprintf(observe_str, sizeof(observe_str), ", Observe %" PRIu32,
                 observe);
    }
#    endif // WITH_AVS_COAP_OBSERVE

#    ifdef WITH_AVS_COAP_BLOCK
    avs_coap_option_block_t block1;
    bool has_block1 =
            (avs_coap_options_get_block(&msg->options, AVS_COAP_BLOCK1, &block1)
             == 0);
    avs_coap_option_block_t block2;
    bool has_block2 =
            (avs_coap_options_get_block(&msg->options, AVS_COAP_BLOCK2, &block2)
             == 0);

    avs_log_internal_l__(
            AVS_LOG_DEBUG, AVS_QUOTE_MACRO(MODULE_NAME), file, (unsigned) line,
            "%s: %s (ID: %" PRIu16 ", token: %s)%s%s%s%s%s, payload: %u B",
            info, AVS_COAP_CODE_STRING(msg->header.code),
            _avs_coap_udp_header_get_id(&msg->header),
            AVS_COAP_TOKEN_HEX(&msg->token), has_block1 ? ", " : "",
            has_block1 ? _AVS_COAP_OPTION_BLOCK_STRING(&block1) : "",
            has_block2 ? ", " : "",
            has_block2 ? _AVS_COAP_OPTION_BLOCK_STRING(&block2) : "",
            observe_str, (unsigned) msg->payload_size);
#    else  // WITH_AVS_COAP_BLOCK
    avs_log_internal_l__(AVS_LOG_DEBUG, AVS_QUOTE_MACRO(MODULE_NAME), file,
                         (unsigned) line,
                         "%s: %s (ID: %" PRIu16 ", token: %s)%s, payload: %u B",
                         info, AVS_COAP_CODE_STRING(msg->header.code),
                         _avs_coap_udp_header_get_id(&msg->header),
                         AVS_COAP_TOKEN_HEX(&msg->token), observe_str,
                         (unsigned) msg->payload_size);
#    endif // WITH_AVS_COAP_BLOCK
}

#    define log_udp_msg_summary(Info, Msg) \
        _log_udp_msg_summary(__FILE__, __LINE__, (Info), (Msg))

static void try_cache_response(avs_coap_udp_ctx_t *ctx,
                               const avs_coap_udp_msg_t *res) {
#    ifdef WITH_AVS_COAP_OBSERVE
    uint16_t msg_id = _avs_coap_udp_header_get_id(&res->header);
    // if the cache still contains an entry with the same ID, drop it to not
    // confuse Reset response to new message with a Cancel Observe to a
    // previously sent Notify
    coap_udp_notify_cache_drop(&ctx->notify_cache, msg_id);

    if (!avs_coap_code_is_response(res->header.code)) {
        return;
    }

    avs_coap_udp_type_t type = _avs_coap_udp_header_get_type(&res->header);
    if ((type == AVS_COAP_UDP_TYPE_CONFIRMABLE
         || type == AVS_COAP_UDP_TYPE_NON_CONFIRMABLE)
            && _avs_coap_option_exists(&res->options,
                                       AVS_COAP_OPTION_OBSERVE)) {
        // Note: Reset response is only expected for CON/NON messages, so we
        // don't store anything for other types.
        coap_udp_notify_cache_put(&ctx->notify_cache, msg_id, &res->token);
    }
#    endif // WITH_AVS_COAP_OBSERVE

    if (!ctx->response_cache) {
        return;
    }

    char addr[AVS_ADDRSTRLEN];
    char port[sizeof("65535")];
    if (avs_is_err(avs_net_socket_get_remote_host(ctx->base.socket, addr,
                                                  sizeof(addr)))
            || avs_is_err(avs_net_socket_get_remote_port(ctx->base.socket, port,
                                                         sizeof(port)))) {
        LOG(DEBUG, _("could not get remote host/port"));
        return;
    }

    (void) _avs_coap_udp_response_cache_add(ctx->response_cache, addr, port,
                                            res, &ctx->tx_params);
}

static avs_error_t coap_udp_send_serialized_msg(avs_coap_udp_ctx_t *ctx,
                                                const avs_coap_udp_msg_t *msg,
                                                const void *msg_buf,
                                                size_t msg_size) {
    log_udp_msg_summary("send", msg);

    try_cache_response(ctx, msg);

    avs_error_t err = avs_net_socket_send(ctx->base.socket, msg_buf, msg_size);
    if (avs_is_err(err)) {
        LOG(DEBUG, _("send failed: ") "%s", AVS_COAP_STRERROR(err));
    }
    return err;
}

static avs_time_monotonic_t get_first_retransmit_time(avs_coap_udp_ctx_t *ctx) {
    avs_coap_retry_state_t initial_state = {
        .retry_count = 0,
        .recv_timeout = AVS_TIME_DURATION_ZERO
    };
    if (avs_is_err(_avs_coap_udp_initial_retry_state(
                &ctx->tx_params, ctx->base.prng_ctx, &initial_state))) {
        return AVS_TIME_MONOTONIC_INVALID;
    }
    return avs_time_monotonic_add(avs_time_monotonic_now(),
                                  initial_state.recv_timeout);
}

static AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *
find_unconfirmed_insert_ptr(avs_coap_udp_ctx_t *ctx,
                            const avs_coap_udp_unconfirmed_msg_t *new_elem) {
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *list_ptr = (AVS_LIST(
            avs_coap_udp_unconfirmed_msg_t) *) &ctx->unconfirmed_messages;

    AVS_LIST_ITERATE_PTR(list_ptr) {
        if (!new_elem->hold && (*list_ptr)->hold) {
            return list_ptr;
        } else if (new_elem->hold == (*list_ptr)->hold
                   && avs_time_monotonic_before(new_elem->next_retransmit,
                                                (*list_ptr)->next_retransmit)) {
            return list_ptr;
        }
    }

    assert(list_ptr);
    assert(*list_ptr == NULL);
    return list_ptr;
}

static AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *
find_first_held_unconfirmed_ptr(avs_coap_udp_ctx_t *ctx) {
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr;

    AVS_LIST_FOREACH_PTR(unconfirmed_ptr, &ctx->unconfirmed_messages) {
        if ((*unconfirmed_ptr)->hold) {
            return unconfirmed_ptr;
        }
    }

    return NULL;
}

static void reschedule_retransmission_job(avs_coap_udp_ctx_t *ctx) {
    if (ctx->unconfirmed_messages) {
        avs_time_monotonic_t target_time;
        if (current_nstart(ctx) < effective_nstart(ctx)) {
            // There are requests we need to send ASAP
            target_time = avs_time_monotonic_now();
        } else {
            target_time = ((avs_coap_udp_unconfirmed_msg_t *)
                                   ctx->unconfirmed_messages)
                                  ->next_retransmit;
        }
        _avs_coap_reschedule_retry_or_request_expired_job(
                (avs_coap_ctx_t *) ctx, target_time);
    }
}

static inline avs_coap_borrowed_msg_t
borrowed_msg_from_udp_msg(const avs_coap_udp_msg_t *msg) {
    return (avs_coap_borrowed_msg_t) {
        .code = msg->header.code,
        .token = msg->token,
        .options = msg->options,
        .payload = msg->payload,
        .payload_size = msg->payload_size,
        .total_payload_size = msg->payload_size
    };
}

static avs_coap_send_result_handler_result_t
call_send_result_handler(avs_coap_udp_ctx_t *ctx,
                         avs_coap_udp_unconfirmed_msg_t *unconfirmed,
                         const avs_coap_udp_msg_t *response_msg,
                         avs_coap_send_result_t result,
                         avs_error_t fail_err) {
    assert(ctx);
    assert(unconfirmed);
    AVS_ASSERT(unconfirmed->send_result_handler,
               "unconfirmed_msg objects with no send_result_handler "
               "are not supposed to be created");
    (void) ctx;
    if (result == AVS_COAP_SEND_RESULT_FAIL) {
        AVS_ASSERT(avs_is_err(fail_err), "fail_errno not set on failure");
    } else {
        AVS_ASSERT(avs_is_ok(fail_err), "fail_errno set on success");
    }

    avs_coap_borrowed_msg_t response_buf;
    const avs_coap_borrowed_msg_t *response = NULL;
    if (response_msg) {
        response_buf = borrowed_msg_from_udp_msg(response_msg);
        response = &response_buf;
    }

    return unconfirmed->send_result_handler(
            (avs_coap_ctx_t *) ctx, result, fail_err, response,
            unconfirmed->send_result_handler_arg);
}

static inline const char *send_result_string(avs_coap_send_result_t result) {
    switch (result) {
    case AVS_COAP_SEND_RESULT_PARTIAL_CONTENT:
        return "partial content";
    case AVS_COAP_SEND_RESULT_OK:
        return "ok";
    case AVS_COAP_SEND_RESULT_FAIL:
        return "fail";
    case AVS_COAP_SEND_RESULT_CANCEL:
        return "cancel";
    }

    return "<unknown>";
}

static void resume_next_unconfirmed(avs_coap_udp_ctx_t *ctx) {
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr =
            find_first_held_unconfirmed_ptr(ctx);
    if (!unconfirmed_ptr) {
        return;
    }

    avs_time_monotonic_t next_retransmit = get_first_retransmit_time(ctx);
    if (!avs_time_monotonic_valid(next_retransmit)) {
        LOG(ERROR,
            _("unable to schedule retransmit: get_first_retransmit_time() "
              "returned invalid time; either the monotonic clock "
              "malfunctioned, UDP tx params are too large to handle or "
              "PRNG failed"));

        // We can't rely on getting valid times for any held job. Fail all
        // of them immediately.

        // Detach held messages so that they can't get unheld in the send result
        // handler
        AVS_LIST(avs_coap_udp_unconfirmed_msg_t) held_messages =
                *unconfirmed_ptr;
        *unconfirmed_ptr = NULL;

        while (held_messages) {
            // Do not use fail_unconfirmed - it indirectly calls this function
            // again, which may result in
            // AVS_LIST_SIZE(ctx->unconfirmed_messages) recursive calls .
            //
            // Note: this loop may be infinite in the most degenerate case
            // where get_first_retransmit_time returns an invalid time **just
            // once** (call above) and every response handler calls
            // avs_coap_client_send_async_request, adding a new held entry to
            // the context.
            avs_coap_udp_unconfirmed_msg_t *unconfirmed =
                    AVS_LIST_DETACH(&held_messages);
            (void) call_send_result_handler(
                    ctx, unconfirmed, NULL, AVS_COAP_SEND_RESULT_FAIL,
                    _avs_coap_err(AVS_COAP_ERR_TIME_INVALID));
            AVS_LIST_DELETE(&unconfirmed);
        }

        return;
    }

    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) unconfirmed =
            AVS_LIST_DETACH(unconfirmed_ptr);
    unconfirmed->hold = false;
    unconfirmed->next_retransmit = next_retransmit;

    LOG(DEBUG, _("msg ") "%s" _(" resumed"),
        AVS_COAP_TOKEN_HEX(&unconfirmed->msg.token));

    avs_error_t send_err =
            coap_udp_send_serialized_msg(ctx, &unconfirmed->msg,
                                         unconfirmed->packet,
                                         unconfirmed->packet_size);
    if (avs_is_err(send_err)) {
        (void) call_send_result_handler(ctx, unconfirmed, NULL,
                                        AVS_COAP_SEND_RESULT_FAIL, send_err);
        AVS_LIST_DELETE(&unconfirmed);
    } else {
        // the msg may need to be retransmitted before other started ones
        AVS_LIST_INSERT(find_unconfirmed_insert_ptr(ctx, unconfirmed),
                        unconfirmed);
    }
}

static void resume_unconfirmed_messages(avs_coap_udp_ctx_t *ctx) {
    assert(current_nstart(ctx) <= ctx->tx_params.nstart);

    const size_t resumed_msgs = current_nstart(ctx);
    const size_t all_msgs = AVS_LIST_SIZE(ctx->unconfirmed_messages);
    const size_t held_msgs = all_msgs - resumed_msgs;

    const size_t msgs_to_resume =
            AVS_MIN(ctx->tx_params.nstart - resumed_msgs, held_msgs);
    LOG(DEBUG, "%u/%u" _(" msgs held; resuming ") "%u", (unsigned) held_msgs,
        (unsigned) all_msgs, (unsigned) msgs_to_resume);

    // Ending up resuming 0 messages here indicates one of:
    //
    // - A held unconfirmed message was canceled (OK),
    // - There is no more held messages to resume (OK),
    // - While handling cleanup of this message, a new one was created and
    //   given higher priority than already enqueued one. This is pretty bad,
    //   as it may result in delaying "old" enqueued messages infinitely.
    //   This is not supposed to happen and indicates a bug in avs_coap.
    //
    // Adding an assert would require passing quite a lot of data from the call
    // site, so I'm just leaving a comment instead in hopes it will help in
    // debugging if the starving case happens at some point.

    // resume_next_unconfirmed() might call handlers which may resume messages
    // themselves, so
    while (current_nstart(ctx) < effective_nstart(ctx)) {
        resume_next_unconfirmed(ctx);
    }
    assert(current_nstart(ctx) == effective_nstart(ctx));
}

static void try_cleanup_unconfirmed(avs_coap_udp_ctx_t *ctx,
                                    avs_coap_udp_unconfirmed_msg_t *unconfirmed,
                                    const avs_coap_udp_msg_t *response,
                                    avs_coap_send_result_t result,
                                    avs_error_t fail_err) {
    assert(ctx);
    assert(unconfirmed);
    AVS_ASSERT(!AVS_LIST_FIND_PTR(&ctx->unconfirmed_messages, unconfirmed),
               "unconfirmed must be detached");
    LOG(DEBUG, _("msg ") "%s" _(": ") "%s",
        AVS_COAP_TOKEN_HEX(&unconfirmed->msg.token),
        send_result_string(result));

    avs_coap_send_result_handler_result_t handler_result =
            call_send_result_handler(ctx, unconfirmed, response, result,
                                     fail_err);

    if (response && result == AVS_COAP_SEND_RESULT_OK
            && handler_result != AVS_COAP_RESPONSE_ACCEPTED) {
        AVS_LIST_INSERT(find_unconfirmed_insert_ptr(ctx, unconfirmed),
                        unconfirmed);
    } else {
        reschedule_retransmission_job(ctx);
        AVS_LIST_DELETE(&unconfirmed);
    }
}

typedef enum {
    AVS_COAP_UDP_EXCHANGE_ANY,
    AVS_COAP_UDP_EXCHANGE_CLIENT_REQUEST,
    AVS_COAP_UDP_EXCHANGE_SERVER_NOTIFICATION
} avs_coap_udp_exchange_direction_t;

static avs_coap_udp_exchange_direction_t direction_from_code(uint8_t code) {
    return avs_coap_code_is_request(code)
                   ? AVS_COAP_UDP_EXCHANGE_CLIENT_REQUEST
                   : AVS_COAP_UDP_EXCHANGE_SERVER_NOTIFICATION;
}

static AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *
find_unconfirmed_ptr(avs_coap_udp_ctx_t *ctx,
                     avs_coap_udp_exchange_direction_t direction,
                     const avs_coap_token_t *token,
                     const uint16_t *id) {
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *list_ptr =
            &ctx->unconfirmed_messages;

    AVS_LIST_ITERATE_PTR(list_ptr) {
        const avs_coap_udp_msg_t *msg = &(*list_ptr)->msg;
        if ((direction == AVS_COAP_UDP_EXCHANGE_ANY
             || direction == direction_from_code(msg->header.code))
                && (!token || avs_coap_token_equal(&msg->token, token))
                && (!id || _avs_coap_udp_header_get_id(&msg->header) == *id)) {
            return list_ptr;
        }
    }

    return NULL;
}

static inline AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *
find_unconfirmed_ptr_by_token(avs_coap_udp_ctx_t *ctx,
                              avs_coap_udp_exchange_direction_t direction,
                              const avs_coap_token_t *token) {
    return find_unconfirmed_ptr(ctx, direction, token, NULL);
}

static inline AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *
find_unconfirmed_ptr_by_msg_id(avs_coap_udp_ctx_t *ctx, uint16_t msg_id) {
    return find_unconfirmed_ptr(ctx, AVS_COAP_UDP_EXCHANGE_ANY, NULL, &msg_id);
}

static inline AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *
find_unconfirmed_ptr_by_response(avs_coap_udp_ctx_t *ctx,
                                 const avs_coap_udp_msg_t *msg) {
    assert(avs_coap_code_is_response(msg->header.code));

    uint16_t id = _avs_coap_udp_header_get_id(&msg->header);

    switch (_avs_coap_udp_header_get_type(&msg->header)) {
    case AVS_COAP_UDP_TYPE_CONFIRMABLE:
    case AVS_COAP_UDP_TYPE_NON_CONFIRMABLE:
        return find_unconfirmed_ptr_by_token(
                ctx, AVS_COAP_UDP_EXCHANGE_CLIENT_REQUEST, &msg->token);
    case AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT:
        return find_unconfirmed_ptr(ctx, AVS_COAP_UDP_EXCHANGE_CLIENT_REQUEST,
                                    &msg->token, &id);
    case AVS_COAP_UDP_TYPE_RESET:
        // this should be detected at packet validation
        AVS_UNREACHABLE("According to RFC7252 Reset MUST be empty");
    }
    AVS_UNREACHABLE("invalid enum value");
    return NULL;
}

static avs_coap_udp_unconfirmed_msg_t *
detach_unconfirmed_by_token(avs_coap_udp_ctx_t *ctx,
                            avs_coap_udp_exchange_direction_t direction,
                            const avs_coap_token_t *token) {
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *msg_ptr =
            find_unconfirmed_ptr_by_token(ctx, direction, token);

    if (msg_ptr) {
        return AVS_LIST_DETACH(msg_ptr);
    }
    return NULL;
}

static void
confirm_unconfirmed(avs_coap_udp_ctx_t *ctx,
                    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *msg_ptr,
                    const avs_coap_udp_msg_t *response) {
    assert(ctx);
    assert(msg_ptr);
    assert(*msg_ptr);
    AVS_ASSERT(AVS_LIST_FIND_PTR(&ctx->unconfirmed_messages, *msg_ptr),
               "unconfirmed_msg must be enqueued");

    avs_coap_udp_unconfirmed_msg_t *msg = AVS_LIST_DETACH(msg_ptr);
    try_cleanup_unconfirmed(ctx, msg, response, AVS_COAP_SEND_RESULT_OK,
                            AVS_OK);
}

static void fail_unconfirmed(avs_coap_udp_ctx_t *ctx,
                             AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *msg_ptr,
                             const avs_coap_udp_msg_t *truncated_msg,
                             avs_error_t err) {
    assert(ctx);
    assert(msg_ptr);
    assert(*msg_ptr);
    AVS_ASSERT(AVS_LIST_FIND_PTR(&ctx->unconfirmed_messages, *msg_ptr),
               "unconfirmed_msg must be enqueued");

    avs_coap_udp_unconfirmed_msg_t *msg = AVS_LIST_DETACH(msg_ptr);
    try_cleanup_unconfirmed(ctx, msg, truncated_msg, AVS_COAP_SEND_RESULT_FAIL,
                            err);
}

static avs_coap_udp_exchange_direction_t
udp_direction(avs_coap_exchange_direction_t direction) {
    switch (direction) {
    case AVS_COAP_EXCHANGE_CLIENT_REQUEST:
        return AVS_COAP_UDP_EXCHANGE_CLIENT_REQUEST;
    case AVS_COAP_EXCHANGE_SERVER_NOTIFICATION:
        return AVS_COAP_UDP_EXCHANGE_SERVER_NOTIFICATION;
    }
    AVS_UNREACHABLE("invalid value of avs_coap_exchange_direction_t");
    return AVS_COAP_UDP_EXCHANGE_ANY;
}

static void coap_udp_abort_delivery(avs_coap_ctx_t *ctx_,
                                    avs_coap_exchange_direction_t direction,
                                    const avs_coap_token_t *token,
                                    avs_coap_send_result_t result,
                                    avs_error_t fail_err) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) msg =
            detach_unconfirmed_by_token(ctx, udp_direction(direction), token);
    if (!msg) {
        return;
    }
    try_cleanup_unconfirmed(ctx, msg, NULL, result, fail_err);
}

static void coap_udp_ignore_current_request(avs_coap_ctx_t *ctx_,
                                            const avs_coap_token_t *token) {
    (void) token;
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    if (ctx->current_request.exists) {
        assert(avs_coap_token_equal(&ctx->current_request.token, token));
        ctx->current_request.exists = false;
    }
}

static void
retransmit_next_message_without_reschedule(avs_coap_udp_ctx_t *ctx) {
    avs_coap_udp_unconfirmed_msg_t *unconfirmed = ctx->unconfirmed_messages;
    if (!unconfirmed
            || avs_time_monotonic_before(avs_time_monotonic_now(),
                                         unconfirmed->next_retransmit)) {
        return;
    }

    if (_avs_coap_udp_all_retries_sent(&unconfirmed->retry_state,
                                       &ctx->tx_params)) {
        LOG(DEBUG,
            _("msg ") "%s" _(": MAX_RETRANSMIT reached without response from "
                             "the server"),
            AVS_COAP_TOKEN_HEX(&unconfirmed->msg.token));

        // retransmission_job is rescheduled by fail_unconfirmed()
        fail_unconfirmed(ctx, &ctx->unconfirmed_messages, NULL,
                         _avs_coap_err(AVS_COAP_ERR_TIMEOUT));
        return;
    }

    if (_avs_coap_udp_update_retry_state(&unconfirmed->retry_state)) {
        fail_unconfirmed(ctx, &ctx->unconfirmed_messages, NULL,
                         _avs_coap_err(AVS_COAP_ERR_TIME_INVALID));
        return;
    }

    LOG(DEBUG, _("msg ") "%s" _(": retry ") "%u/%u",
        AVS_COAP_TOKEN_HEX(&unconfirmed->msg.token),
        unconfirmed->retry_state.retry_count, ctx->tx_params.max_retransmit);

    avs_error_t err = coap_udp_send_serialized_msg(ctx, &unconfirmed->msg,
                                                   unconfirmed->packet,
                                                   unconfirmed->packet_size);
    if (avs_is_err(err)) {
        fail_unconfirmed(ctx, &ctx->unconfirmed_messages, NULL, err);
        return;
    }
    ++ctx->stats.outgoing_retransmissions_count;

    avs_time_monotonic_t next_retransmit =
            avs_time_monotonic_add(unconfirmed->next_retransmit,
                                   unconfirmed->retry_state.recv_timeout);
    if (!avs_time_monotonic_valid(unconfirmed->next_retransmit)) {
        LOG(ERROR,
            _("unable to schedule message retransmission: next_retransmit time "
              "invalid; either the monotonic clock malfunctioned or UDP tx "
              "params are too large to handle"));
        fail_unconfirmed(ctx, &ctx->unconfirmed_messages, NULL,
                         _avs_coap_err(AVS_COAP_ERR_TIME_INVALID));
        return;
    }

    unconfirmed->next_retransmit = next_retransmit;
    unconfirmed = AVS_LIST_DETACH(&ctx->unconfirmed_messages);
    AVS_LIST_INSERT(find_unconfirmed_insert_ptr(ctx, unconfirmed), unconfirmed);

    assert(current_nstart(ctx)
           == AVS_MIN(AVS_LIST_SIZE(ctx->unconfirmed_messages),
                      ctx->tx_params.nstart));
}

static avs_time_monotonic_t coap_udp_on_timeout(avs_coap_ctx_t *ctx_) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    resume_unconfirmed_messages(ctx);
    retransmit_next_message_without_reschedule(ctx);

    if (ctx->unconfirmed_messages) {
        avs_coap_udp_unconfirmed_msg_t *unconfirmed = ctx->unconfirmed_messages;

        LOG(DEBUG, _("next UDP retransmission: ") "%s",
            AVS_TIME_DURATION_AS_STRING(
                    unconfirmed->next_retransmit.since_monotonic_epoch));
        return ctx->unconfirmed_messages->next_retransmit;
    } else {
        return AVS_TIME_MONOTONIC_INVALID;
    }
}

static avs_error_t
enqueue_unconfirmed(avs_coap_udp_ctx_t *ctx,
                    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) unconfirmed) {
    LOG(TRACE, _("msg ") "%s" _(": enqueue"),
        AVS_COAP_TOKEN_HEX(&unconfirmed->msg.token));

    // do not send the message unless there is no other one waiting to be sent
    // that is held for longer than this one
    assert(ctx->tx_params.nstart > 0);
    unconfirmed->hold =
            (AVS_LIST_NTH(ctx->unconfirmed_messages, ctx->tx_params.nstart - 1)
             != NULL);

    // use current time for all held jobs to not cause accidental reordering
    // due to ACK_RANDOM_FACTOR
    avs_time_monotonic_t next_retransmit =
            unconfirmed->hold ? avs_time_monotonic_now()
                              : get_first_retransmit_time(ctx);
    if (!avs_time_monotonic_valid(unconfirmed->next_retransmit)) {
        LOG(ERROR,
            _("unable to enqueue msg: next_retransmit time invalid; "
              "either the monotonic clock malfunctioned, UDP tx params are too "
              "large to handle or PRNG failed"));
        return _avs_coap_err(AVS_COAP_ERR_TIME_INVALID);
    }

    unconfirmed->next_retransmit = next_retransmit;

    if (unconfirmed->hold) {
        LOG(DEBUG, _("msg ") "%s" _(" held due to NSTART = ") "%u",
            AVS_COAP_TOKEN_HEX(&unconfirmed->msg.token),
            (unsigned) ctx->tx_params.nstart);
    } else {
        avs_error_t err =
                coap_udp_send_serialized_msg(ctx, &unconfirmed->msg,
                                             unconfirmed->packet,
                                             unconfirmed->packet_size);
        if (avs_is_err(err)) {
            return err;
        }
    }

    AVS_LIST_INSERT(find_unconfirmed_insert_ptr(ctx, unconfirmed), unconfirmed);
    reschedule_retransmission_job(ctx);
    return AVS_OK;
}

static avs_error_t create_unconfirmed(
        avs_coap_udp_ctx_t *ctx,
        const avs_coap_udp_msg_t *msg,
        AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *out_unconfirmed_msg,
        avs_coap_send_result_handler_t *send_result_handler,
        void *send_result_handler_arg) {
    const size_t msg_size = _avs_coap_udp_msg_size(msg);

    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) unconfirmed_msg =
            (AVS_LIST(avs_coap_udp_unconfirmed_msg_t)) AVS_LIST_NEW_BUFFER(
                    sizeof(avs_coap_udp_unconfirmed_msg_t) + msg_size);
    if (!unconfirmed_msg) {
        return avs_errno(AVS_ENOMEM);
    }

    *unconfirmed_msg = (avs_coap_udp_unconfirmed_msg_t) {
        .send_result_handler = send_result_handler,
        .send_result_handler_arg = send_result_handler_arg,
        .packet_size = msg_size
    };

    avs_error_t err;

    if (avs_is_err((err = _avs_coap_udp_initial_retry_state(
                            &ctx->tx_params, ctx->base.prng_ctx,
                            &unconfirmed_msg->retry_state)))) {
        LOG(ERROR, _("PRNG failed"));
        AVS_LIST_CLEAR(&unconfirmed_msg);
        return err;
    }

    if (avs_is_err((err = _avs_coap_udp_msg_copy(msg, &unconfirmed_msg->msg,
                                                 unconfirmed_msg->packet,
                                                 msg_size)))) {
        LOG(ERROR,
            _("Could not serialize the message as a valid CoAP/UDP packet"));
        AVS_LIST_CLEAR(&unconfirmed_msg);
        return err;
    }

    *out_unconfirmed_msg = unconfirmed_msg;
    return AVS_OK;
}

static avs_coap_udp_type_t choose_msg_type(const avs_coap_udp_ctx_t *ctx,
                                           const avs_coap_borrowed_msg_t *msg,
                                           bool has_send_result_handler) {
    if (avs_coap_code_is_request(msg->code)) {
        /* Use CON if the user requests delivery confirmation, NON otherwise. */
        return has_send_result_handler ? AVS_COAP_UDP_TYPE_CONFIRMABLE
                                       : AVS_COAP_UDP_TYPE_NON_CONFIRMABLE;
    }

    if (avs_coap_code_is_response(msg->code)) {
        /*
         * This may either be a "regular" response, or an Observe notification.
         * Because this layer MUST know what Observes are active (to be able
         * to handle Observe cancellation with Reset response), we may use
         * the message token to distinguish these two cases.
         *
         * - For "regular" responses: use ACK (Piggybacked Response)
         * - For Observe notifications, we should use either CON or NON,
         *   depending on whether delivery confirmation is required.
         */
        if (_avs_coap_option_exists(&msg->options, AVS_COAP_OPTION_OBSERVE)) {
            if (has_send_result_handler) {
                return AVS_COAP_UDP_TYPE_CONFIRMABLE;
            }

            /*
             * HACK: if we're currently processing //some// input message,
             * any message with Observe option is probably a direct response
             * to an Observe request, which should use ACK instead of NON.
             */
            return ctx->current_request.exists
                           ? AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT
                           : AVS_COAP_UDP_TYPE_NON_CONFIRMABLE;
        }

        return has_send_result_handler ? AVS_COAP_UDP_TYPE_CONFIRMABLE
                                       : AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT;
    }

    /* Code should be either a request, response or 0.00 Empty */
    assert(msg->code == AVS_COAP_CODE_EMPTY);

    /*
     * 0.00 Empty has specific semantics. It may be either:
     * - CoAP Ping message (if CON/NON),
     * - Separate Response (if ACK),
     * - Reset (RST; the only code allowed for valid RST messages).
     *
     * Neither has a clear analog in other transports (e.g. CoAP/TCP), so let's
     * arbitrarily assume this means a Separate Response.
     *
     * Note: that a Separate Response will have to use CON (i.e. have to set
     * the delivery handler); otherwise such response will be sent as ACK
     * that does not seem to be allowed. To allow NON Separate Responses,
     * udp_ctx would need to keep track of tokens for sent Separate ACKs.
     * This sounds a bit similar to Observe handling.
     */
    return AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT;
}

static uint16_t assign_id(avs_coap_udp_ctx_t *ctx,
                          const avs_coap_borrowed_msg_t *msg,
                          const avs_coap_udp_type_t type) {
    if (ctx->current_request.exists && avs_coap_code_is_response(msg->code)
            && avs_coap_token_equal(&msg->token, &ctx->current_request.token)) {
        ctx->current_request.exists = false;
        if ((type == AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT
             || type == AVS_COAP_UDP_TYPE_RESET)) {
            return ctx->current_request.msg_id;
        }
    }

    return generate_id(ctx);
}

static avs_error_t
coap_udp_send_message(avs_coap_ctx_t *ctx_,
                      const avs_coap_borrowed_msg_t *msg,
                      avs_coap_send_result_handler_t *send_result_handler,
                      void *send_result_handler_arg) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;

    uint8_t *out_buffer = avs_shared_buffer_acquire(ctx->base.out_buffer);

    const avs_coap_udp_type_t type =
            choose_msg_type(ctx, msg, !!send_result_handler);

    avs_coap_udp_msg_t shared_buffer_msg = {
        .header = _avs_coap_udp_header_init(type, msg->token.size, msg->code,
                                            assign_id(ctx, msg, type)),
        .token = msg->token,
        .options = msg->options,
        .payload = msg->payload,
        .payload_size = msg->payload_size
    };

    size_t shared_buffer_msg_size;
    avs_error_t err =
            _avs_coap_udp_msg_serialize(&shared_buffer_msg, out_buffer,
                                        ctx->base.out_buffer->capacity,
                                        &shared_buffer_msg_size);
    if (avs_is_err(err)) {
        goto end;
    }

    if (type == AVS_COAP_UDP_TYPE_CONFIRMABLE) {
        // The user actually cares about message delivery.
        // We need to store the packet for possible retransmissions.
        AVS_LIST(avs_coap_udp_unconfirmed_msg_t) unconfirmed = NULL;
        err = create_unconfirmed(ctx, &shared_buffer_msg, &unconfirmed,
                                 send_result_handler, send_result_handler_arg);
        if (avs_is_err(err)) {
            goto end;
        }

        assert(unconfirmed);
        err = enqueue_unconfirmed(ctx, unconfirmed);
        if (avs_is_err(err)) {
            // don't call try_cleanup_unconfirmed to avoid calling user-defined
            // handler
            AVS_LIST_DELETE(&unconfirmed);
        }
    } else {
        assert(type != AVS_COAP_UDP_TYPE_CONFIRMABLE);
        // NON/ACK/RST messages ignore NSTART - they are not considered
        // "outstanding interactions" according to RFC7252, 4.7 Congestion
        // Control.
        err = coap_udp_send_serialized_msg(ctx, &shared_buffer_msg, out_buffer,
                                           shared_buffer_msg_size);
    }

end:
    avs_shared_buffer_release(ctx->base.out_buffer);
    return err;
}

static avs_error_t coap_udp_recv_msg(avs_coap_udp_ctx_t *ctx,
                                     avs_coap_udp_msg_t *out_msg,
                                     uint8_t *buf,
                                     size_t buf_size) {
    size_t packet_size;
    avs_error_t err = avs_net_socket_receive(ctx->base.socket, &packet_size,
                                             buf, buf_size);
    if (avs_is_err(err)) {
        LOG(TRACE, _("recv failed"));
        return err;
    }

    err = _avs_coap_udp_msg_parse(out_msg, buf, packet_size);
    if (avs_is_err(err)) {
        LOG(DEBUG, _("recv: malformed packet"));
        return err;
    }

    log_udp_msg_summary("recv", out_msg);
    return AVS_OK;
}

static avs_error_t try_send_cached_response(avs_coap_udp_ctx_t *ctx,
                                            const avs_coap_udp_msg_t *msg,
                                            bool *out_cache_hit) {
    assert(avs_coap_code_is_request(msg->header.code));
    assert(ctx->current_request.exists);
    if (!ctx->response_cache) {
        *out_cache_hit = false;
        return AVS_OK;
    }

    char addr[AVS_ADDRSTRLEN];
    char port[sizeof("65535")];
    if (avs_is_err(avs_net_socket_get_remote_host(ctx->base.socket, addr,
                                                  sizeof(addr)))
            || avs_is_err(avs_net_socket_get_remote_port(ctx->base.socket, port,
                                                         sizeof(port)))) {
        LOG(DEBUG, _("could not get remote remote host/port"));
        *out_cache_hit = false;
        return AVS_OK;
    }

    uint16_t msg_id = _avs_coap_udp_header_get_id(&msg->header);
    avs_coap_udp_cached_response_t cached_response;
    if (avs_is_ok(_avs_coap_udp_response_cache_get(
                ctx->response_cache, addr, port, msg_id, &cached_response))) {
        *out_cache_hit = true;
        ctx->current_request.exists = false;
        return coap_udp_send_serialized_msg(ctx, &cached_response.msg,
                                            cached_response.packet,
                                            cached_response.packet_size);
    } else {
        *out_cache_hit = false;
        return AVS_OK;
    }
}

static avs_error_t handle_request(avs_coap_udp_ctx_t *ctx,
                                  const avs_coap_udp_msg_t *msg,
                                  bool *out_should_handle) {
    *out_should_handle = false;
    switch (_avs_coap_udp_header_get_type(&msg->header)) {
    case AVS_COAP_UDP_TYPE_CONFIRMABLE:
    case AVS_COAP_UDP_TYPE_NON_CONFIRMABLE: {
        bool cache_hit;
        avs_error_t err = try_send_cached_response(ctx, msg, &cache_hit);
        if (cache_hit) {
            ++ctx->stats.incoming_retransmissions_count;
            return err;
        }

        *out_should_handle = true;
        return AVS_OK;
    }

    case AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT:
        // this should be detected at packet validation
        AVS_UNREACHABLE("Requests with ACK type make no sense");
        return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);

    case AVS_COAP_UDP_TYPE_RESET:
        // this should be detected at packet validation
        AVS_UNREACHABLE("According to RFC7252 Reset MUST be empty");
        return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
    }

    AVS_UNREACHABLE("switch above is supposed to be exhaustive");
    return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
}

static avs_error_t
send_empty(avs_coap_udp_ctx_t *ctx, avs_coap_udp_type_t type, uint16_t msg_id) {
    // an Empty message MUST NOT have neither options nor payload, and MUST
    // have a 0-byte token
    avs_coap_udp_msg_t msg = {
        .header =
                _avs_coap_udp_header_init(type, 0, AVS_COAP_CODE_EMPTY, msg_id)
    };

    return coap_udp_send_serialized_msg(ctx, &msg, &msg.header,
                                        sizeof(msg.header));
}

static avs_error_t send_separate_ack(avs_coap_udp_ctx_t *ctx, uint16_t msg_id) {
    return send_empty(ctx, AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT, msg_id);
}

static avs_error_t send_reset(avs_coap_udp_ctx_t *ctx, uint16_t msg_id) {
    return send_empty(ctx, AVS_COAP_UDP_TYPE_RESET, msg_id);
}

static avs_error_t handle_response(avs_coap_udp_ctx_t *ctx,
                                   const avs_coap_udp_msg_t *msg) {
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr =
            find_unconfirmed_ptr_by_response(ctx, msg);
    if (!unconfirmed_ptr) {
        bool is_confirmable = (_avs_coap_udp_header_get_type(&msg->header)
                               == AVS_COAP_UDP_TYPE_CONFIRMABLE);
        LOG(DEBUG,
            _("Received response does not match any known request, ") "%s",
            is_confirmable ? "rejecting" : "ignoring");
        if (is_confirmable) {
            return send_reset(ctx, _avs_coap_udp_header_get_id(&msg->header));
        }
        return AVS_OK;
    }

    switch (_avs_coap_udp_header_get_type(&msg->header)) {
    case AVS_COAP_UDP_TYPE_CONFIRMABLE: {
        // Separate response

        avs_error_t err =
                send_separate_ack(ctx,
                                  _avs_coap_udp_header_get_id(&msg->header));
        if (avs_is_err(err)) {
            fail_unconfirmed(ctx, unconfirmed_ptr, NULL, err);
            return err;
        }
        break;
    }
    case AVS_COAP_UDP_TYPE_NON_CONFIRMABLE:
        // Separate Response with NON
        //
        // RFC7252, 5.2.2. Separate
        // > When the server finally has obtained the resource representation,
        // > it sends the response. [...] (It may also be sent as a
        // > Non-confirmable message; see Section 5.2.3.)
        break;

    case AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT:
        // Piggybacked Response
        break;

    case AVS_COAP_UDP_TYPE_RESET:
        // this should be detected at packet validation
        AVS_UNREACHABLE("According to RFC7252 Reset MUST be empty");
        return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
    }

    confirm_unconfirmed(ctx, unconfirmed_ptr, msg);
    return AVS_OK;
}

static void
ack_request(avs_coap_udp_ctx_t *ctx,
            AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr) {
    assert(ctx);
    assert(unconfirmed_ptr);
    assert(*unconfirmed_ptr);

    // Wait EXCHANGE_LIFETIME for the actual response
    avs_time_monotonic_t next_retransmit = avs_time_monotonic_add(
            avs_time_monotonic_now(),
            avs_coap_udp_exchange_lifetime(&ctx->tx_params));

    if (!avs_time_monotonic_valid((*unconfirmed_ptr)->next_retransmit)) {
        LOG(ERROR,
            _("unable to schedule msg retransmission: next_retransmit time "
              "invalid; either the monotonic clock malfunctioned or UDP tx "
              "params are too large to handle"));
        fail_unconfirmed(ctx, unconfirmed_ptr, NULL,
                         _avs_coap_err(AVS_COAP_ERR_TIME_INVALID));
        return;
    }

    avs_coap_udp_unconfirmed_msg_t *unconfirmed =
            AVS_LIST_DETACH(unconfirmed_ptr);
    // disable further retransmissions
    unconfirmed->retry_state.retry_count = ctx->tx_params.max_retransmit;
    unconfirmed->next_retransmit = next_retransmit;

    AVS_LIST_INSERT(find_unconfirmed_insert_ptr(ctx, unconfirmed), unconfirmed);
    reschedule_retransmission_job(ctx);

    assert(current_nstart(ctx)
           == AVS_MIN(AVS_LIST_SIZE(ctx->unconfirmed_messages),
                      ctx->tx_params.nstart));
}

static avs_error_t handle_empty(avs_coap_udp_ctx_t *ctx,
                                const avs_coap_udp_msg_t *msg) {
    uint16_t msg_id = _avs_coap_udp_header_get_id(&msg->header);
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr =
            find_unconfirmed_ptr_by_msg_id(ctx, msg_id);

    switch (_avs_coap_udp_header_get_type(&msg->header)) {
    case AVS_COAP_UDP_TYPE_CONFIRMABLE:
        // CoAP Ping.
        return send_reset(ctx, msg_id);
    case AVS_COAP_UDP_TYPE_NON_CONFIRMABLE:
        return AVS_OK;

    case AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT:
        // Separate ACK
        if (unconfirmed_ptr) {
            if (avs_coap_code_is_request((*unconfirmed_ptr)->msg.header.code)) {
                // we still need to wait for a response
                ack_request(ctx, unconfirmed_ptr);
            } else {
                // Separate ACK to Separate Response sent by us
                confirm_unconfirmed(ctx, unconfirmed_ptr, NULL);
            }
            return AVS_OK;
        } else {
            LOG(DEBUG,
                _("Unexpected Separate ACK (ID ") "%#04" PRIx16 _(
                        "), ignoring"),
                msg_id);
            return AVS_OK;
        }

    case AVS_COAP_UDP_TYPE_RESET: {
        if (unconfirmed_ptr) {
            // Reset response to our CON request
            fail_unconfirmed(ctx, unconfirmed_ptr, NULL,
                             _avs_coap_err(AVS_COAP_ERR_UDP_RESET_RECEIVED));
        }

#    ifdef WITH_AVS_COAP_OBSERVE
        const avs_coap_token_t *observe_token =
                coap_udp_notify_cache_get(&ctx->notify_cache, msg_id);
        if (observe_token) {
            avs_coap_observe_id_t observe_id = {
                .token = *observe_token
            };
            _avs_coap_observe_cancel((avs_coap_ctx_t *) ctx, &observe_id);
        }
#    endif // WITH_AVS_COAP_OBSERVE

        return AVS_OK;
    }
    }

    AVS_UNREACHABLE("switch above is supposed to be exhaustive");
    return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
}

static void store_request_id(avs_coap_udp_ctx_t *ctx,
                             const avs_coap_udp_msg_t *msg) {
    assert(!ctx->current_request.exists);

    ctx->current_request.exists = true;
    ctx->current_request.msg_id = _avs_coap_udp_header_get_id(&msg->header);
    ctx->current_request.token = msg->token;
}

static avs_error_t handle_msg(avs_coap_udp_ctx_t *ctx,
                              const avs_coap_udp_msg_t *msg,
                              bool *out_should_handle_request) {
    *out_should_handle_request = false;
    if (avs_coap_code_is_request(msg->header.code)) {
        store_request_id(ctx, msg);
        return handle_request(ctx, msg, out_should_handle_request);
    } else if (avs_coap_code_is_response(msg->header.code)) {
        return handle_response(ctx, msg);
    } else if (msg->header.code == AVS_COAP_CODE_EMPTY) {
        return handle_empty(ctx, msg);
    } else {
        LOG(DEBUG, _("Unexpected CoAP code: ") "%s" _(", ignoring"),
            AVS_COAP_CODE_STRING(msg->header.code));
    }
    return AVS_OK;
}

static avs_error_t send_empty_response(avs_coap_udp_ctx_t *ctx,
                                       const avs_coap_udp_msg_t *request,
                                       uint8_t response_code) {
    const avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(
                AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT, request->token.size,
                response_code, _avs_coap_udp_header_get_id(&request->header)),
        .token = request->token
    };

    uint8_t buf[sizeof(avs_coap_udp_header_t) + sizeof(avs_coap_token_t)];
    size_t msg_size = 0;
    if (avs_is_err(_avs_coap_udp_msg_serialize(&msg, buf, sizeof(buf),
                                               &msg_size))) {
        AVS_UNREACHABLE();
    }

    return coap_udp_send_serialized_msg(ctx, &msg, buf, msg_size);
}

static avs_error_t
handle_truncated_request(avs_coap_udp_ctx_t *ctx,
                         const avs_coap_udp_msg_t *truncated_msg) {
    log_udp_msg_summary("recv [truncated request]", truncated_msg);
    assert(avs_coap_code_is_request(truncated_msg->header.code));
    return send_empty_response(ctx, truncated_msg,
                               AVS_COAP_CODE_REQUEST_ENTITY_TOO_LARGE);
}

static void handle_truncated_response(avs_coap_udp_ctx_t *ctx,
                                      const avs_coap_udp_msg_t *truncated_msg) {
    log_udp_msg_summary("recv [truncated response]", truncated_msg);

    assert(avs_coap_code_is_response(truncated_msg->header.code));
    // Truncated response: notify the owner about failure. The handler will
    // be able to detect that truncation happened by inspecting socket errno
    AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr =
            find_unconfirmed_ptr_by_response(ctx, truncated_msg);
    if (unconfirmed_ptr) {
        fail_unconfirmed(ctx, unconfirmed_ptr, truncated_msg,
                         _avs_coap_err(
                                 AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED));
    }
}

static avs_error_t handle_truncated_msg(avs_coap_udp_ctx_t *ctx,
                                        uint8_t *message_buf,
                                        size_t message_buf_size,
                                        avs_coap_udp_msg_t *truncated_msg) {
    bool has_token;
    bool has_options;
    _avs_coap_udp_msg_parse_truncated(truncated_msg, message_buf,
                                      message_buf_size, &has_token,
                                      &has_options);
    if (!has_token) {
        LOG(DEBUG, _("received truncated CoAP message with incomplete token; "
                     "ignoring"));
        return AVS_OK;
    }

    avs_error_t err = AVS_OK;
    if (avs_coap_code_is_request(truncated_msg->header.code)) {
        err = handle_truncated_request(ctx, truncated_msg);
    } else if (avs_coap_code_is_response(truncated_msg->header.code)) {
        if (has_options) {
            handle_truncated_response(ctx, truncated_msg);
        } else {
            err = _avs_coap_err(AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED);
        }
    } else {
        // Neither request nor response - ignore
    }
    return err;
}

static avs_error_t
coap_udp_receive_message(avs_coap_ctx_t *ctx_,
                         uint8_t *in_buffer,
                         size_t in_buffer_capacity,
                         avs_coap_borrowed_msg_t *out_request) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;

    avs_coap_udp_msg_t msg;
    memset(&msg.header, 0, sizeof(msg.header));
    memset(out_request, 0, sizeof(*out_request));

    avs_error_t err =
            coap_udp_recv_msg(ctx, &msg, in_buffer, in_buffer_capacity);
    if (avs_is_ok(err)) {
        bool should_handle_request;
        err = handle_msg(ctx, &msg, &should_handle_request);
        if (should_handle_request) {
            *out_request = borrowed_msg_from_udp_msg(&msg);
        }
        return err;
    }
    if (err.category == AVS_COAP_ERR_CATEGORY) {
        switch ((avs_coap_error_t) err.code) {
        case AVS_COAP_ERR_MALFORMED_MESSAGE:
            LOG(DEBUG, _("malformed CoAP message received, ignoring"));
            return AVS_OK;

        case AVS_COAP_ERR_MALFORMED_OPTIONS: {
            if (avs_coap_code_is_request(msg.header.code)) {
                // As defined in RFC7252, a CoAP message with Bad Option code
                // should be send if options are unrecognized or malformed.
                return send_empty_response(ctx, &msg, AVS_COAP_CODE_BAD_OPTION);
            } else if (avs_coap_code_is_response(msg.header.code)) {
                // At this point token and ID are available in the msg
                // struct.
                AVS_LIST(avs_coap_udp_unconfirmed_msg_t) *unconfirmed_ptr =
                        find_unconfirmed_ptr_by_response(ctx, &msg);
                if (unconfirmed_ptr) {
                    fail_unconfirmed(ctx, unconfirmed_ptr, NULL, err);
                }
                const avs_coap_udp_type_t type =
                        _avs_coap_udp_header_get_type(&msg.header);
                if (type == AVS_COAP_UDP_TYPE_CONFIRMABLE
                        || type == AVS_COAP_UDP_TYPE_NON_CONFIRMABLE) {
                    return send_reset(ctx,
                                      _avs_coap_udp_header_get_id(&msg.header));
                }
            }
            return AVS_OK;
        }
        default:
            break;
        }
    } else if (err.category == AVS_ERRNO_CATEGORY) {
        if (err.code == AVS_EMSGSIZE) {
            return handle_truncated_msg(ctx, in_buffer, in_buffer_capacity,
                                        &msg);
        } else if (err.code == AVS_ETIMEDOUT) {
            // AVS_ETIMEDOUT is expected in some cases,
            // so don't log it as unexpected
            return err;
        }
    }

    LOG(DEBUG,
        _("unhandled error (") "%s" _(") returned from coap_udp_recv_msg()"),
        AVS_COAP_STRERROR(err));
    return err;
}

static avs_error_t coap_udp_accept_observation(avs_coap_ctx_t *ctx_,
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

static void coap_udp_cleanup(avs_coap_ctx_t *ctx_) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;

    while (ctx->unconfirmed_messages) {
        avs_coap_udp_unconfirmed_msg_t *unconfirmed =
                AVS_LIST_DETACH(&ctx->unconfirmed_messages);
        try_cleanup_unconfirmed(ctx, unconfirmed, NULL,
                                AVS_COAP_SEND_RESULT_CANCEL, AVS_OK);
    }
    avs_free(ctx);
}

static avs_coap_base_t *coap_udp_get_base(avs_coap_ctx_t *ctx_) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    return &ctx->base;
}

static avs_coap_stats_t coap_udp_get_stats(avs_coap_ctx_t *ctx_) {
    avs_coap_udp_ctx_t *ctx = (avs_coap_udp_ctx_t *) ctx_;
    return ctx->stats;
}

static avs_error_t coap_udp_setsock(avs_coap_ctx_t *ctx,
                                    avs_net_socket_t *socket) {
    return _avs_coap_ctx_set_socket_base(ctx, socket);
}

static const avs_coap_ctx_vtable_t COAP_UDP_VTABLE = {
    .cleanup = coap_udp_cleanup,
    .get_base = coap_udp_get_base,
    .setsock = coap_udp_setsock,
    .max_outgoing_payload_size = coap_udp_max_outgoing_payload_size,
    .max_incoming_payload_size = coap_udp_max_incoming_payload_size,
    .send_message = coap_udp_send_message,
    .abort_delivery = coap_udp_abort_delivery,
    .ignore_current_request = coap_udp_ignore_current_request,
    .receive_message = coap_udp_receive_message,
    .accept_observation = coap_udp_accept_observation,
    .on_timeout = coap_udp_on_timeout,
    .get_stats = coap_udp_get_stats
};

static bool are_tx_params_sane(const avs_coap_udp_tx_params_t *tx_params) {
    return avs_time_duration_valid(avs_time_duration_fmul(
            tx_params->ack_timeout, tx_params->ack_random_factor));
}

avs_coap_ctx_t *
avs_coap_udp_ctx_create(avs_sched_t *sched,
                        const avs_coap_udp_tx_params_t *udp_tx_params,
                        avs_shared_buffer_t *in_buffer,
                        avs_shared_buffer_t *out_buffer,
                        avs_coap_udp_response_cache_t *cache,
                        avs_crypto_prng_ctx_t *prng_ctx) {
    assert(in_buffer);
    assert(out_buffer);
    assert(prng_ctx);

    const char *error;
    if (udp_tx_params) {
        if (!avs_coap_udp_tx_params_valid(udp_tx_params, &error)) {
            LOG(ERROR, _("invalid UDP transmission parameters: ") "%s", error);
            return NULL;
        }
        if (!are_tx_params_sane(udp_tx_params)) {
            LOG(ERROR,
                _("UDP transmission parameters cause ack_timeout overflow"));
            return NULL;
        }
    }

    uint16_t last_msg_id;
#    ifdef AVS_UNIT_TESTING
    last_msg_id = 0;
#    else  // AVS_UNIT_TESTING
    if (avs_crypto_prng_bytes(prng_ctx, (unsigned char *) &last_msg_id,
                              sizeof(last_msg_id))) {
        LOG(ERROR, "failed to generate random initial msg ID");
        return NULL;
    }
#    endif // AVS_UNIT_TESTING

    avs_coap_udp_ctx_t *ctx =
            (avs_coap_udp_ctx_t *) avs_calloc(1, sizeof(avs_coap_udp_ctx_t));
    if (!ctx) {
        return NULL;
    }

    _avs_coap_base_init(&ctx->base, (avs_coap_ctx_t *) ctx, in_buffer,
                        out_buffer, sched, prng_ctx);

    ctx->vtable = &COAP_UDP_VTABLE;
    ctx->last_mtu = SIZE_MAX;
    ctx->tx_params =
            udp_tx_params ? *udp_tx_params : AVS_COAP_DEFAULT_UDP_TX_PARAMS;
    ctx->response_cache = cache;
    ctx->last_msg_id = last_msg_id;

    return (avs_coap_ctx_t *) ctx;
}

int avs_coap_udp_ctx_set_forced_incoming_mtu(avs_coap_ctx_t *ctx,
                                             size_t forced_incoming_mtu) {
    if (!ctx || ctx->vtable != &COAP_UDP_VTABLE) {
        LOG(ERROR, _("avs_coap_udp_ctx_set_forced_incoming_mtu() called on a "
                     "NULL or non-UDP context"));
        return -1;
    }

    ((avs_coap_udp_ctx_t *) ctx)->forced_incoming_mtu = forced_incoming_mtu;
    return 0;
}

#endif // WITH_AVS_COAP_UDP
