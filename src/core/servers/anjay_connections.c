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

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_stream_inbuf.h>
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_utils.h>

#ifdef WITH_AVS_COAP_UDP
#    include <avsystem/coap/udp.h>
#endif // WITH_AVS_COAP_UDP
#ifdef WITH_AVS_COAP_TCP
#    include <avsystem/coap/tcp.h>
#endif // WITH_AVS_COAP_TCP

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "../anjay_core.h"
#include "../anjay_io_core.h"
#include "../anjay_servers_reload.h"
#include "../anjay_servers_utils.h"
#if defined(ANJAY_WITH_DOWNLOADER) && defined(ANJAY_WITH_LWM2M11)
#    include "../anjay_downloader.h"
#endif // defined(ANJAY_WITH_DOWNLOADER) && defined(ANJAY_WITH_LWM2M11)

#include "../dm/anjay_query.h"

#include "../io/anjay_vtable.h"

#include "anjay_activate.h"
#include "anjay_connections_internal.h"
#include "anjay_security.h"
#include "anjay_server_connections.h"

VISIBILITY_SOURCE_BEGIN

avs_net_socket_t *_anjay_connection_internal_get_socket(
        const anjay_server_connection_t *connection) {
    return connection->conn_socket_;
}

void _anjay_connection_internal_clean_socket(
        anjay_unlocked_t *anjay, anjay_server_connection_t *connection) {
#ifdef ANJAY_WITH_DOWNLOADER
    // This would normally happen as part of _anjay_coap_ctx_cleanup() anyway
    // (by means of the exchange result callback), but on rare occasions the
    // download might be being suspended, and deliberately keep existing even
    // though the exchange is cancelled. So let's abort download manually.
    _anjay_downloader_abort_same_socket(&anjay->downloader,
                                        connection->conn_socket_);
#endif // ANJAY_WITH_DOWNLOADER
    _anjay_coap_ctx_cleanup(anjay, &connection->coap_ctx);
    _anjay_socket_cleanup(anjay, &connection->conn_socket_);
    avs_sched_del(&connection->queue_mode_close_socket_clb);
}

typedef struct {
    anjay_unlocked_output_ctx_t base;
    const anjay_ret_bytes_ctx_vtable_t *ret_bytes_vtable;
    avs_crypto_security_info_tag_t tag;
    avs_crypto_security_info_union_t *out_array;
    size_t out_element_count;
    size_t bytes_remaining;
} read_security_info_ctx_t;

static int read_security_info_ret_bytes_begin(
        anjay_unlocked_output_ctx_t *ctx_,
        size_t length,
        anjay_unlocked_ret_bytes_ctx_t **out_bytes_ctx) {
    read_security_info_ctx_t *ctx = (read_security_info_ctx_t *) ctx_;
    if (ctx->out_array) {
        anjay_log(ERROR, _("value already returned"));
        return -1;
    }
    if (!(ctx->out_array = (avs_crypto_security_info_union_t *) avs_malloc(
                  sizeof(*ctx->out_array) + length))) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    *ctx->out_array = (const avs_crypto_security_info_union_t) {
        .type = ctx->tag,
        .source = AVS_CRYPTO_DATA_SOURCE_BUFFER,
        .info.buffer = {
            .buffer = (char *) ctx->out_array + sizeof(*ctx->out_array),
            .buffer_size = length
        }
    };
    ctx->out_element_count = 1;
    ctx->bytes_remaining = length;
    *out_bytes_ctx = (anjay_unlocked_ret_bytes_ctx_t *) &ctx->ret_bytes_vtable;
    return 0;
}

