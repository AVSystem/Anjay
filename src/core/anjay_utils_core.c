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

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anjay_utils_core.h"

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

                    if (avs_url_percent_decode(chunk->c_str,
                                               &(size_t[]){ 0 }[0])) {
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
                                   opts, AVS_COAP_OPTION_URI_QUERY,
                                   "lt=%" PRId64, *lifetime)))) {
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
                                   opts, AVS_COAP_OPTION_URI_QUERY, "sms=%s",
                                   sms_msisdn)))) {
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

#ifdef ANJAY_TEST
#    include "tests/core/utils.c"
#endif // ANJAY_TEST
