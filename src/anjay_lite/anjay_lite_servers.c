/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>

#include <anj/anj_time.h>
#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>
#include <anj/sdm_notification.h>

#include <anjay_lite/anjay_lite.h>
#include <anjay_lite/anjay_net.h>

#include "anjay_lite_servers.h"

#define SERVERS_NUMBER ANJAY_LITE_ALLOWED_SERVERS_NUMBER

typedef struct {
    anjay_servers_state_t state;
    anjay_lite_conn_conf_t conf;
    fluf_binding_type_t binding;
    uint64_t last_operation_timestamp;
    anjay_net_conn_ref_t conn_ref;
    void *user_data;
    bool active_exchange;
    anjay_servers_request_response_t *response_callback;
    fluf_coap_msg_t coap;
    bool awaiting_exchange_send_res;
    bool awaiting_dm_op_send_res;
    size_t expected_write_size;
} server_t;

static server_t servers[SERVERS_NUMBER];
static uint16_t servers_counter;
static uint8_t msg_buff[ANJAY_LITE_MSG_BUFF_SIZE];
static char payload_buff[ANJAY_LITE_PAYLOAD_BUFF_SIZE];

static inline bool is_ok(anjay_net_op_res_t res) {
    return res == ANJAY_NET_OP_RES_OK;
}

static inline bool is_again(anjay_net_op_res_t res) {
    return res == ANJAY_NET_OP_RES_AGAIN;
}

static inline bool is_err(anjay_net_op_res_t res) {
    return res == ANJAY_NET_OP_RES_ERR;
}

static anjay_net_op_res_t do_open_udp(server_t *server) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_OPEN_UDP,
        .args.open_udp = {
            .hostname = server->conf.udp.hostname,
            .port = (uint16_t) server->conf.udp.port,
            .version = ANJAY_NET_IP_VER_V4
        }
    };
    anjay_net_op_res_t res = anjay_net_op_handler(&ctx);
    if (is_ok(res)) {
        server->conn_ref = ctx.conn_ref;
    }
    return res;
}

static anjay_net_op_res_t do_open_udp_res(server_t *server) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_OPEN_UDP_RES,
        .conn_ref = server->conn_ref
    };

    return anjay_net_op_handler(&ctx);
}

static anjay_net_op_res_t
do_send(server_t *server, size_t length, const uint8_t *buf) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_SEND,
        .conn_ref = server->conn_ref,
        .args.send = {
            .buf = buf,
            .length = length
        }
    };

    return anjay_net_op_handler(&ctx);
}

static anjay_net_op_res_t do_send_res(server_t *server,
                                      size_t *out_write_length) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_SEND_RES,
        .conn_ref = server->conn_ref
    };

    anjay_net_op_res_t res = anjay_net_op_handler(&ctx);
    *out_write_length = ctx.args.send_res.out_write_length;
    return res;
}

static anjay_net_op_res_t do_try_recv(server_t *server,
                                      size_t length,
                                      uint8_t *out_read_buf,
                                      size_t *out_read_length) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_TRY_RECV,
        .conn_ref = server->conn_ref,
        .args.try_recv = {
            .length = length,
            .out_read_buf = out_read_buf
        }
    };

    anjay_net_op_res_t res = anjay_net_op_handler(&ctx);
    *out_read_length = ctx.args.try_recv.out_read_length;
    return res;
}

static anjay_net_op_res_t do_close(server_t *server) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_CLOSE,
        .conn_ref = server->conn_ref
    };
    return anjay_net_op_handler(&ctx);
}

static anjay_net_op_res_t do_close_res(server_t *server) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_CLOSE_RES,
        .conn_ref = server->conn_ref
    };
    return anjay_net_op_handler(&ctx);
}

static anjay_net_op_res_t do_cleanup(server_t *server) {
    anjay_net_op_ctx_t ctx = {
        .op = ANJAY_NET_OP_CLEANUP,
        .conn_ref = server->conn_ref
    };
    return anjay_net_op_handler(&ctx);
}

