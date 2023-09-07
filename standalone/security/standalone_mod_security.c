#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "standalone_mod_security.h"
#include "standalone_security_transaction.h"
#include "standalone_security_utils.h"

#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
#    if !defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE) \
            && !defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)
#        error "At least one of AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE or AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE is required for ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT"
#    endif /* !defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE) && \
              !defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE) */
#endif     // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT

static const security_rid_t SECURITY_RESOURCE_ID[] = {
    SEC_RES_LWM2M_SERVER_URI,
    SEC_RES_BOOTSTRAP_SERVER,
    SEC_RES_SECURITY_MODE,
    SEC_RES_PK_OR_IDENTITY,
    SEC_RES_SERVER_PK,
    SEC_RES_SECRET_KEY,
#ifdef ANJAY_WITH_SMS
    SEC_RES_SMS_SECURITY_MODE,
    SEC_RES_SMS_BINDING_KEY_PARAMS,
    SEC_RES_SMS_BINDING_SECRET_KEYS,
    SEC_RES_SERVER_SMS_NUMBER,
#endif // ANJAY_WITH_SMS
    SEC_RES_SHORT_SERVER_ID,
    SEC_RES_CLIENT_HOLD_OFF_TIME,
    SEC_RES_BOOTSTRAP_TIMEOUT,
#ifdef ANJAY_WITH_LWM2M11
    SEC_RES_MATCHING_TYPE,
    SEC_RES_SNI,
    SEC_RES_CERTIFICATE_USAGE,
    SEC_RES_DTLS_TLS_CIPHERSUITE,
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_COAP_OSCORE
    SEC_RES_OSCORE_SECURITY_MODE
#endif // ANJAY_WITH_COAP_OSCORE
};

void _standalone_sec_instance_update_resource_presence(sec_instance_t *inst) {
    // Sets presence of mandatory resources and updates presence of resources
    // which presence is not persisted and depends on resource value
    inst->present_resources[SEC_RES_LWM2M_SERVER_URI] = true;
    inst->present_resources[SEC_RES_BOOTSTRAP_SERVER] = true;
    inst->present_resources[SEC_RES_SECURITY_MODE] = true;
    inst->present_resources[SEC_RES_PK_OR_IDENTITY] = true;
    inst->present_resources[SEC_RES_SERVER_PK] = true;
    inst->present_resources[SEC_RES_SECRET_KEY] = true;
#ifdef ANJAY_WITH_SMS
    inst->present_resources[SEC_RES_SERVER_SMS_NUMBER] = !!inst->sms_number;
#endif // ANJAY_WITH_SMS
    inst->present_resources[SEC_RES_CLIENT_HOLD_OFF_TIME] =
            (inst->holdoff_s >= 0);
    inst->present_resources[SEC_RES_BOOTSTRAP_TIMEOUT] =
            (inst->bs_timeout_s >= 0);
#ifdef ANJAY_WITH_LWM2M11
    inst->present_resources[SEC_RES_MATCHING_TYPE] = (inst->matching_type >= 0);
    inst->present_resources[SEC_RES_SNI] = !!inst->server_name_indication;
    inst->present_resources[SEC_RES_CERTIFICATE_USAGE] =
            (inst->certificate_usage >= 0);
    inst->present_resources[SEC_RES_DTLS_TLS_CIPHERSUITE] = true;
#endif // ANJAY_WITH_LWM2M11
}

static inline sec_instance_t *find_instance(sec_repr_t *repr, anjay_iid_t iid) {
    if (!repr) {
        return NULL;
    }
    AVS_LIST(sec_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }
    return NULL;
}

