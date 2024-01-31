/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_LITE_SERVERS_H
#define ANJAY_LITE_SERVERS_H

#include <stdbool.h>
#include <stdint.h>

#include <anjay_lite/anjay_lite_config.h>
#include <anjay_lite/anjay_net.h>

#include <fluf/fluf.h>

typedef enum {
    ANJAY_SERVERS_INACTIVE,
    ANJAY_SERVERS_INIT,
    ANJAY_SERVERS_OFFLINE,
    ANJAY_SERVERS_OPEN_IN_PROGRESS,
    ANJAY_SERVERS_ONLINE,
    // TODO: add bootstrap
    ANJAY_SERVERS_REGISTER,
    ANJAY_SERVERS_ERROR,
    ANJAY_SERVERS_CLOSE_IN_PROGRESS,
    ANJAY_SERVERS_INVALID
} anjay_servers_state_t;

typedef void anjay_servers_request_response_t(fluf_data_t *response,
                                              bool is_timeout,
                                              uint16_t server_id);

void anjay_lite_servers_process(anjay_lite_t *anjay_lite);

int anjay_lite_servers_add_server(anjay_lite_conn_conf_t *server_conf,
                                  fluf_binding_type_t binding);

int anjay_lite_servers_exchange_request(
        uint16_t server_id,
        fluf_data_t *request,
        anjay_servers_request_response_t *response_callback);

void anjay_lite_servers_exchange_delete(uint16_t server_id);

anjay_servers_state_t anjay_lite_servers_get_state(uint16_t server_id);
int anjay_lite_servers_set_state(uint16_t server_id,
                                 anjay_servers_state_t new_state);

void _anjay_lite_servers_get_register_payload(anjay_lite_t *anjay_lite,
                                              fluf_data_t *msg);

void anjay_lite_send_process(uint8_t *payload, size_t size);

#endif // ANJAY_LITE_SERVERS_H
