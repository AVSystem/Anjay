/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <inttypes.h>

#include <unistd.h>

#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_net.h>
#include <avsystem/commons/avs_prng.h>
#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/coap.h>
#include <avsystem/coap/udp.h>

static void response_handler(avs_coap_ctx_t *ctx,
                             avs_coap_exchange_id_t exchange_id,
                             avs_coap_client_request_state_t result,
                             const avs_coap_client_async_response_t *response,
                             avs_error_t err,
                             void *finished_) {
    (void) ctx;
    (void) err;

    printf("exchange %s: result %u, response code %d\n",
           AVS_UINT64_AS_STRING(exchange_id.value), result,
           response ? response->header.code : -1);

    *(bool *) finished_ = true;
}

int main() {
    avs_net_socket_t *sock = NULL;
    avs_sched_t *sched = NULL;
    avs_shared_buffer_t *in_buf = NULL;
    avs_shared_buffer_t *out_buf = NULL;
    avs_coap_ctx_t *ctx = NULL;

    int result = 0;

    avs_log_set_default_level(AVS_LOG_TRACE);

    avs_crypto_prng_ctx_t *prng_ctx = avs_crypto_prng_new(NULL, NULL);

    if (avs_is_err(avs_net_udp_socket_create(&sock, NULL))
            || !(sched = avs_sched_new("sched", NULL))
            || !(in_buf = avs_shared_buffer_new(4096))
            || !(out_buf = avs_shared_buffer_new(4096))) {
        result = -1;
    } else if (avs_is_err(avs_net_socket_connect(sock, "127.0.0.1", "5683"))) {
        result = -2;
    } else if (!(ctx = avs_coap_udp_ctx_create(sched,
                                               &AVS_COAP_DEFAULT_UDP_TX_PARAMS,
                                               in_buf, out_buf, NULL, prng_ctx))
               || avs_is_err(avs_coap_ctx_set_socket(ctx, sock))) {
        result = -1;
    } else {
        bool finished = false;

        avs_coap_request_header_t req = {
            .code = AVS_COAP_CODE_GET
        };
        avs_coap_client_send_async_request(ctx, NULL, &req, NULL, NULL,
                                           response_handler, &finished);
        while (!finished) {
            avs_sched_run(sched);
            sleep(1);
        }
    }

    avs_coap_ctx_cleanup(&ctx);
    avs_free(in_buf);
    avs_free(out_buf);
    avs_sched_cleanup(&sched);
    avs_net_socket_cleanup(&sock);
    avs_crypto_prng_free(&prng_ctx);
    return result;
}