static anjay_iid_t get_new_iid(AVS_LIST(sec_instance_t) instances) {
    anjay_iid_t iid = 0;
    AVS_LIST(sec_instance_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static int assign_iid(sec_repr_t *repr, anjay_iid_t *inout_iid) {
    *inout_iid = get_new_iid(repr->instances);
    if (*inout_iid == ANJAY_ID_INVALID) {
        return -1;
    }
    return 0;
}

static void init_instance(sec_instance_t *instance, anjay_iid_t iid) {
    memset(instance, 0, sizeof(sec_instance_t));
    instance->iid = iid;
#ifdef ANJAY_WITH_LWM2M11
    instance->matching_type = -1;
    instance->certificate_usage = -1;
#endif // ANJAY_WITH_LWM2M11
    _standalone_sec_instance_update_resource_presence(instance);
}

static int add_instance(sec_repr_t *repr,
                        const standalone_security_instance_t *instance,
                        anjay_iid_t *inout_iid) {
    if (*inout_iid == ANJAY_ID_INVALID) {
        if (assign_iid(repr, inout_iid)) {
            return -1;
        }
    } else if (find_instance(repr, *inout_iid)) {
        return -1;
    }
    AVS_LIST(sec_instance_t) new_instance =
            AVS_LIST_NEW_ELEMENT(sec_instance_t);
    if (!new_instance) {
        security_log(ERROR, _("out of memory"));
        return -1;
    }
    init_instance(new_instance, *inout_iid);
    if (instance->server_uri) {
        new_instance->server_uri = avs_strdup(instance->server_uri);
        if (!new_instance->server_uri) {
            goto error;
        }
    }
    new_instance->is_bootstrap = instance->bootstrap_server;
    new_instance->security_mode = instance->security_mode;
    new_instance->holdoff_s = instance->client_holdoff_s;
    new_instance->bs_timeout_s = instance->bootstrap_timeout_s;

#ifdef ANJAY_WITH_SECURITY_STRUCTURED
    if ((instance->public_cert_or_psk_identity
         || instance->public_cert_or_psk_identity_size)
                    + (instance->public_cert.desc.source
                       != AVS_CRYPTO_DATA_SOURCE_EMPTY)
                    + (instance->psk_identity.desc.source
                       != AVS_CRYPTO_DATA_SOURCE_EMPTY)
            > 1) {
        security_log(ERROR, _("more than one variant of the Public Key Or "
                              "Identity field specified at the same time"));
        goto error;
    }
    if (instance->public_cert.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (_standalone_sec_init_certificate_chain_resource(
                    &new_instance->public_cert_or_psk_identity,
                    SEC_KEY_AS_KEY_EXTERNAL, &instance->public_cert)) {
            goto error;
        }
    } else if (instance->psk_identity.desc.source
               != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (_standalone_sec_init_psk_identity_resource(
                    &new_instance->public_cert_or_psk_identity,
                    SEC_KEY_AS_KEY_EXTERNAL, &instance->psk_identity)) {
            goto error;
        }
    } else
#endif // ANJAY_WITH_SECURITY_STRUCTURED
    {
        new_instance->public_cert_or_psk_identity.type = SEC_KEY_AS_DATA;
        if (_standalone_raw_buffer_clone(
                    &new_instance->public_cert_or_psk_identity.value.data,
                    &(const standalone_raw_buffer_t) {
                        .data = (void *) (intptr_t)
                                        instance->public_cert_or_psk_identity,
                        .size = instance->public_cert_or_psk_identity_size
                    })) {
            goto error;
        }
    }

#ifdef ANJAY_WITH_SECURITY_STRUCTURED
    if ((instance->private_cert_or_psk_key
         || instance->private_cert_or_psk_key_size)
                    + (instance->private_key.desc.source
                       != AVS_CRYPTO_DATA_SOURCE_EMPTY)
                    + (instance->psk_key.desc.source
                       != AVS_CRYPTO_DATA_SOURCE_EMPTY)
            > 1) {
        security_log(ERROR, _("more than one variant of the Secret Key field "
                              "specified at the same time"));
        goto error;
    }
    if (instance->private_key.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (_standalone_sec_init_private_key_resource(
                    &new_instance->private_cert_or_psk_key,
                    SEC_KEY_AS_KEY_EXTERNAL,
                    &instance->private_key)) {
            goto error;
        }
    } else if (instance->psk_key.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (_standalone_sec_init_psk_key_resource(
                    &new_instance->private_cert_or_psk_key,
                    SEC_KEY_AS_KEY_EXTERNAL,
                    &instance->psk_key)) {
            goto error;
        }
    } else
#endif // ANJAY_WITH_SECURITY_STRUCTURED
    {
        new_instance->private_cert_or_psk_key.type = SEC_KEY_AS_DATA;
        if (_standalone_raw_buffer_clone(
                    &new_instance->private_cert_or_psk_key.value.data,
                    &(const standalone_raw_buffer_t) {
                        .data = (void *) (intptr_t)
                                        instance->private_cert_or_psk_key,
                        .size = instance->private_cert_or_psk_key_size
                    })) {
            goto error;
        }
    }

    if (_standalone_raw_buffer_clone(
                &new_instance->server_public_key,
                &(const standalone_raw_buffer_t) {
                    .data = (void *) (intptr_t) instance->server_public_key,
                    .size = instance->server_public_key_size
                })) {
        goto error;
    }

    if (!new_instance->is_bootstrap) {
        new_instance->ssid = instance->ssid;
        new_instance->present_resources[SEC_RES_SHORT_SERVER_ID] = true;
    }

#ifdef ANJAY_WITH_LWM2M11
    if (instance->matching_type) {
        // values higher than INT8_MAX are invalid anyway,
        // and validation will be done in _standalone_sec_object_validate().
        // This is simpler than adding another validation here.
        new_instance->matching_type =
                (int8_t) AVS_MIN(*instance->matching_type, INT8_MAX);
    }
    if (instance->server_name_indication
            && !(new_instance->server_name_indication =
                         avs_strdup(instance->server_name_indication))) {
        security_log(ERROR, _("Could not copy SNI: out of memory"));
        goto error;
    }
    if (instance->certificate_usage) {
        // same story as with Matching Type
        new_instance->certificate_usage =
                (int8_t) AVS_MIN(*instance->certificate_usage, INT8_MAX);
    }
    if (instance->ciphersuites.num_ids > ANJAY_ID_INVALID) {
        security_log(ERROR, _("Too many ciphersuites specified"));
        goto error;
    }
    for (int32_t i = (int32_t) instance->ciphersuites.num_ids - 1; i >= 0;
         --i) {
        AVS_LIST(sec_cipher_instance_t) cipher_instance =
                AVS_LIST_NEW_ELEMENT(sec_cipher_instance_t);
        if (!cipher_instance) {
            security_log(ERROR,
                         _("Could not copy ciphersuites: out of memory"));
            goto error;
        }
        cipher_instance->riid = (anjay_riid_t) i;
        cipher_instance->cipher_id = instance->ciphersuites.ids[i];
        AVS_LIST_INSERT(&new_instance->enabled_ciphersuites, cipher_instance);
    }
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_COAP_OSCORE
    if (instance->oscore_iid) {
        new_instance->present_resources[SEC_RES_OSCORE_SECURITY_MODE] = true;
        new_instance->oscore_iid = *instance->oscore_iid;
    }
#endif // ANJAY_WITH_COAP_OSCORE

#ifdef ANJAY_WITH_SMS
    new_instance->sms_security_mode = instance->sms_security_mode;
    new_instance->present_resources[SEC_RES_SMS_SECURITY_MODE] =
            !_standalone_sec_validate_sms_security_mode(
                    (int32_t) instance->sms_security_mode);

#    ifdef ANJAY_WITH_SECURITY_STRUCTURED
    if (instance->sms_psk_identity.desc.source
            != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (instance->sms_key_parameters || instance->sms_key_parameters_size) {
            security_log(ERROR,
                         _("more than one variant of the SMS Binding Key "
                           "Parameters field specified at the same time"));
            goto error;
        }
        if (_standalone_sec_init_psk_identity_resource(
                    &new_instance->sms_key_params,
                    SEC_KEY_AS_KEY_EXTERNAL,
                    &instance->sms_psk_identity)) {
            goto error;
        }
        new_instance->present_resources[SEC_RES_SMS_BINDING_KEY_PARAMS] = true;
    } else
#    endif // ANJAY_WITH_SECURITY_STRUCTURED
    {
        new_instance->sms_key_params.type = SEC_KEY_AS_DATA;
        if (_standalone_raw_buffer_clone(
                    &new_instance->sms_key_params.value.data,
                    &(const standalone_raw_buffer_t) {
                        .data = (void *) (intptr_t)
                                        instance->sms_key_parameters,
                        .size = instance->sms_key_parameters_size
                    })) {
            goto error;
        }
        new_instance->present_resources[SEC_RES_SMS_BINDING_KEY_PARAMS] =
                !!instance->sms_key_parameters;
    }

#    ifdef ANJAY_WITH_SECURITY_STRUCTURED
    if (instance->sms_psk_key.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (instance->sms_secret_key || instance->sms_secret_key_size) {
            security_log(ERROR,
                         _("more than one variant of the SMS Binding Secret "
                           "Key(s) field specified at the same time"));
            goto error;
        }
        if (_standalone_sec_init_psk_key_resource(&new_instance->sms_secret_key,
                                                  SEC_KEY_AS_KEY_EXTERNAL,
                                                  &instance->sms_psk_key)) {
            goto error;
        }
        new_instance->present_resources[SEC_RES_SMS_BINDING_SECRET_KEYS] = true;
    } else
#    endif // ANJAY_WITH_SECURITY_STRUCTURED
    {
        new_instance->sms_secret_key.type = SEC_KEY_AS_DATA;
        if (_standalone_raw_buffer_clone(
                    &new_instance->sms_secret_key.value.data,
                    &(const standalone_raw_buffer_t) {
                        .data = (void *) (intptr_t) instance->sms_secret_key,
                        .size = instance->sms_secret_key_size
                    })) {
            goto error;
        }
        new_instance->present_resources[SEC_RES_SMS_BINDING_SECRET_KEYS] =
                !!instance->sms_secret_key;
    }

    if (instance->server_sms_number) {
        new_instance->sms_number = avs_strdup(instance->server_sms_number);
    }
#endif // ANJAY_WITH_SMS

    _standalone_sec_instance_update_resource_presence(new_instance);

    AVS_LIST(sec_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > new_instance->iid) {
            break;
        }
    }
    AVS_LIST_INSERT(ptr, new_instance);

    if (instance->bootstrap_server) {
        security_log(INFO,
                     _("Added instance ") "%u" _(" (bootstrap, URI: ") "%s" _(
                             ")"),
                     *inout_iid, instance->server_uri);
    } else {
        security_log(INFO,
                     _("Added instance ") "%u" _(" (SSID: ") "%u" _(
                             ", URI: ") "%s" _(")"),
                     *inout_iid, instance->ssid, instance->server_uri);
    }

    _standalone_sec_mark_modified(repr);
    return 0;

error:
    _standalone_sec_destroy_instances(&new_instance, true);
    return -1;
}

static int del_instance(sec_repr_t *repr, anjay_iid_t iid) {
    AVS_LIST(sec_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST(sec_instance_t) element = AVS_LIST_DETACH(it);
            _standalone_sec_destroy_instances(&element, true);
            _standalone_sec_mark_modified(repr);
            return 0;
        }
    }

    assert(0);
    return ANJAY_ERR_NOT_FOUND;
}