static void open_connection(server_t *server) {
    anjay_net_op_res_t res = ANJAY_NET_OP_RES_ERR;
    if (server->state == ANJAY_SERVERS_INIT) {
        server->state = ANJAY_SERVERS_OFFLINE;
        res = do_open_udp(server);
    } else if (anj_time_now() - server->last_operation_timestamp
               > ANJAY_LITE_RECONNECTION_TIMEOUT_MS) {

        server->last_operation_timestamp = anj_time_now();
        res = do_open_udp(server);
    }

    if (is_ok(res)) {
        server->state = ANJAY_SERVERS_OPEN_IN_PROGRESS;
    }
}

static void await_open_result(server_t *server) {
    anjay_net_op_res_t res = do_open_udp_res(server);
    if (is_again(res)) {
        return;
    }
    if (is_ok(res)) {
        server->state = ANJAY_SERVERS_ONLINE;
    } else {
        do_cleanup(server);
        server->state = ANJAY_SERVERS_INIT;
    }
}

static void close_connection(server_t *server) {
    if (anj_time_now() - server->last_operation_timestamp
            > ANJAY_LITE_RECONNECTION_TIMEOUT_MS) {
        server->last_operation_timestamp = anj_time_now();
        if (is_ok(do_close(server))) {
            server->state = ANJAY_SERVERS_CLOSE_IN_PROGRESS;
        }
    }
}

static void await_close_result(server_t *server) {
    anjay_net_op_res_t res = do_close_res(server);
    if (is_again(res)) {
        return;
    }
    do_cleanup(server);
    server->state = ANJAY_SERVERS_INIT;
}

static void
get_new_msg(anjay_lite_t *anjay_lite, server_t *server, uint16_t server_id) {
    fluf_data_t data;
    size_t msg_size;

    while (1) {
        anjay_net_op_res_t res = do_try_recv(server, ANJAY_LITE_MSG_BUFF_SIZE,
                                             msg_buff, &msg_size);
        if (is_again(res)) {
            break; // no msg found
        } else if (is_err(res)) {
            server->state = ANJAY_SERVERS_ERROR;
            break;
        }

        memset(&data, 0, sizeof(data));
        if (!fluf_msg_decode(msg_buff, msg_size, server->binding, &data)) {
            // ignore all request before registrationd
            // TODO: ADD BLOCK OPTIONS
            if (data.operation == FLUF_OP_RESPONSE && server->active_exchange) {
                if (server->active_exchange) {
                    // match the repsonse with the request
                    switch (server->binding) {
                    case FLUF_BINDING_UDP:
                    case FLUF_BINDING_DTLS_PSK:
                        if (memcmp(server->coap.coap_udp.token.bytes,
                                   data.coap.coap_udp.token.bytes,
                                   data.coap.coap_udp.token.size)) {
                            // token no match
                            return;
                        }
                        break;
                    default:
                        return;
                    }
                    server->response_callback(&data, false, server_id);
                    server->active_exchange = false;
                }
            }
            // if DM Interface
            size_t response_msg_size = 0;
            if (server->state == ANJAY_SERVERS_REGISTER) {
                if ((data.operation >= FLUF_OP_DM_READ
                     && data.operation <= FLUF_OP_DM_DELETE)
                        || data.operation == FLUF_OP_INF_OBSERVE
                        || data.operation == FLUF_OP_INF_CANCEL_OBSERVE) {
                    if (data.operation == FLUF_OP_INF_OBSERVE
                            || data.operation == FLUF_OP_INF_CANCEL_OBSERVE
                            || data.operation == FLUF_OP_DM_WRITE_ATTR) {
                        sdm_notification(&data, &anjay_lite->dm, payload_buff,
                                         ANJAY_LITE_PAYLOAD_BUFF_SIZE);
                    } else {
                        sdm_process(&anjay_lite->dm_impl, &anjay_lite->dm,
                                    &data, false, payload_buff,
                                    ANJAY_LITE_PAYLOAD_BUFF_SIZE);
                    }
                    if (!fluf_msg_prepare(&data, msg_buff,
                                          ANJAY_LITE_MSG_BUFF_SIZE,
                                          &response_msg_size)) {
                        if (!is_ok(do_send(server, response_msg_size,
                                           msg_buff))) {
                            server->state = ANJAY_SERVERS_ERROR;
                            return;
                        } else {
                            server->expected_write_size = response_msg_size;
                            server->awaiting_dm_op_send_res = true;
                        }
                    }
                }
            }
        }
    }
}

