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

#include <config.h>

#include <errno.h>
#include <inttypes.h>

#include <anjay_modules/time_defs.h>

#define ANJAY_SERVERS_INTERNALS

#include "../dm/query.h"
#include "../anjay_core.h"
#include "../servers.h"
#include "../interface/register.h"

#include "activate.h"
#include "connection_info.h"
#include "register_internal.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static void connection_cleanup(const anjay_t *anjay,
                               anjay_server_connection_t *connection) {
    _anjay_connection_internal_clean_socket(connection);
    _anjay_sched_del(anjay->sched,
                     &connection->queue_mode_close_socket_clb_handle);
}

void _anjay_server_cleanup(const anjay_t *anjay, anjay_server_info_t *server) {
    anjay_log(TRACE, "clear_server SSID %u", server->ssid);

    _anjay_sched_del(anjay->sched, &server->sched_update_or_reactivate_handle);
    _anjay_registration_info_cleanup(&server->data_active.registration_info);
    connection_cleanup(anjay, &server->data_active.udp_connection);
    _anjay_url_cleanup(&server->data_active.uri);
}

anjay_servers_t *_anjay_servers_create(void) {
    return (anjay_servers_t *) calloc(1, sizeof(anjay_servers_t));
}

void _anjay_servers_internal_deregister(anjay_t *anjay,
                                        anjay_servers_t *servers) {
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, servers->servers) {
        if (_anjay_server_active(server)
                && server->ssid != ANJAY_SSID_BOOTSTRAP) {
            _anjay_server_deregister(anjay, server);
        }
    }
}

void _anjay_servers_internal_cleanup(anjay_t *anjay,
                                     anjay_servers_t *servers) {
    anjay_log(TRACE, "cleaning up %lu servers",
              (unsigned long) AVS_LIST_SIZE(servers->servers));

    AVS_LIST_CLEAR(&servers->servers) {
        _anjay_server_cleanup(anjay, servers->servers);
    }
    AVS_LIST_CLEAR(&servers->public_sockets);
}

void _anjay_servers_deregister(anjay_t *anjay) {
    if (anjay->servers) {
        _anjay_servers_internal_deregister(anjay, anjay->servers);
    }
}

void _anjay_servers_cleanup(anjay_t *anjay) {
    if (anjay->servers) {
        _anjay_servers_internal_cleanup(anjay, anjay->servers);
        free(anjay->servers);
        anjay->servers = NULL;
    }
}

void _anjay_servers_cleanup_inactive(anjay_t *anjay) {
    AVS_LIST(anjay_server_info_t) *server_ptr;
    AVS_LIST(anjay_server_info_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(server_ptr, helper,
                                   &anjay->servers->servers) {
        if (!_anjay_server_active(*server_ptr)) {
            _anjay_server_cleanup(anjay, *server_ptr);
            AVS_LIST_DELETE(server_ptr);
        }
    }
}

avs_net_abstract_socket_t *
_anjay_connection_get_online_socket(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (!connection || !_anjay_connection_is_online(connection)) {
        return NULL;
    }
    return _anjay_connection_internal_get_socket(connection);
}

static avs_net_abstract_socket_t *
get_online_connection_socket(anjay_server_info_t *server,
                             anjay_connection_type_t conn_type) {
    return _anjay_connection_get_online_socket((anjay_connection_ref_t) {
                                                   .server = server,
                                                   .conn_type = conn_type
                                               });
}

static int
add_socket_onto_list(AVS_LIST(avs_net_abstract_socket_t *const) *tail_ptr,
                     avs_net_abstract_socket_t *socket) {
    AVS_LIST_INSERT_NEW(avs_net_abstract_socket_t *const, tail_ptr);
    if (!*tail_ptr) {
        anjay_log(ERROR, "Out of memory while building socket list");
        return -1;
    }
    *(avs_net_abstract_socket_t **) (intptr_t) *tail_ptr = socket;
    return 0;
}

AVS_LIST(avs_net_abstract_socket_t *const) anjay_get_sockets(anjay_t *anjay) {
    AVS_LIST_CLEAR(&anjay->servers->public_sockets);
    AVS_LIST(avs_net_abstract_socket_t *const) *tail_ptr =
            &anjay->servers->public_sockets;

    bool sms_active = false;
    anjay_server_info_t *server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        if (!_anjay_server_active(server)) {
            continue;
        }
        avs_net_abstract_socket_t *udp_socket =
                get_online_connection_socket(server, ANJAY_CONNECTION_UDP);
        if (udp_socket && !add_socket_onto_list(tail_ptr, udp_socket)) {
            AVS_LIST_ADVANCE_PTR(&tail_ptr);
        }

        if (get_online_connection_socket(server, ANJAY_CONNECTION_SMS)) {
            sms_active = true;
        }
    }

    if (sms_active) {
        assert(_anjay_sms_router(anjay));
        add_socket_onto_list(tail_ptr, _anjay_sms_poll_socket(anjay));
    }

#ifdef WITH_DOWNLOADER
    _anjay_downloader_get_sockets(&anjay->downloader, tail_ptr);
#endif // WITH_DOWNLOADER
    return anjay->servers->public_sockets;
}