static int sec_list_resources(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    const sec_instance_t *inst =
            find_instance(_standalone_sec_get(obj_ptr), iid);
    assert(inst);

    for (size_t resource = 0; resource < AVS_ARRAY_SIZE(SECURITY_RESOURCE_ID);
         resource++) {
        const anjay_rid_t rid = SECURITY_RESOURCE_ID[resource];
        anjay_dm_emit_res(ctx, rid,
#ifdef ANJAY_WITH_LWM2M11
                          rid != SEC_RES_DTLS_TLS_CIPHERSUITE ? ANJAY_DM_RES_R
                                                              : ANJAY_DM_RES_RM,
#else
                          ANJAY_DM_RES_R,
#endif // ANJAY_WITH_LWM2M11
                          inst->present_resources[rid] ? ANJAY_DM_RES_PRESENT
                                                       : ANJAY_DM_RES_ABSENT);
    }

    return 0;
}

#ifdef ANJAY_WITH_LWM2M11
static int
sec_list_resource_instances(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    assert(rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
    (void) rid;

    const sec_instance_t *inst =
            find_instance(_standalone_sec_get(obj_ptr), iid);
    assert(inst);

    AVS_LIST(sec_cipher_instance_t) it;
    AVS_LIST_FOREACH(it, inst->enabled_ciphersuites) {
        anjay_dm_emit(ctx, it->riid);
    }

    return 0;
}

static AVS_LIST(sec_cipher_instance_t) *
find_cipher_instance_insert_ptr(AVS_LIST(sec_cipher_instance_t) *instances,
                                anjay_riid_t riid) {
    AVS_LIST(sec_cipher_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, instances) {
        if ((*it)->riid >= riid) {
            break;
        }
    }
    return it;
}

static AVS_LIST(sec_cipher_instance_t)
find_cipher_instance(AVS_LIST(sec_cipher_instance_t) instances,
                     anjay_riid_t riid) {
    AVS_LIST(sec_cipher_instance_t) *it =
            find_cipher_instance_insert_ptr(&instances, riid);
    if (it && (*it)->riid == riid) {
        return *it;
    }
    return NULL;
}
#endif // ANJAY_WITH_LWM2M11

static int ret_sec_key_or_data(anjay_output_ctx_t *ctx,
                               const sec_key_or_data_t *res) {
    switch (res->type) {
    case SEC_KEY_AS_DATA:
        return anjay_ret_bytes(ctx, res->value.data.data, res->value.data.size);
#if defined(ANJAY_WITH_SECURITY_STRUCTURED) \
        || defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT)
    case SEC_KEY_AS_KEY_EXTERNAL:
    case SEC_KEY_AS_KEY_OWNED:
        switch (res->value.key.info.type) {
        case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
            return anjay_ret_certificate_chain_info(
                    ctx, (avs_crypto_certificate_chain_info_t) {
                             .desc = res->value.key.info
                         });
        case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
            return anjay_ret_private_key_info(ctx,
                                              (avs_crypto_private_key_info_t) {
                                                  .desc = res->value.key.info
                                              });
        case AVS_CRYPTO_SECURITY_INFO_CERT_REVOCATION_LIST:
            AVS_UNREACHABLE("unsupported tag");
            return -1;
        case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
            return anjay_ret_psk_identity_info(
                    ctx, (avs_crypto_psk_identity_info_t) {
                             .desc = res->value.key.info
                         });
        case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
            return anjay_ret_psk_key_info(ctx, (avs_crypto_psk_key_info_t) {
                                                   .desc = res->value.key.info
                                               });
        }
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
        // fall-through
    default:
        AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
        return ANJAY_ERR_INTERNAL;
    }
}

