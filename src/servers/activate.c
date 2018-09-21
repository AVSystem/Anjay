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

#include <anjay_config.h>

#include <avsystem/commons/errno.h>
#include <inttypes.h>

#include "../dm/query.h"
#include "../servers_utils.h"

#define ANJAY_SERVERS_INTERNALS

#include "activate.h"
#include "register_internal.h"
#include "server_connections.h"
#include "servers_internal.h"

VISIBILITY_SOURCE_BEGIN

typedef enum {
    IAS_SUCCESS = 0,
    IAS_FORBIDDEN,
    IAS_FAILED,
    IAS_CONNECTION_REFUSED,
    IAS_CONNECTION_ERROR
} initialize_active_server_result_t;

static int read_server_uri(anjay_t *anjay,
                           anjay_iid_t security_iid,
                           anjay_url_t *out_uri) {
    char raw_uri[ANJAY_MAX_URL_RAW_LENGTH];

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SERVER_URI);

    if (_anjay_dm_res_read_string(anjay, &path, raw_uri, sizeof(raw_uri))) {
        anjay_log(ERROR, "could not read LwM2M server URI");
        return -1;
    }

    anjay_url_t uri = ANJAY_URL_EMPTY;
    if (_anjay_parse_url(raw_uri, &uri)) {
        _anjay_url_cleanup(&uri);
        anjay_log(ERROR, "could not parse LwM2M server URI: %s", raw_uri);
        return -1;
    }
    if (!*uri.port) {
        switch (uri.protocol) {
        case ANJAY_URL_PROTOCOL_COAP:
            strcpy(uri.port, "5683");
            break;
        case ANJAY_URL_PROTOCOL_COAPS:
            strcpy(uri.port, "5684");
            break;
        }
    }
    *out_uri = uri;
    return 0;
}

static initialize_active_server_result_t
initialize_active_server(anjay_t *anjay, anjay_server_info_t *server) {
    if (anjay_is_offline(anjay)) {
        anjay_log(TRACE,
                  "Anjay is offline, not initializing server SSID %" PRIu16,
                  server->ssid);
        return IAS_FAILED;
    }

    assert(!_anjay_server_active(server));
    assert(server->ssid != ANJAY_SSID_ANY);
    initialize_active_server_result_t result = IAS_FAILED;
    anjay_iid_t security_iid;
    int refresh_result;
    if (_anjay_find_security_iid(anjay, server->ssid, &security_iid)) {
        anjay_log(ERROR, "could not find server Security IID");
    } else if (!read_server_uri(anjay, security_iid, &server->uri)) {
        if ((refresh_result = _anjay_active_server_refresh(anjay, server))) {
            anjay_log(TRACE, "could not initialize sockets for SSID %u",
                      server->ssid);
            if (refresh_result == EPROTO || refresh_result == ETIMEDOUT) {
                result = IAS_CONNECTION_ERROR;
            } else if (refresh_result == ECONNREFUSED) {
                result = IAS_CONNECTION_REFUSED;
            } else {
                result = IAS_FAILED;
            }
        } else if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
            switch (_anjay_server_ensure_valid_registration(anjay, server)) {
            case ANJAY_REGISTRATION_SUCCESS:
                result = IAS_SUCCESS;
                break;
            case ANJAY_REGISTRATION_FAILED:
                result = IAS_FAILED;
                break;
            case ANJAY_REGISTRATION_FORBIDDEN:
                result = IAS_FORBIDDEN;
                break;
            }
            if (result != IAS_SUCCESS) {
                anjay_log(ERROR,
                          "could not ensure registration to server SSID %u",
                          server->ssid);
            }
        } else {
            if (!_anjay_bootstrap_account_prepare(anjay)) {
                result = IAS_SUCCESS;
            } else {
                anjay_log(ERROR,
                          "could not prepare bootstrap account for SSID %u",
                          server->ssid);
            }
        }
    }

    if (result == IAS_SUCCESS) {
        server->reactivate_time = AVS_TIME_REAL_INVALID;
        server->reactivate_failed = false;
        server->num_icmp_failures = 0;
    }
    return result;
}

bool _anjay_can_retry_with_normal_server(anjay_t *anjay) {
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it) || it->ssid == ANJAY_SSID_BOOTSTRAP) {
            continue;
        }
        if (!it->reactivate_failed
                || it->num_icmp_failures < anjay->max_icmp_failures) {
            // there is hope for a successful non-bootstrap connection
            return true;
        }
    }
    return false;
}

bool _anjay_should_retry_bootstrap(anjay_t *anjay) {
#ifdef WITH_BOOTSTRAP
    if (anjay->bootstrap.in_progress) {
        // Bootstrap already in progress, no need to retry
        return false;
    }
    bool bootstrap_server_exists = false;
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, AVS_LIST_NEXT(anjay->servers->servers)) {
        if (it->ssid == ANJAY_SSID_BOOTSTRAP) {
            bootstrap_server_exists = true;
        } else if (_anjay_server_active(it)) {
            // Bootstrap Server is not the only active one
            return false;
        }
    }
    return bootstrap_server_exists
           && !_anjay_can_retry_with_normal_server(anjay);
