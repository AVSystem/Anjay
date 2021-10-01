/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_CORE_H
#define ANJAY_CORE_H

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_net.h>
#include <avsystem/commons/avs_prng.h>
#include <avsystem/commons/avs_shared_buffer.h>
#include <avsystem/commons/avs_stream.h>

#include <avsystem/coap/udp.h>

#include "anjay_dm_core.h"
#include "observe/anjay_observe_core.h"

#include "anjay_bootstrap_core.h"
#include "anjay_downloader.h"
#include "anjay_servers_private.h"
#include "anjay_stats.h"
#include "anjay_utils_private.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_notify_queue_t queue;
    avs_sched_handle_t handle;
} anjay_scheduled_notify_t;

typedef struct {
    unsigned depth;
    AVS_LIST(const anjay_dm_installed_object_t *) objs_in_transaction;
} anjay_transaction_state_t;

typedef struct {
    bool allocated_by_user;
    avs_crypto_prng_ctx_t *ctx;
} anjay_prng_ctx_t;

struct
#ifdef ANJAY_WITH_THREAD_SAFETY
        anjay_unlocked_struct
#else  // ANJAY_WITH_THREAD_SAFETY
        anjay_struct
#endif // ANJAY_WITH_THREAD_SAFETY
{
    anjay_transport_set_t online_transports;

    avs_net_ssl_version_t dtls_version;
    avs_net_socket_configuration_t socket_config;
    avs_sched_t *sched;
#ifdef ANJAY_WITH_THREAD_SAFETY
    avs_sched_t *coap_sched;
    avs_sched_handle_t coap_sched_job_handle;
#endif // ANJAY_WITH_THREAD_SAFETY
    anjay_dm_t dm;
    anjay_security_config_cache_t security_config_from_dm_cache;
    uint16_t udp_listen_port;

    /**
     * List of known LwM2M servers we may want to be connected to. This is
     * semantically a map, keyed (and ordered) by SSID.
     */
    AVS_LIST(anjay_server_info_t) servers;

    /**
     * Cache of anjay_socket_entry_t objects, returned by
     * anjay_get_socket_entries(). These entries are never used for anything
     * inside the library, it's just to allow returning a list from a function
     * without requiring the user to clean it up.
     */
    AVS_LIST(const anjay_socket_entry_t) cached_public_sockets;

    avs_sched_handle_t reload_servers_sched_job_handle;
#ifdef ANJAY_WITH_OBSERVE
    anjay_observe_state_t observe;
#endif
#ifdef ANJAY_WITH_BOOTSTRAP
    anjay_bootstrap_t bootstrap;
#endif
#ifdef WITH_AVS_COAP_UDP
    avs_coap_udp_response_cache_t *udp_response_cache;
    avs_coap_udp_tx_params_t udp_tx_params;
#endif
    avs_net_dtls_handshake_timeouts_t udp_dtls_hs_tx_params;
    avs_net_socket_tls_ciphersuites_t default_tls_ciphersuites;

    anjay_connection_ref_t current_connection;
    anjay_scheduled_notify_t scheduled_notify;

    char *endpoint_name;
    anjay_transaction_state_t transaction_state;

    avs_shared_buffer_t *in_shared_buffer;
    avs_shared_buffer_t *out_shared_buffer;

#ifdef ANJAY_WITH_DOWNLOADER
    anjay_downloader_t downloader;
#endif // ANJAY_WITH_DOWNLOADER
    bool prefer_hierarchical_formats;
#ifdef ANJAY_WITH_NET_STATS
    closed_connections_stats_t closed_connections_stats;
#endif // ANJAY_WITH_NET_STATS
    bool use_connection_id;
    avs_ssl_additional_configuration_clb_t *additional_tls_config_clb;

    anjay_prng_ctx_t prng_ctx;
#if !defined(ANJAY_WITH_THREAD_SAFETY) && defined(ANJAY_ATOMIC_FIELDS_DEFINED)
    anjay_atomic_fields_t atomic_fields;
#endif // !defined(ANJAY_WITH_THREAD_SAFETY) &&
       // defined(ANJAY_ATOMIC_FIELDS_DEFINED)
};

#define ANJAY_DM_DEFAULT_PMIN_VALUE 1

uint8_t _anjay_make_error_response_code(int handler_result);

avs_time_duration_t
_anjay_max_transmit_wait_for_transport(anjay_unlocked_t *anjay,
                                       anjay_socket_transport_t transport);

avs_time_duration_t
_anjay_exchange_lifetime_for_transport(anjay_unlocked_t *anjay,
                                       anjay_socket_transport_t transport);

int _anjay_bind_connection(anjay_unlocked_t *anjay, anjay_connection_ref_t ref);

void _anjay_release_connection(anjay_unlocked_t *anjay);

int _anjay_serve_unlocked(anjay_unlocked_t *anjay,
                          avs_net_socket_t *ready_socket);

int _anjay_parse_request(const avs_coap_request_header_t *hdr,
                         anjay_request_t *out_request);

static inline avs_sched_t *_anjay_get_coap_sched(anjay_unlocked_t *anjay) {
#ifdef ANJAY_WITH_THREAD_SAFETY
    return anjay->coap_sched;
#else  // ANJAY_WITH_THREAD_SAFETY
    return anjay->sched;
#endif // ANJAY_WITH_THREAD_SAFETY
}

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_CORE_H */