static int sec_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
#ifdef ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID || rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
#else  // ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID);
#endif // ANJAY_WITH_LWM2M11

    const sec_instance_t *inst =
            find_instance(_standalone_sec_get(obj_ptr), iid);
    assert(inst);

    switch ((security_rid_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        return anjay_ret_string(ctx, inst->server_uri);
    case SEC_RES_BOOTSTRAP_SERVER:
        return anjay_ret_bool(ctx, inst->is_bootstrap);
    case SEC_RES_SECURITY_MODE:
        return anjay_ret_i64(ctx, (int32_t) inst->security_mode);
    case SEC_RES_SERVER_PK:
        return anjay_ret_bytes(ctx, inst->server_public_key.data,
                               inst->server_public_key.size);
    case SEC_RES_PK_OR_IDENTITY:
        return ret_sec_key_or_data(ctx, &inst->public_cert_or_psk_identity);
    case SEC_RES_SECRET_KEY:
        return ret_sec_key_or_data(ctx, &inst->private_cert_or_psk_key);
    case SEC_RES_SHORT_SERVER_ID:
        return anjay_ret_i64(ctx, (int32_t) inst->ssid);
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        return anjay_ret_i64(ctx, inst->holdoff_s);
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return anjay_ret_i64(ctx, inst->bs_timeout_s);
#ifdef ANJAY_WITH_SMS
    case SEC_RES_SMS_SECURITY_MODE:
        return anjay_ret_i64(ctx, (int32_t) inst->sms_security_mode);
    case SEC_RES_SMS_BINDING_KEY_PARAMS:
        return ret_sec_key_or_data(ctx, &inst->sms_key_params);
    case SEC_RES_SMS_BINDING_SECRET_KEYS:
        return ret_sec_key_or_data(ctx, &inst->sms_secret_key);
    case SEC_RES_SERVER_SMS_NUMBER:
        return anjay_ret_string(ctx, inst->sms_number);
#endif // ANJAY_WITH_SMS
#ifdef ANJAY_WITH_LWM2M11
    case SEC_RES_MATCHING_TYPE:
        return anjay_ret_u64(ctx, (uint64_t) (uint32_t) inst->matching_type);
    case SEC_RES_SNI:
        assert(inst->server_name_indication);
        return anjay_ret_string(ctx, inst->server_name_indication);
    case SEC_RES_CERTIFICATE_USAGE:
        return anjay_ret_u64(ctx,
                             (uint64_t) (uint32_t) inst->certificate_usage);
    case SEC_RES_DTLS_TLS_CIPHERSUITE: {
        AVS_LIST(const sec_cipher_instance_t) rinst =
                find_cipher_instance(inst->enabled_ciphersuites, riid);
        if (!rinst) {
            return ANJAY_ERR_NOT_FOUND;
        }
        return anjay_ret_u64(ctx, rinst->cipher_id);
    }
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_COAP_OSCORE
    case SEC_RES_OSCORE_SECURITY_MODE:
        return anjay_ret_objlnk(ctx, ANJAY_DM_OID_OSCORE, inst->oscore_iid);
#endif // ANJAY_WITH_COAP_OSCORE
    default:
        AVS_UNREACHABLE("Read handler called on unknown Security resource");
        return ANJAY_ERR_NOT_IMPLEMENTED;
    }
}

