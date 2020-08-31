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

#include <inttypes.h>

#include <avsystem/commons/avs_utils.h>

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "anjay_connections_internal.h"
#include "anjay_security.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_connection_security_generic_get_uri(
        anjay_t *anjay,
        anjay_iid_t security_iid,
        avs_url_t **out_uri,
        const anjay_transport_info_t **out_transport_info) {
    assert(out_uri);
    assert(!*out_uri);
    assert(out_transport_info);
    assert(!*out_transport_info);
    char raw_uri[ANJAY_MAX_URL_RAW_LENGTH];

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_SERVER_URI);

    if (_anjay_dm_read_resource_string(anjay, &path, raw_uri,
                                       sizeof(raw_uri))) {
        anjay_log(ERROR, _("could not read LwM2M server URI from ") "%s",
                  ANJAY_DEBUG_MAKE_PATH(&path));
        return -1;
    }

    if (!(*out_uri = avs_url_parse_lenient(raw_uri))
            || !(*out_transport_info = _anjay_transport_info_by_uri_scheme(
                         avs_url_protocol(*out_uri)))
            || avs_url_user(*out_uri) || avs_url_password(*out_uri)
            || (avs_url_port(*out_uri) && !*avs_url_port(*out_uri))) {
        if (*out_uri) {
            avs_url_free(*out_uri);
        }
        *out_uri = NULL;
        *out_transport_info = NULL;
        anjay_log(ERROR, _("could not parse LwM2M server URI: ") "%s", raw_uri);
        return -1;
    }
    return 0;
}

static int get_security_mode(anjay_t *anjay,
                             anjay_iid_t security_iid,
                             anjay_security_mode_t *out_mode) {
    int64_t mode;
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_MODE);

    if (_anjay_dm_read_resource_i64(anjay, &path, &mode)) {
        anjay_log(ERROR,
                  _("could not read LwM2M server security mode from ") "%s",
                  ANJAY_DEBUG_MAKE_PATH(&path));
        return -1;
    }

    switch (mode) {
    case ANJAY_SECURITY_RPK:
        anjay_log(ERROR, _("unsupported security mode: ") "%s",
                  AVS_INT64_AS_STRING(mode));
        return -1;
    case ANJAY_SECURITY_NOSEC:
    case ANJAY_SECURITY_PSK:
    case ANJAY_SECURITY_CERTIFICATE:
    case ANJAY_SECURITY_EST:
        *out_mode = (anjay_security_mode_t) mode;
        return 0;
    default:
        anjay_log(ERROR, _("invalid security mode: ") "%s",
                  AVS_INT64_AS_STRING(mode));
        return -1;
    }
}

static bool
security_matches_transport(anjay_security_mode_t security_mode,
                           const anjay_transport_info_t *transport_info) {
    assert(transport_info);
    if (transport_info->security == ANJAY_TRANSPORT_SECURITY_UNDEFINED) {
        // URI scheme does not specify security,
        // so it is valid for all security modes
        return true;
    }

    const bool is_secure_transport =
            (transport_info->security == ANJAY_TRANSPORT_ENCRYPTED);
    const bool needs_secure_transport = (security_mode != ANJAY_SECURITY_NOSEC);

    if (is_secure_transport != needs_secure_transport) {
        anjay_log(WARNING,
                  _("security mode ") "%d" _(" requires ") "%s" _(
                          "secure protocol, but '") "%s" _("' was configured"),
                  (int) security_mode, needs_secure_transport ? "" : "in",
                  transport_info->uri_scheme);
        return false;
    }

    return true;
}

static int
init_cert_security(anjay_t *anjay,
                   anjay_ssid_t ssid,
                   anjay_iid_t security_iid,
                   anjay_security_config_t *security,
                   avs_net_socket_dane_tlsa_record_t *dane_tlsa_record,
                   anjay_security_mode_t security_mode,
                   char **data_buffer_ptr,
                   const char *data_buffer_end) {
    avs_net_certificate_info_t certificate_info = {
        .ignore_system_trust_store = true
    };

    {
        size_t pk_or_identity_size;
        const void *pk_or_identity = _anjay_connection_security_read_key(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
                data_buffer_ptr, data_buffer_end, &pk_or_identity_size);
        if (!pk_or_identity) {
            return -1;
        }
        certificate_info.client_cert =
                avs_crypto_client_cert_info_from_buffer(pk_or_identity,
                                                        pk_or_identity_size);
    }

    {
        size_t secret_key_size;
        const void *secret_key = _anjay_connection_security_read_key(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_SECRET_KEY,
                data_buffer_ptr, data_buffer_end, &secret_key_size);
        if (!secret_key) {
            return -1;
        }
        certificate_info.client_key =
                avs_crypto_client_key_info_from_buffer(secret_key,
                                                       secret_key_size, NULL);
    }

    if (!(dane_tlsa_record->association_data =
                  _anjay_connection_security_read_key(
                          anjay, security_iid,
                          ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY,
                          data_buffer_ptr, data_buffer_end,
                          &dane_tlsa_record->association_data_size))) {
        return -1;
    }

    if (dane_tlsa_record->association_data_size > 0) {
        certificate_info.server_cert_validation = true;
        certificate_info.dane = true;
        security->dane_tlsa_record = dane_tlsa_record;
    }

    (void) ssid;
    (void) security_mode;

    security->security_info =
            avs_net_security_info_from_certificates(certificate_info);

    return 0;
}

