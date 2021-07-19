/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anjay_core.h"

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_servers.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_url.h>
#include <avsystem/commons/avs_utils.h>

VISIBILITY_SOURCE_BEGIN

typedef enum {
    URL_PARSE_HINT_NONE,
    URL_PARSE_HINT_SKIP_TRAILING_SEPARATOR
} url_parse_chunks_hint_t;

static int url_parse_chunks(const char **url,
                            char delimiter,
                            char parser_terminator,
                            url_parse_chunks_hint_t hint,
                            AVS_LIST(const anjay_string_t) *out_chunks) {
    const char *chunk_begin = *url;
    const char *chunk_end = *url;
    while (true) {
        if (*chunk_end == '\0' || *chunk_end == delimiter
                || *chunk_end == parser_terminator) {
            const size_t chunk_len = (size_t) (chunk_end - chunk_begin);

            if (hint == URL_PARSE_HINT_SKIP_TRAILING_SEPARATOR
                    && (*chunk_end == '\0' || *chunk_end == parser_terminator)
                    && !chunk_len) {
                // trailing separator, ignoring
                *url = chunk_end;
                return 0;
            }

            if (out_chunks) {
                AVS_LIST(anjay_string_t) chunk = (AVS_LIST(
                        anjay_string_t)) AVS_LIST_NEW_BUFFER(chunk_len + 1);
                if (!chunk) {
                    anjay_log(ERROR, _("out of memory"));
                    return -1;
                }
                AVS_LIST_APPEND(out_chunks, chunk);

                if (chunk_len) {
                    memcpy(chunk, chunk_begin, chunk_len);
                    size_t unescaped_length;
                    if (avs_url_percent_decode(chunk->c_str,
                                               &unescaped_length)) {
                        return -1;
                    }
                }
            }

            if (*chunk_end == delimiter) {
                chunk_begin = chunk_end + 1;
            } else {
                *url = chunk_end;
                return 0;
            }
        }

        ++chunk_end;
    }
}

static int copy_nullable_string(char *out, size_t out_size, const char *in) {
    assert(out_size > 0);
    if (!in) {
        out[0] = '\0';
        return 0;
    }
    size_t len = strlen(in);
    if (len >= out_size) {
        return -1;
    }
    strcpy(out, in);
    return 0;
}

int _anjay_url_parse_path_and_query(const char *path,
                                    AVS_LIST(const anjay_string_t) *out_path,
                                    AVS_LIST(const anjay_string_t) *out_query) {
    assert(out_path);
    assert(!*out_path);
    assert(out_query);
    assert(!*out_query);
    int result = 0;
    if (path) {
        if (avs_url_validate_relative_path(path)) {
            return -1;
        }
        if (*path == '/') {
            ++path;
        }
        result = url_parse_chunks(&path, '/', '?',
                                  URL_PARSE_HINT_SKIP_TRAILING_SEPARATOR,
                                  out_path);
        if (!result && *path == '?') {
            ++path;
            result = url_parse_chunks(&path, '&', '\0', URL_PARSE_HINT_NONE,
                                      out_query);
        }
    }
    if (result) {
        AVS_LIST_CLEAR(out_path);
        AVS_LIST_CLEAR(out_query);
    }
    return result;
}