static void await_exchange_send_res(server_t *server, uint16_t server_id) {
    size_t write_length;
    anjay_net_op_res_t res = do_send_res(server, &write_length);
    if (is_again(res)) {
        return;
    }
    server->awaiting_exchange_send_res = false;
    if (is_err(res) || write_length != server->expected_write_size) {
        server->active_exchange = false;
        server->response_callback(NULL, true, server_id);
    }
}

static void await_dm_op_send_res(server_t *server) {
    size_t write_length;
    anjay_net_op_res_t res = do_send_res(server, &write_length);
    if (is_again(res)) {
        return;
    }
    server->awaiting_dm_op_send_res = false;
    if (is_err(res) || write_length != server->expected_write_size) {
        server->state = ANJAY_SERVERS_ERROR;
    }
}

void anjay_lite_servers_exchange_delete(uint16_t server_id) {
    if (server_id < SERVERS_NUMBER) {
        servers[server_id].active_exchange = false;
    }
}

void _anjay_lite_servers_get_register_payload(anjay_lite_t *anjay_lite,
                                              fluf_data_t *msg) {
    sdm_process(&anjay_lite->dm_impl, &anjay_lite->dm, msg, false, payload_buff,
                ANJAY_LITE_PAYLOAD_BUFF_SIZE);
}

int anjay_lite_servers_exchange_request(
        uint16_t server_id,
        fluf_data_t *request,
        anjay_servers_request_response_t *response_callback) {

    if (!response_callback || server_id >= SERVERS_NUMBER
            || servers[server_id].active_exchange) {
        return -1;
    }
    server_t *server = &servers[server_id];
    server->response_callback = response_callback;

    if (server->state != ANJAY_SERVERS_ONLINE
            && server->state != ANJAY_SERVERS_REGISTER) {
        return -1;
    }

    size_t request_msg_size = 0;
    request->binding = server->binding;
    if (!fluf_msg_prepare(request, msg_buff, ANJAY_LITE_MSG_BUFF_SIZE,
                          &request_msg_size)) {
        if (is_ok(do_send(server, request_msg_size, msg_buff))) {
            memcpy(&servers->coap, &request->coap, sizeof(fluf_coap_msg_t));
            server->active_exchange = true;
            server->last_operation_timestamp = anj_time_now();
            server->expected_write_size = request_msg_size;
            server->awaiting_exchange_send_res = true;
            return 0;
        }
    }

    return -1;
}

static void notification_process(anjay_lite_t *anjay_lite, server_t *server) {
    fluf_data_t data;
    size_t response_msg_size = 0;

    memset(&data, 0, sizeof(fluf_data_t));
    data.binding = server->binding;

    sdm_notification_process(&data, &anjay_lite->dm, payload_buff,
                             ANJAY_LITE_PAYLOAD_BUFF_SIZE,
                             FLUF_COAP_FORMAT_SENML_CBOR);
    if (data.operation != FLUF_OP_INF_NON_CON_NOTIFY
            || fluf_msg_prepare(&data, msg_buff, ANJAY_LITE_MSG_BUFF_SIZE,
                                &response_msg_size)) {
        return;
    }

    if (!is_ok(do_send(server, response_msg_size, msg_buff))) {
        server->state = ANJAY_SERVERS_ERROR;
        return;
    } else {
        server->expected_write_size = response_msg_size;
        server->awaiting_dm_op_send_res = true;
    }
}