static int read_security_info_ret_bytes_append(
        anjay_unlocked_ret_bytes_ctx_t *ctx_, const void *data, size_t size) {
    read_security_info_ctx_t *ctx =
            (read_security_info_ctx_t *) AVS_CONTAINER_OF(
                    ctx_, read_security_info_ctx_t, ret_bytes_vtable);
    assert(ctx->out_array);
    if (size > ctx->bytes_remaining) {
        anjay_log(DEBUG, _("tried to write too many bytes"));
        return -1;
    }
    memcpy(((char *) (intptr_t) ctx->out_array->info.buffer.buffer)
                   + (ctx->out_array->info.buffer.buffer_size
                      - ctx->bytes_remaining),
           data, size);
    ctx->bytes_remaining -= size;
    return 0;
}

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
static int read_security_info_ret_security_info(
        anjay_unlocked_output_ctx_t *ctx_,
        const avs_crypto_security_info_union_t *info) {
    read_security_info_ctx_t *ctx = (read_security_info_ctx_t *) ctx_;
    if (ctx->out_array) {
        anjay_log(ERROR, _("value already returned"));
        return -1;
    }
    if (info->type != ctx->tag) {
        anjay_log(ERROR, _("wrong type of security info passed"));
        return -1;
    }
    switch (ctx->tag) {
    case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
        if (avs_is_err(avs_crypto_certificate_chain_info_copy_as_array(
                    (avs_crypto_certificate_chain_info_t **) &ctx->out_array,
                    &ctx->out_element_count,
                    (const avs_crypto_certificate_chain_info_t) {
                        .desc = *info
                    }))) {
            assert(!ctx->out_array);
            assert(!ctx->out_element_count);
            return -1;
        } else {
            assert(ctx->out_array || !ctx->out_element_count);
            return 0;
        }
    case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
        if (avs_is_err(avs_crypto_private_key_info_copy(
                    (avs_crypto_private_key_info_t **) &ctx->out_array,
                    (const avs_crypto_private_key_info_t) {
                        .desc = *info
                    }))) {
            assert(!ctx->out_array);
            return -1;
        } else {
            assert(ctx->out_array);
            ctx->out_element_count = 1;
            return 0;
        }
    case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
        if (avs_is_err(avs_crypto_psk_identity_info_copy(
                    (avs_crypto_psk_identity_info_t **) &ctx->out_array,
                    (const avs_crypto_psk_identity_info_t) {
                        .desc = *info
                    }))) {
            assert(!ctx->out_array);
            return -1;
        } else {
            assert(ctx->out_array);
            ctx->out_element_count = 1;
            return 0;
        }
    case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
        if (avs_is_err(avs_crypto_psk_key_info_copy(
                    (avs_crypto_psk_key_info_t **) &ctx->out_array,
                    (const avs_crypto_psk_key_info_t) {
                        .desc = *info
                    }))) {
            assert(!ctx->out_array);
            return -1;
        } else {
            assert(ctx->out_array);
            ctx->out_element_count = 1;
            return 0;
        }
    default:
        AVS_UNREACHABLE("invalid tag");
        return -1;
    }
}
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */

static const anjay_output_ctx_vtable_t READ_SECURITY_INFO_VTABLE = {
    .bytes_begin = read_security_info_ret_bytes_begin
#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
    ,
    .security_info = read_security_info_ret_security_info
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
};

static const anjay_ret_bytes_ctx_vtable_t READ_SECURITY_INFO_BYTES_VTABLE = {
    .append = read_security_info_ret_bytes_append
};

avs_error_t
_anjay_dm_read_security_info(anjay_unlocked_t *anjay,
                             anjay_iid_t security_iid,
                             anjay_rid_t security_rid,
                             avs_crypto_security_info_tag_t tag,
                             avs_crypto_security_info_union_t **out_array,
                             size_t *out_element_count) {
    assert(anjay);
    assert(out_array);
    assert(!*out_array);
    assert(out_element_count);
    read_security_info_ctx_t ctx = {
        .base = {
            .vtable = &READ_SECURITY_INFO_VTABLE
        },
        .ret_bytes_vtable = &READ_SECURITY_INFO_BYTES_VTABLE,
        .tag = tag
    };
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               security_rid);
    if (_anjay_dm_read_resource_into_ctx(anjay, &path,
                                         (anjay_unlocked_output_ctx_t *) &ctx)
            || ctx.bytes_remaining) {
        anjay_log(WARNING, _("read ") "%s" _(" failed"),
                  ANJAY_DEBUG_MAKE_PATH(&path));
        avs_free(ctx.out_array);
        return avs_errno(AVS_EPROTO);
    }
    *out_array = ctx.out_array;
    *out_element_count = ctx.out_element_count;
    return AVS_OK;
}