int _anjay_url_from_avs_url(const avs_url_t *avs_url,
                            anjay_url_t *out_parsed_url) {
    if (!avs_url) {
        return -1;
    }
    int result = (avs_url_user(avs_url) || avs_url_password(avs_url)) ? -1 : 0;
    if (!result) {
        const char *host = avs_url_host(avs_url);
        const char *port = avs_url_port(avs_url);
        const char *path = avs_url_path(avs_url);
        (void) ((result = copy_nullable_string(out_parsed_url->host,
                                               sizeof(out_parsed_url->host),
                                               host))
                || (result = (port && !*port) ? -1 : 0)
                || (result = copy_nullable_string(out_parsed_url->port,
                                                  sizeof(out_parsed_url->port),
                                                  avs_url_port(avs_url))));
        if (!result) {
            result =
                    _anjay_url_parse_path_and_query(path,
                                                    &out_parsed_url->uri_path,
                                                    &out_parsed_url->uri_query);
        }
    }
    if (!result && !out_parsed_url->port[0]) {
        const anjay_transport_info_t *transport_info = NULL;
        const char *protocol = avs_url_protocol(avs_url);
        if (protocol) {
            transport_info = _anjay_transport_info_by_uri_scheme(protocol);
        }
        if (transport_info && transport_info->default_port) {
            assert(strlen(transport_info->default_port)
                   < sizeof(out_parsed_url->port));
            strcpy(out_parsed_url->port, transport_info->default_port);
        }
    }
    return result;
}

int _anjay_url_parse(const char *raw_url, anjay_url_t *out_parsed_url) {
    avs_url_t *avs_url = avs_url_parse_lenient(raw_url);
    int result = _anjay_url_from_avs_url(avs_url, out_parsed_url);
    avs_url_free(avs_url);
    return result;
}

void _anjay_url_cleanup(anjay_url_t *url) {
    AVS_LIST_CLEAR(&url->uri_path);
    AVS_LIST_CLEAR(&url->uri_query);
}

AVS_LIST(const anjay_string_t) _anjay_make_string_list(const char *string,
                                                       ... /* strings */) {
    va_list list;
    va_start(list, string);

    AVS_LIST(const anjay_string_t) strings_list = NULL;
    AVS_LIST(const anjay_string_t) *strings_list_endptr = &strings_list;
    const char *str = string;

    while (str) {
        size_t len = strlen(str) + 1;
        if (!(*strings_list_endptr =
                      (AVS_LIST(anjay_string_t)) AVS_LIST_NEW_BUFFER(len))) {
            anjay_log(ERROR, _("out of memory"));
            AVS_LIST_CLEAR(&strings_list);
            break;
        }

        memcpy((char *) (intptr_t) (*strings_list_endptr)->c_str, str, len);
        AVS_LIST_ADVANCE_PTR(&strings_list_endptr);
        str = va_arg(list, const char *);
    }

    va_end(list);
    return strings_list;
}

static bool is_valid_lwm2m_1_0_binding_mode(const char *binding_mode) {
    static const char *const VALID_BINDINGS[] = { "U",  "UQ", "S",
                                                  "SQ", "US", "UQS" };
    for (size_t i = 0; i < AVS_ARRAY_SIZE(VALID_BINDINGS); ++i) {
        if (strcmp(binding_mode, VALID_BINDINGS[i]) == 0) {
            return true;
        }
    }

    return false;
}

static const anjay_binding_info_t BINDING_INFOS[] = {
    { 'U', ANJAY_SOCKET_TRANSPORT_UDP },
};

const anjay_binding_info_t *
_anjay_binding_info_by_transport(anjay_socket_transport_t transport) {
    for (size_t i = 0; i < AVS_ARRAY_SIZE(BINDING_INFOS); ++i) {
        if (BINDING_INFOS[i].transport == transport) {
            return &BINDING_INFOS[i];
        }
    }

    AVS_UNREACHABLE("anjay_socket_transport_t value missing in BINDING_INFOS");
    return NULL;
}

bool anjay_binding_mode_valid(const char *binding_mode) {
    return is_valid_lwm2m_1_0_binding_mode(binding_mode);
}

bool _anjay_socket_is_online(avs_net_socket_t *socket) {
    if (!socket) {
        return false;
    }
    avs_net_socket_opt_value_t opt;
    if (avs_is_err(avs_net_socket_get_opt(socket, AVS_NET_SOCKET_OPT_STATE,
                                          &opt))) {
        anjay_log(DEBUG, _("Could not get socket state"));
        return false;
    }
    return opt.state == AVS_NET_SOCKET_STATE_CONNECTED;
}

