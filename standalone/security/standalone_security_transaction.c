#include <assert.h>
#include <string.h>

#include "standalone_security_transaction.h"
#include "standalone_security_utils.h"

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

typedef enum {
    STANDALONE_TRANSPORT_SECURITY_UNDEFINED,
    STANDALONE_TRANSPORT_NOSEC,
    STANDALONE_TRANSPORT_ENCRYPTED
} standalone_transport_security_t;

typedef struct {
    const char *uri_scheme;
    anjay_socket_transport_t transport;
    standalone_transport_security_t security;
} standalone_transport_info_t;

static const standalone_transport_info_t TRANSPORTS[] = {
    {
        .transport = ANJAY_SOCKET_TRANSPORT_UDP,
        .uri_scheme = "coap",
        .security = STANDALONE_TRANSPORT_NOSEC
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_UDP,
        .uri_scheme = "coaps",
        .security = STANDALONE_TRANSPORT_ENCRYPTED
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_TCP,
        .uri_scheme = "coap+tcp",
        .security = STANDALONE_TRANSPORT_NOSEC
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_TCP,
        .uri_scheme = "coaps+tcp",
        .security = STANDALONE_TRANSPORT_ENCRYPTED
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_SMS,
        .uri_scheme = "tel",
        .security = STANDALONE_TRANSPORT_SECURITY_UNDEFINED
    },
#ifdef ANJAY_WITH_LWM2M11
    {
        .transport = ANJAY_SOCKET_TRANSPORT_NIDD,
        .uri_scheme = "coap+nidd",
        .security = STANDALONE_TRANSPORT_NOSEC
    },
    {
        .transport = ANJAY_SOCKET_TRANSPORT_NIDD,
        .uri_scheme = "coaps+nidd",
        .security = STANDALONE_TRANSPORT_ENCRYPTED
    }
#endif // ANJAY_WITH_LWM2M11
};

static const standalone_transport_info_t *
_standalone_transport_info_by_uri_scheme(const char *uri_or_scheme) {
    if (!uri_or_scheme) {
        security_log(ERROR, _("URL scheme not specified"));
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

    security_log(WARNING, _("unsupported URI scheme: ") "%s", uri_or_scheme);
    return NULL;
}

static bool uri_protocol_matching(anjay_security_mode_t security_mode,
                                  const char *uri) {
    const standalone_transport_info_t *transport_info =
            _standalone_transport_info_by_uri_scheme(uri);
    if (!transport_info) {
        return false;
    }
    if (transport_info->security == STANDALONE_TRANSPORT_SECURITY_UNDEFINED) {
        // URI scheme does not specify security,
        // so it is valid for all security modes
        return true;
    }

    const bool is_secure_uri =
            (transport_info->security == STANDALONE_TRANSPORT_ENCRYPTED);
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
#if defined(ANJAY_WITH_SECURITY_STRUCTURED) \
        || defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT)
    case SEC_KEY_AS_KEY_EXTERNAL:
    case SEC_KEY_AS_KEY_OWNED:
        return expected_tag
               && value->value.key.info.source != AVS_CRYPTO_DATA_SOURCE_EMPTY
               && value->value.key.info.type == *expected_tag;
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
    default:
        AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
        return false;
    }
}

