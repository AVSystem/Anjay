/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#include <anjay/stats.h>
#include <anjay_modules/anjay_dm_utils.h>

#include "anjay_core.h"
#include "anjay_servers_utils.h"
#include "anjay_stats.h"

VISIBILITY_SOURCE_BEGIN

#define stats_log(...) _anjay_log(anjay_stats, __VA_ARGS__)

#ifdef ANJAY_WITH_NET_STATS

typedef enum {
    NET_STATS_BYTES_SENT,
    NET_STATS_BYTES_RECEIVED,
    NET_STATS_OUTGOING_RETRANSMISSIONS,
    NET_STATS_INCOMING_RETRANSMISSIONS
} net_stats_type_t;

static uint64_t get_socket_stats(avs_net_socket_t *socket,
                                 net_stats_type_t type) {
    avs_net_socket_opt_value_t bytes_stats;
    switch (type) {
    case NET_STATS_BYTES_SENT: {
        avs_error_t err =
                avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_BYTES_SENT,
                                       &bytes_stats);
        if (avs_is_err(err)) {
            stats_log(DEBUG, _("retrieving socket stats failed (") "%s" _(")"),
                      AVS_COAP_STRERROR(err));
            return 0;
        }
        return bytes_stats.bytes_sent;
    }
    case NET_STATS_BYTES_RECEIVED: {
        avs_error_t err = avs_net_socket_get_opt(
                socket, AVS_NET_SOCKET_OPT_BYTES_RECEIVED, &bytes_stats);
        if (avs_is_err(err)) {
            stats_log(DEBUG, _("retrieving socket stats failed (") "%s" _(")"),
                      AVS_COAP_STRERROR(err));
            return 0;
        }
        return bytes_stats.bytes_received;
    }
    default:
        AVS_UNREACHABLE("this function accepts only NET_STATS_BYTES_SENT or "
                        "NET_STATS_BYTES_RECEIVED");
        return 0;
    }
}

static uint64_t get_current_stats_of_connection(anjay_connection_ref_t conn_ref,
                                                net_stats_type_t type) {
    avs_net_socket_t *socket;
    avs_coap_ctx_t *coap_ctx;
    switch (type) {
    case NET_STATS_BYTES_SENT:
    case NET_STATS_BYTES_RECEIVED:
        socket = _anjay_connection_get_online_socket(conn_ref);
        return socket ? get_socket_stats(socket, type) : 0;
    case NET_STATS_OUTGOING_RETRANSMISSIONS:
        coap_ctx = _anjay_connection_get_coap(conn_ref);
        return coap_ctx ? avs_coap_get_stats(coap_ctx)
                                  .outgoing_retransmissions_count
                        : 0;
    case NET_STATS_INCOMING_RETRANSMISSIONS:
        coap_ctx = _anjay_connection_get_coap(conn_ref);
        return coap_ctx ? avs_coap_get_stats(coap_ctx)
                                  .incoming_retransmissions_count
                        : 0;
    }
    AVS_UNREACHABLE("invalid enum value");
    return 0;
}

static uint64_t get_stats_of_closed_connections(anjay_t *anjay,
                                                net_stats_type_t type) {
    switch (type) {
    case NET_STATS_BYTES_SENT:
        return anjay->closed_connections_stats.socket_stats.bytes_sent;
    case NET_STATS_BYTES_RECEIVED:
        return anjay->closed_connections_stats.socket_stats.bytes_received;
    case NET_STATS_OUTGOING_RETRANSMISSIONS:
        return anjay->closed_connections_stats.coap_stats
                .outgoing_retransmissions_count;
    case NET_STATS_INCOMING_RETRANSMISSIONS:
        return anjay->closed_connections_stats.coap_stats
                .incoming_retransmissions_count;
    }
    AVS_UNREACHABLE("invalid enum value");
    return 0;
}

typedef struct {
    net_stats_type_t net_stats_type;
    uint64_t result_for_active_servers;
} get_current_stats_of_server_args_t;

