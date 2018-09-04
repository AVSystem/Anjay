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

#include <anjay_config.h>

#include <inttypes.h>

#include <avsystem/commons/stream/stream_net.h>
#include <avsystem/commons/utils.h>

#include "../servers_utils.h"
#include "../utils_core.h"
#include "../dm/query.h"

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "connection_info.h"
#include "connections_internal.h"
#include "reload.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

avs_net_abstract_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection) {
    return connection->conn_socket_;
}

void
_anjay_connection_internal_clean_socket(anjay_server_connection_t *connection) {
    avs_net_socket_cleanup(&connection->conn_socket_);
}

static int read_binding_mode(anjay_t *anjay,
                             anjay_ssid_t ssid,
                             anjay_binding_mode_t *out_binding_mode) {
    anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, ANJAY_IID_INVALID,
                               ANJAY_DM_RID_SERVER_BINDING);

    if (_anjay_find_server_iid(anjay, ssid, &path.iid)
            || _anjay_dm_res_read_string(anjay, &path, *out_binding_mode,
                                         sizeof(*out_binding_mode))) {
        anjay_log(WARNING, "could not read binding mode for LwM2M server %u",
                  ssid);
        return -1;
    }
    if (!anjay_binding_mode_valid(*out_binding_mode)) {
        anjay_log(WARNING, "invalid binding mode \"%s\" for LwM2M server %u",
                  *out_binding_mode, ssid);
        return -1;
    }
    return 0;
}

anjay_server_connection_mode_t
_anjay_connection_current_mode(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    if (connection && _anjay_connection_internal_get_socket(connection)) {
        return connection->mode;
    } else {
        return ANJAY_CONNECTION_DISABLED;
    }
}

bool _anjay_connection_is_online(anjay_server_connection_t *connection) {
    avs_net_abstract_socket_t *socket =
            _anjay_connection_internal_get_socket(connection);
    if (!socket) {
        return false;
    }
    avs_net_socket_opt_value_t opt;
    if (avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_STATE, &opt)) {
        anjay_log(ERROR, "Could not get socket state");
        return false;
    }
    return opt.state == AVS_NET_SOCKET_STATE_CONNECTED;
}

static int recreate_socket(anjay_t *anjay,
                           const anjay_connection_type_definition_t *def,
                           anjay_server_connection_t *connection,
                           anjay_connection_info_t *inout_info,
                           bool *out_session_resumed) {
    anjay_server_dtls_keys_t dtls_keys;
    memset(&dtls_keys, 0, sizeof(dtls_keys));

    // At this point, inout_info has "global" settings filled,
    // but transport-specific (i.e. UDP or SMS) fields are not
    if (def->get_connection_info(anjay, inout_info, &dtls_keys)) {
        anjay_log(DEBUG, "could not get %s connection info for server /%u/%u",
                  def->name, ANJAY_DM_OID_SECURITY, inout_info->security_iid);
        return -1;
    }
    _anjay_connection_internal_clean_socket(connection);

    // Socket configuration is slightly different between UDP and SMS
    // connections. That's why we do the common configuration here...
    avs_net_ssl_configuration_t socket_config;
    memset(&socket_config, 0, sizeof(socket_config));
    socket_config.version = anjay->dtls_version;
    socket_config.session_resumption_buffer =
            connection->nontransient_state.dtls_session_buffer;
    socket_config.session_resumption_buffer_size =
            sizeof(connection->nontransient_state.dtls_session_buffer);
    int result = def->get_net_security_info(&socket_config.security,
                                            inout_info, &dtls_keys);
    // ...and pass it as an in/out argument to create_connected_socket() so that
    // it can do any protocol-specific modifications.
    if (!result) {
        result = def->create_connected_socket(anjay, connection, &socket_config,
                                              inout_info);
    }
    if (!result) {
        avs_net_socket_opt_value_t session_resumed;
        if (avs_net_socket_get_opt(connection->conn_socket_,
                                   AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                   &session_resumed)) {
            *out_session_resumed = false;
        } else {
            *out_session_resumed = session_resumed.flag;
        }
    } else {
        avs_net_abstract_socket_t *sock = connection->conn_socket_;
        if (sock) {
            avs_net_socket_close(sock);
        }
    }
    return result;
}