anjay_server_info_t *
_anjay_servers_find_by_udp_socket(anjay_servers_t *servers,
                                  avs_net_abstract_socket_t *socket) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, servers->servers) {
        if (_anjay_connection_internal_get_socket(
                &it->data_active.udp_connection) == socket) {
            return it;
        }
    }

    return NULL;
}

static void deactivate_server_job(anjay_t *anjay, void *ssid_) {
    _anjay_server_deactivate(anjay, (anjay_ssid_t) (intptr_t) ssid_,
                             AVS_TIME_DURATION_ZERO);
}

int _anjay_schedule_socket_update(anjay_t *anjay,
                                  anjay_iid_t security_iid) {
    anjay_ssid_t ssid;
    anjay_server_info_t *server;
    if (!_anjay_ssid_from_security_iid(anjay, security_iid, &ssid)
            && (server = _anjay_servers_find_active(anjay->servers, ssid))) {
        // mark that the registration connection is no longer valid;
        // prevents superfluous Deregister
        server->data_active.registration_info.conn_type
                = ANJAY_CONNECTION_UNSET;
        return _anjay_sched_now(anjay->sched, NULL, deactivate_server_job,
                                (void *) (uintptr_t) ssid);
    }
    return 0;
}

#ifdef WITH_BOOTSTRAP
bool _anjay_servers_is_connected_to_non_bootstrap(anjay_servers_t *servers) {
    AVS_LIST(anjay_server_info_t) server;
    AVS_LIST_FOREACH(server, servers->servers) {
        if (_anjay_server_active(server)
                && server->ssid != ANJAY_SSID_BOOTSTRAP) {
            return true;
        }
    }
    return false;
}
#endif

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_insert_ptr(anjay_servers_t *servers, anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *it;
    AVS_LIST_FOREACH_PTR(it, &servers->servers) {
        if ((*it)->ssid >= ssid) {
            return it;
        }
    }
    return it;
}

AVS_LIST(anjay_server_info_t) *
_anjay_servers_find_ptr(anjay_servers_t *servers, anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *ptr =
            _anjay_servers_find_insert_ptr(servers, ssid);
    if (*ptr && (*ptr)->ssid == ssid) {
        return ptr;
    }

    anjay_log(TRACE, "no server with SSID %u", ssid);
    return NULL;
}

anjay_server_info_t *_anjay_servers_find_active(anjay_servers_t *servers,
                                                anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) *ptr =
            _anjay_servers_find_ptr(servers, ssid);
    assert((!ptr || *ptr) && "_anjay_servers_find_ptr broken");
    return (ptr && *ptr && _anjay_server_active(*ptr)) ? *ptr : NULL;
}

static bool is_valid_coap_uri(const anjay_url_t *uri) {
    if (strcmp(uri->protocol, "coap") && strcmp(uri->protocol, "coaps")) {
        anjay_log(ERROR, "unsupported protocol: %s", uri->protocol);
        return false;
    }
    return true;
}

int _anjay_server_get_uri(anjay_t *anjay,
                          anjay_iid_t security_iid,
                          anjay_url_t *out_uri) {
    char raw_uri[ANJAY_MAX_URL_RAW_LENGTH];

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SERVER_URI);

    if (_anjay_dm_res_read_string(anjay, &path, raw_uri, sizeof(raw_uri))) {
        anjay_log(ERROR, "could not read LwM2M server URI");
        return -1;
    }

    anjay_url_t uri = ANJAY_URL_EMPTY;
    if (_anjay_parse_url(raw_uri, &uri) || !is_valid_coap_uri(&uri)) {
        _anjay_url_cleanup(&uri);
        anjay_log(ERROR, "could not parse LwM2M server URI: %s", raw_uri);
        return -1;
    }
    if (!*uri.port) {
        if (uri.protocol[4]) { // coap_s_
            strcpy(uri.port, "5684");
        } else { // coap_\0_
            strcpy(uri.port, "5683");
        }
    }
    *out_uri = uri;
    return 0;
}

static void disable_server_job(anjay_t *anjay, void *ssid_) {
    anjay_ssid_t ssid = (anjay_ssid_t)(intptr_t)ssid_;

    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(anjay, ssid, &server_iid)) {
        anjay_log(DEBUG, "no Server Object Instance with SSID = %u, disabling "
                  "skipped", ssid);
    } else {
        const avs_time_duration_t disable_timeout =
                _anjay_disable_timeout_from_server_iid(anjay, server_iid);
        _anjay_server_deactivate(anjay, ssid, disable_timeout);
    }
}

int anjay_disable_server(anjay_t *anjay,
                         anjay_ssid_t ssid) {
    if (_anjay_sched_now(anjay->sched, NULL, disable_server_job,
                         (void *) (uintptr_t) ssid)) {
        anjay_log(ERROR, "could not schedule disable_server_job");
        return -1;
    }
    return 0;
}

