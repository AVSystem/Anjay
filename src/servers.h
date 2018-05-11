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

#ifndef ANJAY_SERVERS_H
#define ANJAY_SERVERS_H

#include <anjay/core.h>

#include <anjay_modules/sched.h>
#include <anjay_modules/servers.h>

#include "utils_core.h"
#include "coap/coap_stream.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

// copied from avs_commons/git/http/src/headers.h
// see description there for rationale; TODO: move this to public Commons API?
// note that this _includes_ the terminating null byte
#define ANJAY_UINT_STR_BUF_SIZE(type) ((12*sizeof(type))/5 + 2)

// 6.2.2 Object Version format:
// "The Object Version of an Object is composed of 2 digits separated by a dot"
// However, we're a bit lenient to support proper numbers and not just digits.
#define ANJAY_DM_OBJECT_VERSION_BUF_LENGTH \
        (2 * ANJAY_UINT_STR_BUF_SIZE(unsigned))

typedef struct {
    anjay_oid_t oid;
    char version[ANJAY_DM_OBJECT_VERSION_BUF_LENGTH];
    AVS_LIST(anjay_iid_t) instances;
} anjay_dm_cache_object_t;

typedef struct {
    int64_t lifetime_s;
    AVS_LIST(anjay_dm_cache_object_t) dm;
    anjay_binding_mode_t binding_mode;
} anjay_update_parameters_t;

typedef struct {
    AVS_LIST(const anjay_string_t) endpoint_path;
    anjay_connection_type_t conn_type;
    avs_time_real_t expire_time;
    bool needs_update;
    anjay_update_parameters_t last_update_params;
} anjay_registration_info_t;

typedef enum {
    ANJAY_CONNECTION_DISABLED,
    ANJAY_CONNECTION_ONLINE,
    ANJAY_CONNECTION_QUEUE
} anjay_server_connection_mode_t;

struct anjay_server_info_struct;
typedef struct anjay_server_info_struct anjay_server_info_t;

struct anjay_servers_struct;
typedef struct anjay_servers_struct anjay_servers_t;

typedef struct {
    anjay_ssid_t ssid;
    anjay_connection_type_t type;
} anjay_connection_key_t;

typedef struct {
    anjay_server_info_t *server;
    anjay_connection_type_t conn_type;
} anjay_connection_ref_t;

anjay_servers_t *_anjay_servers_create(void);

/** Deregisters from every active server. */
void _anjay_servers_deregister(anjay_t *anjay);
/**
 * Clears up the <c>servers</c> struct, and releases any allocated resources.
 */
void _anjay_servers_cleanup(anjay_t *anjay);

void _anjay_servers_cleanup_inactive(anjay_t *anjay);

typedef int anjay_servers_foreach_ssid_handler_t(anjay_t *anjay,
                                                 anjay_ssid_t ssid,
                                                 void *data);

int _anjay_servers_foreach_ssid(anjay_t *anjay,
                                anjay_servers_foreach_ssid_handler_t *handler,
                                void *data);

typedef int anjay_servers_foreach_handler_t(anjay_t *anjay,
                                            anjay_server_info_t *server,
                                            void *data);

// inactive servers include administratively disabled ones
// as well as those which were unreachable at connect attempt
int _anjay_servers_foreach_active(anjay_t *anjay,
                                  anjay_servers_foreach_handler_t *handler,
                                  void *data);

anjay_ssid_t _anjay_server_ssid(anjay_server_info_t *server);

anjay_connection_type_t
_anjay_server_registration_conn_type(anjay_server_info_t *server);

const anjay_registration_info_t *
_anjay_server_registration_info(anjay_server_info_t *server);

// Note: if any of the move_* parameters are NULL,
// the relevant fields are not updated
void _anjay_server_update_registration_info(
        anjay_server_info_t *server,
        AVS_LIST(const anjay_string_t) *move_endpoint_path,
        anjay_update_parameters_t *move_params);

void _anjay_server_require_reload(anjay_server_info_t *server);

const anjay_url_t *_anjay_server_uri(anjay_server_info_t *server);

size_t _anjay_servers_count_non_bootstrap(anjay_t *anjay);

/**
 * Returns an active server object associated with given @p socket .
 */
anjay_server_info_t *
_anjay_servers_find_by_udp_socket(anjay_servers_t *servers,
                                  avs_net_abstract_socket_t *socket);

/**
 * Returns a server object for given SSID.
 *
 * NOTE: the bootstrap server is identified by the ANJAY_SSID_BOOTSTRAP
 * constant instead of its actual SSID.
 */
anjay_server_info_t *_anjay_servers_find_active(anjay_servers_t *servers,
                                                anjay_ssid_t ssid);

int _anjay_schedule_reload_servers(anjay_t *anjay);

int _anjay_schedule_socket_update(anjay_t *anjay,
                                  anjay_iid_t security_iid);

#ifdef WITH_BOOTSTRAP
/**
 * Returns true if the client has successfully registered to any non-bootstrap
 * server and its registration has not yet expired.
 */
bool _anjay_servers_is_connected_to_non_bootstrap(anjay_servers_t *servers);
#endif

int _anjay_schedule_reregister(anjay_t *anjay, anjay_server_info_t *server);

int _anjay_schedule_server_reconnect(anjay_t *anjay,
                                     anjay_server_info_t *server);

anjay_binding_mode_t
_anjay_server_cached_binding_mode(anjay_server_info_t *server);

anjay_server_connection_mode_t
_anjay_connection_current_mode(anjay_connection_ref_t ref);

void
_anjay_connection_schedule_queue_mode_close(anjay_t *anjay,
                                            anjay_connection_ref_t ref);

int _anjay_server_setup_registration_connection(anjay_server_info_t *server);

avs_net_abstract_socket_t *
_anjay_connection_get_online_socket(anjay_connection_ref_t ref);

int _anjay_connection_bring_online(anjay_connection_ref_t ref,
                                   bool *out_session_resumed);

void _anjay_connection_suspend(anjay_connection_ref_t conn_ref);


VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_H