#define LOG_VALIDATION_FAILED(SecInstance, ...)                \
    security_log(WARNING, "/%u/%u: " AVS_VARARG0(__VA_ARGS__), \
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
    if (_standalone_sec_validate_security_mode((int32_t) it->security_mode)) {
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
#ifdef ANJAY_WITH_SMS
    if (it->present_resources[SEC_RES_SMS_SECURITY_MODE]) {
        if (_standalone_sec_validate_sms_security_mode(
                    (int32_t) it->sms_security_mode)) {
            LOG_VALIDATION_FAILED(it, "SMS Security mode %d not supported",
                                  (int) it->sms_security_mode);
            return -1;
        }
        if ((it->sms_security_mode == ANJAY_SMS_SECURITY_DTLS_PSK
             || it->sms_security_mode == ANJAY_SMS_SECURITY_SECURE_PACKET)
                && (!it->present_resources[SEC_RES_SMS_BINDING_KEY_PARAMS]
                    || !it->present_resources[SEC_RES_SMS_BINDING_SECRET_KEYS]
                    || !sec_key_or_data_valid(
                               &it->sms_key_params,
                               it->sms_security_mode
                                               == ANJAY_SMS_SECURITY_DTLS_PSK
                                       ? &(const avs_crypto_security_info_tag_t) { AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY }
                                       : NULL)
                    || !sec_key_or_data_valid(
                               &it->sms_secret_key,
                               it->sms_security_mode
                                               == ANJAY_SMS_SECURITY_DTLS_PSK
                                       ? &(const avs_crypto_security_info_tag_t) { AVS_CRYPTO_SECURITY_INFO_PSK_KEY }
                                       : NULL))) {
            LOG_VALIDATION_FAILED(
                    it, "SMS security credentials not fully configured");
            return -1;
        }
    }
#endif // ANJAY_WITH_SMS
#ifdef ANJAY_WITH_LWM2M11
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
#endif // ANJAY_WITH_LWM2M11
    return 0;
}

static int sec_object_validate(anjay_t *anjay, sec_repr_t *repr) {
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
            const standalone_transport_info_t *transport_info =
                    _standalone_transport_info_by_uri_scheme(it->server_uri);
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

#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
static avs_error_t sec_key_store(sec_key_or_data_t *sec_key,
                                 avs_crypto_security_info_tag_t tag,
                                 avs_crypto_prng_ctx_t *prng_ctx,
                                 const char *query) {
    avs_crypto_security_info_union_t src_desc = {
        .type = tag,
        .source = AVS_CRYPTO_DATA_SOURCE_BUFFER,
        .info = {
            .buffer = {
                .buffer = sec_key->value.data.data,
                .buffer_size = sec_key->value.data.size
            }
        }
    };
    switch (tag) {
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
    case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
        return avs_crypto_pki_engine_certificate_store(
                query,
                AVS_CONTAINER_OF(&src_desc, avs_crypto_certificate_chain_info_t,
                                 desc));
    case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
        return avs_crypto_pki_engine_key_store(
                query,
                AVS_CONTAINER_OF(&src_desc, avs_crypto_private_key_info_t,
                                 desc),
                prng_ctx);
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
    case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
        return avs_crypto_psk_engine_identity_store(
                query, AVS_CONTAINER_OF(&src_desc,
                                        avs_crypto_psk_identity_info_t, desc));
    case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
        return avs_crypto_psk_engine_key_store(
                query,
                AVS_CONTAINER_OF(&src_desc, avs_crypto_psk_key_info_t, desc));
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
    default:
        AVS_UNREACHABLE("Unexpected tag value");
        return avs_errno(AVS_EINVAL);
    }
}

static int
maybe_move_sec_key_to_hsm(sec_instance_t *instance,
                          sec_key_or_data_t *sec_key,
                          avs_crypto_security_info_tag_t tag,
                          const char *tag_str,
                          avs_crypto_prng_ctx_t *prng_ctx,
                          standalone_security_hsm_query_cb_t *query_cb,
                          void *query_cb_arg) {
    if (sec_key->type != SEC_KEY_AS_DATA || !sec_key->value.data.size
            || !query_cb) {
        return 0;
    }
    const char *query =
            query_cb(instance->iid,
                     instance->present_resources[SEC_RES_SHORT_SERVER_ID]
                             ? instance->ssid
                             : ANJAY_SSID_BOOTSTRAP,
                     sec_key->value.data.data, sec_key->value.data.size,
                     query_cb_arg);
    if (!query) {
        security_log(ERROR,
                     _("Generating HSM query string for ") "%s" _(" failed"),
                     tag_str);
        return -1;
    }
    if (avs_is_err(sec_key_store(sec_key, tag, prng_ctx, query))) {
        security_log(ERROR, _("Could not store ") "%s" _(" in HSM"), tag_str);
        return -1;
    }
    sec_key_or_data_t new_sec_key;
    memset(&new_sec_key, 0, sizeof(new_sec_key));
    avs_crypto_security_info_union_t dst_desc = {
        .type = tag,
        .source = AVS_CRYPTO_DATA_SOURCE_ENGINE,
        .info = {
            .engine = {
                .query = query
            }
        }
    };
    int result;
    switch (tag) {
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
    case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
        if ((result = _standalone_sec_init_certificate_chain_resource(
                     &new_sec_key, SEC_KEY_AS_KEY_OWNED,
                     AVS_CONTAINER_OF(&dst_desc,
                                      avs_crypto_certificate_chain_info_t,
                                      desc)))) {
            avs_crypto_pki_engine_certificate_rm(query);
        }
        break;
    case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
        if ((result = _standalone_sec_init_private_key_resource(
                     &new_sec_key, SEC_KEY_AS_KEY_OWNED,
                     AVS_CONTAINER_OF(&dst_desc, avs_crypto_private_key_info_t,
                                      desc)))) {
            avs_crypto_pki_engine_key_rm(query);
        }
        break;
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
    case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
        if ((result = _standalone_sec_init_psk_identity_resource(
                     &new_sec_key, SEC_KEY_AS_KEY_OWNED,
                     AVS_CONTAINER_OF(&dst_desc, avs_crypto_psk_identity_info_t,
                                      desc)))) {
            avs_crypto_psk_engine_identity_rm(query);
        }
        break;
    case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
        if ((result = _standalone_sec_init_psk_key_resource(
                     &new_sec_key, SEC_KEY_AS_KEY_OWNED,
                     AVS_CONTAINER_OF(&dst_desc, avs_crypto_psk_key_info_t,
                                      desc)))) {
            avs_crypto_psk_engine_key_rm(query);
        }
        break;
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
    default:
        AVS_UNREACHABLE("Unexpected tag value");
        result = -1;
    }
    if (result) {
        security_log(ERROR,
                     _("Could not allocate new sec_key_or_data_t object"));
        return result;
    }
    _standalone_sec_key_or_data_cleanup(sec_key, true);
    assert(!new_sec_key.prev_ref);
    assert(!new_sec_key.next_ref);
    *sec_key = new_sec_key;
    return 0;
}

static int sec_object_process_keys(sec_repr_t *repr,
                                   avs_crypto_prng_ctx_t *prng_ctx) {
    AVS_LIST(sec_instance_t) instance;
    AVS_LIST_FOREACH(instance, repr->instances) {
        switch (instance->security_mode) {
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
        case ANJAY_SECURITY_PSK:
            if (maybe_move_sec_key_to_hsm(
                        instance, &instance->public_cert_or_psk_identity,
                        AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY, "PSK identity",
                        prng_ctx, repr->hsm_config.psk_identity_cb,
                        repr->hsm_config.psk_identity_cb_arg)) {
                return -1;
            }
            if (maybe_move_sec_key_to_hsm(
                        instance, &instance->private_cert_or_psk_key,
                        AVS_CRYPTO_SECURITY_INFO_PSK_KEY, "PSK key", prng_ctx,
                        repr->hsm_config.psk_key_cb,
                        repr->hsm_config.psk_key_cb_arg)) {
                return -1;
            }
            break;
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
        case ANJAY_SECURITY_CERTIFICATE:
        case ANJAY_SECURITY_EST:
            if (maybe_move_sec_key_to_hsm(
                        instance, &instance->public_cert_or_psk_identity,
                        AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN,
                        "public certificate", prng_ctx,
                        repr->hsm_config.public_cert_cb,
                        repr->hsm_config.public_cert_cb_arg)) {
                return -1;
            }
            if (maybe_move_sec_key_to_hsm(
                        instance, &instance->private_cert_or_psk_key,
                        AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY, "private key",
                        prng_ctx, repr->hsm_config.private_key_cb,
                        repr->hsm_config.private_key_cb_arg)) {
                return -1;
            }
            break;
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
        default:
            // Do nothing
            break;
        }
#    if defined(ANJAY_WITH_SMS) \
            && defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)
        if (instance->present_resources[SEC_RES_SMS_SECURITY_MODE]
                && instance->sms_security_mode == ANJAY_SMS_SECURITY_DTLS_PSK) {
            if (instance->present_resources[SEC_RES_SMS_BINDING_KEY_PARAMS]
                    && maybe_move_sec_key_to_hsm(
                               instance, &instance->sms_key_params,
                               AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY,
                               "SMS PSK identity", prng_ctx,
                               repr->hsm_config.sms_psk_identity_cb,
                               repr->hsm_config.sms_psk_identity_cb_arg)) {
                return -1;
            }
            if (instance->present_resources[SEC_RES_SMS_BINDING_SECRET_KEYS]
                    && maybe_move_sec_key_to_hsm(
                               instance, &instance->sms_secret_key,
                               AVS_CRYPTO_SECURITY_INFO_PSK_KEY, "SMS PSK key",
                               prng_ctx, repr->hsm_config.sms_psk_key_cb,
                               repr->hsm_config.sms_psk_key_cb_arg)) {
                return -1;
            }
        }
#    endif /* defined(ANJAY_WITH_SMS) && \
              defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE) */
    }
    return 0;
}
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT

