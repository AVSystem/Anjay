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
#    include <stdlib.h>
#    include <string.h>

#    include <anjay_modules/anjay_io_utils.h>

#    include "anjay_mod_security.h"
#    include "anjay_security_transaction.h"
#    include "anjay_security_utils.h"

VISIBILITY_SOURCE_BEGIN

static const security_rid_t SECURITY_RESOURCE_ID[] = {
    SEC_RES_LWM2M_SERVER_URI,  SEC_RES_BOOTSTRAP_SERVER,
    SEC_RES_SECURITY_MODE,     SEC_RES_PK_OR_IDENTITY,
    SEC_RES_SERVER_PK,         SEC_RES_SECRET_KEY,
    SEC_RES_SHORT_SERVER_ID,   SEC_RES_CLIENT_HOLD_OFF_TIME,
    SEC_RES_BOOTSTRAP_TIMEOUT,
#    ifdef ANJAY_WITH_LWM2M11
    SEC_RES_MATCHING_TYPE,     SEC_RES_SNI,
    SEC_RES_CERTIFICATE_USAGE, SEC_RES_DTLS_TLS_CIPHERSUITE,
#    endif // ANJAY_WITH_LWM2M11
};

void _anjay_sec_instance_update_resource_presence(sec_instance_t *inst) {
    // Sets presence of mandatory resources and updates presence of resources
    // which presence is not persisted and depends on resource value
    inst->present_resources[SEC_RES_LWM2M_SERVER_URI] = true;
    inst->present_resources[SEC_RES_BOOTSTRAP_SERVER] = true;
    inst->present_resources[SEC_RES_SECURITY_MODE] = true;
    inst->present_resources[SEC_RES_PK_OR_IDENTITY] = true;
    inst->present_resources[SEC_RES_SERVER_PK] = true;
    inst->present_resources[SEC_RES_SECRET_KEY] = true;
    inst->present_resources[SEC_RES_CLIENT_HOLD_OFF_TIME] =
            (inst->holdoff_s >= 0);
    inst->present_resources[SEC_RES_BOOTSTRAP_TIMEOUT] =
            (inst->bs_timeout_s >= 0);
#    ifdef ANJAY_WITH_LWM2M11
    inst->present_resources[SEC_RES_MATCHING_TYPE] = (inst->matching_type >= 0);
    inst->present_resources[SEC_RES_SNI] = !!inst->server_name_indication;
    inst->present_resources[SEC_RES_CERTIFICATE_USAGE] =
            (inst->certificate_usage >= 0);
    inst->present_resources[SEC_RES_DTLS_TLS_CIPHERSUITE] = true;
#    endif // ANJAY_WITH_LWM2M11
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
#    ifdef ANJAY_WITH_LWM2M11
    instance->matching_type = -1;
    instance->certificate_usage = -1;
#    endif // ANJAY_WITH_LWM2M11
    _anjay_sec_instance_update_resource_presence(instance);
}

static int add_instance(sec_repr_t *repr,
                        const anjay_security_instance_t *instance,
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

#    ifdef ANJAY_WITH_SECURITY_STRUCTURED
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
        if (_anjay_sec_init_certificate_chain_resource(
                    &new_instance->public_cert_or_psk_identity,
                    SEC_KEY_AS_KEY_EXTERNAL, &instance->public_cert)) {
            goto error;
        }
    } else if (instance->psk_identity.desc.source
               != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (_anjay_sec_init_psk_identity_resource(
                    &new_instance->public_cert_or_psk_identity,
                    SEC_KEY_AS_KEY_EXTERNAL, &instance->psk_identity)) {
            goto error;
        }
    } else
#    endif // ANJAY_WITH_SECURITY_STRUCTURED
    {
        new_instance->public_cert_or_psk_identity.type = SEC_KEY_AS_DATA;
        if (_anjay_raw_buffer_clone(
                    &new_instance->public_cert_or_psk_identity.value.data,
                    &(const anjay_raw_buffer_t) {
                        .data = (void *) (intptr_t)
                                        instance->public_cert_or_psk_identity,
                        .size = instance->public_cert_or_psk_identity_size
                    })) {
            goto error;
        }
    }

