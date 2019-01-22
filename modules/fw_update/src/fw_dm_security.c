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

#include <anjay_config.h>

#include <inttypes.h>
#include <string.h>

#include <avsystem/commons/url.h>

#include <anjay/fw_update.h>

#include <anjay_modules/dm_utils.h>
#include <anjay_modules/servers.h>
#include <anjay_modules/utils_core.h>

VISIBILITY_SOURCE_BEGIN

#define fw_log(level, ...) _anjay_log(fw_update, level, __VA_ARGS__)

#define DEFAULT_COAPS_PORT "5684"

static bool url_service_matches(const avs_url_t *left, const avs_url_t *right) {
    if (strcmp(avs_url_protocol(left), avs_url_protocol(right)) != 0) {
        return false;
    }
    const char *port_left = avs_url_port(left);
    const char *port_right = avs_url_port(right);
    if (!port_left) {
        port_left = DEFAULT_COAPS_PORT;
    }
    if (!port_right) {
        port_right = DEFAULT_COAPS_PORT;
    }
    return strcmp(port_left, port_right) == 0;
}

typedef struct {
    avs_net_security_info_t security_info;
    anjay_server_dtls_keys_t dtls_keys;
} result_buffer_t;

typedef struct {
    avs_net_security_info_t *result;
    const avs_url_t *url;
} try_security_instance_args_t;

static int try_security_instance(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t security_iid,
                                 void *args_) {
    (void) obj;
    try_security_instance_args_t *args = (try_security_instance_args_t *) args_;

    char raw_server_url[ANJAY_MAX_URL_RAW_LENGTH];
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SERVER_URI);

    if (_anjay_dm_res_read_string(anjay, &path, raw_server_url,
                                  sizeof(raw_server_url))) {
        fw_log(WARNING,
               "could not read LwM2M server URI from /%" PRIu16 "/%" PRIu16
               "/%" PRIu16,
               path.oid, path.iid, path.rid);
        return ANJAY_FOREACH_CONTINUE;
    }

    avs_url_t *server_url = avs_url_parse(raw_server_url);
    if (!server_url) {
        fw_log(WARNING,
               "Could not parse URL from /%" PRIu16 "/%" PRIu16 "/%" PRIu16
               ": %s",
               path.oid, path.iid, path.rid, raw_server_url);
        return ANJAY_FOREACH_CONTINUE;
    }

    int retval = ANJAY_FOREACH_CONTINUE;
    if (strcmp(avs_url_host(server_url), avs_url_host(args->url)) == 0) {
        bool service_matches = url_service_matches(server_url, args->url);
        if (!args->result || service_matches) {
            result_buffer_t *new_result =
                    (result_buffer_t *) avs_calloc(1, sizeof(result_buffer_t));
            int get_result = _anjay_get_security_info(
                    anjay, &new_result->security_info, &new_result->dtls_keys,
                    security_iid, ANJAY_CONNECTION_UDP);
            if (get_result) {
                fw_log(WARNING,
                       "Could not read security information for server "
                       "/%" PRIu16 "/%" PRIu16,
                       ANJAY_DM_OID_SECURITY, security_iid);
            } else if (!new_result->dtls_keys.pk_or_identity_size
                       && !new_result->dtls_keys.server_pk_or_identity_size
                       && !new_result->dtls_keys.secret_key_size) {
                fw_log(DEBUG,
                       "Server /%" PRIu16 "/%" PRIu16
                       " does not use encrypted connection, ignoring",
                       ANJAY_DM_OID_SECURITY, security_iid);
            } else {
                avs_free(args->result);
                AVS_STATIC_ASSERT(offsetof(result_buffer_t, security_info) == 0,
                                  result_buffer_security_info_offset);
                args->result = &new_result->security_info;
                new_result = NULL;
                if (service_matches) {
                    retval = ANJAY_FOREACH_BREAK;
                }
            }
            avs_free(new_result);
        }
    }

    avs_url_free(server_url);
    return retval;
}

avs_net_security_info_t *
anjay_fw_update_load_security_from_dm(anjay_t *anjay, const char *raw_url) {
    assert(anjay);

    const anjay_dm_object_def_t *const *security_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!security_obj) {
        fw_log(ERROR, "Security object not installed");
        return NULL;
    }

    avs_url_t *url = avs_url_parse(raw_url);
    if (!url) {
        fw_log(ERROR, "Could not parse URL: %s", raw_url);
        return NULL;
    }

    try_security_instance_args_t args = {
        .result = NULL,
        .url = url
    };
    _anjay_dm_foreach_instance(anjay, security_obj, try_security_instance,
                               &args);
    avs_url_free(url);

    if (!args.result) {
        fw_log(WARNING,
               "Matching security information not found in data model for URL: "
               "%s",
               raw_url);
    }
    return args.result;
}
