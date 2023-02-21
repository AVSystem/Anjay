/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <inttypes.h>

#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_utils.h>

#include "../anjay_io_core.h"

#define ANJAY_SERVERS_CONNECTION_SOURCE
#define ANJAY_SERVERS_INTERNALS

#include "anjay_connections_internal.h"
#include "anjay_security.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_connection_security_generic_get_uri(
        anjay_unlocked_t *anjay,
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

static int get_security_mode(anjay_unlocked_t *anjay,
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

#ifdef ANJAY_WITH_LWM2M11
static avs_error_t
get_tlsa_settings(anjay_unlocked_t *anjay,
                  anjay_iid_t security_iid,
                  anjay_security_mode_t security_mode,
                  avs_net_socket_dane_tlsa_record_t *out_record) {
    if (security_mode != ANJAY_SECURITY_CERTIFICATE
            && security_mode != ANJAY_SECURITY_EST) {
        return AVS_OK;
    }

    uint64_t tmp;
    if (!_anjay_dm_read_resource_u64(
                anjay,
                &MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                                    ANJAY_DM_RID_SECURITY_MATCHING_TYPE),
                &tmp)) {
        switch (tmp) {
        case 0:
            out_record->matching_type = AVS_NET_SOCKET_DANE_MATCH_FULL;
            break;
        case 1:
            out_record->matching_type = AVS_NET_SOCKET_DANE_MATCH_SHA256;
            break;
        case 3:
            out_record->matching_type = AVS_NET_SOCKET_DANE_MATCH_SHA512;
            break;
        case 2:
            // Matching Type 2 is defined in LwM2M as SHA384
            // which is not supported
        default:
            anjay_log(WARNING, _("unsupported matching type: ") "%s",
                      AVS_UINT64_AS_STRING(tmp));
        }
    }

    if (!_anjay_dm_read_resource_u64(
                anjay,
                &MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                                    ANJAY_DM_RID_SECURITY_CERTIFICATE_USAGE),
                &tmp)) {
        switch (tmp) {
        case (uint64_t) AVS_NET_SOCKET_DANE_CA_CONSTRAINT:
        case (uint64_t) AVS_NET_SOCKET_DANE_SERVICE_CERTIFICATE_CONSTRAINT:
        case (uint64_t) AVS_NET_SOCKET_DANE_TRUST_ANCHOR_ASSERTION:
        case (uint64_t) AVS_NET_SOCKET_DANE_DOMAIN_ISSUED_CERTIFICATE:
            out_record->certificate_usage =
                    (avs_net_socket_dane_certificate_usage_t) tmp;
            break;
        default:
            anjay_log(WARNING, _("unsupported certificate usage: ") "%s",
                      AVS_UINT64_AS_STRING(tmp));
        }
    }

    return AVS_OK;
}
#endif // ANJAY_WITH_LWM2M11

