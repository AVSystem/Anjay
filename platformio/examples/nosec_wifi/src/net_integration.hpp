/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef NET_INTEGRATION_H
#define NET_INTEGRATION_H

#include <Arduino.h>

#include <anj/anj_net.h>

static inline bool net_is_ok(anj_net_op_res_t res) {
    return res == ANJ_NET_OP_RES_OK;
}

static inline bool net_is_again(anj_net_op_res_t res) {
    return res == ANJ_NET_OP_RES_AGAIN;
}

static inline bool net_is_err(anj_net_op_res_t res) {
    return res == ANJ_NET_OP_RES_ERR;
}

anj_net_op_res_t
net_open_udp(anj_net_conn_ref_t *conn_ref, const char *hostname, uint16_t port);

anj_net_op_res_t net_open_udp_res(anj_net_conn_ref_t *conn_ref);

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