typedef enum {
    RESULT_ERROR,
    RESULT_DISABLED,
    RESULT_RESUMED,
    RESULT_NEW_CONNECTION
} refresh_connection_result_t;

static refresh_connection_result_t
ensure_socket_connected(anjay_t *anjay,
                        const anjay_connection_type_definition_t *def,
                        anjay_server_connection_t *connection,
                        anjay_connection_info_t *inout_info,
                        int *out_socket_errno) {
    bool session_resumed = false;
    avs_net_abstract_socket_t *existing_socket =
            _anjay_connection_internal_get_socket(connection);

    *out_socket_errno = 0;

    if (existing_socket == NULL) {
        int result = recreate_socket(
                anjay, def, connection, inout_info, &session_resumed);
        if (result) {
            *out_socket_errno = -result;
            return RESULT_ERROR;
        }
    } else {
        if (_anjay_connection_is_online(connection)) {
            session_resumed = true;
        } else {
            int result = _anjay_connection_internal_bring_online(
                    anjay, connection, &session_resumed);
            if (result) {
                *out_socket_errno = -result;
                return RESULT_ERROR;
            }
        }
    }
    return session_resumed ? RESULT_RESUMED : RESULT_NEW_CONNECTION;
}

int _anjay_connection_init_psk_security(avs_net_security_info_t *security,
                                        const anjay_server_dtls_keys_t *keys) {
    *security = avs_net_security_info_from_psk(
        (avs_net_psk_info_t){
            .psk = keys->secret_key,
            .psk_size = keys->secret_key_size,
            .identity = keys->pk_or_identity,
            .identity_size = keys->pk_or_identity_size
        });
    return 0;
}

avs_net_af_t _anjay_socket_af_from_preferred_endpoint(
        const avs_net_resolved_endpoint_t *endpoint) {
    /*
     * Whenever the socket is bound by connect(), the address family is set to
     * match the remote address. If the socket is bound by a bind() call with
     * NULL local_addr argument, the address family falls back to the original
     * socket preference - by default, AF_UNSPEC. This causes avs_net to attempt
     * to bind to [::]:$PORT, even though the remote host may be an IPv4
     * address. This generally works, because IPv4-mapped IPv6 addresses are a
     * thing.
     *
     * On FreeBSD though, IPv4-mapped IPv6 are disabled by default (see:
     * "Interaction between IPv4/v6 sockets" at
     * https://www.freebsd.org/cgi/man.cgi?query=inet6&sektion=4), which
     * effectively breaks all connect() calls after re-binding to a recently
     * used port.
     *
     * To avoid that, we need to provide a local wildcard address appropriate
     * for the family used by the remote host. This function determines which
     * address family to use; it is then converted into a local address by
     * bind_socket() in utils_core.c.
     */
    char remote_preferred_host[
            sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
    if (!avs_net_resolved_endpoint_get_host(
            endpoint, remote_preferred_host, sizeof(remote_preferred_host))) {
        if (strchr(remote_preferred_host, ':') != NULL) {
            return AVS_NET_AF_INET6;
        } else if (strchr(remote_preferred_host, '.') != NULL) {
            return AVS_NET_AF_INET4;
        }
    }
    return AVS_NET_AF_UNSPEC;
}

static const anjay_connection_type_definition_t *
get_connection_type_def(anjay_connection_type_t type) {
    switch (type) {
    case ANJAY_CONNECTION_UDP:
        return &ANJAY_CONNECTION_DEF_UDP;
    default:
        AVS_UNREACHABLE("unknown connection type");
        return NULL;
    }
}

static refresh_connection_result_t
refresh_connection(anjay_t *anjay,
                   anjay_connection_ref_t ref,
                   anjay_connection_info_t *inout_info,
                   int *out_socket_errno) {
    anjay_server_connection_t *out_connection =
            _anjay_get_server_connection(ref);
    assert(out_connection);
    const anjay_connection_type_definition_t *def =
            get_connection_type_def(ref.conn_type);
    assert(def);
    refresh_connection_result_t result = RESULT_DISABLED;

    *out_socket_errno = 0;

    out_connection->mode = _anjay_get_connection_mode(inout_info->binding_mode,
                                                      ref.conn_type);
    if (out_connection->mode == ANJAY_CONNECTION_DISABLED) {
        _anjay_connection_internal_clean_socket(out_connection);
    } else {
        result = ensure_socket_connected(anjay, def, out_connection, inout_info,
                                         out_socket_errno);
    }
    return result;
}

static int get_common_connection_info(anjay_t *anjay,
                                      anjay_server_info_t *server,
                                      anjay_connection_info_t *out_info) {
    if (_anjay_find_security_iid(anjay, server->ssid,
                                 &out_info->security_iid)) {
        anjay_log(ERROR, "could not find server Security IID");
        return -1;
    }

    out_info->uri = &server->data_active.uri;

    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        int result = avs_simple_snprintf(
                out_info->binding_mode, sizeof(out_info->binding_mode),
                "%s", _anjay_sms_router(anjay) ? "US" : "U");
        return AVS_MIN(result, 0);
    } else if (read_binding_mode(anjay, server->ssid,
                                 &out_info->binding_mode)) {
        return -1;
    }
    return 0;
}

