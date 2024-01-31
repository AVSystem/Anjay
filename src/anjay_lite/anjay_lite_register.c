/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include <fluf/fluf.h>
#include <fluf/fluf_utils.h>

#include <anj/anj_time.h>
#include <anj/sdm_impl.h>

#include "anjay_lite_objs.h"
#include "anjay_lite_register.h"
#include "anjay_lite_servers.h"

typedef struct {
    bool is_active;
    bool is_connected;
    bool waiting_for_response;
    uint16_t id;
    fluf_attr_register_t register_attr;
    char location_path[FLUL_MAX_ALLOWED_LOCATION_PATHS_NUMBER]
                      [ANJAY_LITE_SERVERS_REGISTER_PATH_STR_LEN];
    size_t location_count;
    uint64_t last_update_timestamp;
    uint32_t lifetime;
    bool force_update;
    char lifetime_str[12];
} server_register_t;

static server_register_t servers[ANJAY_LITE_ALLOWED_SERVERS_NUMBER];

static char *bindig_UDP = "U";

static server_register_t *get_server_by_id(uint16_t server_id) {
    for (int i = 0; i < ANJAY_LITE_ALLOWED_SERVERS_NUMBER; i++) {
        if (servers[i].id == server_id) {
            return &servers[i];
        }
    }
    return NULL;
}

static void
register_callback(fluf_data_t *response, bool is_error, uint16_t server_id) {
    server_register_t *server = get_server_by_id(server_id);

    server->waiting_for_response = false;
    if (!is_error) {
        if (response->msg_code == FLUF_COAP_CODE_CREATED) {
            // copy location path
            for (size_t i = 0; i < response->location_path.location_count;
                 i++) {
                memcpy(server->location_path[i],
                       response->location_path.location[i],
                       response->location_path.location_len[i]);
            }
            server->location_count = response->location_path.location_count;
            anjay_lite_servers_set_state(server_id, ANJAY_SERVERS_REGISTER);
            server->last_update_timestamp = anj_time_now();
        }
    } else {
        anjay_lite_servers_set_state(server_id, ANJAY_SERVERS_ERROR);
    }
}

static void send_register_msg(anjay_lite_t *anjay_lite,
                              server_register_t *server) {
    fluf_data_t request;
    memset(&request, 0, sizeof(request));
    request.operation = FLUF_OP_REGISTER;
    _anjay_lite_servers_get_register_payload(anjay_lite, &request);
    request.attr.register_attr = server->register_attr;

    if (!anjay_lite_servers_exchange_request(server->id, &request,
                                             register_callback)) {
        server->waiting_for_response = true;
    }
}

static void
update_callback(fluf_data_t *response, bool is_error, uint16_t server_id) {
    server_register_t *server = get_server_by_id(server_id);

    server->waiting_for_response = false;
    if (!is_error) {
        if (response->msg_code == FLUF_COAP_CODE_CHANGED) {
            server->last_update_timestamp = anj_time_now();
        } else {
            anjay_lite_servers_set_state(server_id, ANJAY_SERVERS_ERROR);
        }
    }
}

static void maybe_send_update_msg(server_register_t *server) {
    if ((anj_time_now() - server->last_update_timestamp) / 1000U
                    > (uint64_t) (server->lifetime / 2)
            || server->force_update) {
        server->force_update = false;
        fluf_data_t request;
        memset(&request, 0, sizeof(request));
        request.operation = FLUF_OP_UPDATE;
        request.location_path.location_count = server->location_count;
        for (size_t i = 0; i < server->location_count; i++) {
            request.location_path.location[i] = server->location_path[i];
            request.location_path.location_len[i] =
                    strlen(server->location_path[i]);
        }

        if (!anjay_lite_servers_exchange_request(server->id, &request,
                                                 update_callback)) {
            server->waiting_for_response = true;
        }
    }
}

int anjay_lite_register_add_server(anjay_lite_conn_conf_t *server_conf,
                                   fluf_binding_type_t binding,
                                   char *endpoint,
                                   uint32_t lifetime) {
    for (int i = 0; i < ANJAY_LITE_ALLOWED_SERVERS_NUMBER; i++) {
        if (!servers[i].is_active) {
            int res = anjay_lite_servers_add_server(server_conf, binding);
            if (res >= 0) {
                server_register_t *server = &servers[i];
                server->is_active = true;
                server->id = (uint16_t) res;
                server->lifetime = lifetime;

                server->register_attr.has_lwm2m_ver = true;
                server->register_attr.lwm2m_ver = FLUF_LWM2M_VERSION_STR;
                server->register_attr.has_binding = true;
                if (binding == FLUF_BINDING_UDP
                        || binding == FLUF_BINDING_DTLS_PSK) {
                    server->register_attr.binding = bindig_UDP;
                }
                server->register_attr.has_endpoint = true;
                server->register_attr.endpoint = endpoint;
                server->register_attr.has_lifetime = true;
                server->register_attr.lifetime = lifetime;
                return 0;
            }
        }
    }
    return -1;
}

void anjay_lite_register_process(anjay_lite_t *anjay_lite) {
    for (int i = 0; i < ANJAY_LITE_ALLOWED_SERVERS_NUMBER; i++) {
        if (servers[i].is_active && !servers[i].waiting_for_response) {
            // check status
            anjay_servers_state_t state =
                    anjay_lite_servers_get_state(servers[i].id);
            if (state == ANJAY_SERVERS_ONLINE) {
                send_register_msg(anjay_lite, &servers[i]);
            } else if (state == ANJAY_SERVERS_REGISTER) {
                servers[i].lifetime = anjay_lite_server_obj_get_lifetime();
                servers[i].force_update =
                        anjay_lite_server_obj_update_trigger_active();
                maybe_send_update_msg(&servers[i]);
            }
        }
    }
}