#    ifdef ANJAY_WITH_SECURITY_STRUCTURED
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
        if (_anjay_sec_init_private_key_resource(
                    &new_instance->private_cert_or_psk_key,
                    SEC_KEY_AS_KEY_EXTERNAL,
                    &instance->private_key)) {
            goto error;
        }
    } else if (instance->psk_key.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        if (_anjay_sec_init_psk_key_resource(
                    &new_instance->private_cert_or_psk_key,
                    SEC_KEY_AS_KEY_EXTERNAL,
                    &instance->psk_key)) {
            goto error;
        }
    } else
#    endif // ANJAY_WITH_SECURITY_STRUCTURED
    {
        new_instance->private_cert_or_psk_key.type = SEC_KEY_AS_DATA;
        if (_anjay_raw_buffer_clone(
                    &new_instance->private_cert_or_psk_key.value.data,
                    &(const anjay_raw_buffer_t) {
                        .data = (void *) (intptr_t)
                                        instance->private_cert_or_psk_key,
                        .size = instance->private_cert_or_psk_key_size
                    })) {
            goto error;
        }
    }

    if (_anjay_raw_buffer_clone(
                &new_instance->server_public_key,
                &(const anjay_raw_buffer_t) {
                    .data = (void *) (intptr_t) instance->server_public_key,
                    .size = instance->server_public_key_size
                })) {
        goto error;
    }

    if (!new_instance->is_bootstrap) {
        new_instance->ssid = instance->ssid;
        new_instance->present_resources[SEC_RES_SHORT_SERVER_ID] = true;
    }

#    ifdef ANJAY_WITH_LWM2M11
    if (instance->matching_type) {
        // values higher than INT8_MAX are invalid anyway,
        // and validation will be done in _anjay_sec_object_validate().
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
#    endif // ANJAY_WITH_LWM2M11

    _anjay_sec_instance_update_resource_presence(new_instance);

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

    _anjay_sec_mark_modified(repr);
    return 0;

error:
    _anjay_sec_destroy_instances(&new_instance, true);
    return -1;
}

static int del_instance(sec_repr_t *repr, anjay_iid_t iid) {
    AVS_LIST(sec_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST(sec_instance_t) element = AVS_LIST_DETACH(it);
            _anjay_sec_destroy_instances(&element, true);
            _anjay_sec_mark_modified(repr);
            return 0;
        }
    }

    assert(0);
    return ANJAY_ERR_NOT_FOUND;
}

static int sec_list_resources(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_ptr,
                              anjay_iid_t iid,
                              anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    for (size_t resource = 0; resource < AVS_ARRAY_SIZE(SECURITY_RESOURCE_ID);
         resource++) {
        const anjay_rid_t rid = SECURITY_RESOURCE_ID[resource];
        _anjay_dm_emit_res_unlocked(ctx, rid,
#    ifdef ANJAY_WITH_LWM2M11
                                    rid != SEC_RES_DTLS_TLS_CIPHERSUITE
                                            ? ANJAY_DM_RES_R
                                            : ANJAY_DM_RES_RM,
#    else
                                    ANJAY_DM_RES_R,
#    endif // ANJAY_WITH_LWM2M11
                                    inst->present_resources[rid]
                                            ? ANJAY_DM_RES_PRESENT
                                            : ANJAY_DM_RES_ABSENT);
    }

    return 0;
}