static bool is_connected(refresh_connection_result_t result) {
    return result == RESULT_RESUMED || result == RESULT_NEW_CONNECTION;
}

int _anjay_active_server_refresh(anjay_t *anjay, anjay_server_info_t *server) {
    anjay_log(TRACE, "refreshing SSID %u", server->ssid);

    anjay_connection_info_t server_info;
    memset(&server_info, 0, sizeof(server_info));
    if (get_common_connection_info(anjay, server, &server_info)) {
        anjay_log(DEBUG, "could not get connection info for SSID %u",
                  server->ssid);
        return -1;
    }

    refresh_connection_result_t udp_result = RESULT_DISABLED;
    int udp_errno = 0;
    refresh_connection_result_t sms_result = RESULT_DISABLED;
    int sms_errno = 0;
    udp_result = refresh_connection(anjay,
                                    (anjay_connection_ref_t) {
                                        .server = server,
                                        .conn_type = ANJAY_CONNECTION_UDP
                                    }, &server_info, &udp_errno);
    (void) sms_errno;

    if (!is_connected(udp_result) && !is_connected(sms_result)) {
        return udp_errno ? udp_errno : -1;
    }


    if ((server->data_active.primary_conn_type == ANJAY_CONNECTION_UDP
                    && udp_result == RESULT_NEW_CONNECTION)
            || (server->data_active.primary_conn_type == ANJAY_CONNECTION_SMS
                    && sms_result == RESULT_NEW_CONNECTION)) {
        // mark that the primary connection is no longer valid;
        // forces re-register
        server->data_active.primary_conn_type = ANJAY_CONNECTION_UNSET;
    }

    return udp_errno;
}

int _anjay_server_setup_primary_connection(anjay_server_info_t *server) {
    assert(_anjay_server_active(server));
    server->data_active.primary_conn_type = ANJAY_CONNECTION_UNSET;
    anjay_connection_ref_t ref = {
        .server = server
    };
    for (ref.conn_type = ANJAY_CONNECTION_FIRST_VALID_;
            ref.conn_type < ANJAY_CONNECTION_LIMIT_;
            ref.conn_type = (anjay_connection_type_t) (ref.conn_type + 1)) {
        if (_anjay_connection_get_online_socket(ref)) {
            server->data_active.primary_conn_type = ref.conn_type;
            return 0;
        }
    }

    anjay_log(ERROR, "No suitable connection found for SSID = %u",
              server->ssid);
    return -1;
}

static void connection_suspend(anjay_connection_ref_t conn_ref) {
    const anjay_server_connection_t *connection =
            _anjay_get_server_connection(conn_ref);
    if (connection) {
        avs_net_abstract_socket_t *socket =
                _anjay_connection_internal_get_socket(connection);
        if (socket) {
            avs_net_socket_close(socket);
        }
    }
}

void _anjay_connection_suspend(anjay_connection_ref_t conn_ref) {
    if (conn_ref.conn_type == ANJAY_CONNECTION_UNSET) {
        for (conn_ref.conn_type = ANJAY_CONNECTION_FIRST_VALID_;
                conn_ref.conn_type < ANJAY_CONNECTION_LIMIT_;
                conn_ref.conn_type =
                        (anjay_connection_type_t) (conn_ref.conn_type + 1)) {
            connection_suspend(conn_ref);
        }
    } else {
        connection_suspend(conn_ref);
    }
}

