#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "example_config.h"

#ifdef EXAMPLE_WITH_DTLS_PSK
#    include <mbedtls/ctr_drbg.h>
#    include <mbedtls/debug.h>
#    include <mbedtls/entropy.h>
#    include <mbedtls/error.h>
#    include <mbedtls/net_sockets.h>
#    include <mbedtls/ssl.h>
#    include <mbedtls/ssl_ciphersuites.h>
#    include <mbedtls/timing.h>
#endif // EXAMPLE_WITH_DTLS_PSK

#include <anj/anj_net.h>

typedef struct {
    char hostname_storage[100];
    char port_storage[6];
    struct addrinfo hints;
    struct gaicb query;
    atomic_bool gai_finished;
    ssize_t last_send_res;
#ifdef EXAMPLE_WITH_DTLS_PSK
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_timing_delay_context timer;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_net_context server_fd;
    bool mbetls_initialized;
#else  // EXAMPLE_WITH_DTLS_PSK
    size_t send_res_await_counter;
    int fd;
#endif // EXAMPLE_WITH_DTLS_PSK
} conn_ctx_t;

// we only support one connection at a time
static conn_ctx_t connection_ctx;

static int af_family_from_ip_ver(anj_net_ip_ver_t ip_ver) {
    return ip_ver == ANJ_NET_IP_VER_V4 ? AF_INET : AF_INET6;
}

static void handle_gai_result(conn_ctx_t *conn_ctx) {
    if (!conn_ctx->query.ar_result) {
        return;
    }
    int fd = socket(conn_ctx->hints.ai_family,
                    conn_ctx->hints.ai_socktype | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return;
    }
    if (connect(fd, conn_ctx->query.ar_result->ai_addr,
                conn_ctx->query.ar_result->ai_addrlen)
            < 0) {
        return;
    }
#ifdef EXAMPLE_WITH_DTLS_PSK
    conn_ctx->server_fd.fd = fd;
#else  // EXAMPLE_WITH_DTLS_PSK
    conn_ctx->fd = fd;
#endif // EXAMPLE_WITH_DTLS_PSK
}

static void gai_cb(union sigval sigval) {
    conn_ctx_t *conn_ctx = (conn_ctx_t *) sigval.sival_ptr;
    handle_gai_result(conn_ctx);
    freeaddrinfo(conn_ctx->query.ar_result);
    atomic_store(&conn_ctx->gai_finished, true);
}

static int copy_str(char *target, size_t target_size, const char *source) {
    int result = snprintf(target, target_size, "%s", source);
    return (result < 0 || (size_t) result >= target_size) ? -1 : 0;
}

#ifdef EXAMPLE_WITH_DTLS_PSK
static const char *PERS = "dtls_client";
static const int SUPPORTED_CIPHERSUITES_LIST[2] = {
    EXAMPLE_SUPPORTED_CIPHERSUITE, 0
};

void clear_mbedtls_context(conn_ctx_t *conn_ctx) {
    mbedtls_net_free(&conn_ctx->server_fd);
    mbedtls_ssl_free(&conn_ctx->ssl);
    mbedtls_ssl_config_free(&conn_ctx->conf);
    mbedtls_ctr_drbg_free(&conn_ctx->ctr_drbg);
    mbedtls_entropy_free(&conn_ctx->entropy);
    memset(conn_ctx, 0, sizeof(*conn_ctx));
}

