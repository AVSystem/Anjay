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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_UTILS_CORE_H
#define ANJAY_INCLUDE_ANJAY_MODULES_UTILS_CORE_H

#ifdef ANJAY_WITH_EVENT_LOOP
#    include <stdatomic.h>
#endif // ANJAY_WITH_EVENT_LOOP

#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_url.h>

#ifdef ANJAY_WITH_THREAD_SAFETY
#    include <avsystem/commons/avs_mutex.h>
#endif // ANJAY_WITH_THREAD_SAFETY

#ifdef ANJAY_WITH_LOGS
#    ifndef AVS_COMMONS_WITH_AVS_LOG
#        error "ANJAY_WITH_LOGS requires avs_log to be enabled"
#    endif
// these macros interfere with avs_log() macro implementation
#    ifdef TRACE
#        undef TRACE
#    endif
#    ifdef DEBUG
#        undef DEBUG
#    endif
#    ifdef INFO
#        undef INFO
#    endif
#    ifdef WARNING
#        undef WARNING
#    endif
#    ifdef ERROR
#        undef ERROR
#    endif
#    include <avsystem/commons/avs_log.h>
#    define _anjay_log(...) avs_log(__VA_ARGS__)
#else
#    include <stdio.h>
#    define _anjay_log(Module, Level, ...) ((void) sizeof(printf(__VA_ARGS__)))
#endif

#include <anjay/core.h>
#include <anjay/dm.h>

#ifdef ANJAY_WITH_DOWNLOADER
#    include <anjay/download.h>
#endif // ANJAY_WITH_DOWNLOADER

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_EVENT_LOOP
typedef enum {
    ANJAY_EVENT_LOOP_IDLE,
    ANJAY_EVENT_LOOP_RUNNING,
    ANJAY_EVENT_LOOP_INTERRUPT
} anjay_event_loop_status_t;
#endif // ANJAY_WITH_EVENT_LOOP

// Please update this condition if anjay_atomic_fields_t ever gets more fields
#if defined(ANJAY_WITH_EVENT_LOOP)
#    define ANJAY_ATOMIC_FIELDS_DEFINED
#endif // defined(ANJAY_WITH_EVENT_LOOP)

#ifdef ANJAY_ATOMIC_FIELDS_DEFINED
typedef struct {
#    ifdef ANJAY_WITH_EVENT_LOOP
    volatile atomic_int event_loop_status;
#    endif // ANJAY_WITH_EVENT_LOOP
} anjay_atomic_fields_t;
#endif // ANJAY_ATOMIC_FIELDS_DEFINED

#ifdef ANJAY_WITH_THREAD_SAFETY

typedef struct anjay_unlocked_struct anjay_unlocked_t;

struct anjay_struct {
    avs_mutex_t *mutex;
#    ifdef ANJAY_ATOMIC_FIELDS_DEFINED
    anjay_atomic_fields_t atomic_fields;
#    endif // ANJAY_ATOMIC_FIELDS_DEFINED
    avs_max_align_t anjay_unlocked_placeholder;
};

void _anjay_reschedule_coap_sched_job(anjay_unlocked_t *anjay);

#    ifdef ANJAY_WITH_NESTED_FUNCTION_MUTEX_LOCKS

// We are compiling on a reasonably recent version of GCC in Debug mode.
// We are using GCC's nested function extension to make sure that there are
// no "return" statements between the lock and unlock statements.
// If the programmer attempts to use "return" there, they'll get at least
// a warning saying that the return type is wrong, as the nested functions
// return anjay_gcc_nested_function_retval_placeholder_t.

#        pragma GCC diagnostic push
#        pragma GCC diagnostic ignored "-Wc++-compat"
#        pragma GCC diagnostic ignored "-Wpedantic"
typedef struct {
} anjay_gcc_nested_function_retval_placeholder_t;
#        pragma GCC diagnostic pop