#ifdef ANJAY_WITH_LWM2M11
static AVS_LIST(sec_cipher_instance_t)
find_or_create_cipher_instance(AVS_LIST(sec_cipher_instance_t) *instances,
                               anjay_riid_t riid) {
    AVS_LIST(sec_cipher_instance_t) *it =
            find_cipher_instance_insert_ptr(instances, riid);

    AVS_LIST(sec_cipher_instance_t) cipher =
            AVS_LIST_INSERT_NEW(sec_cipher_instance_t, it);
    if (cipher) {
        cipher->riid = riid;
    }
    return cipher;
}
#endif // ANJAY_WITH_LWM2M11

static int fetch_sec_key_or_data(anjay_input_ctx_t *ctx,
                                 sec_key_or_data_t *res) {
    _standalone_sec_key_or_data_cleanup(res, true);
    assert(res->type == SEC_KEY_AS_DATA);
    assert(!res->prev_ref);
    assert(!res->next_ref);
    return _standalone_io_fetch_bytes(ctx, &res->value.data);
}

static int sec_write(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
#ifdef ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID || rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
#else  // ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID);
#endif // ANJAY_WITH_LWM2M11
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    sec_instance_t *inst = find_instance(repr, iid);
    int retval;
    assert(inst);

    _standalone_sec_mark_modified(repr);

    switch ((security_rid_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        retval = _standalone_io_fetch_string(ctx, &inst->server_uri);
        break;
    case SEC_RES_BOOTSTRAP_SERVER:
        retval = anjay_get_bool(ctx, &inst->is_bootstrap);
        break;
    case SEC_RES_SECURITY_MODE:
        retval = _standalone_sec_fetch_security_mode(ctx, &inst->security_mode);
        break;
    case SEC_RES_PK_OR_IDENTITY:
        retval = fetch_sec_key_or_data(ctx, &inst->public_cert_or_psk_identity);
        break;
    case SEC_RES_SERVER_PK:
        retval = _standalone_io_fetch_bytes(ctx, &inst->server_public_key);
        break;
    case SEC_RES_SECRET_KEY:
        retval = fetch_sec_key_or_data(ctx, &inst->private_cert_or_psk_key);
        break;
    case SEC_RES_SHORT_SERVER_ID:
        retval = _standalone_sec_fetch_short_server_id(ctx, &inst->ssid);
        break;
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        retval = anjay_get_i32(ctx, &inst->holdoff_s);
        break;
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        retval = anjay_get_i32(ctx, &inst->bs_timeout_s);
        break;
#ifdef ANJAY_WITH_SMS
    case SEC_RES_SMS_SECURITY_MODE:
        retval = _standalone_sec_fetch_sms_security_mode(
                ctx, &inst->sms_security_mode);
        break;
    case SEC_RES_SMS_BINDING_KEY_PARAMS:
        retval = fetch_sec_key_or_data(ctx, &inst->sms_key_params);
        break;
    case SEC_RES_SMS_BINDING_SECRET_KEYS:
        retval = fetch_sec_key_or_data(ctx, &inst->sms_secret_key);
        break;
    case SEC_RES_SERVER_SMS_NUMBER:
        retval = _standalone_io_fetch_string(ctx, &inst->sms_number);
        break;
#endif // ANJAY_WITH_SMS
#ifdef ANJAY_WITH_LWM2M11
    case SEC_RES_MATCHING_TYPE: {
        uint32_t matching_type;
        if (!(retval = anjay_get_u32(ctx, &matching_type))) {
            if (matching_type > 3) {
                retval = ANJAY_ERR_BAD_REQUEST;
            } else {
                inst->matching_type = (int8_t) matching_type;
            }
        }
        break;
    }
    case SEC_RES_SNI:
        retval =
                _standalone_io_fetch_string(ctx, &inst->server_name_indication);
        break;
    case SEC_RES_CERTIFICATE_USAGE: {
        uint32_t certificate_usage;
        if (!(retval = anjay_get_u32(ctx, &certificate_usage))) {
            if (certificate_usage > 3) {
                retval = ANJAY_ERR_BAD_REQUEST;
            } else {
                inst->certificate_usage = (int8_t) certificate_usage;
            }
        }
        break;
    }
#    ifdef ANJAY_WITH_COAP_OSCORE
    case SEC_RES_OSCORE_SECURITY_MODE: {
        anjay_oid_t oid;
        if (!(retval = anjay_get_objlnk(ctx, &oid, &inst->oscore_iid))
                && oid != ANJAY_DM_OID_OSCORE) {
            retval = ANJAY_ERR_BAD_REQUEST;
        }
        break;
    }
#    endif // ANJAY_WITH_COAP_OSCORE
    case SEC_RES_DTLS_TLS_CIPHERSUITE: {
        uint32_t cipher_id;
        if (!(retval = anjay_get_u32(ctx, &cipher_id))) {
            if (cipher_id == 0) {
                security_log(
                        WARNING,
                        _("TLS-NULL-WITH-NULL-NULL cipher is not allowed"));
                retval = ANJAY_ERR_BAD_REQUEST;
            } else if (cipher_id > UINT16_MAX) {
                security_log(WARNING,
                             _("Ciphersuite ID > 65535 is not allowed"));
                retval = ANJAY_ERR_BAD_REQUEST;
            } else {
                AVS_LIST(sec_cipher_instance_t) cipher =
                        find_or_create_cipher_instance(
                                &inst->enabled_ciphersuites, riid);
                if (!cipher) {
                    retval = ANJAY_ERR_INTERNAL;
                } else {
                    cipher->cipher_id = cipher_id;
                }
            }
        }
        break;
    }
#endif // ANJAY_WITH_LWM2M11
    default:
        AVS_UNREACHABLE("Write handler called on unknown Security resource");
        return ANJAY_ERR_NOT_FOUND;
    }

    if (!retval) {
        inst->present_resources[rid] = true;
    }

    return retval;
}

#ifdef ANJAY_WITH_LWM2M11
static int sec_resource_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid) {
    (void) anjay;

    assert(rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
    (void) rid;

    const sec_instance_t *inst =
            find_instance(_standalone_sec_get(obj_ptr), iid);
    assert(inst);

    AVS_LIST_CLEAR(&inst->enabled_ciphersuites);
    return 0;
}

#    ifdef ANJAY_WITH_LWM2M12
static int
sec_resource_instance_remove(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid) {
    (void) anjay;

    assert(rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
    (void) rid;

    sec_instance_t *inst = find_instance(_standalone_sec_get(obj_ptr), iid);
    assert(inst);
    AVS_LIST(sec_cipher_instance_t) *rinst_ptr =
            find_cipher_instance_insert_ptr(&inst->enabled_ciphersuites, riid);
    assert(rinst_ptr && *rinst_ptr && (*rinst_ptr)->riid);
    AVS_LIST_DELETE(rinst_ptr);
    return 0;
}
#    endif // ANJAY_WITH_LWM2M12
#endif     // ANJAY_WITH_LWM2M11

static int sec_list_instances(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    AVS_LIST(sec_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        anjay_dm_emit(ctx, it->iid);
    }
    return 0;
}

static int sec_instance_create(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    assert(iid != ANJAY_ID_INVALID);

    AVS_LIST(sec_instance_t) created = AVS_LIST_NEW_ELEMENT(sec_instance_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    init_instance(created, iid);

    AVS_LIST(sec_instance_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    _standalone_sec_mark_modified(repr);
    return 0;
}

static int sec_instance_remove(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_standalone_sec_get(obj_ptr), iid);
}

static int sec_transaction_begin(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_sec_transaction_begin_impl(_standalone_sec_get(obj_ptr));
}

static int sec_transaction_commit(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_sec_transaction_commit_impl(
            _standalone_sec_get(obj_ptr));
}

static int
sec_transaction_validate(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_sec_transaction_validate_impl(anjay, _standalone_sec_get(
                                                                    obj_ptr));
}

static int
sec_transaction_rollback(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _standalone_sec_transaction_rollback_impl(
            _standalone_sec_get(obj_ptr));
}

static int sec_instance_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
    (void) anjay;
    sec_instance_t *inst = find_instance(_standalone_sec_get(obj_ptr), iid);
    assert(inst);

    _standalone_sec_destroy_instance_fields(inst, true);
    init_instance(inst, iid);
    return 0;
}

static const anjay_dm_object_def_t SECURITY = {
    .oid = ANJAY_DM_OID_SECURITY,
    .handlers = {
        .list_instances = sec_list_instances,
        .instance_create = sec_instance_create,
        .instance_remove = sec_instance_remove,
        .instance_reset = sec_instance_reset,
        .list_resources = sec_list_resources,
#ifdef ANJAY_WITH_LWM2M11
        .list_resource_instances = sec_list_resource_instances,
#endif // ANJAY_WITH_LWM2M11
        .resource_read = sec_read,
        .resource_write = sec_write,
#ifdef ANJAY_WITH_LWM2M11
        .resource_reset = sec_resource_reset,
#endif // ANJAY_WITH_LWM2M11
        .transaction_begin = sec_transaction_begin,
        .transaction_commit = sec_transaction_commit,
        .transaction_validate = sec_transaction_validate,
        .transaction_rollback = sec_transaction_rollback
#ifdef ANJAY_WITH_LWM2M12
        ,
        .resource_instance_remove = sec_resource_instance_remove
#endif // ANJAY_WITH_LWM2M12
    }
};

sec_repr_t *_standalone_sec_get(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr && *obj_ptr == &SECURITY);
    return AVS_CONTAINER_OF(obj_ptr, sec_repr_t, def);
}

int standalone_security_object_add_instance(
        const anjay_dm_object_def_t *const *obj_ptr,
        const standalone_security_instance_t *instance,
        anjay_iid_t *inout_iid) {
    int retval = -1;
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
        retval = -1;
    } else {
        const bool modified_since_persist = repr->modified_since_persist;
        if (!(retval = add_instance(repr, instance, inout_iid))
                && (retval = _standalone_sec_object_validate_and_process_keys(
                            repr->anjay, repr))) {
            (void) del_instance(repr, *inout_iid);
            if (!modified_since_persist) {
                /* validation failed and so in the end no instace is added */
                _standalone_sec_clear_modified(repr);
            }
        }

        if (!retval) {
            if (anjay_notify_instances_changed(repr->anjay, SECURITY.oid)) {
                security_log(WARNING, _("Could not schedule socket reload"));
            }
        }
    }
    return retval;
}

