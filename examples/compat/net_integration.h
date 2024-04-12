#ifndef NET_INTEGRATION_H
#define NET_INTEGRATION_H

#include <stdint.h>

#include <anj/anj_net.h>

#include "example_config.h"

static inline bool net_is_ok(anj_net_op_res_t res) {
    return res == ANJ_NET_OP_RES_OK;
}

static inline bool net_is_again(anj_net_op_res_t res) {
    return res == ANJ_NET_OP_RES_AGAIN;
}

static inline bool net_is_err(anj_net_op_res_t res) {
    return res == ANJ_NET_OP_RES_ERR;
}

#ifdef EXAMPLE_WITH_DTLS_PSK
anj_net_op_res_t net_open_dtls(anj_net_conn_ref_t *conn_ref,
                               const char *hostname,
                               uint16_t port,
                               const char *identity,
                               const char *psk);

anj_net_op_res_t net_open_dtls_res(anj_net_conn_ref_t *conn_ref);
#else  // EXAMPLE_WITH_DTLS_PSK
anj_net_op_res_t
net_open_udp(anj_net_conn_ref_t *conn_ref, const char *hostname, uint16_t port);

anj_net_op_res_t net_open_udp_res(anj_net_conn_ref_t *conn_ref);
#endif // EXAMPLE_WITH_DTLS_PSK

anj_net_op_res_t
net_send(anj_net_conn_ref_t *conn_ref, const uint8_t *buf, size_t length);

anj_net_op_res_t net_send_res(anj_net_conn_ref_t *conn_ref,
                              size_t *out_write_length);

anj_net_op_res_t net_try_recv(anj_net_conn_ref_t *conn_ref,
                              size_t buf_length,
                              uint8_t *out_read_buf,
                              size_t *out_read_length);

anj_net_op_res_t net_close(anj_net_conn_ref_t *conn_ref);

anj_net_op_res_t net_close_res(anj_net_conn_ref_t *conn_ref);

anj_net_op_res_t net_cleanup(anj_net_conn_ref_t *conn_ref);

#endif // NET_INTEGRATION_H