#        define ANJAY_MUTEX_LOCK(AnjayUnlockedVar, AnjayLockedVar)    \
            {                                                         \
                AVS_PRAGMA(GCC diagnostic push)                       \
                AVS_PRAGMA(GCC diagnostic ignored "-Wpedantic")       \
                AVS_PRAGMA(GCC diagnostic ignored "-Wshadow")         \
                inline anjay_gcc_nested_function_retval_placeholder_t \
                mutex_lock_nested_function(                           \
                        anjay_unlocked_t *AnjayUnlockedVar) {         \
                    AVS_PRAGMA(GCC diagnostic pop)                    \
                    (void) AnjayUnlockedVar

#        define ANJAY_MUTEX_UNLOCK(AnjayLockedVar)                      \
            AVS_PRAGMA(GCC diagnostic push)                             \
            AVS_PRAGMA(GCC diagnostic ignored "-Wpedantic")             \
            return (anjay_gcc_nested_function_retval_placeholder_t) {}; \
            AVS_PRAGMA(GCC diagnostic pop)                              \
            }                                                           \
            if (!(AnjayLockedVar)                                       \
                    || avs_mutex_lock((AnjayLockedVar)->mutex)) {       \
                _anjay_log(anjay, ERROR, _("Could not lock mutex"));    \
            } else {                                                    \
                mutex_lock_nested_function(                             \
                        (anjay_unlocked_t *) &(AnjayLockedVar)          \
                                ->anjay_unlocked_placeholder);          \
                _anjay_reschedule_coap_sched_job(                       \
                        (anjay_unlocked_t *) &(AnjayLockedVar)          \
                                ->anjay_unlocked_placeholder);          \
                avs_mutex_unlock((AnjayLockedVar)->mutex);              \
            }                                                           \
            }                                                           \
            (void) 0

#        define ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(AnjayLockedVar,            \
                                                AnjayUnlockedVar)          \
            {                                                              \
                AVS_PRAGMA(GCC diagnostic push)                            \
                AVS_PRAGMA(GCC diagnostic ignored "-Wpedantic")            \
                auto inline anjay_gcc_nested_function_retval_placeholder_t \
                mutex_unlock_for_callback_nested_function(anjay_t *);      \
                mutex_unlock_for_callback_nested_function(                 \
                        AVS_CONTAINER_OF(AnjayUnlockedVar,                 \
                                         anjay_t,                          \
                                         anjay_unlocked_placeholder));     \
                                                                           \
                inline anjay_gcc_nested_function_retval_placeholder_t      \
                mutex_unlock_for_callback_nested_function(                 \
                        anjay_t *AnjayLockedVar) {                         \
                    AVS_PRAGMA(GCC diagnostic pop)                         \
                    avs_mutex_unlock((AnjayLockedVar)->mutex)

#        define ANJAY_MUTEX_LOCK_AFTER_CALLBACK(AnjayLockedVar)         \
            if (avs_mutex_lock(AnjayLockedVar->mutex)) {                \
                _anjay_log(anjay, ERROR, _("Could not lock mutex"));    \
            }                                                           \
            AVS_PRAGMA(GCC diagnostic push)                             \
            AVS_PRAGMA(GCC diagnostic ignored "-Wpedantic")             \
            return (anjay_gcc_nested_function_retval_placeholder_t) {}; \
            AVS_PRAGMA(GCC diagnostic pop)                              \
            }                                                           \
            }                                                           \
            (void) 0

#    else // ANJAY_WITH_NESTED_FUNCTION_MUTEX_LOCKS

// We are either not compiling on GCC, or we are compiling in Release mode.
// Use more conventional code - the lock and unlock clauses are still contain
// stray { and } to make sure that they're paired properly.

