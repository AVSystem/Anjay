/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_ANJAY_H
#define	ANJAY_ANJAY_H

#include <avsystem/commons/list.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/net.h>

#include "dm.h"
#include "observe.h"
#include "sched.h"

#include "servers.h"
#include "utils.h"
#include "interface/bootstrap.h"

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
    anjay_servers_t servers;
#ifdef WITH_OBSERVE
    anjay_observe_state_t observe;
#endif
#ifdef WITH_BOOTSTRAP
    anjay_bootstrap_t bootstrap;
#endif
    avs_stream_abstract_t *comm_stream;
    anjay_scheduled_notify_t scheduled_notify;

    const char *endpoint_name;
    anjay_transaction_state_t transaction_state;
};

#define ANJAY_DM_DEFAULT_PMIN_VALUE 1

#define _anjay_sms_router(Anjay) NULL
#define _anjay_local_msisdn(Anjay) NULL
#define _anjay_sms_poll_socket(Anjay) NULL

uint8_t _anjay_make_error_response_code(int handler_result);

anjay_server_connection_t *
_anjay_get_server_connection(anjay_connection_ref_t ref);

anjay_connection_type_t
_anjay_get_default_connection_type(anjay_active_server_info_t *server);

avs_stream_abstract_t *_anjay_get_server_stream(anjay_t *anjay,
                                                anjay_connection_ref_t ref);

void _anjay_release_server_stream_without_scheduling_queue(anjay_t *anjay);

void _anjay_release_server_stream(anjay_t *anjay,
                                  anjay_connection_ref_t connection);

size_t _anjay_num_non_bootstrap_servers(anjay_t *anjay);

VISIBILITY_PRIVATE_HEADER_END

#endif	/* ANJAY_ANJAY_H */

