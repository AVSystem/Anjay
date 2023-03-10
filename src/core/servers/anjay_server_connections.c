/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <inttypes.h>

#include <avsystem/commons/avs_stream_net.h>
#include <avsystem/commons/avs_utils.h>

#define ANJAY_SERVERS_INTERNALS

#include "../anjay_servers_reload.h"
#include "../anjay_servers_utils.h"
#include "../anjay_utils_private.h"
#include "../dm/anjay_query.h"

#include "anjay_activate.h"
#include "anjay_security.h"
#include "anjay_server_connections.h"
#include "anjay_servers_internal.h"

VISIBILITY_SOURCE_BEGIN

static int read_binding_info(anjay_unlocked_t *anjay,
                             anjay_ssid_t ssid,
                             anjay_binding_mode_t *out_binding_mode,
                             char *out_preferred_transport) {
    anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, ANJAY_ID_INVALID,
                               ANJAY_DM_RID_SERVER_BINDING);
    if (_anjay_find_server_iid(anjay, ssid, &path.ids[ANJAY_ID_IID])) {
        anjay_log(WARNING,
                  _("could not find Server instance for LwM2M server ") "%u",
                  ssid);
        return -1;
    }
    if (_anjay_dm_read_resource_string(anjay, &path, out_binding_mode->data,
                                       sizeof(out_binding_mode->data))) {
        anjay_log(WARNING,
                  _("could not read binding mode for LwM2M server ") "%u",
                  ssid);
        return -1;
    }
    if (!anjay_binding_mode_valid(out_binding_mode->data)) {
        anjay_log(WARNING,
                  _("invalid binding mode \"") "%s" _(
                          "\" for LwM2M server ") "%u",
                  out_binding_mode->data, ssid);
        return -1;
    }
#ifdef ANJAY_WITH_LWM2M11
    char preferred_transport[2];
    if (!_anjay_dm_read_resource_string(
                anjay,
                &MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, path.ids[ANJAY_ID_IID],
                                    ANJAY_DM_RID_SERVER_PREFERRED_TRANSPORT),
                preferred_transport, sizeof(preferred_transport))
            && _anjay_binding_info_by_letter(preferred_transport[0]) != NULL) {
        *out_preferred_transport = preferred_transport[0];
    } else
#endif // ANJAY_WITH_LWM2M11
    {
        *out_preferred_transport = '\0';
    }
    return 0;
}

anjay_conn_session_token_t
_anjay_server_primary_session_token(anjay_server_info_t *server) {
    return _anjay_connections_get_primary_session_token(&server->connections);
}

typedef struct {
    const anjay_ssid_t ssid;
    anjay_binding_mode_t *binding_mode;
    const char preferred_transport;
    anjay_iid_t selected_iid;
    avs_url_t *selected_uri;
    size_t selected_rank;
} select_security_instance_state_t;

/**
 * NOTE: *out_rank is set to one of the following:
 *
 * - 0, if transport_info matches preferred_transport
 * - [1 .. sizeof(binding_mode->data)], if transport_info matches nth letter of
 *   binding_mode->data. 1 corresponds to binding_mode->data[0].
 * - sizeof(binding_mode->data) + 1 (one more than anything possible for the
 *   above), if transport_info is applicable for UDP, but binding_mode->data
 *   does not include 'U'. See below for explanation.
 *
 * Smaller rank number is considered better.
 *
 * Additionally, if a specific transport is not online at the moment, the rank
 * is increased by an additional penalty of sizeof(binding_mode->data) + 2, so
 * that all online protocols have better rank than offline ones. We can't
 * completely eliminate offline transports at this moment, because it is not
 * considered an error if a transport is offline.
 */