int
_anjay_connection_internal_bring_online(anjay_t *anjay,
                                        anjay_server_connection_t *connection,
                                        bool *out_session_resumed) {
    assert(connection);
    assert(connection->conn_socket_);
    assert(!_anjay_connection_is_online(connection));

    avs_net_socket_opt_value_t session_resumed;
    char remote_hostname[ANJAY_MAX_URL_HOSTNAME_SIZE];
    char remote_port[ANJAY_MAX_URL_PORT_SIZE];
    if (avs_net_socket_get_remote_hostname(connection->conn_socket_,
                                           remote_hostname,
                                           sizeof(remote_hostname))
            || avs_net_socket_get_remote_port(connection->conn_socket_,
                                              remote_port,
                                              sizeof(remote_port))) {
        anjay_log(ERROR, "Could not get peer address and port "
                         "of a suspended connection");
        return -1;
    }

    anjay_socket_bind_config_t bind_config = {
        .family = _anjay_socket_af_from_preferred_endpoint(
                &connection->nontransient_state.preferred_endpoint),
        .last_local_port_buffer =
                &connection->nontransient_state.last_local_port,
        .static_port_preference = anjay->udp_listen_port
    };

    if (_anjay_bind_and_connect_socket(connection->conn_socket_, &bind_config,
                                       remote_hostname, remote_port)) {
        goto close_and_fail;
    }

    session_resumed.flag = true;
    if (avs_net_socket_get_opt(connection->conn_socket_,
                               AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                               &session_resumed)) {
        // if avs_net_socket_get_opt() failed, it means that it's not a DTLS
        // socket; if remote_port is empty, it means that it's an SMS socket;
        // we treat a non-DTLS SMS socket as always "resumed",
        // because MSISDN will not change during the library lifetime
        *out_session_resumed = !*remote_port;
    } else {
        *out_session_resumed = session_resumed.flag;
    }
    anjay_log(INFO, "%s to %s:%s",
              *out_session_resumed ? "resumed connection" : "reconnected",
              remote_hostname, remote_port);
    return 0;

    // Labels must be followed by a statement, not a declaration.
    // Fortunately, `;` is a perfectly fine statement in C.
close_and_fail:;
    int result = avs_net_socket_errno(connection->conn_socket_);
    if (avs_net_socket_close(connection->conn_socket_)) {
        anjay_log(ERROR, "Could not close the socket (?!)");
    }
    return result;
}

int _anjay_connection_bring_online(anjay_t *anjay,
                                   anjay_connection_ref_t ref,
                                   bool *out_session_resumed) {
    return _anjay_connection_internal_bring_online(
            anjay, _anjay_get_server_connection(ref), out_session_resumed);
}

int _anjay_get_security_info(anjay_t *anjay,
                             avs_net_security_info_t *out_net_info,
                             anjay_server_dtls_keys_t *out_dtls_keys,
                             anjay_iid_t security_iid,
                             anjay_connection_type_t conn_type) {
    const anjay_connection_type_definition_t *conn_def =
            get_connection_type_def(conn_type);
    anjay_connection_info_t info = {
        .security_iid = security_iid
    };

    memset(out_dtls_keys, 0, sizeof(*out_dtls_keys));
    int result;
    (void) ((result = conn_def->get_connection_info(anjay, &info,
                                                    out_dtls_keys))
            || (result = conn_def->get_net_security_info(out_net_info, &info,
                                                         out_dtls_keys)));
    return result;
}

static void queue_mode_close_socket(anjay_t *anjay, const void *ref_ptr) {
    (void) anjay;
    _anjay_connection_suspend(*(const anjay_connection_ref_t *) ref_ptr);
}

void
_anjay_connection_schedule_queue_mode_close(anjay_t *anjay,
                                            anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    _anjay_sched_del(anjay->sched,
                     &connection->queue_mode_close_socket_clb_handle);
    if (connection->mode != ANJAY_CONNECTION_QUEUE) {
        return;
    }

    avs_time_duration_t delay = avs_coap_max_transmit_wait(
            _anjay_tx_params_for_conn_type(anjay, ref.conn_type));

    // see comment on field declaration for logic summary
    if (_anjay_sched(anjay->sched,
                     &connection->queue_mode_close_socket_clb_handle, delay,
                     queue_mode_close_socket, &ref, sizeof(ref))) {
        anjay_log(ERROR, "could not schedule queue mode operations");
    }
}
