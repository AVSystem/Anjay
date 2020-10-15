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

#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_utils.h>

#include "../anjay_io_core.h"
#include "../io/anjay_vtable.h"

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

typedef struct {
    anjay_output_ctx_t base;
    const anjay_ret_bytes_ctx_vtable_t *ret_bytes_vtable;
    avs_crypto_security_info_tag_t tag;
    avs_crypto_security_info_union_t *out_array;
    size_t out_element_count;
    size_t bytes_remaining;
} read_security_info_ctx_t;

static int
read_security_info_ret_bytes_begin(anjay_output_ctx_t *ctx_,
                                   size_t length,
                                   anjay_ret_bytes_ctx_t **out_bytes_ctx) {
    read_security_info_ctx_t *ctx = (read_security_info_ctx_t *) ctx_;
    if (ctx->out_array) {
        anjay_log(ERROR, _("value already returned"));
        return -1;
    }
    if (!(ctx->out_array = (avs_crypto_security_info_union_t *) avs_malloc(
                  sizeof(*ctx->out_array) + length))) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    *ctx->out_array = (const avs_crypto_security_info_union_t) {
        .type = ctx->tag,
        .source = AVS_CRYPTO_DATA_SOURCE_BUFFER,
        .info.buffer = {
            .buffer = (char *) ctx->out_array + sizeof(*ctx->out_array),
            .buffer_size = length
        }
    };
    ctx->out_element_count = 1;
    ctx->bytes_remaining = length;
    *out_bytes_ctx = (anjay_ret_bytes_ctx_t *) &ctx->ret_bytes_vtable;
    return 0;
}

static int read_security_info_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx_,
                                               const void *data,
                                               size_t size) {
    read_security_info_ctx_t *ctx =
            (read_security_info_ctx_t *) AVS_CONTAINER_OF(
                    ctx_, read_security_info_ctx_t, ret_bytes_vtable);
    assert(ctx->out_array);
    if (size > ctx->bytes_remaining) {
        anjay_log(DEBUG, _("tried to write too many bytes"));
        return -1;
    }
    memcpy(((char *) (intptr_t) ctx->out_array->info.buffer.buffer)
                   + (ctx->out_array->info.buffer.buffer_size
                      - ctx->bytes_remaining),
           data, size);
    ctx->bytes_remaining -= size;
    return 0;
}

static const anjay_output_ctx_vtable_t READ_SECURITY_INFO_VTABLE = {
    .bytes_begin = read_security_info_ret_bytes_begin
};

static const anjay_ret_bytes_ctx_vtable_t READ_SECURITY_INFO_BYTES_VTABLE = {
    .append = read_security_info_ret_bytes_append
};

static avs_error_t
read_security_info(anjay_t *anjay,
                   anjay_iid_t security_iid,
                   anjay_rid_t security_rid,
                   avs_crypto_security_info_tag_t tag,
                   avs_crypto_security_info_union_t **out_array,
                   size_t *out_element_count) {
    assert(anjay);
    assert(out_array);
    assert(!*out_array);
    assert(out_element_count);
    read_security_info_ctx_t ctx = {
        .base = {
            .vtable = &READ_SECURITY_INFO_VTABLE
        },
        .ret_bytes_vtable = &READ_SECURITY_INFO_BYTES_VTABLE,
        .tag = tag
    };
    const anjay_uri_path_t path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SECURITY, security_iid,
                               security_rid);
    if (_anjay_dm_read_resource_into_ctx(anjay, &path,
                                         (anjay_output_ctx_t *) &ctx)
            || ctx.bytes_remaining) {
        anjay_log(WARNING, _("read ") "%s" _(" failed"),
                  ANJAY_DEBUG_MAKE_PATH(&path));
        avs_free(ctx.out_array);
        return avs_errno(AVS_EPROTO);
    }
    *out_array = ctx.out_array;
    *out_element_count = ctx.out_element_count;
    return AVS_OK;
}

static avs_error_t init_cert_security(anjay_t *anjay,
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
        avs_error_t err = read_security_info(
                anjay, security_iid, ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY,
                AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN,
                (avs_crypto_security_info_union_t **) &cache->client_cert_array,
                &element_count);
        if (avs_is_err(err)) {
            return err;
        }
        certificate_info.client_cert =
                avs_crypto_certificate_chain_info_from_array(
                        cache->client_cert_array, element_count);
    }

    {
        AVS_STATIC_ASSERT(sizeof(*cache->client_key)
                                  == sizeof(avs_crypto_security_info_union_t),
                          private_key_info_equivalent_to_union);
        size_t element_count = 0;
        avs_error_t err = read_security_info(
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
                    (anjay_output_ctx_t *) &server_pk_ctx)) {
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

    (void) ssid;
    (void) security_mode;

    security->security_info =
            avs_net_security_info_from_certificates(certificate_info);

    return AVS_OK;
}

static avs_error_t init_security(anjay_t *anjay,
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
                &cache->psk_buffer);
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

avs_error_t _anjay_connection_security_generic_get_config(
        anjay_t *anjay,
        anjay_security_config_t *out_config,
        anjay_security_config_cache_t *cache,
        anjay_connection_info_t *inout_info) {
    anjay_security_mode_t security_mode;
    if (get_security_mode(anjay, inout_info->security_iid, &security_mode)) {
        return avs_errno(AVS_EPROTO);
    }

    memset(out_config, 0, sizeof(*out_config));
    out_config->tls_ciphersuites = anjay->default_tls_ciphersuites;

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