typedef struct {
    anjay_ssid_t ssid;
    avs_time_duration_t timeout;
} disable_server_data_t;

static void disable_server_with_timeout_job(anjay_t *anjay, void *data_) {
    disable_server_data_t *data = (disable_server_data_t *) data_;
    if (_anjay_server_deactivate(anjay, data->ssid, data->timeout)) {
        anjay_log(ERROR, "unable to deactivate server: %" PRIu16, data->ssid);
    } else {
        if (avs_time_duration_valid(data->timeout)) {
            anjay_log(INFO, "server %" PRIu16 " disabled for %" PRId64
                      ".%09" PRId32 " seconds", data->ssid,
                      data->timeout.seconds, data->timeout.nanoseconds);
        } else {
            anjay_log(INFO, "server %" PRIu16 " disabled", data->ssid);
        }
    }
    free(data);
}

int anjay_disable_server_with_timeout(anjay_t *anjay,
                                      anjay_ssid_t ssid,
                                      avs_time_duration_t timeout) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, "invalid SSID: %u", ssid);
        return -1;
    }

    disable_server_data_t *data = (disable_server_data_t *)
            malloc(sizeof(*data));

    if (!data) {
        anjay_log(ERROR, "out of memory");
        return -1;
    }

    data->ssid = ssid;
    data->timeout = timeout;

    if (_anjay_sched_now(anjay->sched, NULL,
                         disable_server_with_timeout_job, data)) {
        free(data);
        anjay_log(ERROR, "could not schedule disable_server_with_timeout_job");
        return -1;
    }

    return 0;
}

int anjay_enable_server(anjay_t *anjay,
                        anjay_ssid_t ssid) {
    if (ssid == ANJAY_SSID_ANY) {
        anjay_log(WARNING, "invalid SSID: %u", ssid);
        return -1;
    }

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);

    if (!server_ptr || !*server_ptr || _anjay_server_active(*server_ptr)) {
        anjay_log(TRACE, "not an inactive server: SSID = %u", ssid);
        return -1;
    }

    return _anjay_server_sched_activate(anjay, *server_ptr,
                                        AVS_TIME_DURATION_ZERO);
}

bool _anjay_server_active(anjay_server_info_t *server) {
    anjay_connection_ref_t ref = {
        .server = server
    };
    for (ref.conn_type = ANJAY_CONNECTION_FIRST_VALID_;
            ref.conn_type < ANJAY_CONNECTION_LIMIT_;
            ref.conn_type = (anjay_connection_type_t) (ref.conn_type + 1)) {
        anjay_server_connection_t *connection =
                _anjay_get_server_connection(ref);
        if (connection && _anjay_connection_internal_get_socket(connection)) {
            return true;
        }
    }

    return false;
}

anjay_server_connection_t *
_anjay_get_server_connection(anjay_connection_ref_t ref) {
    switch (ref.conn_type) {
    case ANJAY_CONNECTION_UDP:
        return &ref.server->data_active.udp_connection;
    default:
        return NULL;
    }
}

anjay_ssid_t _anjay_server_ssid(anjay_server_info_t *server) {
    return server->ssid;
}

anjay_connection_type_t
_anjay_server_registration_conn_type(anjay_server_info_t *server) {
    return server->data_active.registration_info.conn_type;
}

void _anjay_server_require_reload(anjay_server_info_t *server) {
    server->data_active.needs_reload = true;
}

const anjay_url_t *_anjay_server_uri(anjay_server_info_t *server) {
    return &server->data_active.uri;
}

size_t _anjay_servers_count_non_bootstrap(anjay_t *anjay) {
    size_t num_servers = 0;
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_ssid(it) != ANJAY_SSID_BOOTSTRAP) {
            ++num_servers;
        }
    }
    return num_servers;
}

int _anjay_servers_foreach_ssid(anjay_t *anjay,
                                anjay_servers_foreach_ssid_handler_t *handler,
                                void *data) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        int result = handler(anjay, it->ssid, data);
        if (result == ANJAY_FOREACH_BREAK) {
            anjay_log(DEBUG, "servers_foreach_ssid: break on %u", it->ssid);
            return 0;
        } else if (result) {
            anjay_log(ERROR, "servers_foreach_ssid handler failed for %u (%d)",
                      it->ssid, result);
            return result;
        }
    }

    return 0;
}

int _anjay_servers_foreach_active(anjay_t *anjay,
                                  anjay_servers_foreach_handler_t *handler,
                                  void *data) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (!_anjay_server_active(it)) {
            continue;
        }
        int result = handler(anjay, it, data);
        if (result == ANJAY_FOREACH_BREAK) {
            anjay_log(DEBUG, "servers_foreach_ssid: break on %u", it->ssid);
            return 0;
        } else if (result) {
            anjay_log(ERROR, "servers_foreach_ssid handler failed for %u (%d)",
                      it->ssid, result);
            return result;
        }
    }

    return 0;
}
