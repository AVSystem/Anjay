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

#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
#        include <avsystem/commons/avs_persistence.h>
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE

#    include <anjay_modules/anjay_dm_utils.h>

#    include <inttypes.h>
#    include <string.h>

#    include "anjay_mod_security.h"
#    include "anjay_security_transaction.h"
#    include "anjay_security_utils.h"

VISIBILITY_SOURCE_BEGIN

#    define persistence_log(level, ...) \
        _anjay_log(security_persistence, level, __VA_ARGS__)

#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE

static const char MAGIC_V0[] = { 'S', 'E', 'C', '\0' };
static const char MAGIC_V1[] = { 'S', 'E', 'C', '\1' };

static avs_error_t handle_sized_v0_fields(avs_persistence_context_t *ctx,
                                          sec_instance_t *element) {
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->iid)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->has_is_bootstrap)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->has_security_mode)))
            || avs_is_err((err = avs_persistence_bool(ctx, &element->has_ssid)))
            || avs_is_err((
                       err = avs_persistence_bool(ctx, &element->is_bootstrap)))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->ssid)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx, (uint32_t *) &element->holdoff_s)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx, (uint32_t *) &element->bs_timeout_s))));
    return err;
}

static avs_error_t handle_sized_v1_fields(avs_persistence_context_t *ctx,
                                          sec_instance_t *element) {
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_bool(
                                ctx, &element->has_sms_security_mode)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->has_sms_key_params)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->has_sms_secret_key))));
    return err;
}

static avs_error_t handle_raw_buffer(avs_persistence_context_t *ctx,
                                     anjay_raw_buffer_t *buffer) {
    avs_error_t err =
            avs_persistence_sized_buffer(ctx, &buffer->data, &buffer->size);
    if (!buffer->capacity) {
        buffer->capacity = buffer->size;
    }
    return err;
}

static avs_error_t handle_public_key(avs_persistence_context_t *ctx,
                                     sec_key_or_data_t *value,
                                     intptr_t stream_version) {
    (void) stream_version;
    assert(value->type == SEC_KEY_AS_DATA);
    return handle_raw_buffer(ctx, &value->value.data);
}

static avs_error_t handle_private_key(avs_persistence_context_t *ctx,
                                      sec_key_or_data_t *value,
                                      intptr_t stream_version) {
    (void) stream_version;
    assert(value->type == SEC_KEY_AS_DATA);
    return handle_raw_buffer(ctx, &value->value.data);
}

static avs_error_t handle_instance(avs_persistence_context_t *ctx,
                                   void *element_,
                                   void *stream_version_) {
    sec_instance_t *element = (sec_instance_t *) element_;
    const intptr_t stream_version = (intptr_t) stream_version_;

    avs_error_t err = AVS_OK;
    uint16_t security_mode = (uint16_t) element->security_mode;
    if (avs_is_err((err = handle_sized_v0_fields(ctx, element)))
            || avs_is_err((err = avs_persistence_u16(ctx, &security_mode)))
            || avs_is_err((
                       err = avs_persistence_string(ctx, &element->server_uri)))
            || avs_is_err((err = handle_public_key(
                                   ctx, &element->public_cert_or_psk_identity,
                                   stream_version)))
            || avs_is_err((err = handle_private_key(
                                   ctx, &element->private_cert_or_psk_key,
                                   stream_version)))
            || avs_is_err((err = handle_raw_buffer(
                                   ctx, &element->server_public_key)))) {
        return err;
    }
    element->security_mode = (anjay_security_mode_t) security_mode;
    if (stream_version >= 1) {
        uint16_t sms_security_mode = (uint16_t) element->sms_security_mode;
        if (avs_is_err((err = handle_sized_v1_fields(ctx, element)))
                || avs_is_err(
                           (err = avs_persistence_u16(ctx, &sms_security_mode)))
                || avs_is_err((err = handle_raw_buffer(
                                       ctx, &element->sms_key_params)))
                || avs_is_err((err = handle_raw_buffer(
                                       ctx, &element->sms_secret_key)))
                || avs_is_err((err = avs_persistence_string(
                                       ctx, &element->sms_number)))) {
            return err;
        }
        element->sms_security_mode =
                (anjay_sms_security_mode_t) sms_security_mode;
    }
    return err;
}

