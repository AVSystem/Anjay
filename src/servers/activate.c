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

#include <config.h>

#include <inttypes.h>
#include <avsystem/commons/errno.h>

#include "../dm/query.h"

#define ANJAY_SERVERS_INTERNALS

#include "servers_internal.h"
#include "activate.h"
#include "connection_info.h"
#include "register_internal.h"

VISIBILITY_SOURCE_BEGIN

static int initialize_active_server(anjay_t *anjay,
                                    anjay_server_info_t *server) {
    if (anjay_is_offline(anjay)) {
        anjay_log(TRACE,
                  "Anjay is offline, not initializing server SSID %" PRIu16,
                  server->ssid);
        return -1;
    }

    assert(!_anjay_server_active(server));
    assert(server->ssid != ANJAY_SSID_ANY);
    int result = -1;
    anjay_iid_t security_iid;
    if (_anjay_find_security_iid(anjay, server->ssid, &security_iid)) {
        anjay_log(ERROR, "could not find server Security IID");
        goto finish;
    }
    if (_anjay_server_get_uri(anjay, security_iid, &server->data_active.uri)) {
        goto finish;
    }

    if ((result = _anjay_active_server_refresh(anjay, server, false))) {
        anjay_log(TRACE, "could not initialize sockets for SSID %u",
                  server->ssid);
        goto finish;
    }
    if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
        if ((result = _anjay_server_ensure_valid_registration(anjay, server))) {
            anjay_log(ERROR, "could not ensure registration to server SSID %u",
                      server->ssid);
            goto finish;
        }
    } else if ((result = _anjay_bootstrap_account_prepare(anjay))) {
        anjay_log(ERROR, "could not prepare bootstrap account for SSID %u",
                  server->ssid);
        goto finish;
    }

    result = 0;
finish:
    if (!result) {
        server->data_inactive.reactivate_failed = false;
        server->data_inactive.num_icmp_failures = 0;
    }
    return result;
}

bool _anjay_can_retry_with_normal_server(anjay_t *anjay) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it) || it->ssid == ANJAY_SSID_BOOTSTRAP) {
            continue;
        }
        if (!it->data_inactive.reactivate_failed
                || it->data_inactive.num_icmp_failures
                        < anjay->max_icmp_failures) {
            // there is hope for a successful non-bootstrap connection
            return true;
        }
    }
    return false;
}

#ifdef WITH_BOOTSTRAP
static bool should_retry_bootstrap(anjay_t *anjay) {
    if (anjay->bootstrap.in_progress) {
        // Bootstrap already in progress, no need to retry
        return false;
    }
    bool bootstrap_server_found = false;
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, AVS_LIST_NEXT(anjay->servers->servers)) {
        if (_anjay_server_active(it)) {
            if (it->ssid == ANJAY_SSID_BOOTSTRAP) {
                bootstrap_server_found = true;
            } else {
                // Bootstrap Server is not the only active one
                return false;
            }
        }
    }
    return bootstrap_server_found
            && !_anjay_can_retry_with_normal_server(anjay);
}
#else // WITH_BOOTSTRAP
# define should_retry_bootstrap(...) false
#endif // WITH_BOOTSTRAP

bool anjay_all_connections_failed(anjay_t *anjay) {
    if (!anjay->servers->servers) {
        return false;
    }
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it)
                || it->data_inactive.num_icmp_failures
                        < anjay->max_icmp_failures) {
            return false;
        }
    }
    return true;
}

static anjay_sched_retryable_result_t
activate_server_job(anjay_t *anjay, void *ssid_) {
    anjay_ssid_t ssid = (anjay_ssid_t) (uintptr_t) ssid_;

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);

    if (!server_ptr || _anjay_server_active(*server_ptr)) {
        anjay_log(TRACE, "not an inactive server: SSID = %u", ssid);
        return ANJAY_SCHED_FINISH;
    }

    int socket_error = 0;
    if (!(socket_error = initialize_active_server(anjay,
                                                  *server_ptr))) {
        return ANJAY_SCHED_FINISH;
    }

    _anjay_server_cleanup(anjay, *server_ptr);
    (*server_ptr)->data_active.needs_reload = false;
    (*server_ptr)->data_inactive.reactivate_failed = true;
    uint32_t *num_icmp_failures =
            &(*server_ptr)->data_inactive.num_icmp_failures;

    if (socket_error == ECONNREFUSED) {
        ++*num_icmp_failures;
    } else if (socket_error == ANJAY_ERR_FORBIDDEN
                    || socket_error == ETIMEDOUT
                    || socket_error == EPROTO) {
        *num_icmp_failures = anjay->max_icmp_failures;
    }

    if (*num_icmp_failures >= anjay->max_icmp_failures) {
        if (ssid == ANJAY_SSID_BOOTSTRAP) {
            anjay_log(DEBUG, "Bootstrap Server could not be reached. "
                             "Disabling all communication.");
            // Abort any further bootstrap retries.
            _anjay_bootstrap_cleanup(anjay);
        } else {
            if (_anjay_dm_ssid_exists(anjay, ANJAY_SSID_BOOTSTRAP)) {
                if (should_retry_bootstrap(anjay)) {
                    _anjay_bootstrap_account_prepare(anjay);
                }
            } else {
                anjay_log(DEBUG,
                          "Non-Bootstrap Server %" PRIu16
                          " could not be reached.",
                          ssid);
            }
        }
        // kill this job.
        return ANJAY_SCHED_FINISH;
    }
    // We had a failure with either a bootstrap or a non-bootstrap server,
    // retry till it's possible.
    return ANJAY_SCHED_RETRY;
}

