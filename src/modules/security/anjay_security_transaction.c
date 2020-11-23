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

#ifdef ANJAY_WITH_MODULE_SECURITY

#    include <assert.h>
#    include <string.h>

#    include "anjay_security_transaction.h"
#    include "anjay_security_utils.h"

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_utils_core.h>

VISIBILITY_SOURCE_BEGIN

typedef struct {
    anjay_ssid_t ssid;
    anjay_socket_transport_t transport;
} ssid_transport_pair_t;

static int
ssid_transport_pair_cmp(const void *a_, const void *b_, size_t element_size) {
    assert(element_size == sizeof(ssid_transport_pair_t));
    (void) element_size;
    const ssid_transport_pair_t *a = (const ssid_transport_pair_t *) a_;
    const ssid_transport_pair_t *b = (const ssid_transport_pair_t *) b_;
    if (a->ssid != b->ssid) {
        return a->ssid - b->ssid;
    }
    return (int) a->transport - (int) b->transport;
}

static bool uri_protocol_matching(anjay_security_mode_t security_mode,
                                  const char *uri) {
    const anjay_transport_info_t *transport_info =
            _anjay_transport_info_by_uri_scheme(uri);
    if (!transport_info) {
        return false;
    }
    if (transport_info->security == ANJAY_TRANSPORT_SECURITY_UNDEFINED) {
        // URI scheme does not specify security,
        // so it is valid for all security modes
        return true;
    }

    const bool is_secure_uri =
            (transport_info->security == ANJAY_TRANSPORT_ENCRYPTED);
    const bool needs_secure_uri = (security_mode != ANJAY_SECURITY_NOSEC);
    return is_secure_uri == needs_secure_uri;
}

static bool sec_key_or_data_valid(const sec_key_or_data_t *value) {
    switch (value->type) {
    case SEC_KEY_AS_DATA:
        return value->value.data.data;
    default:
        AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
        return false;
    }
}

#    define LOG_VALIDATION_FAILED(SecInstance, ...)                     \
        do {                                                            \
            char buffer[256];                                           \
            int offset = snprintf(buffer, sizeof(buffer),               \
                                  "/%u/%u: ", ANJAY_DM_OID_SECURITY,    \
                                  (unsigned) (SecInstance)->iid);       \
            if (offset < 0) {                                           \
                offset = 0;                                             \
            }                                                           \
            snprintf(&buffer[offset], sizeof(buffer) - (size_t) offset, \
                     __VA_ARGS__);                                      \
            security_log(WARNING, "%s", buffer);                        \
        } while (0)

static int validate_instance(sec_instance_t *it) {
    if (!it->server_uri) {
        LOG_VALIDATION_FAILED(it,
                              "missing mandatory 'Server URI' resource value");
        return -1;
    }
    if (!it->has_is_bootstrap) {
        LOG_VALIDATION_FAILED(
                it, "missing mandatory 'Bootstrap Server' resource value");
        return -1;
    }
    if (!it->has_security_mode) {
        LOG_VALIDATION_FAILED(
                it, "missing mandatory 'Security Mode' resource value");
        return -1;
    }
    if (!it->is_bootstrap && !it->has_ssid) {
        LOG_VALIDATION_FAILED(
                it, "missing mandatory 'Short Server ID' resource value");
        return -1;
    }
    if (_anjay_sec_validate_security_mode((int32_t) it->security_mode)) {
        LOG_VALIDATION_FAILED(it, "Security mode %d not supported",
                              (int) it->security_mode);
        return -1;
    }
    if (!uri_protocol_matching(it->security_mode, it->server_uri)) {
        LOG_VALIDATION_FAILED(
                it,
                "Incorrect protocol in Server Uri '%s' due to security "
                "configuration (coap:// instead of coaps:// or vice versa?)",
                it->server_uri);
        return -1;
    }
    if (it->security_mode != ANJAY_SECURITY_NOSEC
            && it->security_mode != ANJAY_SECURITY_EST) {
        if (!sec_key_or_data_valid(&it->public_cert_or_psk_identity)
                || !sec_key_or_data_valid(&it->private_cert_or_psk_key)) {
            LOG_VALIDATION_FAILED(it,
                                  "security credentials not fully configured");
            return -1;
        }
    }
    if (it->has_sms_security_mode) {
        if (_anjay_sec_validate_sms_security_mode(
                    (int32_t) it->sms_security_mode)) {
            LOG_VALIDATION_FAILED(it, "SMS Security mode %d not supported",
                                  (int) it->sms_security_mode);
            return -1;
        }
        if ((it->sms_security_mode == ANJAY_SMS_SECURITY_DTLS_PSK
             || it->sms_security_mode == ANJAY_SMS_SECURITY_SECURE_PACKET)
                && (!it->sms_key_params.data || !it->sms_secret_key.data)) {
            LOG_VALIDATION_FAILED(
                    it, "SMS security credentials not fully configured");
            return -1;
        }
    }
    return 0;
}