avs_error_t
_anjay_connection_init_psk_security(anjay_unlocked_t *anjay,
                                    anjay_iid_t security_iid,
                                    anjay_rid_t identity_rid,
                                    anjay_rid_t secret_key_rid,
                                    avs_net_security_info_t *security,
                                    anjay_security_config_cache_t *cache) {
    assert(anjay);
    avs_error_t err;
    size_t element_count = 0;

    avs_net_psk_info_t psk_info;
    memset(&psk_info, 0, sizeof(psk_info));

    AVS_STATIC_ASSERT(sizeof(*cache->psk_key)
                              == sizeof(avs_crypto_security_info_union_t),
                      psk_key_info_equivalent_to_union);
    if (avs_is_err(
                (err = _anjay_dm_read_security_info(
                         anjay, security_iid, secret_key_rid,
                         AVS_CRYPTO_SECURITY_INFO_PSK_KEY,
                         (avs_crypto_security_info_union_t **) &cache->psk_key,
                         &element_count)))) {
        return err;
    }
    assert(element_count == 1);
    psk_info.key = *cache->psk_key;

    AVS_STATIC_ASSERT(sizeof(*cache->psk_identity)
                              == sizeof(avs_crypto_security_info_union_t),
                      psk_identity_info_equivalent_to_union);
    if (avs_is_err((err = _anjay_dm_read_security_info(
                            anjay, security_iid, identity_rid,
                            AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY,
                            (avs_crypto_security_info_union_t **) &cache
                                    ->psk_identity,
                            &element_count)))) {
        return err;
    }
    assert(element_count == 1);
    psk_info.identity = *cache->psk_identity;

    *security = avs_net_security_info_from_psk(psk_info);
    return AVS_OK;
}

static const anjay_connection_type_definition_t *
get_connection_type_def(anjay_socket_transport_t type) {
    switch (type) {
#ifdef WITH_AVS_COAP_UDP
    case ANJAY_SOCKET_TRANSPORT_UDP:
        return &ANJAY_CONNECTION_DEF_UDP;
#endif // WITH_AVS_COAP_UDP
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
    case ANJAY_SOCKET_TRANSPORT_TCP:
        return &ANJAY_CONNECTION_DEF_TCP;
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
    default:
        return NULL;
    }
}

static void update_exchange_timeout(anjay_server_info_t *server,
                                    anjay_connection_type_t conn_type) {
    anjay_server_connection_t *conn =
            _anjay_connection_get(&server->connections, conn_type);
    assert(conn->coap_ctx);
    avs_time_duration_t exchange_max_time;
    switch (conn->transport) {
#ifdef WITH_AVS_COAP_UDP
    case ANJAY_SOCKET_TRANSPORT_UDP:
        exchange_max_time = server->anjay->udp_exchange_timeout;
        break;
#endif // WITH_AVS_COAP_UDP
#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
    case ANJAY_SOCKET_TRANSPORT_TCP:
        exchange_max_time = server->anjay->tcp_exchange_timeout;
        break;
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
    default:
        AVS_UNREACHABLE("Invalid connection type");
        return;
    }
    avs_coap_set_exchange_max_time(conn->coap_ctx, exchange_max_time);
}

int _anjay_connection_ensure_coap_context(anjay_server_info_t *server,
                                          anjay_connection_type_t conn_type) {
    anjay_server_connection_t *conn =
            _anjay_connection_get(&server->connections, conn_type);
    const anjay_connection_type_definition_t *def =
            get_connection_type_def(conn->transport);
    assert(def);
    int result = def->ensure_coap_context(server->anjay, conn);
    if (!result) {
        update_exchange_timeout(server, conn_type);
    }
    return result;
}

