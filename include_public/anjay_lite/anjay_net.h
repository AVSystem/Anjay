#ifndef ANJAY_LITE_ANJAY_NET_H
#define ANJAY_LITE_ANJAY_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ANJAY_NET_IP_VER_V4, ANJAY_NET_IP_VER_V6 } anjay_net_ip_ver_t;

typedef enum {
    ANJAY_NET_OP_OPEN_UDP,
    ANJAY_NET_OP_OPEN_UDP_RES,
    ANJAY_NET_OP_OPEN_DTLS,
    ANJAY_NET_OP_OPEN_DTLS_RES,
    ANJAY_NET_OP_TRY_RECV,
    ANJAY_NET_OP_SEND,
    ANJAY_NET_OP_SEND_RES,
    ANJAY_NET_OP_CLOSE,
    ANJAY_NET_OP_CLOSE_RES,
    ANJAY_NET_OP_CLEANUP
} anjay_net_op_t;

typedef union {
    void *ref_ptr;
    int ref_int;
} anjay_net_conn_ref_t;

typedef struct {
    const char *hostname;
    uint16_t port;
    anjay_net_ip_ver_t version;
} anjay_net_op_open_udp_args_t;

typedef struct {
    const char *hostname;
    uint16_t port;
    anjay_net_ip_ver_t version;
    const char *identity;
    const char *psk;
    bool try_resume;
} anjay_net_op_open_dtls_args_t;

typedef struct {
    bool resumed;
} anjay_net_op_open_dtls_res_args_t;

typedef struct {
    size_t length;
    uint8_t *out_read_buf;
    size_t out_read_length;
} anjay_net_op_try_recv_args_t;

typedef struct {
    size_t length;
    const uint8_t *buf;
} anjay_net_op_send_args_t;

typedef struct {
    size_t out_write_length;
} anjay_net_op_send_res_args_t;

typedef struct {
    anjay_net_op_t op;
    anjay_net_conn_ref_t conn_ref;
    union {
        anjay_net_op_open_udp_args_t open_udp;
        anjay_net_op_open_dtls_args_t open_dtls;
        anjay_net_op_open_dtls_res_args_t open_dtls_res;
        anjay_net_op_try_recv_args_t try_recv;
        anjay_net_op_send_args_t send;
        anjay_net_op_send_res_args_t send_res;
    } args;
} anjay_net_op_ctx_t;

typedef enum {
    ANJAY_NET_OP_RES_OK,
    ANJAY_NET_OP_RES_AGAIN,
    ANJAY_NET_OP_RES_ERR
} anjay_net_op_res_t;

// TODO: since Anjay lite for now is using static storage and is not externally
// configurable, provide a function declaration instead of typedef for handler.
// There's no user_ctx in this case, too.

anjay_net_op_res_t anjay_net_op_handler(anjay_net_op_ctx_t *op_ctx);

#ifdef __cplusplus
}
#endif

#endif // ANJAY_LITE_ANJAY_NET_H
