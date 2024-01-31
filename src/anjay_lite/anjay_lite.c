/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <fluf/fluf.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>

#include "anjay_lite_objs.h"
#include "anjay_lite_register.h"
#include "anjay_lite_servers.h"

int anjay_lite_init(anjay_lite_t *anjay_lite) {
    fluf_init(0xffff);

    sdm_obj_t *obj =
            anjay_lite_server_obj_setup(anjay_lite->server_conf.ssid,
                                        anjay_lite->server_conf.lifetime,
                                        anjay_lite->server_conf.binding);
    if (!obj || sdm_add_obj(&anjay_lite->dm, obj)) {
        return -1;
    }
    obj = anjay_lite_security_obj_setup(anjay_lite->server_conf.ssid,
                                        anjay_lite->server_conf.hostname,
                                        anjay_lite->server_conf.security_mode);
    if (!obj || sdm_add_obj(&anjay_lite->dm, obj)) {
        return -1;
    }

    anjay_lite_conn_conf_t server_conf;
    server_conf.udp.hostname = anjay_lite->server_conf.hostname;
    server_conf.udp.port = (int) anjay_lite->server_conf.port;
    server_conf.udp.version = ANJAY_NET_IP_VER_V4;

    return anjay_lite_register_add_server(&server_conf,
                                          anjay_lite->server_conf.binding,
                                          anjay_lite->endpoint_name,
                                          anjay_lite->server_conf.lifetime);
}

void anjay_lite_process(anjay_lite_t *anjay_lite) {
    anjay_lite_servers_process(anjay_lite);
    anjay_lite_register_process(anjay_lite);
}

void anjay_lite_send(uint8_t *payload, size_t size) {
    anjay_lite_send_process(payload, size);
}