avs_error_t anjay_security_object_persist(anjay_t *anjay,
                                          avs_stream_t *out_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    sec_repr_t *repr = _anjay_sec_get(sec_obj);
    if (!repr) {
        return avs_errno(AVS_EBADF);
    }
    avs_error_t err = avs_stream_write(out_stream, MAGIC_V0, sizeof(MAGIC_V0));
    if (avs_is_err(err)) {
        return err;
    }
    avs_persistence_context_t ctx =
            avs_persistence_store_context_create(out_stream);
    err = avs_persistence_list(
            &ctx,
            (AVS_LIST(void) *) (repr->in_transaction ? &repr->saved_instances
                                                     : &repr->instances),
            sizeof(sec_instance_t), handle_instance, (void *) (intptr_t) 0,
            NULL);
    if (avs_is_ok(err)) {
        _anjay_sec_clear_modified(repr);
        persistence_log(INFO, _("Security Object state persisted"));
    }
    return err;
}

avs_error_t anjay_security_object_restore(anjay_t *anjay,
                                          avs_stream_t *in_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    sec_repr_t *repr = _anjay_sec_get(sec_obj);
    if (!repr || repr->in_transaction) {
        return avs_errno(AVS_EBADF);
    }
    sec_repr_t backup = *repr;

    AVS_STATIC_ASSERT(sizeof(MAGIC_V0) == sizeof(MAGIC_V1), magic_size_v0_v1);
    char magic_header[sizeof(MAGIC_V0)];
    avs_error_t err = avs_stream_read_reliably(in_stream, magic_header,
                                               sizeof(magic_header));
    if (avs_is_err(err)) {
        persistence_log(WARNING, _("Could not read Security Object header"));
        return err;
    }

    int version;
    if (!memcmp(magic_header, MAGIC_V0, sizeof(MAGIC_V0))) {
        version = 0;
    } else if (!memcmp(magic_header, MAGIC_V1, sizeof(MAGIC_V1))) {
        version = 1;
    } else {
        persistence_log(WARNING, _("Header magic constant mismatch"));
        return avs_errno(AVS_EBADMSG);
    }
    avs_persistence_context_t restore_ctx =
            avs_persistence_restore_context_create(in_stream);
    repr->instances = NULL;
    err = avs_persistence_list(&restore_ctx,
                               (AVS_LIST(void) *) &repr->instances,
                               sizeof(sec_instance_t), handle_instance,
                               (void *) (intptr_t) version, NULL);
    if (avs_is_ok(err) && _anjay_sec_object_validate(anjay, repr)) {
        err = avs_errno(AVS_EPROTO);
    }
    if (avs_is_err(err)) {
        _anjay_sec_destroy_instances(&repr->instances);
        repr->instances = backup.instances;
    } else {
        _anjay_sec_destroy_instances(&backup.instances);
        _anjay_sec_clear_modified(repr);
        persistence_log(INFO, _("Security Object state restored"));
    }
    return err;
}

#        ifdef ANJAY_TEST
#            include "tests/modules/security/persistence.c"
#        endif

#    else // AVS_COMMONS_WITH_AVS_PERSISTENCE

avs_error_t anjay_security_object_persist(anjay_t *anjay,
                                          avs_stream_t *out_stream) {
    (void) anjay;
    (void) out_stream;
    persistence_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

avs_error_t anjay_security_object_restore(anjay_t *anjay,
                                          avs_stream_t *in_stream) {
    (void) anjay;
    (void) in_stream;
    persistence_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE

#endif // ANJAY_WITH_MODULE_SECURITY
