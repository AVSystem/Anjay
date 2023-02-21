/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVSYSTEM_COAP_UDP_H
#define AVSYSTEM_COAP_UDP_H

#include <avsystem/coap/avs_coap_config.h>

#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_shared_buffer.h>
#include <avsystem/commons/avs_socket.h>

#include <avsystem/coap/ctx.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CoAP transmission params object. */
typedef struct {
    /** RFC 7252: ACK_TIMEOUT */
    avs_time_duration_t ack_timeout;
    /** RFC 7252: ACK_RANDOM_FACTOR */
    double ack_random_factor;
    /** RFC 7252: MAX_RETRANSMIT */
    unsigned max_retransmit;
    /** RFC 7252: NSTART */
    size_t nstart;
} avs_coap_udp_tx_params_t;

/**
 * Fixed-size CoAP UDP response cache object, used to avoid handling requests
 * duplicates.
 *
 * Every sent non-confirmable CoAP response is stored within this object
 * for up to EXCHANGE_LIFETIME [RFC7252]. Whenever a request is received,
 * this cache is looked up first for a response with matching Message ID.
 * If one is found, the request is interpreted as a duplicate of a previously
 * sent and handled one, and the cached response is sent instead of calling
 * an user-defined request handler.
 */
typedef struct avs_coap_udp_response_cache avs_coap_udp_response_cache_t;

/** Default CoAP/UDP transmission parameters, as defined by RFC7252 */
extern const avs_coap_udp_tx_params_t AVS_COAP_DEFAULT_UDP_TX_PARAMS;

#ifdef WITH_AVS_COAP_UDP

/**
 * @param[in]  tx_params     Transmission parameters to check.
 * @param[out] error_details If not NULL, <c>*error_details</c> is set to
 *                           a string describing what part of @p tx_params
 *                           is invalid, or to NULL if @p tx_params are valid.
 *
 * @returns true if @p tx_params are valid according to RFC7252,
 *          false otherwise.
 */
bool avs_coap_udp_tx_params_valid(const avs_coap_udp_tx_params_t *tx_params,
                                  const char **error_details);

/**
 * @returns MAX_TRANSMIT_SPAN value derived from @p tx_params according to the
 *          formula specified in RFC7252.
 */
avs_time_duration_t
avs_coap_udp_max_transmit_span(const avs_coap_udp_tx_params_t *tx_params);

/**
 * @returns MAX_TRANSMIT_WAIT value derived from @p tx_params according to the
 *          formula specified in RFC7252.
 */
avs_time_duration_t
avs_coap_udp_max_transmit_wait(const avs_coap_udp_tx_params_t *tx_params);

/**
 * @returns EXCHANGE_LIFETIME value derived from @p tx_params according
 *          to the formula specified in RFC7252.
 */
avs_time_duration_t
avs_coap_udp_exchange_lifetime(const avs_coap_udp_tx_params_t *tx_params);

/**
 * Creates a response cache object.
 *
 * @param capacity Number of bytes the cache should be able to hold.
 *
 * @return Created response cache object, or NULL if there is not enough memory
 *         or @p capacity is 0.
 *
 * NOTE: NULL @ref avs_coap_udp_response_cache_t object is equivalent to a
 * correct, always-empty cache object.
 */
avs_coap_udp_response_cache_t *
avs_coap_udp_response_cache_create(size_t capacity);

/**
 * Frees any resources used by given @p cache_ptr and sets <c>*cache_ptr</c>
 * to NULL.
 *
 * @param cache_ptr Pointer to the cache object to free. May be NULL, or point
 *                  to NULL, in which case a call to this function is a no-op.
 */
void avs_coap_udp_response_cache_release(
        avs_coap_udp_response_cache_t **cache_ptr);