void standalone_security_object_cleanup(
        const anjay_dm_object_def_t *const *obj_ptr) {
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    if (repr->in_transaction) {
        _standalone_sec_destroy_instances(&repr->instances, true);
        _standalone_sec_destroy_instances(&repr->saved_instances,
                                          repr->saved_modified_since_persist);
    } else {
        assert(!repr->saved_instances);
        _standalone_sec_destroy_instances(&repr->instances,
                                          repr->modified_since_persist);
    }
#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
    if (repr->prng_ctx && !repr->prng_allocated_by_user) {
        avs_crypto_prng_free(&repr->prng_ctx);
    }
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
    avs_free(repr);
}

void standalone_security_object_purge(
        const anjay_dm_object_def_t *const *obj_ptr) {
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);

    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
    } else {
        if (repr->instances) {
            _standalone_sec_mark_modified(repr);
        }
        _standalone_sec_destroy_instances(&repr->saved_instances, true);
        _standalone_sec_destroy_instances(&repr->instances, true);
        if (anjay_notify_instances_changed(repr->anjay, SECURITY.oid)) {
            security_log(WARNING, _("Could not schedule socket reload"));
        }
    }
}

bool standalone_security_object_is_modified(
        const anjay_dm_object_def_t *const *obj_ptr) {
    bool result = false;
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
    } else {
        if (repr->in_transaction) {
            result = repr->saved_modified_since_persist;
        } else {
            result = repr->modified_since_persist;
        }
    }
    return result;
}

