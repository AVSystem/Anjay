/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJ_NET_H
#define ANJ_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ANJ_NET_IP_VER_V4, ANJ_NET_IP_VER_V6 } anj_net_ip_ver_t;

typedef enum {
    ANJ_NET_OP_OPEN_UDP,
    ANJ_NET_OP_OPEN_UDP_RES,
    ANJ_NET_OP_OPEN_DTLS,
    ANJ_NET_OP_OPEN_DTLS_RES,
    ANJ_NET_OP_TRY_RECV,
    ANJ_NET_OP_SEND,
    ANJ_NET_OP_SEND_RES,
    ANJ_NET_OP_CLOSE,
    ANJ_NET_OP_CLOSE_RES,
    ANJ_NET_OP_CLEANUP
} anj_net_op_t;

typedef union {
    void *ref_ptr;
    int ref_int;
} anj_net_conn_ref_t;

typedef struct {
    const char *hostname;
    uint16_t port;
    anj_net_ip_ver_t version;
} anj_net_op_open_udp_args_t;

typedef struct {
    const char *hostname;
    uint16_t port;
    anj_net_ip_ver_t version;
    const char *identity;
    const char *psk;
    bool try_resume;
} anj_net_op_open_dtls_args_t;

typedef struct {
    bool resumed;
} anj_net_op_open_dtls_res_args_t;

typedef struct {
    size_t length;
    uint8_t *out_read_buf;
    size_t out_read_length;
} anj_net_op_try_recv_args_t;

typedef struct {
    size_t length;
    const uint8_t *buf;
} anj_net_op_send_args_t;

typedef struct {
    size_t out_write_length;
} anj_net_op_send_res_args_t;

typedef struct {
    anj_net_op_t op;
    anj_net_conn_ref_t conn_ref;
    union {
        anj_net_op_open_udp_args_t open_udp;
        anj_net_op_open_dtls_args_t open_dtls;
        anj_net_op_open_dtls_res_args_t open_dtls_res;
        anj_net_op_try_recv_args_t try_recv;
        anj_net_op_send_args_t send;
        anj_net_op_send_res_args_t send_res;
    } args;
} anj_net_op_ctx_t;

typedef enum {
    ANJ_NET_OP_RES_OK,
    ANJ_NET_OP_RES_AGAIN,
    ANJ_NET_OP_RES_ERR
} anj_net_op_res_t;

anj_net_op_res_t anj_net_op_handler(anj_net_op_ctx_t *op_ctx);

/**
 * @return The number of milliseconds that have elapsed since the system was
 * started
 */
uint64_t anj_time_now();

#ifdef __cplusplus
}
#endif

#endif // ANJ_NET_H