static int rank_uri(anjay_unlocked_t *anjay,
                    anjay_binding_mode_t *binding_mode,
                    char preferred_transport,
                    const anjay_transport_info_t *transport_info,
                    size_t *out_rank) {
    assert(transport_info);
    if (!_anjay_socket_transport_supported(anjay, transport_info->transport)) {
        anjay_log(WARNING, _("support for protocol ") "%s" _(" is not enabled"),
                  transport_info->uri_scheme);
        return -1;
    }
    const char *rank_ptr;
    char transport_binding =
            _anjay_binding_info_by_transport(transport_info->transport)->letter;
    if (transport_binding == preferred_transport) {
        *out_rank = 0;
    } else if ((rank_ptr = strchr(binding_mode->data,
                                  *(uint8_t *) &transport_binding))) {
        *out_rank = (size_t) (rank_ptr - binding_mode->data) + 1;
    } else if (transport_binding == 'U') {
        // According to LwM2M TS 1.1.1, 6.2.1.2. Behaviour with Current
        // Transport Binding and Modes:
        // > The client SHALL assume that the server supports the UDP binding
        // > even if the server does not include UDP ("U") in the "binding"
        // > resource of the LwM2M server object (/1/x/7).
        *out_rank = sizeof(binding_mode->data) + 1;
    } else {
        anjay_log(DEBUG,
                  _("protocol ") "%s" _(" is not present in Binding resource"),
                  transport_info->uri_scheme);
        return -1;
    }
    if (!_anjay_socket_transport_is_online(anjay, transport_info->transport)) {
        *out_rank += sizeof(binding_mode->data) + 2;
    }
    return 0;
}

static void update_selected_security_instance_if_ranked_better(
        anjay_unlocked_t *anjay,
        select_security_instance_state_t *state,
        anjay_iid_t iid,
        avs_url_t **move_uri,
        const anjay_transport_info_t *transport_info) {
    size_t rank;
    if (!rank_uri(anjay, state->binding_mode, state->preferred_transport,
                  transport_info, &rank)
            && (state->selected_iid == ANJAY_ID_INVALID
                || rank < state->selected_rank)) {
        // This is the first matching entry or it has better rank than the
        // previously selected one - let's store it.
        avs_url_t *tmp_uri = state->selected_uri;
        state->selected_uri = *move_uri;
        *move_uri = tmp_uri; // for cleanup below
        state->selected_iid = iid;
        state->selected_rank = rank;
    }

    avs_url_free(*move_uri);
    *move_uri = NULL;
}

static int select_security_instance_clb(anjay_unlocked_t *anjay,
                                        const anjay_dm_installed_object_t *obj,
                                        anjay_iid_t iid,
                                        void *state_) {
    (void) obj;
    select_security_instance_state_t *state =
            (select_security_instance_state_t *) state_;
    anjay_ssid_t ssid;
    avs_url_t *uri = NULL;
    const anjay_transport_info_t *transport_info = NULL;
    if (_anjay_ssid_from_security_iid(anjay, iid, &ssid)
            || ssid != state->ssid) {
        return ANJAY_FOREACH_CONTINUE;
    }

    if (!_anjay_connection_security_generic_get_uri(anjay, iid, &uri,
                                                    &transport_info)) {
        update_selected_security_instance_if_ranked_better(
                anjay, state, iid, &uri, transport_info);
    }
    assert(!uri);

    return ANJAY_FOREACH_CONTINUE;
}

static int select_security_instance(anjay_unlocked_t *anjay,
                                    anjay_ssid_t ssid,
                                    anjay_binding_mode_t *binding_mode,
                                    char preferred_transport,
                                    anjay_iid_t *out_security_iid,
                                    avs_url_t **out_uri) {
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    select_security_instance_state_t state = {
        .ssid = ssid,
        .binding_mode = binding_mode,
        .preferred_transport = preferred_transport,
        .selected_iid = ANJAY_ID_INVALID,
        .selected_uri = NULL,
        .selected_rank = SIZE_MAX
    };
    int result =
            _anjay_dm_foreach_instance(anjay, obj, select_security_instance_clb,
                                       &state);
    if (result) {
        avs_url_free(state.selected_uri);
        return result;
    }
    if (state.selected_iid == ANJAY_ID_INVALID) {
        assert(!state.selected_uri);
        anjay_log(
                WARNING,
                _("could not find Security Instance matching Server ") "%" PRIu16
                        _(" configuration"),
                ssid);
        return -1;
    }
    *out_security_iid = state.selected_iid;
    *out_uri = state.selected_uri;
    return 0;
}

