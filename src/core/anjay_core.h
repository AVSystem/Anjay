/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_CORE_H
#define ANJAY_CORE_H

#include <avsystem/commons/avs_errno.h>
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

#ifdef ANJAY_WITH_ATTR_STORAGE
#    include "attr_storage/anjay_attr_storage.h"
#endif // ANJAY_WITH_ATTR_STORAGE
#ifdef ANJAY_WITH_SEND
#    include "anjay_lwm2m_send.h"
#endif // ANJAY_WITH_SEND

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

#ifdef ANJAY_WITH_LWM2M11
static inline bool
_anjay_trust_store_valid(const anjay_trust_store_t *trust_store) {
    return trust_store->use_system_wide || trust_store->certs
           || trust_store->crls;
}

void _anjay_trust_store_cleanup(anjay_trust_store_t *trust_store);
#endif // ANJAY_WITH_LWM2M11

struct
#ifdef ANJAY_WITH_THREAD_SAFETY
        anjay_unlocked_struct
#else  // ANJAY_WITH_THREAD_SAFETY
        anjay_struct
#endif // ANJAY_WITH_THREAD_SAFETY
{
    anjay_transport_set_t online_transports;

#ifdef ANJAY_WITH_LWM2M11
    anjay_lwm2m_version_config_t lwm2m_version_config;
    anjay_queue_mode_preference_t queue_mode_preference;
    anjay_trust_store_t initial_trust_store;
    bool rebuild_client_cert_chain;
#endif // ANJAY_WITH_LWM2M11
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

    // defined(ANJAY_WITH_CORE_PERSISTENCE)

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
    avs_time_duration_t udp_exchange_timeout;
#endif
    avs_net_dtls_handshake_timeouts_t udp_dtls_hs_tx_params;
    avs_net_socket_tls_ciphersuites_t default_tls_ciphersuites;

#if defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)
    size_t coap_tcp_max_options_size;
    avs_time_duration_t coap_tcp_request_timeout;
    avs_time_duration_t tcp_exchange_timeout;
#endif // defined(ANJAY_WITH_LWM2M11) && defined(WITH_AVS_COAP_TCP)

    anjay_scheduled_notify_t scheduled_notify;

    char *endpoint_name;
    anjay_transaction_state_t transaction_state;

#ifdef ANJAY_WITH_SEND
    anjay_sender_t sender;
#endif // ANJAY_WITH_SEND

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

#ifdef ANJAY_WITH_ATTR_STORAGE
    anjay_attr_storage_t attr_storage;
#endif // ANJAY_WITH_ATTR_STORAGE

    anjay_prng_ctx_t prng_ctx;
#if !defined(ANJAY_WITH_THREAD_SAFETY) && defined(ANJAY_ATOMIC_FIELDS_DEFINED)
    anjay_atomic_fields_t atomic_fields;
#endif // !defined(ANJAY_WITH_THREAD_SAFETY) &&
       // defined(ANJAY_ATOMIC_FIELDS_DEFINED)
};

#define ANJAY_DM_DEFAULT_PMIN_VALUE 0

uint8_t _anjay_make_error_response_code(int handler_result);

avs_time_duration_t
_anjay_max_transmit_wait_for_transport(anjay_unlocked_t *anjay,
                                       anjay_socket_transport_t transport);

avs_time_duration_t
_anjay_exchange_lifetime_for_transport(anjay_unlocked_t *anjay,
                                       anjay_socket_transport_t transport);

int _anjay_serve_unlocked(anjay_unlocked_t *anjay,
                          avs_net_socket_t *ready_socket);

static inline avs_sched_t *_anjay_get_coap_sched(anjay_unlocked_t *anjay) {
#ifdef ANJAY_WITH_THREAD_SAFETY
    return anjay->coap_sched;
#else  // ANJAY_WITH_THREAD_SAFETY
    return anjay->sched;
#endif // ANJAY_WITH_THREAD_SAFETY
}

#if defined(ANJAY_WITH_ATTR_STORAGE)

// clang-format off
#    define ANJAY_PERSIST_EVAL_PERIODS_ATTR (1 << 0)
#    define ANJAY_PERSIST_CON_ATTR          (1 << 1)
#    define ANJAY_PERSIST_HQMAX_ATTR        (1 << 2)
#    define ANJAY_PERSIST_EDGE_ATTR         (1 << 3)
// clang-format on

#    define ANJAY_PERSIST_ALL_ATTR                                \
        (ANJAY_PERSIST_EVAL_PERIODS_ATTR | ANJAY_PERSIST_CON_ATTR \
         | ANJAY_PERSIST_HQMAX_ATTR | ANJAY_PERSIST_EDGE_ATTR)

avs_error_t _anjay_persistence_dm_r_attributes(avs_persistence_context_t *ctx,
                                               anjay_dm_r_attributes_t *attrs,
                                               int32_t bitmask);
avs_error_t _anjay_persistence_dm_oi_attributes(avs_persistence_context_t *ctx,
                                                anjay_dm_oi_attributes_t *attrs,
                                                int32_t bitmask);

#endif /* (defined(ANJAY_WITH_CORE_PERSISTENCE) &&       \
          defined(ANJAY_WITH_OBSERVATION_ATTRIBUTES)) || \
          defined(ANJAY_WITH_ATTR_STORAGE) */

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_CORE_H */