static avs_error_t init_cert_security(anjay_unlocked_t *anjay,
                                      anjay_ssid_t ssid,
                                      anjay_iid_t security_iid,
                                      anjay_security_config_t *security,
                                      anjay_security_mode_t security_mode,
                                      anjay_security_config_cache_t *cache) {
    avs_net_certificate_info_t certificate_info = {
        .ignore_system_trust_store = true
    };

    {
        AVS_STATIC_ASSERT(sizeof(*cache->client_cert_array)
                                  == sizeof(avs_crypto_security_info_union_t),
                          certificate_chain_info_equivalent_to_union);
        size_t element_count = 0;
        avs_error_t err = _anjay_dm_read_security_info(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
                AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN,
                (avs_crypto_security_info_union_t **) &cache->client_cert_array,
                &element_count);
        if (avs_is_err(err)) {
            return err;
        }
        switch (element_count) {
        case 0:
            break;
        case 1:
            certificate_info.client_cert = cache->client_cert_array[0];
            break;
        default:
            certificate_info.client_cert =
                    avs_crypto_certificate_chain_info_from_array(
                            cache->client_cert_array, element_count);
        }
    }

    {
        AVS_STATIC_ASSERT(sizeof(*cache->client_key)
                                  == sizeof(avs_crypto_security_info_union_t),
                          private_key_info_equivalent_to_union);
        size_t element_count = 0;
        avs_error_t err = _anjay_dm_read_security_info(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_SECRET_KEY,
                AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY,
                (avs_crypto_security_info_union_t **) &cache->client_key,
                &element_count);
        if (avs_is_err(err)) {
            return err;
        }
        assert(element_count == 1);
        certificate_info.client_key = *cache->client_key;
    }

    avs_stream_t *server_pk_membuf = avs_stream_membuf_create();
    if (!server_pk_membuf) {
        anjay_log(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }
    avs_net_socket_dane_tlsa_record_t dane_tlsa_record = {
        .certificate_usage = AVS_NET_SOCKET_DANE_DOMAIN_ISSUED_CERTIFICATE
    };
    avs_error_t err = AVS_OK;
#ifdef ANJAY_WITH_LWM2M11
    err = get_tlsa_settings(anjay, security_iid, security_mode,
                            &dane_tlsa_record);
#endif // ANJAY_WITH_LWM2M11
    if (avs_is_ok(err)) {
        err = avs_stream_write(server_pk_membuf, &dane_tlsa_record,
                               sizeof(dane_tlsa_record));
    }
    if (avs_is_ok(err)) {
        anjay_output_buf_ctx_t server_pk_ctx =
                _anjay_output_buf_ctx_init(server_pk_membuf);
        const anjay_uri_path_t server_pk_path =
                MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                                   ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY);
        if (_anjay_dm_read_resource_into_ctx(
                    anjay, &server_pk_path,
                    (anjay_unlocked_output_ctx_t *) &server_pk_ctx)) {
            anjay_log(WARNING, _("read ") "%s" _(" failed"),
                      ANJAY_DEBUG_MAKE_PATH(&server_pk_path));
            err = avs_errno(AVS_EPROTO);
        }
    }
    if (avs_is_ok(err)) {
        void *buffer = NULL;
        size_t buffer_size;
        if (avs_is_ok((err = avs_stream_membuf_take_ownership(
                               server_pk_membuf, &buffer, &buffer_size)))) {
            AVS_ASSERT(
                    ((uintptr_t) buffer)
                                    % AVS_ALIGNOF(
                                              avs_net_socket_dane_tlsa_record_t)
                            == 0,
                    "avs_stream_membuf_take_ownership returned misaligned "
                    "pointer");
            if (buffer_size > sizeof(avs_net_socket_dane_tlsa_record_t)) {
                cache->dane_tlsa_record =
                        (avs_net_socket_dane_tlsa_record_t *) buffer;
                cache->dane_tlsa_record->association_data =
                        (char *) buffer
                        + sizeof(avs_net_socket_dane_tlsa_record_t);
                cache->dane_tlsa_record->association_data_size =
                        buffer_size - sizeof(avs_net_socket_dane_tlsa_record_t);
            } else {
                avs_free(buffer);
            }
        }
    }
    avs_stream_cleanup(&server_pk_membuf);
    if (avs_is_err(err)) {
        return err;
    }

    if (cache->dane_tlsa_record) {
        certificate_info.server_cert_validation = true;
        certificate_info.dane = true;
        security->dane_tlsa_record = cache->dane_tlsa_record;
    }

#ifdef ANJAY_WITH_LWM2M11
    const anjay_trust_store_t *trust_store =
            _anjay_get_trust_store(anjay, ssid, security_mode);
    if (trust_store) {
        certificate_info.ignore_system_trust_store =
                !trust_store->use_system_wide;
        certificate_info.trusted_certs =
                avs_crypto_certificate_chain_info_from_list(trust_store->certs);
        certificate_info.cert_revocation_lists =
                avs_crypto_cert_revocation_list_info_from_list(
                        trust_store->crls);
        certificate_info.rebuild_client_cert_chain =
                anjay->rebuild_client_cert_chain;
        if (trust_store != &anjay->initial_trust_store) {
            // Enforce usage of non-initial trust store
            certificate_info.server_cert_validation = true;
        }
    }
    if (dane_tlsa_record.certificate_usage == AVS_NET_SOCKET_DANE_CA_CONSTRAINT
            || dane_tlsa_record.certificate_usage
                           == AVS_NET_SOCKET_DANE_SERVICE_CERTIFICATE_CONSTRAINT) {
        // Certificate Usage modes 0 and 1 require PKIX validation,
        // so enable validation even if no certificate is explicitly
        // specified
        certificate_info.server_cert_validation = true;
    }
#endif // ANJAY_WITH_LWM2M11
    (void) ssid;
    (void) security_mode;

    security->security_info =
            avs_net_security_info_from_certificates(certificate_info);

    return AVS_OK;
}