#        define ANJAY_MUTEX_LOCK(AnjayUnlockedVar, AnjayLockedVar)   \
            if (!(AnjayLockedVar)                                    \
                    || avs_mutex_lock((AnjayLockedVar)->mutex)) {    \
                _anjay_log(anjay, ERROR, _("Could not lock mutex")); \
            } else {                                                 \
                anjay_unlocked_t *AnjayUnlockedVar =                 \
                        (anjay_unlocked_t *) &(AnjayLockedVar)       \
                                ->anjay_unlocked_placeholder;        \
                (void) AnjayUnlockedVar

#        define ANJAY_MUTEX_UNLOCK(AnjayLockedVar)         \
            _anjay_reschedule_coap_sched_job(              \
                    (anjay_unlocked_t *) &(AnjayLockedVar) \
                            ->anjay_unlocked_placeholder); \
            avs_mutex_unlock((AnjayLockedVar)->mutex);     \
            }                                              \
            (void) 0

#        define ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(AnjayLockedVar,       \
                                                AnjayUnlockedVar)     \
            {                                                         \
                anjay_t *AnjayLockedVar =                             \
                        AVS_CONTAINER_OF(AnjayUnlockedVar,            \
                                         anjay_t,                     \
                                         anjay_unlocked_placeholder); \
                avs_mutex_unlock(AnjayLockedVar->mutex)

#        define ANJAY_MUTEX_LOCK_AFTER_CALLBACK(AnjayLockedVar)      \
            if (avs_mutex_lock(AnjayLockedVar->mutex)) {             \
                _anjay_log(anjay, ERROR, _("Could not lock mutex")); \
            }                                                        \
            }                                                        \
            (void) 0

#    endif // ANJAY_WITH_NESTED_FUNCTION_MUTEX_LOCKS

#else // ANJAY_WITH_THREAD_SAFETY

// Thread safety is disabled - use basically no-op blocks, although still
// containing stray { and } to make sure they're paired properly.

typedef anjay_t anjay_unlocked_t;

#    define ANJAY_MUTEX_LOCK(AnjayUnlockedVar, AnjayLockedVar)     \
        if (!(AnjayLockedVar)) {                                   \
            _anjay_log(anjay, ERROR, _("Anjay pointer is NULL"));  \
        } else {                                                   \
            anjay_unlocked_t *AnjayUnlockedVar = (AnjayLockedVar); \
            (void) AnjayUnlockedVar

#    define ANJAY_MUTEX_UNLOCK(AnjayLockedVar) \
        }                                      \
        (void) 0

#    define ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(AnjayLockedVar, AnjayUnlockedVar) \
        {                                                                     \
            anjay_t *AnjayLockedVar = (AnjayUnlockedVar);                     \
            (void) AnjayLockedVar

#    define ANJAY_MUTEX_LOCK_AFTER_CALLBACK(AnjayLockedVar) \
        }                                                   \
        (void) 0

#endif // ANJAY_WITH_THREAD_SAFETY

typedef enum {
    /**
     * Given URI scheme does not imply any security configuration.
     */
    ANJAY_TRANSPORT_SECURITY_UNDEFINED,

    /**
     * Given URI scheme implies unencrypted communication (e.g. "coap", "http").
     */
    ANJAY_TRANSPORT_NOSEC,

    /**
     * Given URI scheme implies encrypted communication (e.g. "coaps", "https").
     */
    ANJAY_TRANSPORT_ENCRYPTED
} anjay_transport_security_t;

/** Set of properties of a transport-specific variant of CoAP. */
typedef struct anjay_transport_info {
    /**
     * CoAP URI scheme part, e.g. "coap"/"coaps"/"coap+tcp"/"coaps+tcp"
     */
    const char *uri_scheme;

    /**
     * Port to use for URIs that do not include one, usually 5683 or 5684
     */
    const char *default_port;

    /**
     * Underlying socket type, e.g. UDP/TCP
     */
    anjay_socket_transport_t transport;

    /**
     * Required avs_commons socket type, e.g. UDP/DTLS/TCP/SSL. NULL if a custom
     * socket type (not creatable using
     * avs_net_(tcp|udp|ssl|dtls)_socket_create()) is required.
     */
    const avs_net_socket_type_t *socket_type;

    /**
     * Security requirements related to uri_scheme.
     */
    anjay_transport_security_t security;
} anjay_transport_info_t;

