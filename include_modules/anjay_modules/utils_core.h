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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_UTILS_CORE_H
#define ANJAY_INCLUDE_ANJAY_MODULES_UTILS_CORE_H

#include <avsystem/commons/list.h>

#ifdef WITH_AVS_LOG
#    include <avsystem/commons/log.h>
#    define _anjay_log(...) avs_log(__VA_ARGS__)
#else
#    define _anjay_log(...) ((void) 0)
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_string {
    char c_str[1]; // actually a FAM, but a struct must not consist of FAM only
} anjay_string_t;

#define ANJAY_MAX_URL_RAW_LENGTH 256
#define ANJAY_MAX_URL_HOSTNAME_SIZE \
    (ANJAY_MAX_URL_RAW_LENGTH       \
     - sizeof("coaps://"            \
              ":0"))
#define ANJAY_MAX_URL_PORT_SIZE sizeof("65535")

typedef enum {
    ANJAY_URL_PROTOCOL_COAP,
    ANJAY_URL_PROTOCOL_COAPS
} anjay_url_protocol_t;

typedef struct anjay_url {
    anjay_url_protocol_t protocol;
    char host[ANJAY_MAX_URL_HOSTNAME_SIZE];
    char port[ANJAY_MAX_URL_PORT_SIZE];
    AVS_LIST(const anjay_string_t) uri_path;
    AVS_LIST(const anjay_string_t) uri_query;
} anjay_url_t;

#define ANJAY_URL_EMPTY   \
    (anjay_url_t) {       \
        .uri_path = NULL, \
        .uri_query = NULL \
    }

#define ANJAY_FOREACH_BREAK INT_MIN
#define ANJAY_FOREACH_CONTINUE 0

/**
 * Parses endpoint name into hostname, path and port number. Additionally
 * extracts Uri-Path and Uri-Query options as (unsecaped) strings.
 *
 * NOTE: @p out_parsed_url MUST be initialized with ANJAY_URL_EMPTY or otherwise
 * the behavior is undefined.
 */
int _anjay_url_parse(const char *raw_url, anjay_url_t *out_parsed_url);

int _anjay_url_copy(anjay_url_t *out_copy, const anjay_url_t *source);

/**
 * Frees any allocated memory by @ref _anjay_parse_url
 */
void _anjay_url_cleanup(anjay_url_t *url);

typedef char anjay_binding_mode_t[8];

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_UTILS_CORE_H */
