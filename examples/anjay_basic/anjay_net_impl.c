/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <anj/anj_time.h>

#include <anjay_lite/anjay_net.h>

typedef struct {
    char hostname_storage[100];
    char port_storage[6];
    struct addrinfo hints;
    struct gaicb query;
    atomic_bool gai_finished;
    int fd;
    size_t send_res_await_counter;
    ssize_t last_send_res;
} conn_ctx_t;

static int af_family_from_ip_ver(anjay_net_ip_ver_t ip_ver) {
    return ip_ver == ANJAY_NET_IP_VER_V4 ? AF_INET : AF_INET6;
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
    conn_ctx->fd = fd;
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

anjay_net_op_res_t anjay_net_op_handler(anjay_net_op_ctx_t *op_ctx) {
    switch (op_ctx->op) {
    case ANJAY_NET_OP_OPEN_UDP: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) calloc(1, sizeof(*conn_ctx));
        if (!conn_ctx) {
            return ANJAY_NET_OP_RES_ERR;
        }

        atomic_init(&conn_ctx->gai_finished, false);
        conn_ctx->fd = -1;
        if (copy_str(conn_ctx->hostname_storage,
                     sizeof(conn_ctx->hostname_storage),
                     op_ctx->args.open_udp.hostname)) {
            return ANJAY_NET_OP_RES_ERR;
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
            free(conn_ctx);
            return ANJAY_NET_OP_RES_ERR;
        }

        op_ctx->conn_ref.ref_ptr = conn_ctx;
        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_OPEN_UDP_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        if (!atomic_load(&conn_ctx->gai_finished)) {
            return ANJAY_NET_OP_RES_AGAIN;
        }
        if (conn_ctx->fd < 0) {
            return ANJAY_NET_OP_RES_ERR;
        }
        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_TRY_RECV: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        errno = 0;
        ssize_t received =
                recv(conn_ctx->fd, op_ctx->args.try_recv.out_read_buf,
                     op_ctx->args.try_recv.length, 0);

        if (received < 0) {
            return errno == EWOULDBLOCK ? ANJAY_NET_OP_RES_AGAIN
                                        : ANJAY_NET_OP_RES_ERR;
        }

        op_ctx->args.try_recv.out_read_length = (size_t) received;
        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_SEND: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        errno = 0;
        ssize_t sent = send(conn_ctx->fd, op_ctx->args.send.buf,
                            op_ctx->args.send.length, 0);

        if (sent == -1) {
            // HACK: in case of errno == EWOULDBLOCK I'd probably have to
            // properly copy the packet and schedule it to be sent in next
            // attempts/calls with ANJAY_NET_OP_SEND_RES
            //
            // I imagine that implementation with a modem like BG96 would
            // copy the buffer and await for the result of send operation
            // which would be polled by ANJAY_NET_OP_SEND_RES. Current API
            // seems to be the one which matches both possible interfaces
            // the best.
            return ANJAY_NET_OP_RES_ERR;
        }

        // Just return it in next iteration
        conn_ctx->last_send_res = sent;
        // Modem implementation could require repetetive asking for
        // send result, so simulate it with a counter
        conn_ctx->send_res_await_counter = 0;

        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_SEND_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;

        if (conn_ctx->send_res_await_counter++ < 2) {
            return ANJAY_NET_OP_RES_AGAIN;
        }

        if (conn_ctx->last_send_res < 0) {
            return ANJAY_NET_OP_RES_ERR;
        }

        op_ctx->args.send_res.out_write_length =
                (size_t) conn_ctx->last_send_res;
        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_CLOSE: {
        // close in Linux seems to be immediate? so let's just do it in
        // function which asks for result xD
        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_CLOSE_RES: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        if (conn_ctx->fd != -1) {
            return close(conn_ctx->fd) ? ANJAY_NET_OP_RES_ERR
                                       : ANJAY_NET_OP_RES_OK;
        }
        return ANJAY_NET_OP_RES_OK;
    }
    case ANJAY_NET_OP_CLEANUP: {
        conn_ctx_t *conn_ctx = (conn_ctx_t *) op_ctx->conn_ref.ref_ptr;
        free(conn_ctx);
        return ANJAY_NET_OP_RES_OK;
    }
    default: { return ANJAY_NET_OP_RES_ERR; }
    }
}
