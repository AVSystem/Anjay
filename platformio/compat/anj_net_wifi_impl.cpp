/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiUdp.h>

#include <avsystem/commons/avs_log.h>

#include <anj/anj_net.h>

struct socket_ctx {
    bool taken;
    WiFiUDP socket;
    IPAddress remote_addr;
    uint16_t remote_port;
    size_t bytes_sent;
} ctxs[2];

static socket_ctx *find_free_socket_ctx() {
    for (socket_ctx &ctx : ctxs) {
        if (!ctx.taken) {
            return &ctx;
        }
    }
    return nullptr;
}

static socket_ctx *get_socket_ctx(anj_net_op_ctx_t *op_ctx) {
    return reinterpret_cast<socket_ctx *>(op_ctx->conn_ref.ref_ptr);
}

static uint16_t port_offset;
static uint16_t get_next_local_port() {
    return 49152 + (port_offset++ % 16384);
}

anj_net_op_res_t anj_net_op_handler(anj_net_op_ctx_t *op_ctx) {
    switch (op_ctx->op) {
    case ANJ_NET_OP_OPEN_UDP: {
        socket_ctx *ctx = find_free_socket_ctx();
        if (!ctx) {
            avs_log(anj_net, ERROR, "No more free socket contexts");
            return ANJ_NET_OP_RES_ERR;
        }
        if (op_ctx->args.open_udp.version != ANJ_NET_IP_VER_V4) {
            avs_log(anj_net, ERROR, "Wrong IP version");
            return ANJ_NET_OP_RES_ERR;
        }
        if (WiFi.hostByName(op_ctx->args.open_udp.hostname, ctx->remote_addr)
                != 1) {
            avs_log(anj_net, ERROR, "Failed to resolve hostname");
            return ANJ_NET_OP_RES_ERR;
        }
        if (ctx->socket.begin(get_next_local_port()) != 1) {
            return ANJ_NET_OP_RES_ERR;
        }

        ctx->remote_port = op_ctx->args.open_udp.port;
        ctx->taken = true;
        op_ctx->conn_ref.ref_ptr = reinterpret_cast<void *>(ctx);
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_OPEN_UDP_RES: {
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_SEND: {
        socket_ctx *ctx = get_socket_ctx(op_ctx);
        if (ctx->socket.beginPacket(ctx->remote_addr, ctx->remote_port) != 1) {
            avs_log(anj_net, ERROR, "Failed to begin packet");
            return ANJ_NET_OP_RES_ERR;
        }
        if (ctx->socket.write(op_ctx->args.send.buf, op_ctx->args.send.length)
                != op_ctx->args.send.length) {
            avs_log(anj_net, ERROR, "Failed to write entire packet");
            return ANJ_NET_OP_RES_ERR;
        }
        if (ctx->socket.endPacket() != 1) {
            avs_log(anj_net, ERROR, "Failed to send packet");
            return ANJ_NET_OP_RES_ERR;
        }
        avs_log(anj_net, DEBUG, "sent %d bytes", op_ctx->args.send.length);
        ctx->bytes_sent = op_ctx->args.send.length;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_SEND_RES: {
        socket_ctx *ctx = get_socket_ctx(op_ctx);
        op_ctx->args.send_res.out_write_length = ctx->bytes_sent;
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_TRY_RECV: {
        socket_ctx *ctx = get_socket_ctx(op_ctx);
        int available = ctx->socket.parsePacket();
        if (available <= 0) {
            return ANJ_NET_OP_RES_AGAIN;
        }
        int read = ctx->socket.read(op_ctx->args.try_recv.out_read_buf,
                                    op_ctx->args.try_recv.length);
        if (read != available) {
            avs_log(anj_net, ERROR, "Failed to read whole packet");
            return ANJ_NET_OP_RES_ERR;
        }
        op_ctx->args.try_recv.out_read_length = read;
        avs_log(anj_net, DEBUG, "received %d bytes", read);
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLOSE: {
        socket_ctx *ctx = get_socket_ctx(op_ctx);
        ctx->socket.stop();
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLOSE_RES: {
        return ANJ_NET_OP_RES_OK;
    }
    case ANJ_NET_OP_CLEANUP: {
        socket_ctx *ctx = get_socket_ctx(op_ctx);
        ctx->taken = false;
        return ANJ_NET_OP_RES_OK;
    }
    default: { return ANJ_NET_OP_RES_ERR; }
    }
}