int _anjay_server_sched_activate(anjay_t *anjay,
                                 anjay_server_info_t *server,
                                 avs_time_duration_t reactivate_delay) {
    // start the backoff procedure from the beginning
    assert(!_anjay_server_active(server));
    server->data_inactive.reactivate_failed = false;
    server->data_inactive.num_icmp_failures = 0;
    _anjay_sched_del(anjay->sched, &server->sched_update_or_reactivate_handle);
    if (_anjay_sched_retryable(anjay->sched,
                               &server->sched_update_or_reactivate_handle,
                               reactivate_delay, ANJAY_SERVER_RETRYABLE_BACKOFF,
                               activate_server_job,
                               (void *) (uintptr_t) server->ssid)) {
        anjay_log(TRACE, "could not schedule reactivate job for server SSID %u",
                  server->ssid);
        return -1;
    }
    return 0;
}

int _anjay_servers_sched_reactivate_all_given_up(anjay_t *anjay) {
    int result = 0;

    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it)
                || !it->data_inactive.reactivate_failed
                || it->data_inactive.num_icmp_failures
                        < anjay->max_icmp_failures) {
            continue;
        }
        int partial = _anjay_server_sched_activate(anjay, it,
                                                   AVS_TIME_DURATION_ZERO);
        if (!result) {
            result = partial;
        }
    }

    return result;
}

void _anjay_servers_add(anjay_servers_t *servers,
                        AVS_LIST(anjay_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);
    AVS_LIST(anjay_server_info_t) *insert_ptr =
            _anjay_servers_find_insert_ptr(servers, server->ssid);

    assert(insert_ptr);
    AVS_ASSERT((!*insert_ptr || (*insert_ptr)->ssid != server->ssid),
               "attempting to insert a duplicate of an already existing server "
               "entry");

    AVS_LIST_INSERT(insert_ptr, server);
}

int _anjay_server_deactivate(anjay_t *anjay,
                             anjay_ssid_t ssid,
                             avs_time_duration_t reactivate_delay) {
    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);
    if (!server_ptr) {
        anjay_log(ERROR, "SSID %" PRIu16 " is not a known server", ssid);
        return -1;
    }

    if (_anjay_server_active(*server_ptr)) {
        // Return value intentionally ignored.
        // There isn't much we can do in case it fails and De-Register is
        // optional anyway. _anjay_serve_deregister logs the error cause.
        _anjay_server_deregister(anjay, *server_ptr);
    }
    _anjay_server_cleanup(anjay, *server_ptr);
    // we don't do the following in _anjay_server_cleanup() so that conn_type
    // can be reused after restoring from persistence in activate_server_job()
    (*server_ptr)->data_active.registration_info.conn_type =
            ANJAY_CONNECTION_UNSET;
    if (avs_time_duration_valid(reactivate_delay)
            && _anjay_server_sched_activate(anjay, *server_ptr,
                                            reactivate_delay)) {
        // not much we can do other than removing the server altogether
        anjay_log(ERROR, "could not reschedule server reactivation");
        AVS_LIST_DELETE(server_ptr);
        return -1;
    }
    return 0;
}

AVS_LIST(anjay_server_info_t)
_anjay_servers_create_inactive(anjay_ssid_t ssid) {
    AVS_LIST(anjay_server_info_t) new_server =
            AVS_LIST_NEW_ELEMENT(anjay_server_info_t);
    if (!new_server) {
        anjay_log(ERROR, "out of memory");
        return NULL;
    }

    new_server->ssid = ssid;
    new_server->data_active.registration_info.conn_type =
            ANJAY_CONNECTION_UNSET;
    return new_server;
}
