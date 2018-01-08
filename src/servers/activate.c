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

#include "../dm/query.h"

#define ANJAY_SERVERS_INTERNALS

#include "servers_internal.h"
#include "activate.h"
#include "connection_info.h"
#include "register_internal.h"

VISIBILITY_SOURCE_BEGIN

static void active_server_detach_delete(
        anjay_t *anjay,
        AVS_LIST(anjay_active_server_info_t) *server_ptr) {
    _anjay_server_cleanup(anjay, *server_ptr);
    AVS_LIST_DELETE(server_ptr);
}

static bool is_valid_coap_uri(const anjay_url_t *uri) {
    if (strcmp(uri->protocol, "coap") && strcmp(uri->protocol, "coaps")) {
        anjay_log(ERROR, "unsupported protocol: %s", uri->protocol);
        return false;
    }
    return true;
}

static int get_server_uri(anjay_t *anjay,
                          anjay_iid_t security_iid,
                          anjay_url_t *out_uri) {
    enum { MAX_SERVER_URI_LENGTH = 256 };
    char raw_uri[MAX_SERVER_URI_LENGTH];

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SERVER_URI);

    if (_anjay_dm_res_read_string(anjay, &path, raw_uri, sizeof(raw_uri))) {
        anjay_log(ERROR, "could not read LwM2M server URI");
        return -1;
    }

    anjay_url_t uri = ANJAY_URL_EMPTY;
    if (_anjay_parse_url(raw_uri, &uri) || !is_valid_coap_uri(&uri)) {
        _anjay_url_cleanup(&uri);
        anjay_log(ERROR, "could not parse LwM2M server URI: %s", raw_uri);
        return -1;
    }
    if (!*uri.port) {
        if (uri.protocol[4]) { // coap_s_
            strcpy(uri.port, "5684");
        } else { // coap_\0_
            strcpy(uri.port, "5683");
        }
    }
    *out_uri = uri;
    return 0;
}

static int
initialize_active_server(anjay_t *anjay,
                         anjay_ssid_t ssid,
                         anjay_active_server_info_t *server) {
    if (anjay_is_offline(anjay)) {
        anjay_log(TRACE,
                  "Anjay is offline, not initializing server SSID %" PRIu16,
                  ssid);
        return -1;
    }

    server->ssid = ssid;
    anjay_iid_t security_iid;
    if (_anjay_find_security_iid(anjay, ssid, &security_iid)) {
        anjay_log(ERROR, "could not find server Security IID");
        return -1;
    }
    if (get_server_uri(anjay, security_iid, &server->uri)) {
        return -1;
    }
    if (_anjay_server_refresh(anjay, server, false)) {
        anjay_log(TRACE, "could not initialize sockets for SSID %u",
                  server->ssid);
        return -1;
    }

    if (server->ssid != ANJAY_SSID_BOOTSTRAP) {
        if (_anjay_server_register(anjay, server)) {
            anjay_log(ERROR, "could not register to server SSID %u",
                      server->ssid);
            return -1;
        }
    } else {
        if (_anjay_bootstrap_account_prepare(anjay)) {
            anjay_log(ERROR, "could not prepare bootstrap account for SSID %u",
                      server->ssid);
            return -1;
        }
    }

    return 0;
}

static AVS_LIST(anjay_active_server_info_t)
create_active_server_from_ssid(anjay_t *anjay,
                               anjay_ssid_t ssid) {
    AVS_LIST(anjay_active_server_info_t) server =
            AVS_LIST_NEW_ELEMENT(anjay_active_server_info_t);

    if (!server) {
        anjay_log(ERROR, "out of memory");
        return NULL;
    }

    if (initialize_active_server(anjay, ssid, server)) {
        active_server_detach_delete(anjay, &server);
        return NULL;
    }

    return server;
}

static bool should_retry_bootstrap(anjay_t *anjay) {
    if (anjay->bootstrap.in_progress) {
        // Bootstrap already in progress, no need to retry
        return false;
    }
    if (!anjay->servers.active
            || AVS_LIST_NEXT(anjay->servers.active)
            || anjay->servers.active->ssid != ANJAY_SSID_BOOTSTRAP) {
        // Bootstrap Server not present or is not the only active one
        return false;
    }
    AVS_LIST(anjay_inactive_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers.inactive) {
        if (!it->reactivate_failed) {
            // there is hope for a successful non-bootstrap connection
            return false;
        }
    }
    return true;
}

static int activate_server_job(anjay_t *anjay, void *ssid_) {
    anjay_ssid_t ssid = (anjay_ssid_t) (uintptr_t) ssid_;

    AVS_LIST(anjay_inactive_server_info_t) *inactive_server_ptr =
            _anjay_servers_find_inactive_ptr(&anjay->servers, ssid);

    if (!inactive_server_ptr) {
        anjay_log(TRACE, "not an inactive server: SSID = %u", ssid);
        return 0;
    }
    assert(*inactive_server_ptr && "_anjay_servers_find_inactive_ptr broken");

    AVS_LIST(anjay_active_server_info_t) new_server =
            create_active_server_from_ssid(anjay, ssid);
    if (!new_server) {
        (*inactive_server_ptr)->reactivate_failed = true;
        if (ssid != ANJAY_SSID_BOOTSTRAP && should_retry_bootstrap(anjay)) {
            _anjay_bootstrap_account_prepare(anjay);
        }
        return -1;
    }
    /**
     * No need to remove job handle as we return 0 and scheduler will do it
     * for us (this is retryable job).
     */
    if (*inactive_server_ptr) {
        // might have been removed by start_bootstrap_if_not_already_started()
        AVS_LIST_DELETE(inactive_server_ptr);
    }
    _anjay_servers_add_active(&anjay->servers, new_server);
    return 0;
}

