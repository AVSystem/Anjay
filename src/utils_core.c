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

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils_core.h"

#include <avsystem/commons/errno.h>
#include <avsystem/commons/url.h>
#include <avsystem/commons/utils.h>

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
    const char *ptr = *url;
    do {
        if ((*ptr == '\0' || *ptr == delimiter || *ptr == parser_terminator)
                && ptr != *url) {
            const size_t chunk_len = (size_t) (ptr - *url - 1);

            if (hint == URL_PARSE_HINT_SKIP_TRAILING_SEPARATOR
                    && (*ptr == '\0' || *ptr == parser_terminator)
                    && !chunk_len) {
                // trailing separator, ignoring
                *url = ptr;
                return 0;
            }

            if (out_chunks) {
                AVS_LIST(anjay_string_t) chunk = (AVS_LIST(
                        anjay_string_t)) AVS_LIST_NEW_BUFFER(chunk_len + 1);
                if (!chunk) {
                    anjay_log(ERROR, "out of memory");
                    return -1;
                }
                AVS_LIST_APPEND(out_chunks, chunk);

                if (chunk_len) {
                    // skip separator
                    memcpy(chunk, *url + 1, chunk_len);

                    if (avs_url_percent_decode(chunk->c_str,
                                               &(size_t[]){ 0 }[0])) {
                        return -1;
                    }
                }
            }
            *url = ptr;
        }

        if (*ptr == parser_terminator) {
            *url = ptr;
            break;
        }
        ++ptr;
    } while (**url);
    return 0;
}

static int copy_string(char *out, size_t out_size, const char *in) {
    if (!in) {
        return -1;
    }
    size_t len = strlen(in);
    if (len >= out_size) {
        return -1;
    }
    strcpy(out, in);
    return 0;
}

static int copy_nullable_string(char *out, size_t out_size, const char *in) {
    assert(out_size > 0);
    if (!in) {
        out[0] = '\0';
        return 0;
    } else {
        return copy_string(out, out_size, in);
    }
}

int _anjay_url_parse(const char *raw_url, anjay_url_t *out_parsed_url) {
    assert(!out_parsed_url->uri_path);
    assert(!out_parsed_url->uri_query);
    avs_url_t *avs_url = avs_url_parse(raw_url);
    if (!avs_url) {
        return -1;
    }
    int result = (avs_url_user(avs_url) || avs_url_password(avs_url)) ? -1 : 0;
    if (!result) {
        const char *protocol = avs_url_protocol(avs_url);
        if (avs_strcasecmp(protocol, "coap") == 0) {
            out_parsed_url->protocol = ANJAY_URL_PROTOCOL_COAP;
        } else if (avs_strcasecmp(protocol, "coaps") == 0) {
            out_parsed_url->protocol = ANJAY_URL_PROTOCOL_COAPS;
        } else {
            anjay_log(ERROR, "Unknown or unsupported protocol: %s", protocol);
            result = -1;
        }
    }
    (void) (result
            || (result = copy_string(out_parsed_url->host,
                                     sizeof(out_parsed_url->host),
                                     avs_url_host(avs_url)))
            || (result = copy_nullable_string(out_parsed_url->port,
                                              sizeof(out_parsed_url->port),
                                              avs_url_port(avs_url))));
    if (!result) {
        const char *path_ptr = avs_url_path(avs_url);
        if (path_ptr) {
            result = url_parse_chunks(&path_ptr, '/', '?',
                                      URL_PARSE_HINT_SKIP_TRAILING_SEPARATOR,
                                      &out_parsed_url->uri_path);
            if (!result && *path_ptr == '?') {
                result = url_parse_chunks(&path_ptr, '&', '\0',
                                          URL_PARSE_HINT_NONE,
                                          &out_parsed_url->uri_query);
            }
        }
    }
    if (result) {
        _anjay_url_cleanup(out_parsed_url);
    }
    avs_url_free(avs_url);
    return result;
}

int _anjay_url_copy(anjay_url_t *out_copy, const anjay_url_t *source) {
    out_copy->protocol = source->protocol;
    memcpy(out_copy->host, source->host, sizeof(out_copy->host));
    memcpy(out_copy->port, source->port, sizeof(out_copy->port));
    if (_anjay_copy_string_list(&out_copy->uri_path, source->uri_path)
            || _anjay_copy_string_list(&out_copy->uri_query,
                                       source->uri_query)) {
        return -1;
    }
    return 0;
}