void anjay_lite_servers_process(anjay_lite_t *anjay_lite) {
    for (uint16_t i = 0; i < SERVERS_NUMBER; i++) {
        if (servers[i].state == ANJAY_SERVERS_OFFLINE
                || servers[i].state == ANJAY_SERVERS_INIT) {
            open_connection(&servers[i]);
        } else if (servers[i].state == ANJAY_SERVERS_OPEN_IN_PROGRESS) {
            await_open_result(&servers[i]);
        } else if (servers[i].state == ANJAY_SERVERS_ONLINE
                   || servers[i].state == ANJAY_SERVERS_REGISTER) {
            if (servers[i].awaiting_exchange_send_res) {
                await_exchange_send_res(&servers[i], i);
            } else if (servers[i].awaiting_dm_op_send_res) {
                await_dm_op_send_res(&servers[i]);
            } else {
                get_new_msg(anjay_lite, &servers[i], i);
                notification_process(anjay_lite, &servers[i]);
            }
        } else if (servers[i].state == ANJAY_SERVERS_ERROR) {
            close_connection(&servers[i]);
        } else if (servers[i].state == ANJAY_SERVERS_CLOSE_IN_PROGRESS) {
            await_close_result(&servers[i]);
        }
        // check timeouts
        if (servers[i].active_exchange
                && (anj_time_now() - servers[i].last_operation_timestamp
                    > ANJAY_LITE_RESPONSE_TIMEOUT_MS)) {
            servers[i].active_exchange = false;
            servers[i].response_callback(NULL, true, i);
        }
    }
}

int anjay_lite_servers_add_server(anjay_lite_conn_conf_t *server_conf,
                                  fluf_binding_type_t binding) {
    if (servers_counter == SERVERS_NUMBER || !server_conf) {
        return -1;
    }

    servers[servers_counter].binding = binding;
    memcpy(&(servers[servers_counter].conf), server_conf,
           sizeof(anjay_lite_conn_conf_t));
    servers[servers_counter].state = ANJAY_SERVERS_OFFLINE;

    int ret = (int) servers_counter;
    servers_counter++;
    return ret;
}

anjay_servers_state_t anjay_lite_servers_get_state(uint16_t server_id) {
    if (server_id < SERVERS_NUMBER) {
        return servers[server_id].state;
    }
    return ANJAY_SERVERS_INVALID;
}

int anjay_lite_servers_set_state(uint16_t server_id,
                                 anjay_servers_state_t new_state) {
    if (server_id >= SERVERS_NUMBER) {
        return -1;
    }

    anjay_servers_state_t *state = &(servers[server_id].state);

    // allowed transitions
    if (*state == ANJAY_SERVERS_ONLINE && new_state == ANJAY_SERVERS_REGISTER) {
        *state = ANJAY_SERVERS_REGISTER;
        return 0;
    } else if (new_state == ANJAY_SERVERS_ERROR) {
        *state = ANJAY_SERVERS_ERROR;
        return 0;
    }

    return -1;
}

void anjay_lite_send_process(uint8_t *payload, size_t size) {
    for (int i = 0; i < SERVERS_NUMBER; i++) {
        if (servers[i].state == ANJAY_SERVERS_REGISTER) {
            fluf_data_t data;
            memset(&data, 0, sizeof(fluf_data_t));
            uint8_t msg_buf[512];
            size_t msg_buf_size;

            data.binding = FLUF_BINDING_UDP;
            data.operation = FLUF_OP_INF_SEND;
            data.content_format = FLUF_COAP_FORMAT_SENML_CBOR;
            data.payload = payload;
            data.payload_size = size;

            if (fluf_msg_prepare(&data, msg_buf, sizeof(msg_buf),
                                 &msg_buf_size)) {
                return;
            }
            do_send(&servers[i], msg_buf_size, msg_buf);
        }
    }
}