static int init_security(anjay_t *anjay,
                         anjay_ssid_t ssid,
                         anjay_iid_t security_iid,
                         anjay_security_config_t *security,
                         avs_net_socket_dane_tlsa_record_t *dane_tlsa_record,
                         anjay_security_mode_t security_mode,
                         char **data_buffer_ptr,
                         const char *data_buffer_end) {
    switch (security_mode) {
    case ANJAY_SECURITY_NOSEC:
        return 0;
    case ANJAY_SECURITY_PSK:
        return _anjay_connection_init_psk_security(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
                ANJAY_DM_RID_SECURITY_SECRET_KEY, &security->security_info,
                data_buffer_ptr, data_buffer_end);
    case ANJAY_SECURITY_CERTIFICATE:
    case ANJAY_SECURITY_EST:
        return init_cert_security(anjay, ssid, security_iid, security,
                                  dane_tlsa_record, security_mode,
                                  data_buffer_ptr, data_buffer_end);
    case ANJAY_SECURITY_RPK:
    default:
        anjay_log(ERROR, _("unsupported security mode: ") "%d",
                  (int) security_mode);
        return -1;
    }
}

typedef struct {
    anjay_security_config_t security_config;
    avs_net_socket_dane_tlsa_record_t dane_tlsa_record;
    avs_max_align_t data_buffer[];
} security_config_with_data_t;

anjay_security_config_t *_anjay_connection_security_generic_get_config(
        anjay_t *anjay, anjay_connection_info_t *inout_info) {
    anjay_security_mode_t security_mode;
    if (get_security_mode(anjay, inout_info->security_iid, &security_mode)) {
        return NULL;
    }

    security_config_with_data_t *result = NULL;
    size_t data_buffer_size = 0;
    if (security_mode != ANJAY_SECURITY_NOSEC) {
        data_buffer_size +=
                ANJAY_MAX_PK_OR_IDENTITY_SIZE + ANJAY_MAX_SECRET_KEY_SIZE;
    }
    if (security_mode == ANJAY_SECURITY_CERTIFICATE
            || security_mode == ANJAY_SECURITY_EST) {
        data_buffer_size += ANJAY_MAX_SERVER_PK_OR_IDENTITY_SIZE;
    }

    char *data_buffer_ptr = NULL;
    const char *data_buffer_end;
    if ((result = (security_config_with_data_t *) avs_calloc(
                 1, offsetof(security_config_with_data_t, data_buffer)
                            + data_buffer_size))) {
        result->security_config.tls_ciphersuites =
                anjay->default_tls_ciphersuites;
        result->dane_tlsa_record.certificate_usage =
                AVS_NET_SOCKET_DANE_DOMAIN_ISSUED_CERTIFICATE;
        data_buffer_ptr = (char *) result->data_buffer;
        data_buffer_end = data_buffer_ptr + data_buffer_size;
    }

    if (!result
            || init_security(anjay, inout_info->ssid, inout_info->security_iid,
                             &result->security_config,
                             &result->dane_tlsa_record, security_mode,
                             &data_buffer_ptr, data_buffer_end)) {
        goto error;
    }
    inout_info->is_encrypted = (security_mode != ANJAY_SECURITY_NOSEC);
    anjay_log(DEBUG, _("server ") "/%u/%u" _(": security mode = ") "%d",
              ANJAY_DM_OID_SECURITY, inout_info->security_iid,
              (int) security_mode);
    AVS_STATIC_ASSERT(offsetof(security_config_with_data_t, security_config)
                              == 0,
                      security_config_pointers_castable);
    return &result->security_config;
error:
    avs_free(result);
    return NULL;
}