const anjay_dm_object_def_t **
standalone_security_object_install(anjay_t *anjay) {
    sec_repr_t *repr = (sec_repr_t *) avs_calloc(1, sizeof(sec_repr_t));
    if (!repr) {
        security_log(ERROR, _("out of memory"));
        return NULL;
    }
    repr->def = &SECURITY;
    repr->anjay = anjay;
    AVS_STATIC_ASSERT(offsetof(sec_repr_t, def) == 0, def_is_first_field);
    if (anjay_register_object(anjay, &repr->def)) {
        avs_free(repr);
        return NULL;
    }
    return &repr->def;
}

#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
const anjay_dm_object_def_t **standalone_security_object_install_with_hsm(
        anjay_t *anjay,
        const standalone_security_hsm_configuration_t *hsm_config,
        avs_crypto_prng_ctx_t *prng_ctx) {
    assert(anjay);
    bool prng_allocated_by_user = !!prng_ctx;
    if (!prng_allocated_by_user) {
        prng_ctx = avs_crypto_prng_new(NULL, NULL);
        if (!prng_ctx) {
            security_log(ERROR, _("Could not create PRNG context"));
            return NULL;
        }
    }
    const anjay_dm_object_def_t **result =
            standalone_security_object_install(anjay);
    if (result && hsm_config) {
        sec_repr_t *repr = _standalone_sec_get(result);
        repr->hsm_config = *hsm_config;
        repr->prng_ctx = prng_ctx;
        repr->prng_allocated_by_user = prng_allocated_by_user;
    }
    return result;
}

