/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANJAY_SERVERS_SERVERS_H
#define ANJAY_SERVERS_SERVERS_H

#include <anjay/core.h>

#include "../servers.h"

#include "connection_info.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

struct anjay_servers_struct {
    AVS_LIST(anjay_server_info_t) servers;

    AVS_LIST(anjay_socket_entry_t) public_sockets;
};

struct anjay_server_info_struct {
    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP
    anjay_sched_handle_t sched_update_or_reactivate_handle;

    // These fields are valid only for active servers
    struct {
        anjay_url_t uri;

        bool needs_reload;
        anjay_server_connection_t udp_connection;

        anjay_connection_type_t primary_conn_type;
        anjay_registration_info_t registration_info;
    } data_active;

    // These fields are valid only for inactive servers
    struct {
        bool reactivate_failed;
        uint32_t num_icmp_failures;
    } data_inactive;
};

/**
 * Retryable job backoff configuration for retryable server jobs
 * (Register/Update).
 */
#define ANJAY_SERVER_RETRYABLE_BACKOFF \
    ((anjay_sched_retryable_backoff_t){ \
        .delay = { 1, 0 }, \
        .max_delay = { 120, 0 } \
     })

void _anjay_servers_internal_deregister(anjay_t *anjay,
                                        anjay_servers_t *servers);

void _anjay_servers_internal_cleanup(anjay_t *anjay,
                                     anjay_servers_t *servers);

/**
 * Cleans up server data. Does not send De-Register message.
 */
void _anjay_server_cleanup(const anjay_t *anjay, anjay_server_info_t *server);

bool _anjay_server_active(anjay_server_info_t *server);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(anjay_servers_t *servers, anjay_ssid_t ssid);

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_ptr(anjay_servers_t *servers, anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVERS_H
