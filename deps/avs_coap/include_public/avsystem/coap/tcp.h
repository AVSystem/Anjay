/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVSYSTEM_COAP_TCP_H
#define AVSYSTEM_COAP_TCP_H

#include <avsystem/coap/avs_coap_config.h>

#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_shared_buffer.h>
#include <avsystem/commons/avs_socket.h>

#include <avsystem/coap/ctx.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WITH_AVS_COAP_TCP

/**
 * Creates a CoAP/TCP context without associated socket.
 *
 * IMPORTANT: The socket MUST be set via @ref avs_coap_ctx_set_socket() before
 * any operations on the context are performed. Otherwise the behavior is
 * undefined.
 *
 * @param sched           Scheduler object that will be used to detect cases
 *                        where the server does not respond to our request.
 *
 *                        MUST NOT be NULL. Created context object does not take
 *                        ownership of the scheduler, which MUST outlive created
 *                        CoAP context object.
 *
 * @param in_buffer       Pointer to a shared input buffer.
 *
 * @param out_buffer      Pointer to a shared output buffer.
 *
 * @param max_opts_size   Size of buffer which will be allocated to handle
 *                        options. Any message with options longer than
 *                        @p max_opts_size will not be handled and an error
 *                        will be returned from on_data_available method.
 *                        MUST BE equal or greater than
 *                        @ref AVS_COAP_MAX_TOKEN_LENGTH .
 *
 * @param request_timeout Time to wait for incoming response after sending a
 *                        request. After this time request is considered
 *                        unsuccessful and response handler is called with
 *                        result indicating failure.
 *                        Used also as time to wait for initial CSM.
 *
 * @param prng_ctx      PRNG context to use for token generation. MUST NOT be
 *                      @c NULL . MUST outlive the created CoAP context.
 *
 * @returns Created CoAP/TCP context on success, NULL if there isn't enough
 *          memory to create the context or buffer sizes requirements are not
 *          met.
 */
avs_coap_ctx_t *avs_coap_tcp_ctx_create(avs_sched_t *sched,
                                        avs_shared_buffer_t *in_buffer,
                                        avs_shared_buffer_t *out_buffer,
                                        size_t max_opts_size,
                                        avs_time_duration_t request_timeout,
                                        avs_crypto_prng_ctx_t *prng_ctx);

#endif // WITH_AVS_COAP_TCP

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_TCP_H