static avs_error_t init_security(anjay_unlocked_t *anjay,
                                 anjay_ssid_t ssid,
                                 anjay_iid_t security_iid,
                                 anjay_security_config_t *security,
                                 anjay_security_mode_t security_mode,
                                 anjay_security_config_cache_t *cache) {
    switch (security_mode) {
    case ANJAY_SECURITY_NOSEC:
        return AVS_OK;
    case ANJAY_SECURITY_PSK:
        return _anjay_connection_init_psk_security(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
                ANJAY_DM_RID_SECURITY_SECRET_KEY, &security->security_info,
                cache);
    case ANJAY_SECURITY_CERTIFICATE:
    case ANJAY_SECURITY_EST:
        return init_cert_security(anjay, ssid, security_iid, security,
                                  security_mode, cache);
    case ANJAY_SECURITY_RPK:
    default:
        anjay_log(ERROR, _("unsupported security mode: ") "%d",
                  (int) security_mode);
        return avs_errno(AVS_EINVAL);
    }
}

#ifdef ANJAY_WITH_LWM2M11
static int read_ciphersuite_list(anjay_unlocked_t *anjay,
                                 anjay_iid_t security_iid,
                                 uint32_t **out_u32_ciphersuites,
                                 size_t *out_num_ciphersuites) {
    assert(out_u32_ciphersuites);
    assert(!*out_u32_ciphersuites);
    assert(out_num_ciphersuites);
    assert(!*out_num_ciphersuites);

    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               ANJAY_DM_RID_SECURITY_DTLS_TLS_CIPHERSUITE);

    int result = _anjay_dm_read_resource_u32_array(
            anjay, &path, out_u32_ciphersuites, out_num_ciphersuites);
    if (result) {
        assert(!*out_u32_ciphersuites);
        assert(!*out_num_ciphersuites);
        if (result == ANJAY_ERR_NOT_FOUND
                || result == ANJAY_ERR_METHOD_NOT_ALLOWED) {
            return 0;
        } else {
            return result;
        }
    }

    for (size_t i = 0; i < *out_num_ciphersuites; ++i) {
        if ((*out_u32_ciphersuites)[i] > UINT16_MAX) {
            anjay_log(ERROR,
                      _("cipher ID too large: ") "%" PRIu32 _(" > ") "%" PRIu16,
                      (*out_u32_ciphersuites)[i], UINT16_MAX);
            avs_free(*out_u32_ciphersuites);
            *out_u32_ciphersuites = NULL;
            *out_num_ciphersuites = 0;
            return -1;
        }
    }

    return 0;
}
#endif // ANJAY_WITH_LWM2M11

avs_error_t _anjay_connection_security_generic_get_config(
        anjay_unlocked_t *anjay,
        anjay_security_config_t *out_config,
        anjay_security_config_cache_t *cache,
        anjay_connection_info_t *inout_info) {
    anjay_security_mode_t security_mode;
    if (get_security_mode(anjay, inout_info->security_iid, &security_mode)) {
        return avs_errno(AVS_EPROTO);
    }

    memset(out_config, 0, sizeof(*out_config));
    out_config->tls_ciphersuites = anjay->default_tls_ciphersuites;

    if (inout_info->transport_info
            && !security_matches_transport(security_mode,
                                           inout_info->transport_info)) {
        return avs_errno(AVS_EPROTO);
    }

#ifdef ANJAY_WITH_LWM2M11
    if (security_mode != ANJAY_SECURITY_NOSEC
            && read_ciphersuite_list(anjay, inout_info->security_iid,
                                     &cache->ciphersuites.ids,
                                     &cache->ciphersuites.num_ids)) {
        assert(!cache->ciphersuites.ids);
        return avs_errno(AVS_EPROTO);
    }

    if (cache->ciphersuites.num_ids == 0) {
        anjay_log(DEBUG,
                  _("no ciphers configured for security IID ") "%" PRIu16 _(
                          ", using ") "%s" _(" defaults"),
                  inout_info->security_iid,
                  anjay->default_tls_ciphersuites.num_ids > 0
                          ? "anjay_configuration_t"
                          : "TLS backend");
    } else {
        out_config->tls_ciphersuites = cache->ciphersuites;
    }
#endif // ANJAY_WITH_LWM2M11

    avs_error_t err =
            init_security(anjay, inout_info->ssid, inout_info->security_iid,
                          out_config, security_mode, cache);
    if (avs_is_err(err)) {
        return err;
    }
    inout_info->is_encrypted = (security_mode != ANJAY_SECURITY_NOSEC);
    anjay_log(DEBUG, _("server ") "/%u/%u" _(": security mode = ") "%d",
              ANJAY_DM_OID_SECURITY, inout_info->security_iid,
              (int) security_mode);
    return AVS_OK;
}
