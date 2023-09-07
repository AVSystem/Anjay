/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_UDP_UDP_CTX_H
#define AVS_COAP_SRC_UDP_UDP_CTX_H

#include "../avs_coap_ctx.h"
#include "avs_coap_udp_msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/** Retry state object used to calculate retransmission timeouts. */
typedef struct {
    /**
     * Number of original packet retransmissions left to try.
     *
     * The value of retries_left shall vary between 0 and MAX_RETRANSMIT
     * inclusively.
     */
    unsigned retries_left;
    /**
     * Amount of time to wait for the response (either to an initial packet or a
     * retransmitted one).
     */
    avs_time_duration_t recv_timeout;
} avs_coap_retry_state_t;

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

    /** Current state of retransmission timeout calculation. */
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

#ifdef WITH_AVS_COAP_OBSERVE
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
#endif // WITH_AVS_COAP_OBSERVE

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
#ifdef WITH_AVS_COAP_OBSERVE
    avs_coap_udp_notify_cache_t notify_cache;
#endif // WITH_AVS_COAP_OBSERVE
} avs_coap_udp_ctx_t;

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_UDP_UDP_CTX_H