#ifdef ANJAY_WITH_LWM2M11
static int read_server_sni(anjay_unlocked_t *anjay,
                           anjay_iid_t security_iid,
                           anjay_server_name_indication_t *out_sni) {
    out_sni->sni[0] = '\0';

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SNI);
    int result = _anjay_dm_read_resource_string(anjay, &path, out_sni->sni,
                                                sizeof(out_sni->sni));
    if (result == ANJAY_ERR_NOT_FOUND
            || result == ANJAY_ERR_METHOD_NOT_ALLOWED) {
        anjay_log(TRACE, _("no SNI for /0/") "%u" _(", using defaults"),
                  (unsigned) security_iid);
        return 0;
    }
    if (!result) {
        anjay_log(TRACE, _("using SNI ") "%s" _(" for /0/") "%u", out_sni->sni,
                  (unsigned) security_iid);
    }
    return result;
}
#endif // ANJAY_WITH_LWM2M11

void _anjay_active_server_refresh(anjay_server_info_t *server) {
    anjay_log(TRACE, _("refreshing SSID ") "%u", server->ssid);

    int result = 0;
    anjay_iid_t security_iid = ANJAY_ID_INVALID;
    avs_url_t *uri = NULL;
    anjay_server_name_indication_t sni = { "" };
    if (server->ssid == ANJAY_SSID_BOOTSTRAP) {
        const anjay_transport_info_t *transport_info = NULL;
        if ((security_iid = _anjay_find_bootstrap_security_iid(server->anjay))
                == ANJAY_ID_INVALID) {
            anjay_log(ERROR, _("could not find server Security IID"));
            result = -1;
        } else if (!_anjay_connection_security_generic_get_uri(
                           server->anjay, security_iid, &uri,
                           &transport_info)) {
            assert(uri);
            assert(transport_info);
            if ((result = avs_simple_snprintf(server->binding_mode.data,
                                              sizeof(server->binding_mode.data),
                                              "%c",
                                              _anjay_binding_info_by_transport(
                                                      transport_info->transport)
                                                      ->letter))
                    >= 0) {
#ifdef ANJAY_WITH_LWM2M11
                result = read_server_sni(server->anjay, security_iid, &sni);
#else  // ANJAY_WITH_LWM2M11
                result = 0;
#endif // ANJAY_WITH_LWM2M11
            }
        }
    } else {
        char preferred_transport;
        (void) ((result = read_binding_info(server->anjay, server->ssid,
                                            &server->binding_mode,
                                            &preferred_transport))
                || (result = select_security_instance(
                            server->anjay, server->ssid, &server->binding_mode,
                            preferred_transport, &security_iid, &uri))
#ifdef ANJAY_WITH_LWM2M11
                || (result = read_server_sni(server->anjay, security_iid, &sni))
#endif // ANJAY_WITH_LWM2M11
        );
    }
    if (!result) {
        _anjay_server_connections_refresh(server, security_iid, &uri, &sni);
    }
    avs_url_free(uri);
    if (result) {
        _anjay_server_on_refreshed(server, ANJAY_SERVER_CONNECTION_OFFLINE,
                                   avs_errno(AVS_EPROTO));
    }
}

bool _anjay_connection_outgoing_exchanges_in_progress(
        anjay_connection_ref_t conn_ref) {
    assert(conn_ref.server->ssid != ANJAY_SSID_BOOTSTRAP);
    if (conn_ref.conn_type == ANJAY_CONNECTION_PRIMARY
            && avs_coap_exchange_id_valid(
                       conn_ref.server->registration_exchange_state
                               .exchange_id)) {
        return true;
    }
    if (_anjay_observe_confirmable_in_delivery(conn_ref)) {
        return true;
    }
#ifdef ANJAY_WITH_DOWNLOADER
    if (_anjay_downloader_same_socket_transfer_ongoing(
                &conn_ref.server->anjay->downloader,
                _anjay_connection_internal_get_socket(
                        _anjay_get_server_connection(conn_ref)))) {
        return true;
    }
#endif // ANJAY_WITH_DOWNLOADER
#ifdef ANJAY_WITH_SEND
    if (_anjay_send_in_progress(conn_ref)) {
        return true;
    }
#endif // ANJAY_WITH_SEND
    return false;
}