/**
 * Creates a CoAP/UDP context without associated socket.
 *
 * IMPORTANT: The socket MUST be set via @ref avs_coap_ctx_set_socket() before
 * any operations on the context are performed. Otherwise the behavior is
 * undefined.
 *
 * @param sched         Scheduler object that will be used to manage
 *                      retransmissions.
 *
 *                      MUST NOT be NULL. Created context object does not take
 *                      ownership of the scheduler, which MUST outlive created
 *                      CoAP context object.
 *
 * @param udp_tx_params UDP transmission parameters used by the CoAP context.
 *                      They are copied into CoAP context object, so the
 *                      pointer does not need to be kept valid after the call.
 *
 * @param in_buffer     Buffer used for temporary storage of incoming packets.
 *
 *                      MUST NOT be NULL and MUST be different from
 *                      @p out_buffer . Created context object does not take
 *                      ownership of the buffer, which MUST outlive created
 *                      CoAP context object.
 *
 * @param out_buffer    Buffer used for temporary storage of outgoing packets.
 *
 *                      MUST NOT be NULL and MUST be different from
 *                      @p in_buffer . Created context object does not take
 *                      ownership of the buffer, which MUST outlive created
 *                      CoAP context object.
 *
 * @param cache         Response cache to use for handling duplicate requests.
 *
 *                      MAY be NULL or shared between multiple CoAP context
 *                      objects, but MUST outlive all CoAP context objects it
 *                      is passed to.
 *
 * @param prng_ctx      PRNG context to use for token generation. MUST NOT be
 *                      @c NULL . MUST outlive the created CoAP context.
 *
 * @returns Created CoAP/UDP context on success, NULL on error.
 *
 * NOTE: @p in_buffer and @p out_buffer may be reused across different CoAP
 * contexts if they are not used concurrently.
 */
avs_coap_ctx_t *
avs_coap_udp_ctx_create(avs_sched_t *sched,
                        const avs_coap_udp_tx_params_t *udp_tx_params,
                        avs_shared_buffer_t *in_buffer,
                        avs_shared_buffer_t *out_buffer,
                        avs_coap_udp_response_cache_t *cache,
                        avs_crypto_prng_ctx_t *prng_ctx);

/**
 * Sets forced incoming MTU on a CoAP/UDP context.
 *
 * This value will be used when calculating BLOCK size to request from the
 * remote endpoint when performing renegotiation, and will have impact on the
 * result of @ref avs_coap_max_incoming_message_payload.
 *
 * @param ctx                 CoAP/UDP context to operate on, previously created
 *                            using @ref avs_coap_udp_ctx_create.
 *
 * @param forced_incoming_mtu Number of bytes expected to be the upper limit of
 *                            incoming message size, calculated on the datagram
 *                            layer (similar to @c AVS_NET_SOCKET_OPT_INNER_MTU)
 *                            or @c 0 to disable this mechanism and use MTU
 *                            reported by socket instead.
 *
 * @returns 0 on success, or -1 if @p ctx is not a CoAP/UDP context created
 *          by @ref avs_coap_udp_ctx_create.
 */
int avs_coap_udp_ctx_set_forced_incoming_mtu(avs_coap_ctx_t *ctx,
                                             size_t forced_incoming_mtu);

/**
 * Sets CoAP/UDP context transmission params.
 *
 * @param ctx    CoAP/UDP context to operate on.
 * @param params New UDP transmission params. Passing <c>NULL</c>
 *               will cause default transmission params to be set.
 *
 * @returns 0 on success, negative value if passed context is not
 *          a CoAP/UDP one or transmission params are invalid.
 */
int avs_coap_udp_ctx_set_tx_params(avs_coap_ctx_t *ctx,
                                   const avs_coap_udp_tx_params_t *tx_params);

/**
 * Gets CoAP/UDP context transmission params.
 *
 * @param ctx CoAP/UDP context to operate on.
 *
 * @returns UDP transmission params on success,
 *          <c>NULL</c> on failure.
 */
const avs_coap_udp_tx_params_t *
avs_coap_udp_ctx_get_tx_params(avs_coap_ctx_t *ctx);

#endif // WITH_AVS_COAP_UDP

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_UDP_H
