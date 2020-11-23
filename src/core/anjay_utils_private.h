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

#ifndef ANJAY_UTILS_PRIVATE_H
#define ANJAY_UTILS_PRIVATE_H

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_socket.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/option.h>
#include <avsystem/coap/token.h>

#include <anjay_modules/anjay_raw_buffer.h>
#include <anjay_modules/anjay_utils_core.h>

#include <anjay/dm.h>

#include <stdbool.h>
#include <stddef.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#define anjay_log(...) _anjay_log(anjay, __VA_ARGS__)

int _anjay_safe_strtoll(const char *in, long long *value);
int _anjay_safe_strtoull(const char *in, unsigned long long *value);
int _anjay_safe_strtod(const char *in, double *value);

AVS_LIST(const anjay_string_t)
_anjay_make_string_list(const char *string, ... /* strings */) AVS_F_SENTINEL;

// Const pointer cast is to ensure, that passed NULL will have the proper type,
// regardless of toolchain used
#define ANJAY_MAKE_STRING_LIST(...) \
    _anjay_make_string_list(__VA_ARGS__, (const char *) NULL)

typedef enum { ANJAY_LWM2M_DUMMY_VERSION } anjay_lwm2m_dummy_version_t;
#define anjay_lwm2m_version_t anjay_lwm2m_dummy_version_t
#define ANJAY_LWM2M_VERSION_1_0 ANJAY_LWM2M_DUMMY_VERSION

static inline const char *
_anjay_lwm2m_version_as_string(anjay_lwm2m_version_t version) {
    (void) version;
    return "1.0";
}

bool _anjay_socket_is_online(avs_net_socket_t *socket);

avs_error_t _anjay_coap_add_query_options(avs_coap_options_t *opts,
                                          const anjay_lwm2m_version_t *version,
                                          const char *endpoint_name,
                                          const int64_t *lifetime,
                                          const char *binding_mode,
                                          bool lwm2m11_queue_mode,
                                          const char *sms_msisdn);

avs_error_t
_anjay_coap_add_string_options(avs_coap_options_t *opts,
                               AVS_LIST(const anjay_string_t) strings,
                               uint16_t opt_number);

static inline size_t _anjay_max_power_of_2_not_greater_than(size_t bound) {
    int exponent = -1;
    while (bound) {
        bound >>= 1;
        ++exponent;
    }
    return (exponent >= 0) ? ((size_t) 1 << exponent) : 0;
}

static inline const char *_anjay_token_to_string(const avs_coap_token_t *token,
                                                 char *out_buffer,
                                                 size_t out_size) {
    if (avs_hexlify(out_buffer, out_size, NULL, token->bytes, token->size)) {
        AVS_UNREACHABLE("avs_hexlify() failed");
    }
    return out_buffer;
}

#define ANJAY_TOKEN_TO_STRING(token)                                       \
    _anjay_token_to_string(&(token),                                       \
                           &(char[sizeof((token).bytes) * 2 + 1]){ 0 }[0], \
                           sizeof((token).bytes) * 2 + 1)

static inline bool _anjay_was_session_resumed(avs_net_socket_t *socket) {
    avs_net_socket_opt_value_t session_resumed;
    if (avs_is_err(avs_net_socket_get_opt(socket,
                                          AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                          &session_resumed))) {
        return false;
    }
    return session_resumed.flag;
}

int _anjay_copy_tls_ciphersuites(avs_net_socket_tls_ciphersuites_t *dest,
                                 const avs_net_socket_tls_ciphersuites_t *src);

anjay_transport_set_t
_anjay_transport_set_remove_unavailable(anjay_t *anjay,
                                        anjay_transport_set_t set);

bool _anjay_socket_transport_included(anjay_transport_set_t set,
                                      anjay_socket_transport_t transport);

bool _anjay_socket_transport_is_online(anjay_t *anjay,
                                       anjay_socket_transport_t transport);

#define ANJAY_SMS_URI_SCHEME "tel"

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_UTILS_PRIVATE_H
