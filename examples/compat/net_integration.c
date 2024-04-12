#include <stdint.h>
#include <stdio.h>

#include <anj/anj_net.h>

#include "net_integration.h"

#ifdef EXAMPLE_WITH_DTLS_PSK
anj_net_op_res_t net_open_dtls(anj_net_conn_ref_t *conn_ref,
                               const char *hostname,
                               uint16_t port,
                               const char *identity,
                               const char *psk) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_OPEN_DTLS,
        .args.open_dtls = {
            .hostname = hostname,
            .port = port,
            .version = ANJ_NET_IP_VER_V4,
            .identity = identity,
            .psk = psk,
            .try_resume = false
        }
    };
    anj_net_op_res_t res = anj_net_op_handler(&ctx);
    if (net_is_ok(res)) {
        *conn_ref = ctx.conn_ref;
    }
    return res;
}

anj_net_op_res_t net_open_dtls_res(anj_net_conn_ref_t *conn_ref) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_OPEN_DTLS_RES,
        .conn_ref = *conn_ref,
        .args.open_dtls_res = {
            .resumed = false
        }
    };
    return anj_net_op_handler(&ctx);
}
#else  // EXAMPLE_WITH_DTLS_PSK
anj_net_op_res_t net_open_udp(anj_net_conn_ref_t *conn_ref,
                              const char *hostname,
                              uint16_t port) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_OPEN_UDP,
        .args.open_udp = {
            .hostname = hostname,
            .port = port,
            .version = ANJ_NET_IP_VER_V4
        }
    };
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
#endif // EXAMPLE_WITH_DTLS_PSK

anj_net_op_res_t
net_send(anj_net_conn_ref_t *conn_ref, const uint8_t *buf, size_t length) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_SEND,
        .conn_ref = *conn_ref,
        .args.send = {
            .buf = buf,
            .length = length
        }
    };
    return anj_net_op_handler(&ctx);
}

anj_net_op_res_t net_send_res(anj_net_conn_ref_t *conn_ref,
                              size_t *out_write_length) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_SEND_RES,
        .conn_ref = *conn_ref
    };
    anj_net_op_res_t res = anj_net_op_handler(&ctx);
    *out_write_length = ctx.args.send_res.out_write_length;
    return res;
}

anj_net_op_res_t net_try_recv(anj_net_conn_ref_t *conn_ref,
                              size_t buf_length,
                              uint8_t *out_read_buf,
                              size_t *out_read_length) {
    anj_net_op_ctx_t ctx = {
        .op = ANJ_NET_OP_TRY_RECV,
        .conn_ref = *conn_ref,
        .args.try_recv = {
            .length = buf_length,
            .out_read_buf = out_read_buf
        }
    };
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