static int get_current_stats_of_server(anjay_t *anjay,
                                       anjay_server_info_t *server,
                                       void *args_) {
    (void) anjay;
    get_current_stats_of_server_args_t *args =
            (get_current_stats_of_server_args_t *) args_;
    anjay_connection_type_t conn_type;
    ANJAY_CONNECTION_TYPE_FOREACH(conn_type) {
        const anjay_connection_ref_t conn_ref = {
            .server = server,
            .conn_type = conn_type
        };
        args->result_for_active_servers +=
                get_current_stats_of_connection(conn_ref, args->net_stats_type);
    }
    return 0;
}

static uint64_t get_stats_of_all_connections(anjay_t *anjay,
                                             net_stats_type_t type) {
    get_current_stats_of_server_args_t args = {
        .net_stats_type = type,
        .result_for_active_servers = 0
    };
    _anjay_servers_foreach_active(anjay, get_current_stats_of_server, &args);
    return args.result_for_active_servers
           + get_stats_of_closed_connections(anjay, type);
}

uint64_t anjay_get_tx_bytes(anjay_t *anjay) {
    return get_stats_of_all_connections(anjay, NET_STATS_BYTES_SENT);
}

uint64_t anjay_get_rx_bytes(anjay_t *anjay) {
    return get_stats_of_all_connections(anjay, NET_STATS_BYTES_RECEIVED);
}

uint64_t anjay_get_num_incoming_retransmissions(anjay_t *anjay) {
    return get_stats_of_all_connections(anjay,
                                        NET_STATS_INCOMING_RETRANSMISSIONS);
}

uint64_t anjay_get_num_outgoing_retransmissions(anjay_t *anjay) {
    return get_stats_of_all_connections(anjay,
                                        NET_STATS_OUTGOING_RETRANSMISSIONS);
}

void _anjay_coap_ctx_cleanup(anjay_t *anjay, avs_coap_ctx_t **ctx) {
    if (ctx && *ctx) {
        avs_coap_stats_t stats = avs_coap_get_stats(*ctx);
        anjay->closed_connections_stats.coap_stats
                .outgoing_retransmissions_count +=
                stats.outgoing_retransmissions_count;
        anjay->closed_connections_stats.coap_stats
                .incoming_retransmissions_count +=
                stats.incoming_retransmissions_count;
    }
    avs_coap_ctx_cleanup(ctx);
}

#else // ANJAY_WITH_NET_STATS

uint64_t anjay_get_tx_bytes(anjay_t *anjay) {
    (void) anjay;
    stats_log(ERROR,
              _("NET_STATS feature disabled. Anjay was compiled without "
                "ANJAY_WITH_NET_STATS option."));
    return 0;
}

uint64_t anjay_get_rx_bytes(anjay_t *anjay) {
    (void) anjay;
    stats_log(ERROR,
              _("NET_STATS feature disabled. Anjay was compiled without "
                "ANJAY_WITH_NET_STATS option."));
    return 0;
}

uint64_t anjay_get_num_incoming_retransmissions(anjay_t *anjay) {
    (void) anjay;
    stats_log(ERROR,
              _("NET_STATS feature disabled. Anjay was compiled without "
                "ANJAY_WITH_NET_STATS option."));
    return 0;
}

uint64_t anjay_get_num_outgoing_retransmissions(anjay_t *anjay) {
    (void) anjay;
    stats_log(ERROR,
              _("NET_STATS feature disabled. Anjay was compiled without "
                "ANJAY_WITH_NET_STATS option."));
    return 0;
}

void _anjay_coap_ctx_cleanup(anjay_t *anjay, avs_coap_ctx_t **ctx) {
    (void) anjay;
    avs_coap_ctx_cleanup(ctx);
}

#endif // ANJAY_WITH_NET_STATS

avs_error_t _anjay_socket_cleanup(anjay_t *anjay, avs_net_socket_t **socket) {
    assert(socket);
    if (*socket) {
        avs_net_socket_shutdown(*socket);
#ifdef ANJAY_WITH_NET_STATS
        anjay->closed_connections_stats.socket_stats.bytes_sent +=
                get_socket_stats(*socket, NET_STATS_BYTES_SENT);
        anjay->closed_connections_stats.socket_stats.bytes_received +=
                get_socket_stats(*socket, NET_STATS_BYTES_RECEIVED);
#endif // ANJAY_WITH_NET_STATS
        (void) anjay;
    }
    return avs_net_socket_cleanup(socket);
}