typedef struct anjay_string {
    char c_str[1]; // actually a FAM, but a struct must not consist of FAM only
} anjay_string_t;

#define ANJAY_MAX_URL_RAW_LENGTH 256
#define ANJAY_MAX_URL_HOSTNAME_SIZE \
    (ANJAY_MAX_URL_RAW_LENGTH       \
     - sizeof("coaps://"            \
              ":0"))
#define ANJAY_MAX_URL_PORT_SIZE sizeof("65535")

typedef struct anjay_url {
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

int _anjay_url_parse_path_and_query(const char *path,
                                    AVS_LIST(const anjay_string_t) *out_path,
                                    AVS_LIST(const anjay_string_t) *out_query);

int _anjay_url_from_avs_url(const avs_url_t *avs_url,
                            anjay_url_t *out_parsed_url);

/**
 * Parses endpoint name into hostname, path and port number. Additionally
 * extracts Uri-Path and Uri-Query options as (unsecaped) strings.
 *
 * NOTE: @p out_parsed_url MUST be initialized with ANJAY_URL_EMPTY or otherwise
 * the behavior is undefined.
 */
int _anjay_url_parse(const char *raw_url, anjay_url_t *out_parsed_url);

/**
 * Frees any allocated memory by @ref _anjay_url_parse
 */
void _anjay_url_cleanup(anjay_url_t *url);

typedef struct {
    char data[8];
} anjay_binding_mode_t;

static inline void _anjay_update_ret(int *var, int new_retval) {
    if (!*var) {
        *var = new_retval;
    }
}

typedef struct anjay_binding_info {
    char letter;
    anjay_socket_transport_t transport;
} anjay_binding_info_t;

int _anjay_security_config_from_dm_unlocked(anjay_unlocked_t *anjay,
                                            anjay_security_config_t *out_config,
                                            const char *raw_url);

const anjay_binding_info_t *
_anjay_binding_info_by_transport(anjay_socket_transport_t transport);

const anjay_transport_info_t *
_anjay_transport_info_by_uri_scheme(const char *uri_or_scheme);

const char *_anjay_default_port_by_url(const anjay_url_t *url);

void _anjay_find_matching_coap_context_and_socket(
        anjay_unlocked_t *anjay,
        const char *raw_url,
        avs_coap_ctx_t **out_coap,
        avs_net_socket_t **out_socket);

typedef struct {
    void *psk_buffer;
    avs_crypto_certificate_chain_info_t *trusted_certs_array;
    avs_crypto_cert_revocation_list_info_t *cert_revocation_lists_array;
    avs_crypto_certificate_chain_info_t *client_cert_array;
    avs_crypto_private_key_info_t *client_key;
    avs_net_socket_dane_tlsa_record_t *dane_tlsa_record;
    avs_net_socket_tls_ciphersuites_t ciphersuites;
} anjay_security_config_cache_t;

void _anjay_security_config_cache_cleanup(anjay_security_config_cache_t *cache);

#ifdef ANJAY_WITH_DOWNLOADER
avs_error_t _anjay_download_unlocked(anjay_unlocked_t *anjay,
                                     const anjay_download_config_t *config,
                                     anjay_download_handle_t *out_handle);

void _anjay_download_abort_unlocked(anjay_unlocked_t *anjay,
                                    anjay_download_handle_t handle);
#endif // ANJAY_WITH_DOWNLOADER

bool _anjay_ongoing_registration_exists_unlocked(anjay_unlocked_t *anjay);

avs_sched_t *_anjay_get_scheduler_unlocked(anjay_unlocked_t *anjay);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_UTILS_CORE_H */
