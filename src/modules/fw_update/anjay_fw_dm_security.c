/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_FW_UPDATE

#    include <inttypes.h>
#    include <string.h>

#    include <avsystem/commons/avs_url.h>

#    include <anjay/fw_update.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_servers.h>
#    include <anjay_modules/anjay_utils_core.h>

VISIBILITY_SOURCE_BEGIN

#    define fw_log(level, ...) _anjay_log(fw_update, level, __VA_ARGS__)

#    define DEFAULT_COAPS_PORT "5684"

static bool url_service_matches(const avs_url_t *left, const avs_url_t *right) {
    const char *protocol_left = avs_url_protocol(left);
    const char *protocol_right = avs_url_protocol(right);
    // NULL protocol means that the URL is protocol-relative (e.g.
    // //avsystem.com). In that case protocol is essentially undefined (i.e.,
    // dependent on where such link is contained). We don't consider two
    // undefined protocols as equivalent, similar to comparing NaNs.
    if (!protocol_left || !protocol_right
            || strcmp(protocol_left, protocol_right) != 0) {
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

static bool has_valid_keys(const avs_net_security_info_t *info) {
    switch (info->mode) {
    case AVS_NET_SECURITY_CERTIFICATE:
        return info->data.cert.server_cert_validation
               || info->data.cert.client_cert.desc.info.buffer.buffer_size > 0
               || info->data.cert.client_key.desc.info.buffer.buffer_size > 0;
    case AVS_NET_SECURITY_PSK:
        return info->data.psk.identity_size > 0 || info->data.psk.psk_size > 0;
    }
    return false;
}

typedef struct {
    anjay_security_config_t *result;
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

    if (_anjay_dm_read_resource_string(anjay, &path, raw_server_url,
                                       sizeof(raw_server_url))) {
        fw_log(WARNING, _("could not read LwM2M server URI from ") "%s",
               ANJAY_DEBUG_MAKE_PATH(&path));
        return ANJAY_FOREACH_CONTINUE;
    }

    avs_url_t *server_url = avs_url_parse_lenient(raw_server_url);
    if (!server_url) {
        fw_log(WARNING, _("Could not parse URL from ") "%s" _(": ") "%s",
               ANJAY_DEBUG_MAKE_PATH(&path), raw_server_url);
        return ANJAY_FOREACH_CONTINUE;
    }

    int retval = ANJAY_FOREACH_CONTINUE;
    if (avs_url_host(server_url)
            && strcmp(avs_url_host(server_url), avs_url_host(args->url)) == 0) {
        bool service_matches = url_service_matches(server_url, args->url);
        if (!args->result || service_matches) {
            anjay_security_config_t *new_result =
                    _anjay_get_security_config(anjay, security_iid);
            if (!new_result) {
                fw_log(WARNING,
                       _("Could not read security information for "
                         "server ") "/%" PRIu16 "/%" PRIu16,
                       ANJAY_DM_OID_SECURITY, security_iid);
            } else if (!has_valid_keys(&new_result->security_info)) {
                fw_log(DEBUG,
                       _("Server ") "/%" PRIu16 "/%" PRIu16 _(
                               " does not use encrypted connection, ignoring"),
                       ANJAY_DM_OID_SECURITY, security_iid);
            } else {
                avs_free(args->result);
                args->result = new_result;
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

anjay_security_config_t *
anjay_fw_update_load_security_from_dm(anjay_t *anjay, const char *raw_url) {
    assert(anjay);

    const anjay_dm_object_def_t *const *security_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!security_obj) {
        fw_log(ERROR, _("Security object not installed"));
        return NULL;
    }

    avs_url_t *url = avs_url_parse(raw_url);
    if (!url) {
        fw_log(ERROR, _("Could not parse URL: ") "%s", raw_url);
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
               _("Matching security information not found in data model for "
                 "URL: ") "%s",
               raw_url);
    }
    return args.result;
}

#endif // ANJAY_WITH_MODULE_FW_UPDATE
