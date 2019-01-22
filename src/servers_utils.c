/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <avsystem/commons/utils.h>

#include <anjay_modules/dm_utils.h>

#include "servers.h"
#include "servers_utils.h"

#include "dm/query.h"

#include "interface/register.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    avs_net_abstract_socket_t *socket;
    anjay_server_info_t *out;
} find_by_udp_socket_args_t;

static int find_by_udp_socket_clb(anjay_t *anjay,
                                  anjay_server_info_t *server,
                                  void *args_) {
    (void) anjay;
    find_by_udp_socket_args_t *args = (find_by_udp_socket_args_t *) args_;
    const anjay_connection_ref_t ref = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_UDP
    };
    if (_anjay_connection_get_online_socket(ref) == args->socket) {
        args->out = server;
        return ANJAY_FOREACH_BREAK;
    }
    return ANJAY_FOREACH_CONTINUE;
}

anjay_server_info_t *
_anjay_servers_find_by_udp_socket(anjay_t *anjay,
                                  avs_net_abstract_socket_t *socket) {
    assert(socket);
    find_by_udp_socket_args_t arg = {
        .socket = socket,
        .out = NULL
    };
    if (_anjay_servers_foreach_active(anjay, find_by_udp_socket_clb, &arg)) {
        return NULL;
    }
    return arg.out;
}

typedef struct {
    anjay_ssid_t ssid;
    anjay_server_info_t *out;
} find_active_args_t;

static int
find_active_clb(anjay_t *anjay, anjay_server_info_t *server, void *args_) {
    (void) anjay;
    find_active_args_t *args = (find_active_args_t *) args_;
    if (_anjay_server_ssid(server) == args->ssid) {
        args->out = server;
        return ANJAY_FOREACH_BREAK;
    }
    return ANJAY_FOREACH_CONTINUE;
}

anjay_server_info_t *_anjay_servers_find_active(anjay_t *anjay,
                                                anjay_ssid_t ssid) {
    find_active_args_t arg = {
        .ssid = ssid,
        .out = NULL
    };
    if (_anjay_servers_foreach_active(anjay, find_active_clb, &arg)) {
        return NULL;
    }
    return arg.out;
}

bool _anjay_server_registration_expired(anjay_server_info_t *server) {
    const anjay_registration_info_t *registration_info =
            _anjay_server_registration_info(server);
    assert(registration_info);
    if (!_anjay_conn_session_tokens_equal(_anjay_server_primary_session_token(
                                                  server),
                                          registration_info->session_token)) {
        anjay_log(DEBUG,
                  "Registration session changed for SSID = %u, "
                  "forcing re-register",
                  _anjay_server_ssid(server));
        return true;
    }
    avs_time_duration_t remaining =
            _anjay_register_time_remaining(registration_info);
    // avs_time_duration_less() returns false when either argument is INVALID;
    // the direction of this comparison is chosen so that it causes the
    // registration to be considered expired
    if (!avs_time_duration_less(AVS_TIME_DURATION_ZERO, remaining)) {
        anjay_log(DEBUG,
                  "Registration Lifetime expired for SSID = %u, "
                  "forcing re-register",
                  _anjay_server_ssid(server));
        return true;
    }
    return false;
}

int _anjay_schedule_socket_update(anjay_t *anjay, anjay_iid_t security_iid) {
    anjay_ssid_t ssid;
    anjay_server_info_t *server;
    if (!_anjay_ssid_from_security_iid(anjay, security_iid, &ssid)
            && (server = _anjay_servers_find_active(anjay, ssid))) {
        // mark the registration as expired; prevents superfluous Deregister
        _anjay_server_update_registration_info(server, NULL,
                                               &(anjay_update_parameters_t) {
                                                   .lifetime_s = -1
                                               });
        return anjay_disable_server_with_timeout(anjay, ssid,
                                                 AVS_TIME_DURATION_ZERO);
    }
    return 0;
}

AVS_LIST(avs_net_abstract_socket_t *const) anjay_get_sockets(anjay_t *anjay) {
    // We rely on the fact that the "socket" field is first in
    // anjay_socket_entry_t, which means that both "entry" and "&entry->socket"
    // point to exactly the same memory location. The "next" pointer location in
    // AVS_LIST is independent from the stored data type, so it's safe to do
    // such "cast".
    AVS_STATIC_ASSERT(offsetof(anjay_socket_entry_t, socket) == 0,
                      entry_socket_is_first_field);
    return &anjay_get_socket_entries(anjay)->socket;
}

static const char CONN_TYPE_LETTERS[] = {
    [ANJAY_CONNECTION_UDP] = 'U'
};

anjay_server_connection_mode_t
_anjay_get_connection_mode(const char *binding_mode,
                           anjay_connection_type_t conn_type) {
    const char *type_letter_ptr =
            strchr(binding_mode, CONN_TYPE_LETTERS[conn_type]);
    if (!type_letter_ptr) {
        return ANJAY_CONNECTION_DISABLED;
    }
    if (type_letter_ptr[1] == 'Q') {
        return ANJAY_CONNECTION_QUEUE;
    } else {
        return ANJAY_CONNECTION_ONLINE;
    }
}

void _anjay_server_actual_binding_mode(anjay_binding_mode_t *out_binding_mode,
                                       anjay_server_info_t *server) {
    AVS_STATIC_ASSERT(sizeof(*out_binding_mode)
                              > (sizeof("xQ") - 1) * ANJAY_CONNECTION_LIMIT_,
                      anjay_binding_mode_t_size);

    char *ptr = *out_binding_mode;
    anjay_connection_ref_t ref = {
        .server = server
    };
    ANJAY_CONNECTION_TYPE_FOREACH(ref.conn_type) {
        anjay_server_connection_mode_t mode =
                _anjay_connection_current_mode(ref);
        if (mode != ANJAY_CONNECTION_DISABLED) {
            *ptr++ = CONN_TYPE_LETTERS[ref.conn_type];
            if (mode == ANJAY_CONNECTION_QUEUE) {
                *ptr++ = 'Q';
            }
        }
    }
    *ptr = '\0';
}