void _anjay_url_cleanup(anjay_url_t *url) {
    AVS_LIST_CLEAR(&url->uri_path);
    AVS_LIST_CLEAR(&url->uri_query);
}

#ifdef ANJAY_TEST

uint32_t _anjay_rand32(anjay_rand_seed_t *seed) {
    // simple deterministic generator for testing purposes
    return (*seed = 1103515245u * *seed + 12345u);
}

#else

#    if AVS_RAND_MAX >= UINT32_MAX
#        define RAND32_ITERATIONS 1
#    elif AVS_RAND_MAX >= UINT16_MAX
#        define RAND32_ITERATIONS 2
#    else
/* standard guarantees RAND_MAX to be at least 32767 */
#        define RAND32_ITERATIONS 3
#    endif

uint32_t _anjay_rand32(anjay_rand_seed_t *seed) {
    uint32_t result = 0;
    int i;
    for (i = 0; i < RAND32_ITERATIONS; ++i) {
        result *= (uint32_t) AVS_RAND_MAX + 1;
        result += (uint32_t) avs_rand_r(seed);
    }
    return result;
}
#endif

int _anjay_copy_string_list(AVS_LIST(const anjay_string_t) *outptr,
                            AVS_LIST(const anjay_string_t) input) {
    assert(!*outptr);
    AVS_LIST(const anjay_string_t) *endptr = outptr;

    AVS_LIST_ITERATE(input) {
        size_t len = strlen(input->c_str) + 1;
        if (!(*endptr = (AVS_LIST(anjay_string_t)) AVS_LIST_NEW_BUFFER(len))) {
            anjay_log(ERROR, "out of memory");
            AVS_LIST_CLEAR(outptr);
            return -1;
        }

        memcpy((char *) (intptr_t) (*endptr)->c_str, input->c_str, len);
        AVS_LIST_ADVANCE_PTR(&endptr);
    }
    return 0;
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
            anjay_log(ERROR, "out of memory");
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

bool anjay_binding_mode_valid(const char *binding_mode) {
    static const char *const VALID_BINDINGS[] = { "U",  "UQ", "S",
                                                  "SQ", "US", "UQS" };
    for (size_t i = 0; i < AVS_ARRAY_SIZE(VALID_BINDINGS); ++i) {
        if (strcmp(binding_mode, VALID_BINDINGS[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int append_string_query_arg(AVS_LIST(const anjay_string_t) *list,
                                   const char *name,
                                   const char *value) {
    const size_t size = strlen(name) + sizeof("=") + strlen(value);
    AVS_LIST(anjay_string_t) arg =
            (AVS_LIST(anjay_string_t)) AVS_LIST_NEW_BUFFER(size);

    if (!arg
            || avs_simple_snprintf(arg->c_str, size, "%s=%s", name, value)
                           < 0) {
        AVS_LIST_CLEAR(&arg);
    } else {
        AVS_LIST_APPEND(list, arg);
    }

    return !arg ? -1 : 0;
}

AVS_LIST(const anjay_string_t)
_anjay_make_query_string_list(const char *version,
                              const char *endpoint_name,
                              const int64_t *lifetime,
                              const char *binding_mode,
                              const char *sms_msisdn) {
    AVS_LIST(const anjay_string_t) list = NULL;

    if (version && append_string_query_arg(&list, "lwm2m", version)) {
        goto fail;
    }

    if (endpoint_name && append_string_query_arg(&list, "ep", endpoint_name)) {
        goto fail;
    }

    if (lifetime) {
        assert(*lifetime > 0);
        size_t lt_size = sizeof("lt=") + 16;
        AVS_LIST(anjay_string_t) lt =
                (AVS_LIST(anjay_string_t)) AVS_LIST_NEW_BUFFER(lt_size);
        if (!lt
                || avs_simple_snprintf(lt->c_str, lt_size, "lt=%" PRId64,
                                       *lifetime)
                               < 0) {
            goto fail;
        }
        AVS_LIST_APPEND(&list, lt);
    }

    if (binding_mode && append_string_query_arg(&list, "b", binding_mode)) {
        goto fail;
    }

    if (sms_msisdn && append_string_query_arg(&list, "sms", sms_msisdn)) {
        goto fail;
    }

    return list;

fail:
    AVS_LIST_CLEAR(&list);
    return NULL;
}

#ifdef ANJAY_TEST
#    include "test/utils.c"
#endif // ANJAY_TEST