#    ifdef ANJAY_WITH_LWM2M11
static int
sec_list_resource_instances(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    assert(rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
    (void) rid;

    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    AVS_LIST(sec_cipher_instance_t) it;
    AVS_LIST_FOREACH(it, inst->enabled_ciphersuites) {
        _anjay_dm_emit_unlocked(ctx, it->riid);
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
#    endif // ANJAY_WITH_LWM2M11

static int ret_sec_key_or_data(anjay_unlocked_output_ctx_t *ctx,
                               const sec_key_or_data_t *res) {
    switch (res->type) {
    case SEC_KEY_AS_DATA:
        return _anjay_ret_bytes_unlocked(ctx, res->value.data.data,
                                         res->value.data.size);
#    if defined(ANJAY_WITH_SECURITY_STRUCTURED)
    case SEC_KEY_AS_KEY_EXTERNAL:
    case SEC_KEY_AS_KEY_OWNED:
        return _anjay_ret_security_info_unlocked(ctx, &res->value.key.info);
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
              defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
    default:
        AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
        return ANJAY_ERR_INTERNAL;
    }
}

static int sec_read(anjay_unlocked_t *anjay,
                    const anjay_dm_installed_object_t obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_unlocked_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
#    ifdef ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID || rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
#    else  // ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID);
#    endif // ANJAY_WITH_LWM2M11

    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    switch ((security_rid_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        return _anjay_ret_string_unlocked(ctx, inst->server_uri);
    case SEC_RES_BOOTSTRAP_SERVER:
        return _anjay_ret_bool_unlocked(ctx, inst->is_bootstrap);
    case SEC_RES_SECURITY_MODE:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) inst->security_mode);
    case SEC_RES_SERVER_PK:
        return _anjay_ret_bytes_unlocked(ctx, inst->server_public_key.data,
                                         inst->server_public_key.size);
    case SEC_RES_PK_OR_IDENTITY:
        return ret_sec_key_or_data(ctx, &inst->public_cert_or_psk_identity);
    case SEC_RES_SECRET_KEY:
        return ret_sec_key_or_data(ctx, &inst->private_cert_or_psk_key);
    case SEC_RES_SHORT_SERVER_ID:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) inst->ssid);
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        return _anjay_ret_i64_unlocked(ctx, inst->holdoff_s);
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return _anjay_ret_i64_unlocked(ctx, inst->bs_timeout_s);
#    ifdef ANJAY_WITH_LWM2M11
    case SEC_RES_MATCHING_TYPE:
        return _anjay_ret_u64_unlocked(
                ctx, (uint64_t) (uint32_t) inst->matching_type);
    case SEC_RES_SNI:
        assert(inst->server_name_indication);
        return _anjay_ret_string_unlocked(ctx, inst->server_name_indication);
    case SEC_RES_CERTIFICATE_USAGE:
        return _anjay_ret_u64_unlocked(
                ctx, (uint64_t) (uint32_t) inst->certificate_usage);
    case SEC_RES_DTLS_TLS_CIPHERSUITE: {
        AVS_LIST(const sec_cipher_instance_t) rinst =
                find_cipher_instance(inst->enabled_ciphersuites, riid);
        if (!rinst) {
            return ANJAY_ERR_NOT_FOUND;
        }
        return _anjay_ret_u64_unlocked(ctx, rinst->cipher_id);
    }
#    endif // ANJAY_WITH_LWM2M11
    default:
        AVS_UNREACHABLE("Read handler called on unknown Security resource");
        return ANJAY_ERR_NOT_IMPLEMENTED;
    }
}

#    ifdef ANJAY_WITH_LWM2M11
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
#    endif // ANJAY_WITH_LWM2M11

static int fetch_sec_key_or_data(anjay_unlocked_input_ctx_t *ctx,
                                 sec_key_or_data_t *res) {
    _anjay_sec_key_or_data_cleanup(res, true);
    assert(res->type == SEC_KEY_AS_DATA);
    assert(!res->prev_ref);
    assert(!res->next_ref);
    return _anjay_io_fetch_bytes(ctx, &res->value.data);
}

