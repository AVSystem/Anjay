/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stdint.h>
#include <stdio.h>

#include <anj/anj_net.h>

#include <net_integration.hpp>

anj_net_op_res_t net_open_udp(anj_net_conn_ref_t *conn_ref,
                              const char *hostname,
                              uint16_t port) {
    anj_net_op_ctx_t ctx;
    ctx.op = ANJ_NET_OP_OPEN_UDP;
    ctx.args.open_udp.hostname = hostname;
    ctx.args.open_udp.port = port;
    ctx.args.open_udp.version = ANJ_NET_IP_VER_V4;
    anj_net_op_res_t res = anj_net_op_handler(&ctx);
    if (net_is_ok(res)) {
        *conn_ref = ctx.conn_ref;
    }
    return res;
}

anj_net_op_res_t net_open_udp_res(anj_net_conn_ref_t *conn_ref) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_OPEN_UDP_RES,
        .conn_ref = *conn_ref
    };
    return anj_net_op_handler(&ctx);
}

anj_net_op_res_t
net_send(anj_net_conn_ref_t *conn_ref, const uint8_t *buf, size_t length) {
    anj_net_op_ctx_t ctx;
    ctx.op = ANJ_NET_OP_SEND;
    ctx.conn_ref = *conn_ref;
    ctx.args.send.buf = buf;
    ctx.args.send.length = length;
    return anj_net_op_handler(&ctx);
}

anj_net_op_res_t net_send_res(anj_net_conn_ref_t *conn_ref,
                              size_t *out_write_length) {
    anj_net_op_ctx_t ctx;
    ctx.op = ANJ_NET_OP_SEND_RES;
    ctx.conn_ref = *conn_ref;
    anj_net_op_res_t res = anj_net_op_handler(&ctx);
    *out_write_length = ctx.args.send_res.out_write_length;
    return res;
}

anj_net_op_res_t net_try_recv(anj_net_conn_ref_t *conn_ref,
                              size_t buf_length,
                              uint8_t *out_read_buf,
                              size_t *out_read_length) {
    anj_net_op_ctx_t ctx;
    ctx.op = ANJ_NET_OP_TRY_RECV;
    ctx.conn_ref = *conn_ref;
    ctx.args.try_recv.length = buf_length;
    ctx.args.try_recv.out_read_buf = out_read_buf;
    anj_net_op_res_t res = anj_net_op_handler(&ctx);
    *out_read_length = ctx.args.try_recv.out_read_length;

    if (buf_length == *out_read_length) {
        printf("Recv message does not fit in out_read_buf\n");
        return ANJ_NET_OP_RES_ERR;
    }
    return res;
}

anj_net_op_res_t net_close(anj_net_conn_ref_t *conn_ref) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_CLOSE,
        .conn_ref = *conn_ref
    };
    return anj_net_op_handler(&ctx);
}

anj_net_op_res_t net_close_res(anj_net_conn_ref_t *conn_ref) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_CLOSE_RES,
        .conn_ref = *conn_ref
    };
    return anj_net_op_handler(&ctx);
}

anj_net_op_res_t net_cleanup(anj_net_conn_ref_t *conn_ref) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_CLEANUP,
        .conn_ref = *conn_ref
    };
    return anj_net_op_handler(&ctx);
}