avs_error_t _anjay_coap_add_query_options(avs_coap_options_t *opts,
                                          const anjay_lwm2m_version_t *version,
                                          const char *endpoint_name,
                                          const int64_t *lifetime,
                                          const char *binding_mode,
                                          bool lwm2m11_queue_mode,
                                          const char *sms_msisdn) {
    avs_error_t err;
    if (version
            && avs_is_err(
                       (err = avs_coap_options_add_string_f(
                                opts, AVS_COAP_OPTION_URI_QUERY, "lwm2m=%s",
                                _anjay_lwm2m_version_as_string(*version))))) {
        return err;
    }

    if (endpoint_name
            && avs_is_err((err = avs_coap_options_add_string_f(
                                   opts, AVS_COAP_OPTION_URI_QUERY, "ep=%s",
                                   endpoint_name)))) {
        return err;
    }

    assert(lifetime == NULL || *lifetime > 0);
    if (lifetime
            && avs_is_err((err = avs_coap_options_add_string_f(
                                   opts, AVS_COAP_OPTION_URI_QUERY, "lt=%s",
                                   AVS_INT64_AS_STRING(*lifetime))))) {
        return err;
    }

    if (binding_mode
            && avs_is_err((err = avs_coap_options_add_string_f(
                                   opts, AVS_COAP_OPTION_URI_QUERY, "b=%s",
                                   binding_mode)))) {
        return err;
    }

    (void) lwm2m11_queue_mode;

    if (sms_msisdn
            && avs_is_err((err = avs_coap_options_add_string_f(
                                   opts, AVS_COAP_OPTION_URI_QUERY, "sms%s%s",
                                   *sms_msisdn ? "=" : "", sms_msisdn)))) {
        return err;
    }

    return AVS_OK;
}

avs_error_t
_anjay_coap_add_string_options(avs_coap_options_t *opts,
                               AVS_LIST(const anjay_string_t) strings,
                               uint16_t opt_number) {
    AVS_LIST(const anjay_string_t) it;
    AVS_LIST_FOREACH(it, strings) {
        avs_error_t err =
                avs_coap_options_add_string(opts, opt_number, it->c_str);
        if (avs_is_err(err)) {
            return err;
        }
    }
    return AVS_OK;
}

static const anjay_transport_info_t TRANSPORTS[] = {
    {
        .transport = ANJAY_SOCKET_TRANSPORT_UDP,
        .socket_type = &(const avs_net_socket_type_t) { AVS_NET_UDP_SOCKET },
        .uri_scheme = "coap",
        .default_port = "5683",
        .security = ANJAY_TRANSPORT_NOSEC
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_UDP,
        .socket_type = &(const avs_net_socket_type_t) { AVS_NET_DTLS_SOCKET },
        .uri_scheme = "coaps",
        .default_port = "5684",
        .security = ANJAY_TRANSPORT_ENCRYPTED
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_TCP,
        .socket_type = &(const avs_net_socket_type_t) { AVS_NET_TCP_SOCKET },
        .uri_scheme = "coap+tcp",
        .default_port = "5683",
        .security = ANJAY_TRANSPORT_NOSEC
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_TCP,
        .socket_type = &(const avs_net_socket_type_t) { AVS_NET_SSL_SOCKET },
        .uri_scheme = "coaps+tcp",
        .default_port = "5684",
        .security = ANJAY_TRANSPORT_ENCRYPTED
    },
};

const anjay_transport_info_t *
_anjay_transport_info_by_uri_scheme(const char *uri_or_scheme) {
    if (!uri_or_scheme) {
        anjay_log(ERROR, _("URL scheme not specified"));
        return NULL;
    }

    for (size_t i = 0; i < AVS_ARRAY_SIZE(TRANSPORTS); ++i) {
        size_t scheme_size = strlen(TRANSPORTS[i].uri_scheme);
        if (avs_strncasecmp(uri_or_scheme, TRANSPORTS[i].uri_scheme,
                            scheme_size)
                        == 0
                && (uri_or_scheme[scheme_size] == '\0'
                    || uri_or_scheme[scheme_size] == ':')) {
            return &TRANSPORTS[i];
        }
    }

    anjay_log(WARNING, _("unsupported URI scheme: ") "%s", uri_or_scheme);
    return NULL;
}