avs_error_t _anjay_server_connection_internal_bring_online(
        anjay_server_info_t *server, anjay_connection_type_t conn_type) {
    assert(server);
    anjay_server_connection_t *connection =
            _anjay_connection_get(&server->connections, conn_type);
    assert(connection);
    assert(connection->conn_socket_);

    const anjay_connection_type_definition_t *def =
            get_connection_type_def(connection->transport);
    assert(def);

    if (_anjay_connection_is_online(connection)) {
        anjay_log(DEBUG, _("socket already connected"));
        connection->state = ANJAY_SERVER_CONNECTION_STABLE;
        connection->needs_observe_flush = true;
        return AVS_OK;
    }

    // defined(ANJAY_WITH_CORE_PERSISTENCE)

    bool session_resumed;
    bool should_attempt_context_wrapping;
    avs_error_t err = def->connect_socket(server->anjay, connection);
    if (avs_is_err(err)) {
        goto error;
    }

    if (!(session_resumed =
                  _anjay_was_session_resumed(connection->conn_socket_))) {
        _anjay_conn_session_token_reset(&connection->session_token);
        // Clean up and recreate the CoAP context to discard observations
        _anjay_coap_ctx_cleanup(server->anjay, &connection->coap_ctx);
    }

    if (_anjay_connection_ensure_coap_context(server, conn_type)) {
        err = avs_errno(AVS_ENOMEM);
        goto error;
    }
    if (!avs_coap_ctx_has_socket(connection->coap_ctx)
            && avs_is_err((err = avs_coap_ctx_set_socket(
                                   connection->coap_ctx,
                                   connection->conn_socket_)))) {
        anjay_log(ERROR, _("could not assign socket to CoAP/UDP context"));
        goto error;
    }

    if (session_resumed) {
        if (!connection->stateful
                || _anjay_was_connection_id_resumed(connection->conn_socket_)) {
            connection->state = ANJAY_SERVER_CONNECTION_STABLE;
            anjay_log(INFO, "statelessly resumed connection");
        } else {
            connection->state = ANJAY_SERVER_CONNECTION_FRESHLY_CONNECTED;
            anjay_log(INFO, "statefully resumed connection");
        }
    } else {
        connection->state = ANJAY_SERVER_CONNECTION_FRESHLY_CONNECTED;
        anjay_log(INFO, "reconnected");
    }
    // NOTE: needs_observe_flush also controls flushing Send messages,
    // so we need it even if there are no observations due to new session
    connection->needs_observe_flush = true;
    return AVS_OK;

error:
    connection->state = ANJAY_SERVER_CONNECTION_OFFLINE;
    _anjay_coap_ctx_cleanup(server->anjay, &connection->coap_ctx);

    if (connection->conn_socket_
            && avs_is_err(avs_net_socket_close(connection->conn_socket_))) {
        anjay_log(WARNING, _("Could not close the socket (?!)"));
    }
    return err;
}

static void connection_cleanup(anjay_unlocked_t *anjay,
                               anjay_server_connection_t *connection) {
    _anjay_connection_internal_clean_socket(anjay, connection);
    _anjay_url_cleanup(&connection->uri);
}

void _anjay_connections_close(anjay_unlocked_t *anjay,
                              anjay_connections_t *connections) {
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        connection_cleanup(anjay,
                           _anjay_connection_get(connections, conn_type));
    }
}

anjay_conn_session_token_t
_anjay_connections_get_primary_session_token(anjay_connections_t *connections) {
    return _anjay_connection_get(connections, ANJAY_CONNECTION_PRIMARY)
            ->session_token;
}

void _anjay_connection_internal_invalidate_session(
        anjay_server_connection_t *connection) {
    memset(connection->nontransient_state.dtls_session_buffer, 0,
           sizeof(connection->nontransient_state.dtls_session_buffer));
}

static avs_error_t
recreate_socket(anjay_unlocked_t *anjay,
                const anjay_connection_type_definition_t *def,
                anjay_server_connection_t *connection,
                anjay_connection_info_t *inout_info) {
    avs_net_ssl_configuration_t socket_config;
    memset(&socket_config, 0, sizeof(socket_config));

    assert(!_anjay_connection_internal_get_socket(connection));
    socket_config.backend_configuration = anjay->socket_config;
    socket_config.backend_configuration.reuse_addr = 1;
#ifndef ANJAY_WITHOUT_IP_STICKINESS
    socket_config.backend_configuration.preferred_endpoint =
            &connection->nontransient_state.preferred_endpoint;
#endif // ANJAY_WITHOUT_IP_STICKINESS
    socket_config.version = anjay->dtls_version;
    socket_config.session_resumption_buffer =
            connection->nontransient_state.dtls_session_buffer;
    socket_config.session_resumption_buffer_size =
            sizeof(connection->nontransient_state.dtls_session_buffer);
    socket_config.dtls_handshake_timeouts =
            def->get_dtls_handshake_timeouts(anjay);
    socket_config.additional_configuration_clb =
            anjay->additional_tls_config_clb;
    socket_config.server_name_indication = inout_info->sni.sni;
    socket_config.use_connection_id = anjay->use_connection_id;
    socket_config.prng_ctx = anjay->prng_ctx.ctx;

    // At this point, inout_info has "global" settings filled,
    // but transport-specific (i.e. UDP or SMS) fields are not
    anjay_security_config_t security_config;
    anjay_security_config_cache_t security_config_cache;
    memset(&security_config_cache, 0, sizeof(security_config_cache));
    avs_error_t err;
    {
        err = _anjay_connection_security_generic_get_config(
                anjay, &security_config, &security_config_cache, inout_info);
    }
    if (avs_is_err(err)) {
        anjay_log(DEBUG,
                  _("could not get ") "%s" _(
                          " security config for server ") "/%u/%u",
                  def->name, ANJAY_DM_OID_SECURITY, inout_info->security_iid);
    } else {
        socket_config.security = security_config.security_info;
        socket_config.ciphersuites = security_config.tls_ciphersuites;
        if (avs_is_err((err = def->prepare_connection(
                                anjay, connection, &socket_config,
                                security_config.dane_tlsa_record, inout_info)))
                && connection->conn_socket_) {
            avs_net_socket_shutdown(connection->conn_socket_);
            avs_net_socket_close(connection->conn_socket_);
        }
        // defined(ANJAY_WITH_CORE_PERSISTENCE)
    }
    _anjay_security_config_cache_cleanup(&security_config_cache);
    return err;
}

