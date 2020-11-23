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
#    include <stdlib.h>
#    include <string.h>

#    include <avsystem/commons/avs_utils.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_io_utils.h>

#    include "anjay_mod_security.h"
#    include "anjay_security_transaction.h"
#    include "anjay_security_utils.h"

VISIBILITY_SOURCE_BEGIN

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

    new_instance->has_is_bootstrap = true;
    new_instance->has_security_mode = true;
    if (new_instance->is_bootstrap) {
        new_instance->has_ssid = false;
    } else {
        new_instance->ssid = instance->ssid;
        new_instance->has_ssid = true;
    }

    new_instance->sms_security_mode = instance->sms_security_mode;
    if (_anjay_raw_buffer_clone(
                &new_instance->sms_key_params,
                &(const anjay_raw_buffer_t) {
                    .data = (void *) (intptr_t) instance->sms_key_parameters,
                    .size = instance->sms_key_parameters_size
                })) {
        goto error;
    }
    if (_anjay_raw_buffer_clone(
                &new_instance->sms_secret_key,
                &(const anjay_raw_buffer_t) {
                    .data = (void *) (intptr_t) instance->sms_secret_key,
                    .size = instance->sms_secret_key_size
                })) {
        goto error;
    }
    if (instance->server_sms_number) {
        new_instance->sms_number = avs_strdup(instance->server_sms_number);
    }
    new_instance->has_sms_security_mode =
            !_anjay_sec_validate_sms_security_mode(
                    (int32_t) instance->sms_security_mode);
    new_instance->has_sms_key_params = !!instance->sms_key_parameters;
    new_instance->has_sms_secret_key = !!instance->sms_secret_key;

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
    _anjay_sec_destroy_instances(&new_instance);
    return -1;
}