anj_net_op_res_t anj_net_op_handler(anj_net_op_ctx_t *op_ctx) {
    switch (op_ctx->op) {
    case ANJ_NET_OP_OPEN_DTLS: {
        conn_ctx_t *conn_ctx = &connection_ctx;

        mbedtls_net_init(&conn_ctx->server_fd);
        mbedtls_ssl_init(&conn_ctx->ssl);
        mbedtls_ctr_drbg_init(&conn_ctx->ctr_drbg);
        mbedtls_entropy_init(&conn_ctx->entropy);
        mbedtls_ssl_config_init(&conn_ctx->conf);

        if (mbedtls_ctr_drbg_seed(&conn_ctx->ctr_drbg, mbedtls_entropy_func,
                                  &conn_ctx->entropy,
                                  (const unsigned char *) PERS, strlen(PERS))) {
            clear_mbedtls_context(conn_ctx);
            return ANJ_NET_OP_RES_ERR;
        }
        if (mbedtls_ssl_config_defaults(&conn_ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT)) {
            clear_mbedtls_context(conn_ctx);
            return ANJ_NET_OP_RES_ERR;
        }
        mbedtls_ssl_conf_authmode(&conn_ctx->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_rng(&conn_ctx->conf, mbedtls_ctr_drbg_random,
                             &conn_ctx->ctr_drbg);
        if (mbedtls_ssl_conf_psk(&conn_ctx->conf, op_ctx->args.open_dtls.psk,
                                 strlen(op_ctx->args.open_dtls.psk),
                                 op_ctx->args.open_dtls.identity,
                                 strlen(op_ctx->args.open_dtls.identity))) {
            clear_mbedtls_context(conn_ctx);
            return ANJ_NET_OP_RES_ERR;
        }
        mbedtls_ssl_conf_ciphersuites(&conn_ctx->conf,
                                      SUPPORTED_CIPHERSUITES_LIST);
        if (mbedtls_ssl_setup(&conn_ctx->ssl, &conn_ctx->conf)) {
            clear_mbedtls_context(conn_ctx);
            return ANJ_NET_OP_RES_ERR;
        }

        atomic_init(&conn_ctx->gai_finished, false);
        conn_ctx->server_fd.fd = -1;
        if (copy_str(conn_ctx->hostname_storage,
                     sizeof(conn_ctx->hostname_storage),
                     op_ctx->args.open_udp.hostname)) {
            return ANJ_NET_OP_RES_ERR;
        }
        snprintf(conn_ctx->port_storage, sizeof(conn_ctx->port_storage),
                 "%" PRIu16, op_ctx->args.open_udp.port);
        conn_ctx->hints.ai_family =
                af_family_from_ip_ver(op_ctx->args.open_udp.version);
        conn_ctx->hints.ai_socktype = SOCK_DGRAM;
        conn_ctx->query.ar_request = &conn_ctx->hints;
        conn_ctx->query.ar_name = conn_ctx->hostname_storage;
        conn_ctx->query.ar_service = conn_ctx->port_storage;

        struct sigevent callback = {
            .sigev_notify = SIGEV_THREAD,
            .sigev_value.sival_ptr = conn_ctx,
            .sigev_notify_function = gai_cb
        };

        struct gaicb *query_ptr = &conn_ctx->query;
        if (getaddrinfo_a(GAI_NOWAIT, &query_ptr, 1, &callback)) {
            memset(conn_ctx, 0, sizeof(*conn_ctx));
            return ANJ_NET_OP_RES_ERR;
        }

        op_ctx->conn_ref.ref_ptr = conn_ctx;
        conn_ctx->mbetls_initialized = false;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_OPEN_DTLS_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        if (!atomic_load(&conn_ctx->gai_finished)) {
            return ANJ_NET_OP_RES_AGAIN;
        }
        if (conn_ctx->server_fd.fd < 0) {
            return ANJ_NET_OP_RES_ERR;
        }
        if (!conn_ctx->mbetls_initialized) {
            conn_ctx->mbetls_initialized = true;
            mbedtls_ssl_set_bio(&conn_ctx->ssl, &conn_ctx->server_fd,
                                mbedtls_net_send, mbedtls_net_recv, NULL);
            mbedtls_ssl_set_timer_cb(&conn_ctx->ssl, &conn_ctx->timer,
                                     mbedtls_timing_set_delay,
                                     mbedtls_timing_get_delay);
            if (mbedtls_net_set_nonblock(&conn_ctx->server_fd)) {
                return ANJ_NET_OP_RES_ERR;
            }
        }
        int ret = mbedtls_ssl_handshake(&conn_ctx->ssl);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ
                || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return ANJ_NET_OP_RES_AGAIN;
        } else if (ret) {
            return ANJ_NET_OP_RES_ERR;
        }
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_TRY_RECV: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        errno = 0;
        int ret = mbedtls_ssl_read(&conn_ctx->ssl,
                                   op_ctx->args.try_recv.out_read_buf,
                                   op_ctx->args.try_recv.length);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ
                || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return ANJ_NET_OP_RES_AGAIN;
        } else if (ret < 0) {
            return ANJ_NET_OP_RES_ERR;
        }
        op_ctx->args.try_recv.out_read_length = (size_t) ret;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_SEND: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        errno = 0;
        int ret = mbedtls_ssl_write(&conn_ctx->ssl, op_ctx->args.send.buf,
                                    op_ctx->args.send.length);
        // Just return it in next iteration
        conn_ctx->last_send_res = ret;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_SEND_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        if (conn_ctx->last_send_res < 0) {
            return ANJ_NET_OP_RES_ERR;
        }
        op_ctx->args.send_res.out_write_length =
                (size_t) conn_ctx->last_send_res;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLOSE: {
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLOSE_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        if (conn_ctx->server_fd.fd != -1) {
            return close(conn_ctx->server_fd.fd) ? ANJ_NET_OP_RES_ERR
                                                 : ANJ_NET_OP_RES_OK;
        }
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLEANUP: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        clear_mbedtls_context(conn_ctx);
        return ANJ_NET_OP_RES_OK;
    }
    default: { return ANJ_NET_OP_RES_ERR; }
    }
}
#else  // EXAMPLE_WITH_DTLS_PSK
anj_net_op_res_t anj_net_op_handler(anj_net_op_ctx_t *op_ctx) {
    switch (op_ctx->op) {
    case ANJ_NET_OP_OPEN_UDP: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) calloc(1, sizeof(*conn_ctx));
        if (!conn_ctx) {
            return ANJ_NET_OP_RES_ERR;
        }

        atomic_init(&conn_ctx->gai_finished, false);
        conn_ctx->fd = -1;
        if (copy_str(conn_ctx->hostname_storage,
                     sizeof(conn_ctx->hostname_storage),
                     op_ctx->args.open_udp.hostname)) {
            return ANJ_NET_OP_RES_ERR;
        }
        snprintf(conn_ctx->port_storage, sizeof(conn_ctx->port_storage),
                 "%" PRIu16, op_ctx->args.open_udp.port);
        conn_ctx->hints.ai_family =
                af_family_from_ip_ver(op_ctx->args.open_udp.version);
        conn_ctx->hints.ai_socktype = SOCK_DGRAM;
        conn_ctx->query.ar_request = &conn_ctx->hints;
        conn_ctx->query.ar_name = conn_ctx->hostname_storage;
        conn_ctx->query.ar_service = conn_ctx->port_storage;

        struct sigevent callback = {
            .sigev_notify = SIGEV_THREAD,
            .sigev_value.sival_ptr = conn_ctx,
            .sigev_notify_function = gai_cb
        };

        struct gaicb *query_ptr = &conn_ctx->query;
        if (getaddrinfo_a(GAI_NOWAIT, &query_ptr, 1, &callback)) {
            memset(conn_ctx, 0, sizeof(*conn_ctx));
            return ANJ_NET_OP_RES_ERR;
        }

        op_ctx->conn_ref.ref_ptr = conn_ctx;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_OPEN_UDP_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        if (!atomic_load(&conn_ctx->gai_finished)) {
            return ANJ_NET_OP_RES_AGAIN;
        }
        if (conn_ctx->fd < 0) {
            return ANJ_NET_OP_RES_ERR;
        }
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_TRY_RECV: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        errno = 0;
        ssize_t received =
                recv(conn_ctx->fd, op_ctx->args.try_recv.out_read_buf,
                     op_ctx->args.try_recv.length, 0);

        if (received < 0) {
            return errno == EWOULDBLOCK ? ANJ_NET_OP_RES_AGAIN
                                        : ANJ_NET_OP_RES_ERR;
        }

        op_ctx->args.try_recv.out_read_length = (size_t) received;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_SEND: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        errno = 0;
        ssize_t sent = send(conn_ctx->fd, op_ctx->args.send.buf,
                            op_ctx->args.send.length, 0);

        if (sent == -1) {
            // HACK: in case of errno == EWOULDBLOCK I'd probably have to
            // properly copy the packet and schedule it to be sent in next
            // attempts/calls with ANJ_NET_OP_SEND_RES
            //
            // I imagine that implementation with a modem like BG96 would
            // copy the buffer and await for the result of send operation
            // which would be polled by ANJ_NET_OP_SEND_RES. Current API
            // seems to be the one which matches both possible interfaces
            // the best.
            return ANJ_NET_OP_RES_ERR;
        }

        // Just return it in next iteration
        conn_ctx->last_send_res = sent;
        // Modem implementation could require repetetive asking for
        // send result, so simulate it with a counter
        conn_ctx->send_res_await_counter = 0;

        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_SEND_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        if (conn_ctx->send_res_await_counter++ < 2) {
            return ANJ_NET_OP_RES_AGAIN;
        }

        if (conn_ctx->last_send_res < 0) {
            return ANJ_NET_OP_RES_ERR;
        }

        op_ctx->args.send_res.out_write_length =
                (size_t) conn_ctx->last_send_res;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLOSE: {
        // close in Linux seems to be immediate? so let's just do it in
        // function which asks for result
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLOSE_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        if (conn_ctx->fd != -1) {
            return close(conn_ctx->fd) ? ANJ_NET_OP_RES_ERR : ANJ_NET_OP_RES_OK;
        }
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLEANUP: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        memset(conn_ctx, 0, sizeof(*conn_ctx));
        return ANJ_NET_OP_RES_OK;
    }
    default: { return ANJ_NET_OP_RES_ERR; }
    }
}
#endif // EXAMPLE_WITH_DTLS_PSK