static avs_error_t
ensure_socket_connected(anjay_server_info_t *server,
                        anjay_connection_type_t conn_type,
                        anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *connection =
            _anjay_connection_get(&server->connections, conn_type);
    assert(connection);
    const anjay_connection_type_definition_t *def =
            get_connection_type_def(connection->transport);
    assert(def);
    avs_net_socket_t *existing_socket =
            _anjay_connection_internal_get_socket(connection);

    if (existing_socket == NULL) {
        avs_error_t err =
                recreate_socket(server->anjay, def, connection, inout_info);
        if (avs_is_err(err)) {
            connection->state = ANJAY_SERVER_CONNECTION_OFFLINE;
            return err;
        }
    }

    return _anjay_server_connection_internal_bring_online(server, conn_type);
}

static bool should_primary_connection_be_online(anjay_server_info_t *server) {
    anjay_connection_ref_t ref = {
        .server = server,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    };
    anjay_server_connection_t *connection = _anjay_get_server_connection(ref);
    avs_net_socket_t *socket =
            _anjay_connection_internal_get_socket(connection);
    // Server is supposed to be active, so we need to create the socket
    return !socket
           // Bootstrap Server has no concept of queue mode
           || server->ssid == ANJAY_SSID_BOOTSTRAP
           // if connection is already online, no reason to disconnect it
           || _anjay_connection_get_online_socket(ref)
           // if registration expired, we need to connect to renew it
           || server->registration_info.update_forced
           || _anjay_server_registration_expired(server)
           // if queue mode is not enabled, server shall always be online
           || !server->registration_info.queue_mode
           // if there are notifications to be sent, we need to send them
           || _anjay_observe_needs_flushing(ref)
#ifdef ANJAY_WITH_SEND
           // if there are Send messages to be sent, we need to send them
           || _anjay_send_has_deferred(server->anjay, server->ssid)
#endif // ANJAY_WITH_SEND
            ;
}