int _anjay_copy_tls_ciphersuites(avs_net_socket_tls_ciphersuites_t *dest,
                                 const avs_net_socket_tls_ciphersuites_t *src) {
    assert(!dest->ids);
    if (src->num_ids) {
        if (!(dest->ids = (uint32_t *) avs_calloc(src->num_ids,
                                                  sizeof(*dest->ids)))) {
            anjay_log(ERROR, _("out of memory"));
            return -1;
        }
        memcpy(dest->ids, src->ids, src->num_ids * sizeof(*src->ids));
    }
    dest->num_ids = src->num_ids;
    return 0;
}

#define DEFAULT_COAPS_PORT "5684"
#define DEFAULT_COAP_PORT "5683"

static bool url_service_matches(const avs_url_t *left,
                                const avs_url_t *right,
                                const char *default_port) {
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
        port_left = default_port;
    }
    if (!port_right) {
        port_right = default_port;
    }
    return strcmp(port_left, port_right) == 0;
}

typedef union {
    struct {
        anjay_security_config_t *out;
        anjay_security_config_cache_t *cache;
        bool config_found;
    } security;
    struct {
        avs_coap_ctx_t *coap;
        // socket used by the `coap` instance above
        avs_net_socket_t *socket;
    } socket_info;
} security_or_socket_info_t;

typedef int
try_security_instance_callback_t(anjay_unlocked_t *anjay,
                                 security_or_socket_info_t *out_info,
                                 anjay_ssid_t ssid,
                                 anjay_iid_t security_iid,
                                 const avs_url_t *url,
                                 const avs_url_t *server_url);

typedef struct {
    security_or_socket_info_t *info;
    const avs_url_t *url;
    try_security_instance_callback_t *clb;
} try_security_instance_args_t;

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

static int
try_security_instance_read_security(anjay_unlocked_t *anjay,
                                    security_or_socket_info_t *out_info,
                                    anjay_ssid_t ssid,
                                    anjay_iid_t security_iid,
                                    const avs_url_t *url,
                                    const avs_url_t *server_url) {
    anjay_security_config_t new_result;
    anjay_security_config_cache_t cache_backup = *out_info->security.cache;
    memset(out_info->security.cache, 0, sizeof(*out_info->security.cache));
    if (avs_is_err(_anjay_get_security_config(anjay, &new_result,
                                              out_info->security.cache, ssid,
                                              security_iid))) {
        anjay_log(WARNING,
                  _("Could not read security information for "
                    "server ") "/%" PRIu16 "/%" PRIu16,
                  ANJAY_DM_OID_SECURITY, security_iid);
    } else if (!has_valid_keys(&new_result.security_info)
               && !new_result.dane_tlsa_record) {
        anjay_log(DEBUG,
                  _("Server ") "/%" PRIu16
                               "/%" PRIu16 _(" does not use encrypted "
                                             "connection, ignoring"),
                  ANJAY_DM_OID_SECURITY, security_iid);
    } else {
        _anjay_security_config_cache_cleanup(&cache_backup);
        *out_info->security.out = new_result;
        out_info->security.config_found = true;
        if (url_service_matches(server_url, url, DEFAULT_COAPS_PORT)) {
            // this is the best match we could get
            return ANJAY_FOREACH_BREAK;
        }
        // and here we are left with "some match", not necessarily the best
        // one, and thus we'll continue looking
        return ANJAY_FOREACH_CONTINUE;
    }
    _anjay_security_config_cache_cleanup(out_info->security.cache);
    *out_info->security.cache = cache_backup;
    return ANJAY_FOREACH_CONTINUE;
}