static int sec_write(anjay_unlocked_t *anjay,
                     const anjay_dm_installed_object_t obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_unlocked_input_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
#    ifdef ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID || rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
#    else  // ANJAY_WITH_LWM2M11
    assert(riid == ANJAY_ID_INVALID);
#    endif // ANJAY_WITH_LWM2M11
    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
    sec_instance_t *inst = find_instance(repr, iid);
    int retval;
    assert(inst);

    _anjay_sec_mark_modified(repr);

    switch ((security_rid_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        retval = _anjay_io_fetch_string(ctx, &inst->server_uri);
        break;
    case SEC_RES_BOOTSTRAP_SERVER:
        retval = _anjay_get_bool_unlocked(ctx, &inst->is_bootstrap);
        break;
    case SEC_RES_SECURITY_MODE:
        retval = _anjay_sec_fetch_security_mode(ctx, &inst->security_mode);
        break;
    case SEC_RES_PK_OR_IDENTITY:
        retval = fetch_sec_key_or_data(ctx, &inst->public_cert_or_psk_identity);
        break;
    case SEC_RES_SERVER_PK:
        retval = _anjay_io_fetch_bytes(ctx, &inst->server_public_key);
        break;
    case SEC_RES_SECRET_KEY:
        retval = fetch_sec_key_or_data(ctx, &inst->private_cert_or_psk_key);
        break;
    case SEC_RES_SHORT_SERVER_ID:
        retval = _anjay_sec_fetch_short_server_id(ctx, &inst->ssid);
        break;
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        retval = _anjay_get_i32_unlocked(ctx, &inst->holdoff_s);
        break;
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        retval = _anjay_get_i32_unlocked(ctx, &inst->bs_timeout_s);
        break;
#    ifdef ANJAY_WITH_LWM2M11
    case SEC_RES_MATCHING_TYPE: {
        uint32_t matching_type;
        if (!(retval = _anjay_get_u32_unlocked(ctx, &matching_type))) {
            if (matching_type > 3) {
                retval = ANJAY_ERR_BAD_REQUEST;
            } else {
                inst->matching_type = (int8_t) matching_type;
            }
        }
        break;
    }
    case SEC_RES_SNI:
        retval = _anjay_io_fetch_string(ctx, &inst->server_name_indication);
        break;
    case SEC_RES_CERTIFICATE_USAGE: {
        uint32_t certificate_usage;
        if (!(retval = _anjay_get_u32_unlocked(ctx, &certificate_usage))) {
            if (certificate_usage > 3) {
                retval = ANJAY_ERR_BAD_REQUEST;
            } else {
                inst->certificate_usage = (int8_t) certificate_usage;
            }
        }
        break;
    }
    case SEC_RES_DTLS_TLS_CIPHERSUITE: {
        uint32_t cipher_id;
        if (!(retval = _anjay_get_u32_unlocked(ctx, &cipher_id))) {
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
#    endif // ANJAY_WITH_LWM2M11
    default:
        AVS_UNREACHABLE("Write handler called on unknown Security resource");
        return ANJAY_ERR_NOT_FOUND;
    }

    if (!retval) {
        inst->present_resources[rid] = true;
    }

    return retval;
}

#    ifdef ANJAY_WITH_LWM2M11
static int sec_resource_reset(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid) {
    (void) anjay;

    assert(rid == SEC_RES_DTLS_TLS_CIPHERSUITE);
    (void) rid;

    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    AVS_LIST_CLEAR(&inst->enabled_ciphersuites);
    return 0;
}

#    endif // ANJAY_WITH_LWM2M11

static int sec_list_instances(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_ptr,
                              anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
    AVS_LIST(sec_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        _anjay_dm_emit_unlocked(ctx, it->iid);
    }
    return 0;
}

static int sec_instance_create(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
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
    _anjay_sec_mark_modified(repr);
    return 0;
}

static int sec_instance_remove(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_anjay_sec_get(obj_ptr), iid);
}

static int sec_transaction_begin(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_begin_impl(_anjay_sec_get(obj_ptr));
}

static int sec_transaction_commit(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_commit_impl(_anjay_sec_get(obj_ptr));
}

static int sec_transaction_validate(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_validate_impl(anjay, _anjay_sec_get(obj_ptr));
}

static int sec_transaction_rollback(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_rollback_impl(_anjay_sec_get(obj_ptr));
}

static int sec_instance_reset(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_ptr,
                              anjay_iid_t iid) {
    (void) anjay;
    sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    _anjay_sec_destroy_instance_fields(inst, true);
    init_instance(inst, iid);
    return 0;
}

static const anjay_unlocked_dm_object_def_t SECURITY = {
    .oid = ANJAY_DM_OID_SECURITY,
    .handlers = {
        .list_instances = sec_list_instances,
        .instance_create = sec_instance_create,
        .instance_remove = sec_instance_remove,
        .instance_reset = sec_instance_reset,
        .list_resources = sec_list_resources,
#    ifdef ANJAY_WITH_LWM2M11
        .list_resource_instances = sec_list_resource_instances,
#    endif // ANJAY_WITH_LWM2M11
        .resource_read = sec_read,
        .resource_write = sec_write,
#    ifdef ANJAY_WITH_LWM2M11
        .resource_reset = sec_resource_reset,
#    endif // ANJAY_WITH_LWM2M11
        .transaction_begin = sec_transaction_begin,
        .transaction_commit = sec_transaction_commit,
        .transaction_validate = sec_transaction_validate,
        .transaction_rollback = sec_transaction_rollback
    }
};

sec_repr_t *_anjay_sec_get(const anjay_dm_installed_object_t obj_ptr) {
    const anjay_unlocked_dm_object_def_t *const *unlocked_def =
            _anjay_dm_installed_object_get_unlocked(&obj_ptr);
    assert(*unlocked_def == &SECURITY);
    return AVS_CONTAINER_OF(unlocked_def, sec_repr_t, def);
}

int anjay_security_object_add_instance(
        anjay_t *anjay_locked,
        const anjay_security_instance_t *instance,
        anjay_iid_t *inout_iid) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj_ptr =
            _anjay_dm_find_object_by_oid(anjay, SECURITY.oid);
    sec_repr_t *repr = obj_ptr ? _anjay_sec_get(*obj_ptr) : NULL;
    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
        retval = -1;
    } else {
        const bool modified_since_persist = repr->modified_since_persist;
        if (!(retval = add_instance(repr, instance, inout_iid))
                && (retval = _anjay_sec_object_validate_and_process_keys(
                            anjay, repr))) {
            (void) del_instance(repr, *inout_iid);
            if (!modified_since_persist) {
                /* validation failed and so in the end no instace is added */
                _anjay_sec_clear_modified(repr);
            }
        }

        if (!retval) {
            if (_anjay_notify_instances_changed_unlocked(anjay, SECURITY.oid)) {
                security_log(WARNING, _("Could not schedule socket reload"));
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

static void security_delete(void *repr_) {
    sec_repr_t *repr = (sec_repr_t *) repr_;
    if (repr->in_transaction) {
        _anjay_sec_destroy_instances(&repr->instances, true);
        _anjay_sec_destroy_instances(&repr->saved_instances,
                                     repr->saved_modified_since_persist);
    } else {
        assert(!repr->saved_instances);
        _anjay_sec_destroy_instances(&repr->instances,
                                     repr->modified_since_persist);
    }
    // NOTE: repr itself will be freed when cleaning the objects list
}

void anjay_security_object_purge(anjay_t *anjay_locked) {
    assert(anjay_locked);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, SECURITY.oid);
    sec_repr_t *repr = sec_obj ? _anjay_sec_get(*sec_obj) : NULL;

    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
    } else {
        if (repr->instances) {
            _anjay_sec_mark_modified(repr);
        }
        _anjay_sec_destroy_instances(&repr->saved_instances, true);
        _anjay_sec_destroy_instances(&repr->instances, true);
        if (_anjay_notify_instances_changed_unlocked(anjay, SECURITY.oid)) {
            security_log(WARNING, _("Could not schedule socket reload"));
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

bool anjay_security_object_is_modified(anjay_t *anjay_locked) {
    assert(anjay_locked);
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, SECURITY.oid);
    if (!sec_obj) {
        security_log(ERROR, _("Security object is not registered"));
    } else {
        sec_repr_t *repr = _anjay_sec_get(*sec_obj);
        if (repr->in_transaction) {
            result = repr->saved_modified_since_persist;
        } else {
            result = repr->modified_since_persist;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static sec_repr_t *security_install_unlocked(anjay_unlocked_t *anjay) {
    AVS_LIST(sec_repr_t) repr = AVS_LIST_NEW_ELEMENT(sec_repr_t);
    if (!repr) {
        security_log(ERROR, _("out of memory"));
        return NULL;
    }
    int result = -1;
    repr->def = &SECURITY;
    _anjay_dm_installed_object_init_unlocked(&repr->def_ptr, &repr->def);
    if (!_anjay_dm_module_install(anjay, security_delete, repr)) {
        AVS_STATIC_ASSERT(offsetof(sec_repr_t, def_ptr) == 0,
                          def_ptr_is_first_field);
        AVS_LIST(anjay_dm_installed_object_t) entry = &repr->def_ptr;
        if (_anjay_register_object_unlocked(anjay, &entry)) {
            result = _anjay_dm_module_uninstall(anjay, security_delete);
            assert(!result);
            result = -1;
        } else {
            result = 0;
        }
    }
    if (result) {
        AVS_LIST_CLEAR(&repr);
    }
    return repr;
}

int anjay_security_object_install(anjay_t *anjay_locked) {
    assert(anjay_locked);
    sec_repr_t *repr = NULL;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    repr = security_install_unlocked(anjay);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return repr ? 0 : -1;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/security/api.c"
#    endif

#endif // ANJAY_WITH_MODULE_SECURITY