static void
mark_hsm_sec_key_or_data_permanent(sec_repr_t *repr,
                                   sec_key_or_data_t *sec_key_or_data) {
    if (sec_key_or_data->type == SEC_KEY_AS_KEY_OWNED) {
        sec_key_or_data->type = SEC_KEY_AS_KEY_EXTERNAL;
        repr->modified_since_persist = true;
        for (sec_key_or_data_t *it = sec_key_or_data->prev_ref; it;
             it = it->prev_ref) {
            it->type = SEC_KEY_AS_KEY_EXTERNAL;
            if (repr->in_transaction) {
                repr->saved_modified_since_persist = true;
            }
        }
        for (sec_key_or_data_t *it = sec_key_or_data->next_ref; it;
             it = it->next_ref) {
            it->type = SEC_KEY_AS_KEY_EXTERNAL;
            if (repr->in_transaction) {
                repr->saved_modified_since_persist = true;
            }
        }
    }
}

static void mark_hsm_instance_permanent(sec_repr_t *repr,
                                        sec_instance_t *instance) {
    mark_hsm_sec_key_or_data_permanent(repr,
                                       &instance->public_cert_or_psk_identity);
    mark_hsm_sec_key_or_data_permanent(repr,
                                       &instance->private_cert_or_psk_key);
#    ifdef ANJAY_WITH_SMS
    mark_hsm_sec_key_or_data_permanent(repr, &instance->sms_key_params);
    mark_hsm_sec_key_or_data_permanent(repr, &instance->sms_secret_key);
#    endif // ANJAY_WITH_SMS
}

void standalone_security_mark_hsm_permanent(
        const anjay_dm_object_def_t *const *obj_ptr, anjay_ssid_t ssid) {
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
    } else {
        AVS_LIST(sec_instance_t) instance;
        AVS_LIST_FOREACH(instance, repr->instances) {
            if (ssid == ANJAY_SSID_ANY
                    || (ssid == ANJAY_SSID_BOOTSTRAP
                        && instance->present_resources[SEC_RES_BOOTSTRAP_SERVER]
                        && instance->is_bootstrap)
                    || ((!instance->present_resources[SEC_RES_BOOTSTRAP_SERVER]
                         || !instance->is_bootstrap)
                        && instance->present_resources[SEC_RES_SHORT_SERVER_ID]
                        && ssid == instance->ssid)) {
                mark_hsm_instance_permanent(repr, instance);
            }
        }
    }
}
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