static bool optional_strings_equal(const char *left, const char *right) {
    if (left && right) {
        return strcmp(left, right) == 0;
    } else {
        return !left && !right;
    }
}

static int try_security_instance(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t *obj,
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
        anjay_log(WARNING, _("could not read LwM2M server URI from ") "%s",
                  ANJAY_DEBUG_MAKE_PATH(&path));
        return ANJAY_FOREACH_CONTINUE;
    }

    avs_url_t *server_url = avs_url_parse_lenient(raw_server_url);
    if (!server_url) {
        anjay_log(WARNING, _("Could not parse URL from ") "%s" _(": ") "%s",
                  ANJAY_DEBUG_MAKE_PATH(&path), raw_server_url);
        return ANJAY_FOREACH_CONTINUE;
    }

    int retval = ANJAY_FOREACH_CONTINUE;
    if (optional_strings_equal(avs_url_host(server_url),
                               avs_url_host(args->url))) {
        anjay_ssid_t ssid;
        if (!_anjay_ssid_from_security_iid(anjay, security_iid, &ssid)) {
            retval = args->clb(anjay, args->info, ssid, security_iid, args->url,
                               server_url);
        }
    }

    avs_url_free(server_url);
    return retval;
}

static void try_get_info_from_dm(anjay_unlocked_t *anjay,
                                 const char *raw_url,
                                 security_or_socket_info_t *out_info,
                                 try_security_instance_callback_t *clb) {
    assert(anjay);

    const anjay_dm_installed_object_t *security_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    if (!security_obj) {
        anjay_log(ERROR, _("Security object not installed"));
        return;
    }

    avs_url_t *url = avs_url_parse_lenient(raw_url);
    if (!url) {
        anjay_log(ERROR, _("Could not parse URL: ") "%s", raw_url);
        return;
    }
    try_security_instance_args_t args = {
        .info = out_info,
        .url = url,
        .clb = clb
    };
    _anjay_dm_foreach_instance(anjay, security_obj, try_security_instance,
                               &args);
    avs_url_free(url);
}

int _anjay_security_config_from_dm_unlocked(anjay_unlocked_t *anjay,
                                            anjay_security_config_t *out_config,
                                            const char *raw_url) {
    security_or_socket_info_t info = {
        .security = {
            .out = out_config,
            .cache = &anjay->security_config_from_dm_cache
        }
    };
    try_get_info_from_dm(anjay, raw_url, &info,
                         try_security_instance_read_security);
    if (!info.security.config_found) {
        anjay_log(WARNING,
                  _("Matching security information not found in data model for "
                    "URL: ") "%s",
                  raw_url);
        return -1;
    }
    return 0;
}

int anjay_security_config_from_dm(anjay_t *anjay_locked,
                                  anjay_security_config_t *out_config,
                                  const char *raw_url) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result =
            _anjay_security_config_from_dm_unlocked(anjay, out_config, raw_url);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static int map_str_conversion_result(const char *input, const char *endptr) {
    return (!*input || isspace((unsigned char) *input) || errno || !endptr
            || *endptr)
                   ? -1
                   : 0;
}

int _anjay_safe_strtoll(const char *in, long long *value) {
    errno = 0;
    char *endptr = NULL;
    *value = strtoll(in, &endptr, 10);
    return map_str_conversion_result(in, endptr);
}

int _anjay_safe_strtoull(const char *in, unsigned long long *value) {
    errno = 0;
    char *endptr = NULL;
    *value = strtoull(in, &endptr, 10);
    return map_str_conversion_result(in, endptr);
}

int _anjay_safe_strtod(const char *in, double *value) {
    errno = 0;
    char *endptr = NULL;
    *value = strtod(in, &endptr);
    return map_str_conversion_result(in, endptr);
}

#ifdef ANJAY_TEST
#    include "tests/core/utils.c"
#endif // ANJAY_TEST
