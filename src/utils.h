/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_UTILS_H
#define	ANJAY_UTILS_H

#include <avsystem/commons/list.h>

#include <anjay_modules/utils.h>

#include <anjay/anjay.h>

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define anjay_log(...) _anjay_log(anjay, __VA_ARGS__)

#define ANJAY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ANJAY_MAX(a, b) ((a) > (b) ? (a) : (b))

int _anjay_safe_strtoll(const char *in, long long *value);
int _anjay_safe_strtod(const char *in, double *value);

#define ANJAY_MAX_URL_PROTO_SIZE sizeof("coaps")
#define ANJAY_MAX_URL_HOSTNAME_SIZE (256 - ANJAY_MAX_URL_PROTO_SIZE - (sizeof("://" ":0") - 1))
#define ANJAY_MAX_URL_PORT_SIZE sizeof("65535")

typedef struct anjay_url {
    char protocol[ANJAY_MAX_URL_PROTO_SIZE];
    char host[ANJAY_MAX_URL_HOSTNAME_SIZE];
    char port[ANJAY_MAX_URL_PORT_SIZE];
} anjay_url_t;

int _anjay_parse_url(const char *raw_url, anjay_url_t *parsed_url);

#ifdef ANJAY_TEST
typedef uint32_t anjay_rand_seed_t;
#else
typedef unsigned anjay_rand_seed_t;
#endif
uint32_t _anjay_rand32(anjay_rand_seed_t *seed);

ssize_t _anjay_snprintf(char *buffer,
                        size_t buffer_size,
                        const char *fmt,
                        ...) AVS_F_PRINTF(3, 4);

typedef struct anjay_string {
    char c_str[1]; // actually a FAM, but a struct must not consist of FAM only
} anjay_string_t;

AVS_LIST(const anjay_string_t)
_anjay_make_string_list(const char *string,
                        ... /* strings */) AVS_F_SENTINEL;

AVS_LIST(const anjay_string_t)
_anjay_make_query_string_list(const char *version,
                              const char *endpoint_name,
                              const int64_t *lifetime,
                              anjay_binding_mode_t queue_mode);

static inline bool _anjay_is_power_of_2(size_t value) {
    return value > 0 && !(value & (value - 1));
}

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_UTILS_H