#else  // WITH_BOOTSTRAP
    (void) anjay;
    return false;
#endif // WITH_BOOTSTRAP
}

/**
 * Checks whether all servers are inactive and have reached the limit of ICMP
 * failures (see the activation flow described in
 * _anjay_schedule_reload_servers() docs for details).
 */
bool anjay_all_connections_failed(anjay_t *anjay) {
    if (!anjay->servers->servers) {
        return false;
    }
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (_anjay_server_active(it)
                || it->num_icmp_failures < anjay->max_icmp_failures) {
            return false;
        }
    }
    return true;
}

static void activate_server_job(anjay_t *anjay, const void *ssid_ptr) {
    anjay_ssid_t ssid = *(const anjay_ssid_t *) ssid_ptr;

    AVS_LIST(anjay_server_info_t) *server_ptr =
            _anjay_servers_find_ptr(anjay->servers, ssid);

    if (!server_ptr || _anjay_server_active(*server_ptr)) {
        anjay_log(TRACE, "not an inactive server: SSID = %u", ssid);
        return;
    }

    initialize_active_server_result_t registration_result =
            initialize_active_server(anjay, *server_ptr);
    if (registration_result == IAS_SUCCESS) {
        return;
    }

    _anjay_server_clean_active_data(anjay, *server_ptr);
    (*server_ptr)->reactivate_failed = true;

    if (registration_result == IAS_CONNECTION_REFUSED) {
        ++(*server_ptr)->num_icmp_failures;
    } else if (registration_result == IAS_FORBIDDEN
               || registration_result == IAS_CONNECTION_ERROR) {
        (*server_ptr)->num_icmp_failures = anjay->max_icmp_failures;
    }

    if ((*server_ptr)->num_icmp_failures < anjay->max_icmp_failures) {
        // We had a failure with either a bootstrap or a non-bootstrap server,
        // retry till it's possible.
        if (_anjay_servers_schedule_next_retryable(anjay->sched, *server_ptr,
                                                   activate_server_job, ssid)) {
            anjay_log(ERROR,
                      "could not reschedule reactivate job for server SSID %u",
                      ssid);
        }
        return;
    }

    if (ssid == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(DEBUG, "Bootstrap Server could not be reached. "
                         "Disabling all communication.");
        // Abort any further bootstrap retries.
        _anjay_bootstrap_cleanup(anjay);
    } else if (_anjay_should_retry_bootstrap(anjay)) {
        if (_anjay_servers_find_active(anjay, ANJAY_SSID_BOOTSTRAP)) {
            _anjay_bootstrap_account_prepare(anjay);
        } else {
            anjay_enable_server(anjay, ANJAY_SSID_BOOTSTRAP);
        }
    } else {
        anjay_log(DEBUG,
                  "Non-Bootstrap Server %" PRIu16 " could not be reached.",
                  ssid);
    }
    // kill this job.
    (*server_ptr)->reactivate_time = AVS_TIME_REAL_INVALID;
}

int _anjay_server_sched_activate(anjay_t *anjay,
                                 anjay_server_info_t *server,
                                 avs_time_duration_t reactivate_delay) {
    // start the backoff procedure from the beginning
    assert(!_anjay_server_active(server));
    server->reactivate_time =
            avs_time_real_add(avs_time_real_now(), reactivate_delay);
    server->reactivate_failed = false;
    server->num_icmp_failures = 0;
    _anjay_sched_del(anjay->sched, &server->next_action_handle);
    if (_anjay_servers_schedule_first_retryable(
                anjay->sched, server, reactivate_delay, activate_server_job,
                server->ssid)) {
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
        if (_anjay_server_active(it) || !it->reactivate_failed
                || it->num_icmp_failures < anjay->max_icmp_failures) {
            continue;
        }
        int partial =
                _anjay_server_sched_activate(anjay, it, AVS_TIME_DURATION_ZERO);
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

    if (_anjay_server_active(*server_ptr)
            && !_anjay_server_registration_expired(*server_ptr)) {
        // Return value intentionally ignored.
        // There isn't much we can do in case it fails and De-Register is
        // optional anyway. _anjay_serve_deregister logs the error cause.
        _anjay_server_deregister(anjay, *server_ptr);
    }
    _anjay_server_clean_active_data(anjay, *server_ptr);
    (*server_ptr)->registration_info.expire_time = AVS_TIME_REAL_INVALID;
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
    new_server->reactivate_time = AVS_TIME_REAL_INVALID;
    return new_server;
}
