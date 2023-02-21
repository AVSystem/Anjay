/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H
#define ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H

#include <anjay/core.h>

#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    ANJAY_CONNECTION_UNSET = -1,
    ANJAY_CONNECTION_PRIMARY = 0,
    ANJAY_CONNECTION_LIMIT_
} anjay_connection_type_t;

#define ANJAY_CONNECTION_TYPE_FOREACH(Var)                                     \
    for ((Var) = (anjay_connection_type_t) 0; (Var) < ANJAY_CONNECTION_LIMIT_; \
         (Var) = (anjay_connection_type_t) ((Var) + 1))

// inactive servers include administratively disabled ones
// as well as those which were unreachable at connect attempt
struct anjay_server_info_struct;
typedef struct anjay_server_info_struct anjay_server_info_t;

typedef struct {
    anjay_server_info_t *server;
    anjay_connection_type_t conn_type;
} anjay_connection_ref_t;

avs_coap_ctx_t *_anjay_connection_get_coap(anjay_connection_ref_t ref);

/**
 * Reads security information (security mode, keys etc.) for a given Security
 * object instance. This is part of the servers subsystem because it reuses some
 * private code that is also used when refreshing server connections - namely,
 * connection_type_definition_t instances that query the data model for security
 * information, abstracting away the fact that UDP/TCP and SMS security
 * information is stored in different resources.
 *
 * It's currently only used in the Firmware Update module, to allow deriving the
 * security information from the data model when it's not explicitly specified.
 */
avs_error_t _anjay_get_security_config(anjay_unlocked_t *anjay,
                                       anjay_security_config_t *out_config,
                                       anjay_security_config_cache_t *cache,
                                       anjay_ssid_t ssid,
                                       anjay_iid_t security_iid);

#if defined(ANJAY_WITH_LWM2M11)
bool _anjay_bootstrap_server_exists(anjay_unlocked_t *anjay);
#endif // defined(ANJAY_WITH_LWM2M11) || defined(ANJAY_WITH_EST)

/**
 * Returns an active server object associated with given @p socket .
 */
anjay_server_info_t *
_anjay_servers_find_by_primary_socket(anjay_unlocked_t *anjay,
                                      avs_net_socket_t *socket);

/**
 * Reschedules Update for a specified server or all servers. In the very end,
 * it calls schedule_update(), which basically speeds up the scheduled Update
 * operation (it is normally scheduled for "just before the lifetime expires",
 * this function reschedules it to now. The scheduled job is
 * server_next_action_job() with the action set to
 * ANJAY_SERVER_NEXT_ACTION_REFRESH action, and it is also used for regular
 * Updates.
 *
 * Aside from being a public API, this is also called in:
 *
 * - anjay_register_object() and anjay_unregister_object(), to force an Update
 *   when the set of available Objects changed
 * - serv_execute(), as a default implementation of Registration Update Trigger
 * - server_modified_notify(), to force an Update whenever Lifetime or Binding
 *   change
 * - _anjay_schedule_reregister(), although that's probably rather superfluous -
 *   see the docs of that function for details
 */
int _anjay_schedule_registration_update_unlocked(anjay_unlocked_t *anjay,
                                                 anjay_ssid_t ssid);

/**
 * Basically the same as anjay_disable_server(), but with explicit timeout value
 * instead of reading it from the data model.
 *
 * Aside from being a public API, it is called from:
 *
 * - bootstrap_finish_impl(), to deactivate the Bootstrap Server connection if
 *   legacy Server-Initiated Bootstrap is disabled
 * - serv_execute(), as a reference implementation of the Disable resource
 * - _anjay_schedule_socket_update(), to force reconnection of all sockets
 */
int _anjay_schedule_disable_server_with_explicit_timeout_unlocked(
        anjay_unlocked_t *anjay,
        anjay_ssid_t ssid,
        avs_time_duration_t timeout);

/**
 * Schedules server activation immediately, after some sanity checks.
 *
 * The activation request is rejected if someone tries to enable the Bootstrap
 * Server, Client-Initiated Bootstrap is not supposed to be performed, and
 * legacy Server-Initiated Bootstrap is administratively disabled.
 */
int _anjay_enable_server_unlocked(anjay_unlocked_t *anjay, anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_SERVERS_H */