int _standalone_sec_object_validate_and_process_keys(anjay_t *anjay,
                                                     sec_repr_t *repr) {
    int result = sec_object_validate(anjay, repr);
#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
    if (!result) {
        // NOTE: THIS IS A HACK. We move key material to HSM storage during the
        // validation stage, because:
        // - We cannot do it during the write stage, because we need to know
        //   what type of key we're dealing with, and the security mode might be
        //   written later.
        // - We shouldn't really do it at the commit stage, because the commit
        //   operation is supposed to be as unlikely to fail as possible, and
        //   storing keys on HSM can fail easily.
        // So we exploit the fact that the act of moving a key to HSM is
        // "transparent" in terms of the semantic data model state - i.e.,
        // whether the operation is successful or not, the data model contains
        // the same information as far as the LwM2M spec is concerned. In other
        // words, what we do here does not change the data model, only its
        // internal representation, so we should be safe doing that during the
        // validation stage.
        result = sec_object_process_keys(repr, repr->prng_ctx);
    }
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
    return result;
}

int _standalone_sec_transaction_begin_impl(sec_repr_t *repr) {
    assert(!repr->saved_instances);
    assert(!repr->in_transaction);
    repr->saved_instances = _standalone_sec_clone_instances(repr);
    if (!repr->saved_instances && repr->instances) {
        return ANJAY_ERR_INTERNAL;
    }
    repr->saved_modified_since_persist = repr->modified_since_persist;
    repr->in_transaction = true;
    return 0;
}

int _standalone_sec_transaction_commit_impl(sec_repr_t *repr) {
    assert(repr->in_transaction);
    _standalone_sec_destroy_instances(&repr->saved_instances, true);
    repr->in_transaction = false;
    return 0;
}

int _standalone_sec_transaction_validate_impl(anjay_t *anjay,
                                              sec_repr_t *repr) {
    assert(repr->in_transaction);
    return _standalone_sec_object_validate_and_process_keys(anjay, repr);
}

int _standalone_sec_transaction_rollback_impl(sec_repr_t *repr) {
    assert(repr->in_transaction);
    _standalone_sec_destroy_instances(&repr->instances, true);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    repr->modified_since_persist = repr->saved_modified_since_persist;
    repr->in_transaction = false;
    return 0;
}