int _anjay_sec_object_validate(anjay_t *anjay, sec_repr_t *repr) {
    AVS_LIST(ssid_transport_pair_t) seen_ssid_transport_pairs = NULL;
    AVS_LIST(sec_instance_t) it;
    int result = 0;
    bool bootstrap_server_present = false;
    (void) anjay;

    AVS_LIST_FOREACH(it, repr->instances) {
        /* Assume something will go wrong */
        result = ANJAY_ERR_BAD_REQUEST;
        if (validate_instance(it)) {
            goto finish;
        }

        if (it->is_bootstrap) {
            if (bootstrap_server_present) {
                goto finish;
            }
            bootstrap_server_present = true;
        } else {
            const anjay_transport_info_t *transport_info =
                    _anjay_transport_info_by_uri_scheme(it->server_uri);
            if (!transport_info
                    || !AVS_LIST_INSERT_NEW(ssid_transport_pair_t,
                                            &seen_ssid_transport_pairs)) {
                result = ANJAY_ERR_INTERNAL;
                goto finish;
            }
            seen_ssid_transport_pairs->ssid = it->ssid;
            seen_ssid_transport_pairs->transport = transport_info->transport;
        }

        /* We are still there - nothing went wrong, continue */
        result = 0;
    }

    if (!result && seen_ssid_transport_pairs) {
        AVS_LIST_SORT(&seen_ssid_transport_pairs, ssid_transport_pair_cmp);
        AVS_LIST(ssid_transport_pair_t) prev = seen_ssid_transport_pairs;
        AVS_LIST(ssid_transport_pair_t) next =
                AVS_LIST_NEXT(seen_ssid_transport_pairs);
        while (next) {
            if (prev->ssid == next->ssid
                    && prev->transport == next->transport) {
                /* Duplicate found */
                result = ANJAY_ERR_BAD_REQUEST;
                break;
            }
            prev = next;
            next = AVS_LIST_NEXT(next);
        }
    }
finish:
    AVS_LIST_CLEAR(&seen_ssid_transport_pairs);
    return result;
}

int _anjay_sec_transaction_begin_impl(sec_repr_t *repr) {
    assert(!repr->saved_instances);
    assert(!repr->in_transaction);
    repr->saved_instances = _anjay_sec_clone_instances(repr);
    if (!repr->saved_instances && repr->instances) {
        return ANJAY_ERR_INTERNAL;
    }
    repr->saved_modified_since_persist = repr->modified_since_persist;
    repr->in_transaction = true;
    return 0;
}

int _anjay_sec_transaction_commit_impl(sec_repr_t *repr) {
    assert(repr->in_transaction);
    _anjay_sec_destroy_instances(&repr->saved_instances);
    repr->in_transaction = false;
    return 0;
}

int _anjay_sec_transaction_validate_impl(anjay_t *anjay, sec_repr_t *repr) {
    assert(repr->in_transaction);
    return _anjay_sec_object_validate(anjay, repr);
}

int _anjay_sec_transaction_rollback_impl(sec_repr_t *repr) {
    assert(repr->in_transaction);
    _anjay_sec_destroy_instances(&repr->instances);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    repr->modified_since_persist = repr->saved_modified_since_persist;
    repr->in_transaction = false;
    return 0;
}

#endif // ANJAY_WITH_MODULE_SECURITY