static void cancel_exchanges(anjay_connection_ref_t conn_ref) {
    anjay_server_connection_t *conn = _anjay_get_server_connection(conn_ref);
#ifdef ANJAY_WITH_DOWNLOADER
    _anjay_downloader_suspend_same_socket(&conn_ref.server->anjay->downloader,
                                          _anjay_connection_internal_get_socket(
                                                  conn));
#endif // ANJAY_WITH_DOWNLOADER
    if (conn_ref.conn_type == ANJAY_CONNECTION_PRIMARY) {
#ifdef ANJAY_WITH_BOOTSTRAP
        if (conn_ref.server->ssid == ANJAY_SSID_BOOTSTRAP) {
            if (avs_coap_exchange_id_valid(
                        conn_ref.server->anjay->bootstrap
                                .outgoing_request_exchange_id)) {
                avs_coap_exchange_cancel(conn->coap_ctx,
                                         conn_ref.server->anjay->bootstrap
                                                 .outgoing_request_exchange_id);
            }
        } else
#endif // ANJAY_WITH_BOOTSTRAP
                if (avs_coap_exchange_id_valid(
                            conn_ref.server->registration_exchange_state
                                    .exchange_id)) {
            avs_coap_exchange_cancel(
                    conn->coap_ctx,
                    conn_ref.server->registration_exchange_state.exchange_id);
        }
    }
    _anjay_observe_interrupt(conn_ref);
#ifdef ANJAY_WITH_SEND
    _anjay_send_interrupt(conn_ref);
#endif // ANJAY_WITH_SEND
}

void _anjay_servers_interrupt_offline(anjay_unlocked_t *anjay) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers) {
        anjay_connection_type_t conn_type;
        ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
            anjay_connection_ref_t ref = {
                .server = it,
                .conn_type = conn_type
            };
            anjay_server_connection_t *conn = _anjay_get_server_connection(ref);
            avs_net_socket_t *socket =
                    _anjay_connection_internal_get_socket(conn);
            if (socket
                    && !_anjay_socket_transport_is_online(anjay,
                                                          conn->transport)) {
                cancel_exchanges(ref);
                _anjay_observe_interrupt(ref);
                if (conn_type == ANJAY_CONNECTION_PRIMARY) {
                    avs_sched_del(&it->next_action_handle);
#ifdef ANJAY_WITH_BOOTSTRAP
                    if (it->ssid == ANJAY_SSID_BOOTSTRAP) {
                        avs_sched_del(
                                &anjay->bootstrap
                                         .client_initiated_bootstrap_handle);
                    }
#endif // ANJAY_WITH_BOOTSTRAP
                }
            }
        }
    }
}

void _anjay_connection_suspend(anjay_connection_ref_t conn_ref) {
    anjay_server_connection_t *conn = _anjay_get_server_connection(conn_ref);
    avs_net_socket_t *socket = _anjay_connection_internal_get_socket(conn);
    cancel_exchanges(conn_ref);
    if (socket) {
        avs_net_socket_shutdown(socket);
        avs_net_socket_close(socket);
    }
}

anjay_socket_transport_t
_anjay_connection_transport(anjay_connection_ref_t conn_ref) {
    anjay_server_connection_t *connection =
            _anjay_get_server_connection(conn_ref);
    assert(connection);
    assert(_anjay_connection_internal_get_socket(connection));
    return connection->transport;
}

void _anjay_connection_mark_stable(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    assert(_anjay_connection_is_online(connection));
    connection->state = ANJAY_SERVER_CONNECTION_STABLE;
}