static int del_instance(sec_repr_t *repr, anjay_iid_t iid) {
    AVS_LIST(sec_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST(sec_instance_t) element = AVS_LIST_DETACH(it);
            _anjay_sec_destroy_instances(&element);
            _anjay_sec_mark_modified(repr);
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
    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    anjay_dm_emit_res(ctx, SEC_RES_LWM2M_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SEC_RES_BOOTSTRAP_SERVER, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SEC_RES_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SEC_RES_PK_OR_IDENTITY, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SEC_RES_SERVER_PK, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SEC_RES_SECRET_KEY, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, SEC_RES_SMS_SECURITY_MODE, ANJAY_DM_RES_R,
                      inst->has_sms_security_mode ? ANJAY_DM_RES_PRESENT
                                                  : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SEC_RES_SMS_BINDING_KEY_PARAMS, ANJAY_DM_RES_R,
                      inst->has_sms_key_params ? ANJAY_DM_RES_PRESENT
                                               : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SEC_RES_SMS_BINDING_SECRET_KEYS, ANJAY_DM_RES_R,
                      inst->has_sms_secret_key ? ANJAY_DM_RES_PRESENT
                                               : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SEC_RES_SERVER_SMS_NUMBER, ANJAY_DM_RES_R,
                      inst->sms_number ? ANJAY_DM_RES_PRESENT
                                       : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SEC_RES_SHORT_SERVER_ID, ANJAY_DM_RES_R,
                      inst->has_ssid ? ANJAY_DM_RES_PRESENT
                                     : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SEC_RES_CLIENT_HOLD_OFF_TIME, ANJAY_DM_RES_R,
                      inst->holdoff_s >= 0 ? ANJAY_DM_RES_PRESENT
                                           : ANJAY_DM_RES_ABSENT);
    anjay_dm_emit_res(ctx, SEC_RES_BOOTSTRAP_TIMEOUT, ANJAY_DM_RES_R,
                      inst->bs_timeout_s >= 0 ? ANJAY_DM_RES_PRESENT
                                              : ANJAY_DM_RES_ABSENT);
    return 0;
}

static int sec_read(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);

    const sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    switch ((security_resource_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        return anjay_ret_string(ctx, inst->server_uri);
    case SEC_RES_BOOTSTRAP_SERVER:
        return anjay_ret_bool(ctx, inst->is_bootstrap);
    case SEC_RES_SECURITY_MODE:
        return anjay_ret_i32(ctx, (int32_t) inst->security_mode);
    case SEC_RES_SERVER_PK:
        return anjay_ret_bytes(ctx, inst->server_public_key.data,
                               inst->server_public_key.size);
    case SEC_RES_PK_OR_IDENTITY:
        switch (inst->public_cert_or_psk_identity.type) {
        case SEC_KEY_AS_DATA:
            return anjay_ret_bytes(
                    ctx, inst->public_cert_or_psk_identity.value.data.data,
                    inst->public_cert_or_psk_identity.value.data.size);
        default:
            AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
            return ANJAY_ERR_INTERNAL;
        }
    case SEC_RES_SECRET_KEY:
        switch (inst->private_cert_or_psk_key.type) {
        case SEC_KEY_AS_DATA:
            return anjay_ret_bytes(
                    ctx, inst->private_cert_or_psk_key.value.data.data,
                    inst->private_cert_or_psk_key.value.data.size);
        default:
            AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
            return ANJAY_ERR_INTERNAL;
        }
    case SEC_RES_SHORT_SERVER_ID:
        return anjay_ret_i32(ctx, (int32_t) inst->ssid);
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        return anjay_ret_i32(ctx, inst->holdoff_s);
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return anjay_ret_i32(ctx, inst->bs_timeout_s);
    case SEC_RES_SMS_SECURITY_MODE:
        return anjay_ret_i32(ctx, (int32_t) inst->sms_security_mode);
    case SEC_RES_SMS_BINDING_KEY_PARAMS:
        return anjay_ret_bytes(ctx, inst->sms_key_params.data,
                               inst->sms_key_params.size);
    case SEC_RES_SMS_BINDING_SECRET_KEYS:
        return anjay_ret_bytes(ctx, inst->sms_secret_key.data,
                               inst->sms_secret_key.size);
    case SEC_RES_SERVER_SMS_NUMBER:
        return anjay_ret_string(ctx, inst->sms_number);
    default:
        AVS_UNREACHABLE("Read handler called on unknown Security resource");
        return ANJAY_ERR_NOT_IMPLEMENTED;
    }
}

static int sec_write(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid,
                     anjay_rid_t rid,
                     anjay_riid_t riid,
                     anjay_input_ctx_t *ctx) {
    (void) anjay;
    (void) riid;
    assert(riid == ANJAY_ID_INVALID);
    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
    sec_instance_t *inst = find_instance(repr, iid);
    int retval;
    assert(inst);

    _anjay_sec_mark_modified(repr);

    switch ((security_resource_t) rid) {
    case SEC_RES_LWM2M_SERVER_URI:
        return _anjay_io_fetch_string(ctx, &inst->server_uri);
    case SEC_RES_BOOTSTRAP_SERVER:
        if (!(retval = anjay_get_bool(ctx, &inst->is_bootstrap))) {
            inst->has_is_bootstrap = true;
        }
        return retval;
    case SEC_RES_SECURITY_MODE:
        if (!(retval = _anjay_sec_fetch_security_mode(ctx,
                                                      &inst->security_mode))) {
            inst->has_security_mode = true;
        }
        return retval;
    case SEC_RES_PK_OR_IDENTITY:
        _anjay_sec_key_or_data_cleanup(&inst->public_cert_or_psk_identity);
        assert(inst->public_cert_or_psk_identity.type == SEC_KEY_AS_DATA);
        return _anjay_io_fetch_bytes(
                ctx, &inst->public_cert_or_psk_identity.value.data);
    case SEC_RES_SERVER_PK:
        return _anjay_io_fetch_bytes(ctx, &inst->server_public_key);
    case SEC_RES_SECRET_KEY:
        _anjay_sec_key_or_data_cleanup(&inst->private_cert_or_psk_key);
        assert(inst->private_cert_or_psk_key.type == SEC_KEY_AS_DATA);
        return _anjay_io_fetch_bytes(ctx,
                                     &inst->private_cert_or_psk_key.value.data);
    case SEC_RES_SHORT_SERVER_ID:
        if (!(retval = _anjay_sec_fetch_short_server_id(ctx, &inst->ssid))) {
            inst->has_ssid = true;
        }
        return retval;
    case SEC_RES_CLIENT_HOLD_OFF_TIME:
        return anjay_get_i32(ctx, &inst->holdoff_s);
    case SEC_RES_BOOTSTRAP_TIMEOUT:
        return anjay_get_i32(ctx, &inst->bs_timeout_s);
    case SEC_RES_SMS_SECURITY_MODE:
        if (!(retval = _anjay_sec_fetch_sms_security_mode(
                      ctx, &inst->sms_security_mode))) {
            inst->has_sms_security_mode = true;
        }
        return retval;
    case SEC_RES_SMS_BINDING_KEY_PARAMS:
        if (!(retval = _anjay_io_fetch_bytes(ctx, &inst->sms_key_params))) {
            inst->has_sms_key_params = true;
        }
        return retval;
    case SEC_RES_SMS_BINDING_SECRET_KEYS:
        if (!(retval = _anjay_io_fetch_bytes(ctx, &inst->sms_secret_key))) {
            inst->has_sms_secret_key = true;
        }
        return retval;
    case SEC_RES_SERVER_SMS_NUMBER:
        return _anjay_io_fetch_string(ctx, &inst->sms_number);
    default:
        AVS_UNREACHABLE("Write handler called on unknown Security resource");
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int sec_list_instances(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_dm_list_ctx_t *ctx) {
    (void) anjay;

    sec_repr_t *repr = _anjay_sec_get(obj_ptr);
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

static int sec_instance_remove(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid) {
    (void) anjay;
    return del_instance(_anjay_sec_get(obj_ptr), iid);
}

static int sec_transaction_begin(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_begin_impl(_anjay_sec_get(obj_ptr));
}

static int sec_transaction_commit(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_commit_impl(_anjay_sec_get(obj_ptr));
}

static int
sec_transaction_validate(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_validate_impl(anjay, _anjay_sec_get(obj_ptr));
}

static int
sec_transaction_rollback(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    return _anjay_sec_transaction_rollback_impl(_anjay_sec_get(obj_ptr));
}

static int sec_instance_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid) {
    (void) anjay;
    sec_instance_t *inst = find_instance(_anjay_sec_get(obj_ptr), iid);
    assert(inst);

    _anjay_sec_destroy_instance_fields(inst);
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
        .resource_read = sec_read,
        .resource_write = sec_write,
        .transaction_begin = sec_transaction_begin,
        .transaction_commit = sec_transaction_commit,
        .transaction_validate = sec_transaction_validate,
        .transaction_rollback = sec_transaction_rollback
    }
};

sec_repr_t *_anjay_sec_get(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr && *obj_ptr == &SECURITY);
    return AVS_CONTAINER_OF(obj_ptr, sec_repr_t, def);
}

int anjay_security_object_add_instance(
        anjay_t *anjay,
        const anjay_security_instance_t *instance,
        anjay_iid_t *inout_iid) {
    assert(anjay);

    const anjay_dm_object_def_t *const *obj_ptr =
            _anjay_dm_find_object_by_oid(anjay, SECURITY.oid);
    sec_repr_t *repr = _anjay_sec_get(obj_ptr);

    if (!repr) {
        security_log(ERROR, _("Security object is not registered"));
        return -1;
    }

    const bool modified_since_persist = repr->modified_since_persist;
    int retval = add_instance(repr, instance, inout_iid);
    if (!retval && (retval = _anjay_sec_object_validate(anjay, repr))) {
        (void) del_instance(repr, *inout_iid);
        if (!modified_since_persist) {
            /* validation failed and so in the end no instace is added */
            _anjay_sec_clear_modified(repr);
        }
    }

    if (!retval) {
        if (anjay_notify_instances_changed(anjay, SECURITY.oid)) {
            security_log(WARNING, _("Could not schedule socket reload"));
        }
    }

    return retval;
}

static void security_purge(sec_repr_t *repr) {
    if (repr->instances) {
        _anjay_sec_mark_modified(repr);
    }
    _anjay_sec_destroy_instances(&repr->instances);
    _anjay_sec_destroy_instances(&repr->saved_instances);
}

static void security_delete(void *repr) {
    security_purge((sec_repr_t *) repr);
    avs_free(repr);
}

void anjay_security_object_purge(anjay_t *anjay) {
    assert(anjay);

    const anjay_dm_object_def_t *const *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, SECURITY.oid);
    sec_repr_t *repr = _anjay_sec_get(sec_obj);

    security_purge(repr);

    if (anjay_notify_instances_changed(anjay, SECURITY.oid)) {
        security_log(WARNING, _("Could not schedule socket reload"));
    }
}

bool anjay_security_object_is_modified(anjay_t *anjay) {
    assert(anjay);

    sec_repr_t *repr =
            _anjay_sec_get(_anjay_dm_find_object_by_oid(anjay, SECURITY.oid));
    return repr->in_transaction ? repr->saved_modified_since_persist
                                : repr->modified_since_persist;
}

static const anjay_dm_module_t SECURITY_MODULE = {
    .deleter = security_delete
};

int anjay_security_object_install(anjay_t *anjay) {
    assert(anjay);

    sec_repr_t *repr = (sec_repr_t *) avs_calloc(1, sizeof(sec_repr_t));
    if (!repr) {
        security_log(ERROR, _("out of memory"));
        return -1;
    }

    repr->def = &SECURITY;

    if (_anjay_dm_module_install(anjay, &SECURITY_MODULE, repr)) {
        avs_free(repr);
        return -1;
    }

    if (anjay_register_object(anjay, &repr->def)) {
        // this will free repr
        int result = _anjay_dm_module_uninstall(anjay, &SECURITY_MODULE);
        assert(!result);
        (void) result;
        return -1;
    }

    return 0;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/security/api.c"
#    endif

#endif // ANJAY_WITH_MODULE_SECURITY