static int sched_reactivate_server(anjay_t *anjay,
                                   anjay_inactive_server_info_t *server,
                                   avs_time_duration_t reactivate_delay) {
    _anjay_sched_del(anjay->sched, &server->sched_reactivate_handle);
    if (_anjay_sched_retryable(anjay->sched, &server->sched_reactivate_handle,
                               reactivate_delay, ANJAY_SERVER_RETRYABLE_BACKOFF,
                               activate_server_job,
                               (void *) (uintptr_t) server->ssid)) {
        anjay_log(TRACE, "could not schedule reactivate job for server SSID %u",
                  server->ssid);
        return -1;
    }
    return 0;
}

int _anjay_server_sched_activate(anjay_t *anjay,
                                 anjay_servers_t *servers,
                                 anjay_ssid_t ssid,
                                 avs_time_duration_t delay) {
    AVS_LIST(anjay_inactive_server_info_t) *inactive_server_ptr =
            _anjay_servers_find_inactive_ptr(servers, ssid);

    if (!inactive_server_ptr) {
        anjay_log(TRACE, "not an inactive server: SSID = %u", ssid);
        return -1;
    }
    assert(*inactive_server_ptr && "_anjay_servers_find_inactive_ptr broken");

    return sched_reactivate_server(anjay, *inactive_server_ptr, delay);
}

void _anjay_servers_add_active(anjay_servers_t *servers,
                               AVS_LIST(anjay_active_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);
    assert(!_anjay_servers_find_inactive_ptr(servers, server->ssid)
           && "attempting to insert an active server while an inactive one"
              " with the same SSID already exists");

    AVS_LIST(anjay_active_server_info_t) *insert_ptr =
            _anjay_servers_find_active_insert_ptr(servers, server->ssid);

    assert(insert_ptr);
    assert((!*insert_ptr || (*insert_ptr)->ssid != server->ssid)
           && "attempting to insert a duplicate of an already existing active"
              " server entry");

    AVS_LIST_INSERT(insert_ptr, server);
}

anjay_inactive_server_info_t *
_anjay_server_deactivate(anjay_t *anjay,
                         anjay_servers_t *servers,
                         anjay_ssid_t ssid,
                         avs_time_duration_t reactivate_delay) {
    if (ssid == ANJAY_SSID_BOOTSTRAP) {
        anjay_log(ERROR, "cannot deactivate Bootstrap Server");
        return NULL;
    }

    AVS_LIST(anjay_active_server_info_t) *active_server_ptr =
            _anjay_servers_find_active_ptr(servers, ssid);

    if (!active_server_ptr) {
        anjay_log(TRACE, "not an active server: SSID = %u", ssid);
        return NULL;
    }
    assert(*active_server_ptr && "_anjay_servers_find_active_ptr broken");

    AVS_LIST(anjay_inactive_server_info_t) new_server =
            _anjay_servers_create_inactive(ssid);
    if (!new_server) {
        return NULL;
    }

    if (sched_reactivate_server(anjay, new_server, reactivate_delay)) {
        AVS_LIST_CLEAR(&new_server);
        return NULL;
    }

    // Return value intentionally ignored.
    // There isn't much we can do in case it fails and De-Register is optional
    // anyway. _anjay_serve_deregister logs the error cause.
    _anjay_server_deregister(anjay, *active_server_ptr);
    active_server_detach_delete(anjay, active_server_ptr);

    _anjay_servers_add_inactive(servers, new_server);
    return new_server;
}

AVS_LIST(anjay_inactive_server_info_t)
_anjay_servers_create_inactive(anjay_ssid_t ssid) {
    AVS_LIST(anjay_inactive_server_info_t) new_server =
            AVS_LIST_NEW_ELEMENT(anjay_inactive_server_info_t);
    if (!new_server) {
        anjay_log(ERROR, "out of memory");
        return NULL;
    }

    new_server->ssid = ssid;
    return new_server;
}

void _anjay_servers_add_inactive(anjay_servers_t *servers,
                                 AVS_LIST(anjay_inactive_server_info_t) server) {
    assert(AVS_LIST_SIZE(server) == 1);
    assert(!_anjay_servers_find_active_ptr(servers, server->ssid)
           && "attempting to insert an inactive server while an active one with"
              " the same SSID already exists");

    AVS_LIST(anjay_inactive_server_info_t) *insert_ptr =
            _anjay_servers_find_inactive_insert_ptr(servers, server->ssid);

    assert(insert_ptr);
    assert((!*insert_ptr || (*insert_ptr)->ssid != server->ssid)
           && "attempting to insert a duplicate of an already existing inactive"
              " server entry");

    AVS_LIST_INSERT(insert_ptr, server);
}