void _anjay_connection_bring_online(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    (void) connection;
    assert(connection);
    assert(!_anjay_connection_is_online(connection));
    assert(connection->transport != ANJAY_SOCKET_TRANSPORT_INVALID);
    assert(_anjay_socket_transport_supported(ref.server->anjay,
                                             connection->transport));
    if (!_anjay_socket_transport_is_online(ref.server->anjay,
                                           connection->transport)) {
        anjay_log(DEBUG, _("transport is entering offline mode, not bringing "
                           "the socket online"));
    } else {
        avs_error_t err =
                _anjay_server_connection_internal_bring_online(ref.server,
                                                               ref.conn_type);
        anjay_server_connection_state_t state =
                _anjay_connection_get(&ref.server->connections,
                                      ANJAY_CONNECTION_PRIMARY)
                        ->state;
        _anjay_server_on_refreshed(ref.server, state, err);
    }
}

static void queue_mode_close_socket(avs_sched_t *sched, const void *ref_ptr) {
    static const long RETRY_DELAY_S = 1;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    anjay_connection_ref_t ref = *(const anjay_connection_ref_t *) ref_ptr;
    bool skip_suspend = false;
    if (_anjay_connection_outgoing_exchanges_in_progress(ref)) {
        anjay_log(DEBUG, _("outgoing exchanges in progress, deferring socket "
                           "closure for queue mode"));
        anjay_server_connection_t *connection =
                _anjay_get_server_connection(ref);
        if (connection
                && !AVS_SCHED_DELAYED(
                           sched, &connection->queue_mode_close_socket_clb,
                           avs_time_duration_from_scalar(RETRY_DELAY_S,
                                                         AVS_TIME_S),
                           queue_mode_close_socket, &ref, sizeof(ref))) {
            skip_suspend = true;
        } else {
            anjay_log(WARNING, _("could not delay queue mode operations, "
                                 "suspending the connection now"));
        }
    }
    if (!skip_suspend) {
        _anjay_connection_suspend(ref);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

void _anjay_connection_schedule_queue_mode_close(anjay_connection_ref_t ref) {
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    assert(connection);
    assert(_anjay_connection_is_online(connection));

    avs_sched_del(&connection->queue_mode_close_socket_clb);
    if (ref.conn_type != ANJAY_CONNECTION_PRIMARY
            || !ref.server->registration_info.queue_mode) {
        return;
    }

    avs_time_duration_t delay =
            _anjay_max_transmit_wait_for_transport(ref.server->anjay,
                                                   connection->transport);

    // see comment on field declaration for logic summary
    if (AVS_SCHED_DELAYED(ref.server->anjay->sched,
                          &connection->queue_mode_close_socket_clb, delay,
                          queue_mode_close_socket, &ref, sizeof(ref))) {
        anjay_log(ERROR, _("could not schedule queue mode operations"));
    }
}

const anjay_url_t *_anjay_connection_uri(anjay_connection_ref_t ref) {
    return &_anjay_get_server_connection(ref)->uri;
}

void _anjay_connections_flush_notifications(anjay_connections_t *connections) {
    anjay_server_info_t *server =
            AVS_CONTAINER_OF(connections, anjay_server_info_t, connections);
    if (_anjay_server_registration_expired(server)) {
        anjay_log(TRACE, _("Server has no valid registration, not flushing "
                           "notifications"));
        return;
    }

    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        const anjay_connection_ref_t ref = {
            .server = server,
            .conn_type = conn_type
        };
        anjay_server_connection_t *connection =
                _anjay_connection_get(connections, ref.conn_type);
        if (connection->needs_observe_flush
                && _anjay_connection_is_online(connection)
                && (server->ssid == ANJAY_SSID_BOOTSTRAP
                    || !_anjay_observe_sched_flush(ref))) {
            connection->needs_observe_flush = false;
        }
    }
}

typedef struct {
    const avs_coap_udp_tx_params_t *tx_params;
    anjay_transport_set_t transport_set;
} update_server_instance_tx_params_args_t;

static int update_server_instance_tx_params(anjay_unlocked_t *anjay,
                                            anjay_server_info_t *server,
                                            void *args_) {
    (void) anjay;

    update_server_instance_tx_params_args_t *args =
            (update_server_instance_tx_params_args_t *) args_;

    anjay_connection_type_t type;
    ANJAY_CONNECTION_TYPE_FOREACH(type) {
        anjay_server_connection_t *conn =
                _anjay_connection_get(&server->connections, type);
        if (conn->coap_ctx
                && _anjay_socket_transport_included(args->transport_set,
                                                    conn->transport)) {
            avs_coap_udp_ctx_set_tx_params(conn->coap_ctx, args->tx_params);
        }
    }

    return 0;
}

avs_error_t
anjay_update_transport_tx_params(anjay_t *anjay_locked,
                                 anjay_transport_set_t transport_set,
                                 const avs_coap_udp_tx_params_t *tx_params) {
    assert(anjay_locked);

    if (!tx_params) {
        anjay_log(ERROR, _("given transmission parameters are NULL"));
        return avs_errno(AVS_EINVAL);
    } else {
        const char *error_message;
        if (!avs_coap_udp_tx_params_valid(tx_params, &error_message)) {
            assert(error_message);
            anjay_log(ERROR,
                      _("UDP transmission params validation failed with the "
                        "following error message: ") "%s",
                      error_message);
            return avs_errno(AVS_EINVAL);
        }
    }

    avs_error_t err = avs_errno(AVS_EINVAL);

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

#ifdef WITH_AVS_COAP_UDP
    if (transport_set.udp) {
        anjay->udp_tx_params = *tx_params;
        err = AVS_OK;
    }
#endif // WITH_AVS_COAP_UDP

    if (avs_is_err(err)) {
        anjay_log(ERROR, _("no transport for which transmission parameters "
                           "could be changed was given"));
    } else {
        _anjay_servers_foreach_active(
                anjay, update_server_instance_tx_params,
                &(update_server_instance_tx_params_args_t) {
                    .tx_params = tx_params,
                    .transport_set = transport_set
                });
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}

typedef struct {
    avs_time_duration_t exchange_update_timeout;
    anjay_transport_set_t transport_set;
} update_server_exchange_deadline_args_t;

static int update_server_coap_exchange_timeout(anjay_unlocked_t *anjay,
                                               anjay_server_info_t *server,
                                               void *args_) {
    (void) anjay;

    update_server_exchange_deadline_args_t *args =
            (update_server_exchange_deadline_args_t *) args_;

    anjay_connection_type_t type;
    ANJAY_CONNECTION_TYPE_FOREACH(type) {
        anjay_server_connection_t *conn =
                _anjay_connection_get(&server->connections, type);
        if (conn->coap_ctx
                && _anjay_socket_transport_included(args->transport_set,
                                                    conn->transport)) {
            avs_coap_set_exchange_max_time(conn->coap_ctx,
                                           args->exchange_update_timeout);
        }
    }

    return 0;
}

avs_error_t
anjay_update_coap_exchange_timeout(anjay_t *anjay_locked,
                                   anjay_transport_set_t transport_set,
                                   avs_time_duration_t exchange_timeout) {
    assert(anjay_locked);

    avs_error_t err = avs_errno(AVS_EINVAL);

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

#ifdef WITH_AVS_COAP_UDP
    if (transport_set.udp) {
        anjay->udp_exchange_timeout = exchange_timeout;
        err = AVS_OK;
    }
#endif // WITH_AVS_COAP_UDP
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
    if (transport_set.tcp) {
        anjay->tcp_exchange_timeout = exchange_timeout;
        err = AVS_OK;
    }
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)

    if (avs_is_err(err)) {
        anjay_log(ERROR, _("no transport for which exchange timeout could be "
                           "changed was given"));
    } else {
        _anjay_servers_foreach_active(
                anjay, update_server_coap_exchange_timeout,
                &(update_server_exchange_deadline_args_t) {
                    .exchange_update_timeout = exchange_timeout,
                    .transport_set = transport_set
                });
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}
