/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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

static bool
sec_key_or_data_valid(const sec_key_or_data_t *value,
                      const avs_crypto_security_info_tag_t *expected_tag) {
    (void) expected_tag;
    switch (value->type) {
    case SEC_KEY_AS_DATA:
        return value->value.data.data;
#    if defined(ANJAY_WITH_SECURITY_STRUCTURED)
    case SEC_KEY_AS_KEY_EXTERNAL:
    case SEC_KEY_AS_KEY_OWNED:
        return expected_tag
               && value->value.key.info.source != AVS_CRYPTO_DATA_SOURCE_EMPTY
               && value->value.key.info.type == *expected_tag;
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
              defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
    default:
        AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
        return false;
    }
}

#    define LOG_VALIDATION_FAILED(SecInstance, ...)           \
        security_log(                                         \
                WARNING, "/%u/%u: " AVS_VARARG0(__VA_ARGS__), \
                ANJAY_DM_OID_SECURITY,                        \
                (unsigned) (SecInstance)->iid AVS_VARARG_REST(__VA_ARGS__))

static int validate_instance(sec_instance_t *it) {
    if (!it->server_uri) {
        LOG_VALIDATION_FAILED(it,
                              "missing mandatory 'Server URI' resource value");
        return -1;
    }
    if (!it->present_resources[SEC_RES_BOOTSTRAP_SERVER]) {
        LOG_VALIDATION_FAILED(
                it, "missing mandatory 'Bootstrap Server' resource value");
        return -1;
    }
    if (!it->present_resources[SEC_RES_SECURITY_MODE]) {
        LOG_VALIDATION_FAILED(
                it, "missing mandatory 'Security Mode' resource value");
        return -1;
    }
    if (!it->is_bootstrap && !it->present_resources[SEC_RES_SHORT_SERVER_ID]) {
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
        if (!sec_key_or_data_valid(
                    &it->public_cert_or_psk_identity,
                    &(const avs_crypto_security_info_tag_t) {
                            it->security_mode == ANJAY_SECURITY_PSK
                                    ? AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY
                                    : AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN })
                || !sec_key_or_data_valid(
                           &it->private_cert_or_psk_key,
                           &(const avs_crypto_security_info_tag_t) {
                                   it->security_mode == ANJAY_SECURITY_PSK
                                           ? AVS_CRYPTO_SECURITY_INFO_PSK_KEY
                                           : AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY })) {
            LOG_VALIDATION_FAILED(it,
                                  "security credentials not fully configured");
            return -1;
        }
    }
#    ifdef ANJAY_WITH_LWM2M11
    if (it->matching_type > 3) {
        LOG_VALIDATION_FAILED(it, "Matching Type set to an invalid value");
        return -1;
    }
    if (it->matching_type == 2) {
        LOG_VALIDATION_FAILED(it, "SHA-384 Matching Type is not supported");
        return -1;
    }
    if (it->certificate_usage > 3) {
        LOG_VALIDATION_FAILED(it, "Certificate Usage set to an invalid value");
        return -1;
    }
#    endif // ANJAY_WITH_LWM2M11
    return 0;
}

static int sec_object_validate(anjay_unlocked_t *anjay, sec_repr_t *repr) {
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

int _anjay_sec_object_validate_and_process_keys(anjay_unlocked_t *anjay,
                                                sec_repr_t *repr) {
    int result = sec_object_validate(anjay, repr);
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
    _anjay_sec_destroy_instances(&repr->saved_instances, true);
    repr->in_transaction = false;
    return 0;
}

int _anjay_sec_transaction_validate_impl(anjay_unlocked_t *anjay,
                                         sec_repr_t *repr) {
    assert(repr->in_transaction);
    return _anjay_sec_object_validate_and_process_keys(anjay, repr);
}

int _anjay_sec_transaction_rollback_impl(sec_repr_t *repr) {
    assert(repr->in_transaction);
    _anjay_sec_destroy_instances(&repr->instances, true);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    repr->modified_since_persist = repr->saved_modified_since_persist;
    repr->in_transaction = false;
    return 0;
}

#endif // ANJAY_WITH_MODULE_SECURITY
