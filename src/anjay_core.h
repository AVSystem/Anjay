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

#ifndef ANJAY_CORE_H
#define ANJAY_CORE_H

#include <avsystem/commons/list.h>
#include <avsystem/commons/net.h>
#include <avsystem/commons/stream.h>

#include "dm_core.h"
#include "observe/observe_core.h"

#include "downloader.h"
#include "interface/bootstrap_core.h"
#include "servers.h"
#include "utils_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_notify_queue_t queue;
    anjay_sched_handle_t handle;
} anjay_scheduled_notify_t;

typedef struct {
    unsigned depth;
    AVS_LIST(const anjay_dm_object_def_t *const *) objs_in_transaction;
} anjay_transaction_state_t;

struct anjay_struct {
    bool offline;
    avs_net_ssl_version_t dtls_version;
    avs_net_socket_configuration_t udp_socket_config;
    anjay_sched_t *sched;
    anjay_dm_t dm;
    uint16_t udp_listen_port;
    anjay_servers_t *servers;
    anjay_sched_handle_t reload_servers_sched_job_handle;
#ifdef WITH_OBSERVE
    anjay_observe_state_t observe;
#endif
#ifdef WITH_BOOTSTRAP
    anjay_bootstrap_t bootstrap;
#endif
    avs_coap_tx_params_t udp_tx_params;
    avs_net_dtls_handshake_timeouts_t udp_dtls_hs_tx_params;
    avs_coap_ctx_t *coap_ctx;
    avs_stream_abstract_t *comm_stream;
    anjay_connection_ref_t current_connection;
    anjay_scheduled_notify_t scheduled_notify;

    const char *endpoint_name;
    anjay_transaction_state_t transaction_state;

    uint8_t *in_buffer;
    size_t in_buffer_size;
    uint8_t *out_buffer;
    size_t out_buffer_size;

#ifdef WITH_DOWNLOADER
    anjay_downloader_t downloader;
#endif // WITH_DOWNLOADER
};

#define ANJAY_DM_DEFAULT_PMIN_VALUE 1

#    define _anjay_sms_router(Anjay) NULL
#    define _anjay_local_msisdn(Anjay) NULL
#    define _anjay_sms_poll_socket(Anjay) NULL

uint8_t _anjay_make_error_response_code(int handler_result);

const avs_coap_tx_params_t *
_anjay_tx_params_for_conn_type(anjay_t *anjay,
                               anjay_connection_type_t conn_type);

int _anjay_bind_server_stream(anjay_t *anjay, anjay_connection_ref_t ref);

void _anjay_release_server_stream_without_scheduling_queue(anjay_t *anjay);

void _anjay_release_server_stream(anjay_t *anjay);

/**
 * @param anjay Pointer to the Anjay object, passed to scheduled jobs. Not
 *              dereferenced by the scheduler object.
 *
 * @returns Created scheduler object, or NULL if there is not enough memory.
 */
anjay_sched_t *_anjay_sched_new(anjay_t *anjay);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_CORE_H */