static avs_error_t refresh_connection(anjay_server_info_t *server,
                                      anjay_connection_type_t conn_type,
                                      bool enabled,
                                      anjay_connection_info_t *inout_info) {
    anjay_server_connection_t *out_connection =
            _anjay_connection_get(&server->connections, conn_type);
    assert(out_connection);

    _anjay_url_cleanup(&out_connection->uri);

    if (!enabled) {
        if (conn_type == ANJAY_CONNECTION_PRIMARY) {
            _anjay_connection_suspend((anjay_connection_ref_t) {
                .server = server,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
            out_connection->state = ANJAY_SERVER_CONNECTION_OFFLINE;
        } else {
            // Disabled trigger connection does not matter much,
            // so treat it as stable
            // defined(ANJAY_WITH_CORE_PERSISTENCE)
            _anjay_connection_internal_clean_socket(server->anjay,
                                                    out_connection);
            out_connection->state = ANJAY_SERVER_CONNECTION_STABLE;
        }
        out_connection->needs_observe_flush = false;
        return AVS_OK;
    } else {
        return ensure_socket_connected(server, conn_type, inout_info);
    }
}

void _anjay_server_connections_refresh(
        anjay_server_info_t *server,
        anjay_iid_t security_iid,
        avs_url_t **move_uri,
        const anjay_server_name_indication_t *sni) {
    anjay_connection_info_t server_info = {
        .ssid = server->ssid,
        .security_iid = security_iid,
    };
    if (*move_uri) {
        server_info.uri = *move_uri;
        server_info.transport_info = _anjay_transport_info_by_uri_scheme(
                avs_url_protocol(*move_uri));
        *move_uri = NULL;
    }
    memcpy(&server_info.sni, sni, sizeof(*sni));

    if (security_iid != ANJAY_ID_INVALID) {
        server->last_used_security_iid = security_iid;
    }
    anjay_server_connection_t *primary_conn =
            _anjay_connection_get(&server->connections,
                                  ANJAY_CONNECTION_PRIMARY);
    if (server_info.transport_info
            && (!_anjay_socket_transport_supported(
                        server->anjay, server_info.transport_info->transport)
                || !_anjay_socket_transport_is_online(
                           server->anjay,
                           server_info.transport_info->transport))) {
        anjay_log(WARNING,
                  _("transport required for protocol ") "%s" _(
                          " is not supported or offline"),
                  server_info.transport_info->uri_scheme);
        server_info.transport_info = NULL;
    }
    if (server_info.transport_info
            && primary_conn->transport
                           != server_info.transport_info->transport) {
        char old_binding[] = "(none)";
        if (primary_conn->transport != ANJAY_SOCKET_TRANSPORT_INVALID) {
            old_binding[0] =
                    _anjay_binding_info_by_transport(primary_conn->transport)
                            ->letter;
            old_binding[1] = '\0';
        }
        char new_binding = _anjay_binding_info_by_transport(
                                   server_info.transport_info->transport)
                                   ->letter;
        const char *host = avs_url_host(server_info.uri);
        const char *port = avs_url_port(server_info.uri);
        anjay_log(INFO,
                  _("server /0/") "%u" _(": transport change: ") "%s" _(
                          " -> ") "%c" _(" (uri: ") "%s://%s%s%s" _(")"),
                  security_iid, old_binding, new_binding,
                  server_info.transport_info->uri_scheme, host ? host : "",
                  port ? ":" : "", port ? port : "");
        // change in transport binding requries creating a different type of
        // socket and possibly CoAP context
        connection_cleanup(server->anjay, primary_conn);
        primary_conn->transport = server_info.transport_info->transport;
        server->registration_info.expire_time = AVS_TIME_REAL_INVALID;
        // defined(ANJAY_WITH_CORE_PERSISTENCE)
    }

    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        anjay_server_connection_t *connection =
                _anjay_connection_get(&server->connections, conn_type);
        connection->state = ANJAY_SERVER_CONNECTION_IN_PROGRESS;
        avs_sched_del(&connection->queue_mode_close_socket_clb);
    }
    avs_error_t err = refresh_connection(
            server, ANJAY_CONNECTION_PRIMARY,
            !!server_info.transport_info
                    && should_primary_connection_be_online(server),
            &server_info);

    // TODO T2391: fall back to another transport if connection failed
    _anjay_server_on_refreshed(server, primary_conn->state, err);
    _anjay_connection_info_cleanup(&server_info);
}

avs_error_t _anjay_get_security_config(anjay_unlocked_t *anjay,
                                       anjay_security_config_t *out_config,
                                       anjay_security_config_cache_t *cache,
                                       anjay_ssid_t ssid,
                                       anjay_iid_t security_iid) {
    anjay_connection_info_t info = {
        .ssid = ssid,
        .security_iid = security_iid
    };
    avs_error_t err =
            _anjay_connection_security_generic_get_config(anjay, out_config,
                                                          cache, &info);
    _anjay_connection_info_cleanup(&info);
    return err;
}

#ifdef ANJAY_WITH_LWM2M11
void _anjay_server_update_last_ssl_alert_code(const anjay_server_info_t *info,
                                              uint8_t level,
                                              uint8_t description) {
    (void) level;

    if (info->ssid == ANJAY_SSID_BOOTSTRAP) {
        // This operation does not make sense for Bootstrap Server, because it
        // has no Server Instance.
        return;
    }

    anjay_iid_t server_iid;
    if (_anjay_find_server_iid(info->anjay, info->ssid, &server_iid)) {
        anjay_log(
                DEBUG,
                _("could not find Server Instance associated with SSID ") "%u",
                (unsigned) info->ssid);
        return;
    }

    anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, server_iid,
                               ANJAY_DM_RID_SERVER_TLS_DTLS_ALERT_CODE);

    (void) _anjay_dm_write_resource_u64(info->anjay, path, description, NULL);
}
#endif // ANJAY_WITH_LWM2M11

bool _anjay_socket_transport_supported(anjay_unlocked_t *anjay,
                                       anjay_socket_transport_t type) {
    if (get_connection_type_def(type) == NULL) {
        return false;
    }

    (void) anjay;
    return true;
}
