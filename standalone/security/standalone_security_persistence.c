#include <inttypes.h>
#include <string.h>

#include "standalone_mod_security.h"
#include "standalone_security_transaction.h"
#include "standalone_security_utils.h"

#ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
#    include <avsystem/commons/avs_persistence.h>
#endif // AVS_COMMONS_WITH_AVS_PERSISTENCE

#define persistence_log(level, ...) \
    avs_log(security_persistence, level, __VA_ARGS__)

#ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE

static const char MAGIC_V0[] = { 'S', 'E', 'C', '\0' };
static const char MAGIC_V1[] = { 'S', 'E', 'C', '\1' };
static const char MAGIC_V2[] = { 'S', 'E', 'C', '\2' };
static const char MAGIC_V3[] = { 'S', 'E', 'C', '\3' };
static const char MAGIC_V4[] = { 'S', 'E', 'C', '\4' };
static const char MAGIC_V5[] = { 'S', 'E', 'C', '\5' };

static avs_error_t handle_sized_v0_fields(avs_persistence_context_t *ctx,
                                          sec_instance_t *element) {
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->iid)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->present_resources
                                                 [SEC_RES_BOOTSTRAP_SERVER])))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->present_resources
                                                 [SEC_RES_SECURITY_MODE])))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->present_resources
                                                 [SEC_RES_SHORT_SERVER_ID])))
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
#    ifdef ANJAY_WITH_SMS
    (void) (avs_is_err((err = avs_persistence_bool(
                                ctx, &element->present_resources
                                              [SEC_RES_SMS_SECURITY_MODE])))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources
                                            [SEC_RES_SMS_BINDING_KEY_PARAMS])))
            || avs_is_err(
                       (err = avs_persistence_bool(
                                ctx,
                                &element->present_resources
                                         [SEC_RES_SMS_BINDING_SECRET_KEYS]))));
#    else  // ANJAY_WITH_SMS
    (void) element;
    (void) (avs_is_err((err = avs_persistence_bool(ctx, &(bool) { false })))
            || avs_is_err((err = avs_persistence_bool(ctx, &(bool) { false })))
            || avs_is_err(
                       (err = avs_persistence_bool(ctx, &(bool) { false }))));
#    endif // ANJAY_WITH_SMS
    return err;
}

static avs_error_t handle_ciphersuite_entry(avs_persistence_context_t *ctx,
                                            void *element,
                                            void *user_data) {
    (void) user_data;

    sec_cipher_instance_t *inst = (sec_cipher_instance_t *) element;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &inst->riid)))
            || avs_is_err((err = avs_persistence_u32(ctx, &inst->cipher_id))));
    if (avs_is_ok(err) && inst->cipher_id == 0) {
        return avs_errno(AVS_EBADMSG);
    }
    return err;
}

static avs_error_t handle_sized_v2_fields(avs_persistence_context_t *ctx,
                                          sec_instance_t *element) {
    AVS_LIST(sec_cipher_instance_t) enabled_ciphersuites = NULL;
    char *server_name_indication = NULL;
#    ifdef ANJAY_WITH_LWM2M11
    enabled_ciphersuites = element->enabled_ciphersuites;
    server_name_indication = element->server_name_indication;
#    endif // ANJAY_WITH_LWM2M11
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_list(
                                ctx, (void **) &enabled_ciphersuites,
                                sizeof(*enabled_ciphersuites),
                                handle_ciphersuite_entry, NULL, avs_free)))
            || avs_is_err((err = avs_persistence_string(
                                   ctx, &server_name_indication)))
#    ifdef ANJAY_WITH_COAP_OSCORE
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources
                                            [SEC_RES_OSCORE_SECURITY_MODE])))
            || avs_is_err(
                       (err = avs_persistence_u16(ctx, &element->oscore_iid)))
#    else  // ANJAY_WITH_COAP_OSCORE
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, (bool *) &(bool) { false })))
            || avs_is_err((err = avs_persistence_u16(
                                   ctx, (uint16_t *) &(uint16_t) { 0 })))
#    endif // ANJAY_WITH_COAP_OSCORE
    );
#    ifdef ANJAY_WITH_LWM2M11
    element->enabled_ciphersuites = enabled_ciphersuites;
    element->server_name_indication = server_name_indication;
#    else  // ANJAY_WITH_LWM2M11
    (void) element;
    AVS_LIST_CLEAR(&enabled_ciphersuites);
    avs_free(server_name_indication);
#    endif // ANJAY_WITH_LWM2M11
    return err;
}

static avs_error_t handle_sized_v3_fields(avs_persistence_context_t *ctx,
                                          sec_instance_t *element) {
#    ifndef ANJAY_WITH_LWM2M11
    (void) element;
#    endif // ANJAY_WITH_LWM2M11
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_i8(ctx,
#    ifdef ANJAY_WITH_LWM2M11
                                                 &element->matching_type
#    else  // ANJAY_WITH_LWM2M11
                                                 &(int8_t) { -1 }
#    endif // ANJAY_WITH_LWM2M11
                                                 )))
            || avs_is_err((err = avs_persistence_i8(ctx,
#    ifdef ANJAY_WITH_LWM2M11
                                                    &element->certificate_usage
#    else  // ANJAY_WITH_LWM2M11
                                                    &(int8_t) { -1 }
#    endif // ANJAY_WITH_LWM2M11
                                                    ))));
    return err;
}

#    ifdef ANJAY_WITH_LWM2M11
static void reset_v3_fields(sec_instance_t *element) {
    element->matching_type = -1;
    element->certificate_usage = -1;
}
#    endif // ANJAY_WITH_LWM2M11

#    if defined(ANJAY_WITH_SECURITY_STRUCTURED) \
            || defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT)
static avs_error_t handle_sec_key_or_data_type(avs_persistence_context_t *ctx,
                                               sec_key_or_data_type_t *type) {
    avs_persistence_direction_t direction = avs_persistence_direction(ctx);
    int8_t type_ch;
    if (direction == AVS_PERSISTENCE_STORE) {
        switch (*type) {
        case SEC_KEY_AS_DATA:
            type_ch = 'D';
            break;
        case SEC_KEY_AS_KEY_EXTERNAL:
            type_ch = 'K';
            break;
        case SEC_KEY_AS_KEY_OWNED:
            type_ch = 'O';
            break;
        default:
            AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
            return avs_errno(AVS_EINVAL);
        }
    }

    avs_error_t err = avs_persistence_i8(ctx, &type_ch);
    if (avs_is_err(err)) {
        return err;
    }

    if (direction == AVS_PERSISTENCE_RESTORE) {
        switch (type_ch) {
        case 'D':
            *type = SEC_KEY_AS_DATA;
            break;
        case 'K':
            *type = SEC_KEY_AS_KEY_EXTERNAL;
            break;
        case 'O':
            *type = SEC_KEY_AS_KEY_OWNED;
            break;
        default:
            return avs_errno(AVS_EIO);
        }
    }
    return AVS_OK;
}

static avs_error_t handle_sec_key_tag(avs_persistence_context_t *ctx,
                                      avs_crypto_security_info_tag_t *tag) {
    avs_persistence_direction_t direction = avs_persistence_direction(ctx);
    int8_t tag_ch;
    if (direction == AVS_PERSISTENCE_STORE) {
        switch (*tag) {
        case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
            tag_ch = 'C';
            break;
        case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
            tag_ch = 'K';
            break;
        case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
            tag_ch = 'I';
            break;
        case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
            tag_ch = 'P';
            break;
        default:
            AVS_UNREACHABLE("invalid value of avs_crypto_security_info_tag_t");
            return avs_errno(AVS_EINVAL);
        }
    }

    avs_error_t err = avs_persistence_i8(ctx, &tag_ch);
    if (avs_is_err(err)) {
        return err;
    }

    if (direction == AVS_PERSISTENCE_RESTORE) {
        switch (tag_ch) {
        case 'C':
            *tag = AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN;
            break;
        case 'K':
            *tag = AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY;
            break;
        case 'I':
            *tag = AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY;
            break;
        case 'P':
            *tag = AVS_CRYPTO_SECURITY_INFO_PSK_KEY;
            break;
        default:
            return avs_errno(AVS_EIO);
        }
    }
    return AVS_OK;
}

static avs_error_t
handle_sec_key_certificate_chain(avs_persistence_context_t *ctx,
                                 sec_key_or_data_t *value) {
    assert(value->type == SEC_KEY_AS_KEY_EXTERNAL
           || value->type == SEC_KEY_AS_KEY_OWNED);
    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_STORE) {
        return avs_crypto_certificate_chain_info_persist(
                ctx, (avs_crypto_certificate_chain_info_t) {
                         .desc = value->value.key.info
                     });
    } else {
        avs_crypto_certificate_chain_info_t *array = NULL;
        size_t element_count;
        avs_error_t err = avs_crypto_certificate_chain_info_array_persistence(
                ctx, &array, &element_count);
        if (avs_is_ok(err)) {
            assert(!value->value.key.heap_buf);
            assert(!value->prev_ref);
            assert(!value->next_ref);
            value->value.key.info =
                    avs_crypto_certificate_chain_info_from_array(array,
                                                                 element_count)
                            .desc;
            value->value.key.heap_buf = array;
        }
        return err;
    }
}

static avs_error_t handle_sec_key_private_key(avs_persistence_context_t *ctx,
                                              sec_key_or_data_t *value) {
    assert(value->type == SEC_KEY_AS_KEY_EXTERNAL
           || value->type == SEC_KEY_AS_KEY_OWNED);
    avs_crypto_private_key_info_t *key_info = NULL;
    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_STORE) {
        key_info = AVS_CONTAINER_OF(&value->value.key.info,
                                    avs_crypto_private_key_info_t, desc);
    }
    avs_error_t err = avs_crypto_private_key_info_persistence(ctx, &key_info);
    if (avs_is_ok(err)
            && avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        assert(!value->value.key.heap_buf);
        assert(!value->prev_ref);
        assert(!value->next_ref);
        value->value.key.info = key_info->desc;
        value->value.key.heap_buf = key_info;
    }
    return err;
}

static avs_error_t handle_sec_key_psk_identity(avs_persistence_context_t *ctx,
                                               sec_key_or_data_t *value) {
    assert(value->type == SEC_KEY_AS_KEY_EXTERNAL
           || value->type == SEC_KEY_AS_KEY_OWNED);
    avs_crypto_psk_identity_info_t *key_info = NULL;
    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_STORE) {
        key_info = AVS_CONTAINER_OF(&value->value.key.info,
                                    avs_crypto_psk_identity_info_t, desc);
    }
    avs_error_t err = avs_crypto_psk_identity_info_persistence(ctx, &key_info);
    if (avs_is_ok(err)
            && avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        assert(!value->value.key.heap_buf);
        assert(!value->prev_ref);
        assert(!value->next_ref);
        value->value.key.info = key_info->desc;
        value->value.key.heap_buf = key_info;
    }
    return err;
}

static avs_error_t handle_sec_key_psk_key(avs_persistence_context_t *ctx,
                                          sec_key_or_data_t *value) {
    assert(value->type == SEC_KEY_AS_KEY_EXTERNAL
           || value->type == SEC_KEY_AS_KEY_OWNED);
    avs_crypto_psk_key_info_t *key_info = NULL;
    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_STORE) {
        key_info = AVS_CONTAINER_OF(&value->value.key.info,
                                    avs_crypto_psk_key_info_t, desc);
    }
    avs_error_t err = avs_crypto_psk_key_info_persistence(ctx, &key_info);
    if (avs_is_ok(err)
            && avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        assert(!value->value.key.heap_buf);
        assert(!value->prev_ref);
        assert(!value->next_ref);
        value->value.key.info = key_info->desc;
        value->value.key.heap_buf = key_info;
    }
    return err;
}
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
              defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */

static avs_error_t handle_raw_buffer(avs_persistence_context_t *ctx,
                                     standalone_raw_buffer_t *buffer) {
    avs_error_t err =
            avs_persistence_sized_buffer(ctx, &buffer->data, &buffer->size);
    if (!buffer->capacity) {
        buffer->capacity = buffer->size;
    }
    return err;
}

static avs_error_t
handle_sec_key_or_data(avs_persistence_context_t *ctx,
                       sec_key_or_data_t *value,
                       intptr_t stream_version,
                       intptr_t min_version_for_key,
                       avs_crypto_security_info_tag_t default_tag) {
#    if defined(ANJAY_WITH_SECURITY_STRUCTURED) \
            || defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT)
    if (stream_version >= min_version_for_key) {
        avs_error_t err = handle_sec_key_or_data_type(ctx, &value->type);
        if (avs_is_err(err)) {
            return err;
        }

        if (value->type == SEC_KEY_AS_KEY_EXTERNAL
                || value->type == SEC_KEY_AS_KEY_OWNED) {
            avs_crypto_security_info_tag_t tag = default_tag;
            if (stream_version >= 5) {
                if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_STORE) {
                    tag = value->value.key.info.type;
                }
                if (avs_is_err((err = handle_sec_key_tag(ctx, &tag)))) {
                    return err;
                }
            }

            switch (tag) {
            case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
                return handle_sec_key_certificate_chain(ctx, value);
            case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
                return handle_sec_key_private_key(ctx, value);
            case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
                return handle_sec_key_psk_identity(ctx, value);
            case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
                return handle_sec_key_psk_key(ctx, value);
            default:
                AVS_UNREACHABLE(
                        "invalid value of avs_crypto_security_info_tag_t");
                return avs_errno(AVS_EINVAL);
            }
        }
    }
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
              defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
    (void) stream_version;
    (void) min_version_for_key;
    (void) default_tag;
    assert(value->type == SEC_KEY_AS_DATA);
    avs_error_t err = handle_raw_buffer(ctx, &value->value.data);
    assert(avs_is_err(err)
           || avs_persistence_direction(ctx) != AVS_PERSISTENCE_RESTORE
           || (!value->prev_ref && !value->next_ref));
    return err;
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
            || avs_is_err((err = handle_sec_key_or_data(
                                   ctx, &element->public_cert_or_psk_identity,
                                   stream_version,
                                   /* min_version_for_key = */ 4,
                                   AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN)))
            || avs_is_err((err = handle_sec_key_or_data(
                                   ctx, &element->private_cert_or_psk_key,
                                   stream_version,
                                   /* min_version_for_key = */ 4,
                                   AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY)))
            || avs_is_err((err = handle_raw_buffer(
                                   ctx, &element->server_public_key)))) {
        return err;
    }
    element->security_mode = (anjay_security_mode_t) security_mode;
    if (stream_version >= 1) {
#    ifdef ANJAY_WITH_SMS
        uint16_t sms_security_mode = (uint16_t) element->sms_security_mode;
        sec_key_or_data_t *sms_key_params_ptr = &element->sms_key_params;
        sec_key_or_data_t *sms_secret_key_ptr = &element->sms_secret_key;
        char *sms_number = element->sms_number;
#    else  // ANJAY_WITH_SMS
        uint16_t sms_security_mode = 3; // NoSec
        sec_key_or_data_t *sms_key_params_ptr =
                &(sec_key_or_data_t) { SEC_KEY_AS_DATA };
        sec_key_or_data_t *sms_secret_key_ptr =
                &(sec_key_or_data_t) { SEC_KEY_AS_DATA };
        char *sms_number = NULL;
#    endif // ANJAY_WITH_SMS
        if (avs_is_err((err = handle_sized_v1_fields(ctx, element)))
                || avs_is_err(
                           (err = avs_persistence_u16(ctx, &sms_security_mode)))
                || avs_is_err((err = handle_sec_key_or_data(
                                       ctx, sms_key_params_ptr, stream_version,
                                       /* min_version_for_key = */ 5,
                                       AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY)))
                || avs_is_err((err = handle_sec_key_or_data(
                                       ctx, sms_secret_key_ptr, stream_version,
                                       /* min_version_for_key = */ 5,
                                       AVS_CRYPTO_SECURITY_INFO_PSK_KEY)))
                || avs_is_err(
                           (err = avs_persistence_string(ctx, &sms_number)))) {
            return err;
        }
#    ifdef ANJAY_WITH_SMS
        element->sms_security_mode =
                (anjay_sms_security_mode_t) sms_security_mode;
        element->sms_number = sms_number;
#    else  // ANJAY_WITH_SMS
        _standalone_sec_key_or_data_cleanup(sms_key_params_ptr, false);
        _standalone_sec_key_or_data_cleanup(sms_secret_key_ptr, false);
        avs_free(sms_number);
#    endif // ANJAY_WITH_SMS
    }
    if (stream_version >= 2) {
        err = handle_sized_v2_fields(ctx, element);
    }
    if (avs_is_ok(err)) {
        if (stream_version >= 3) {
            err = handle_sized_v3_fields(ctx, element);
        }
#    ifdef ANJAY_WITH_LWM2M11
        else if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
            reset_v3_fields(element);
        }
#    endif // ANJAY_WITH_LWM2M11
    }

    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        _standalone_sec_instance_update_resource_presence(element);
    }

    return err;
}

avs_error_t
standalone_security_object_persist(const anjay_dm_object_def_t *const *obj_ptr,
                                   avs_stream_t *out_stream) {
    avs_error_t err = avs_errno(AVS_EINVAL);
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    if (!repr) {
        err = avs_errno(AVS_EBADF);
    } else if (avs_is_ok((err = avs_stream_write(out_stream, MAGIC_V5,
                                                 sizeof(MAGIC_V5))))) {
        avs_persistence_context_t ctx =
                avs_persistence_store_context_create(out_stream);
        err = avs_persistence_list(
                &ctx,
                (AVS_LIST(void) *) (repr->in_transaction
                                            ? &repr->saved_instances
                                            : &repr->instances),
                sizeof(sec_instance_t), handle_instance, (void *) (intptr_t) 5,
                NULL);
        if (avs_is_ok(err)) {
            _standalone_sec_clear_modified(repr);
            persistence_log(INFO, _("Security Object state persisted"));
        }
    }
    return err;
}

avs_error_t
standalone_security_object_restore(const anjay_dm_object_def_t *const *obj_ptr,
                                   avs_stream_t *in_stream) {
    avs_error_t err = avs_errno(AVS_EINVAL);
    sec_repr_t *repr = _standalone_sec_get(obj_ptr);
    if (!repr || repr->in_transaction) {
        err = avs_errno(AVS_EBADF);
    } else {
        sec_repr_t backup = *repr;

        AVS_STATIC_ASSERT(sizeof(MAGIC_V0) == sizeof(MAGIC_V1),
                          magic_size_v0_v1);
        AVS_STATIC_ASSERT(sizeof(MAGIC_V1) == sizeof(MAGIC_V2),
                          magic_size_v1_v2);
        AVS_STATIC_ASSERT(sizeof(MAGIC_V2) == sizeof(MAGIC_V3),
                          magic_size_v2_v3);
        AVS_STATIC_ASSERT(sizeof(MAGIC_V3) == sizeof(MAGIC_V4),
                          magic_size_v3_v4);
        AVS_STATIC_ASSERT(sizeof(MAGIC_V4) == sizeof(MAGIC_V5),
                          magic_size_v4_v5);
        char magic_header[sizeof(MAGIC_V0)];
        int version = -1;
        if (avs_is_err(
                    (err = avs_stream_read_reliably(in_stream, magic_header,
                                                    sizeof(magic_header))))) {
            persistence_log(WARNING,
                            _("Could not read Security Object header"));
        } else if (!memcmp(magic_header, MAGIC_V0, sizeof(MAGIC_V0))) {
            version = 0;
        } else if (!memcmp(magic_header, MAGIC_V1, sizeof(MAGIC_V1))) {
            version = 1;
        } else if (!memcmp(magic_header, MAGIC_V2, sizeof(MAGIC_V2))) {
            version = 2;
        } else if (!memcmp(magic_header, MAGIC_V3, sizeof(MAGIC_V3))) {
            version = 3;
        } else if (!memcmp(magic_header, MAGIC_V4, sizeof(MAGIC_V4))) {
            version = 4;
        } else if (!memcmp(magic_header, MAGIC_V5, sizeof(MAGIC_V5))) {
            version = 5;
        } else {
            persistence_log(WARNING, _("Header magic constant mismatch"));
            err = avs_errno(AVS_EBADMSG);
        }
        if (avs_is_ok(err)) {
            avs_persistence_context_t restore_ctx =
                    avs_persistence_restore_context_create(in_stream);
            repr->instances = NULL;
            err = avs_persistence_list(&restore_ctx,
                                       (AVS_LIST(void) *) &repr->instances,
                                       sizeof(sec_instance_t), handle_instance,
                                       (void *) (intptr_t) version, NULL);
            if (avs_is_ok(err)
                    && _standalone_sec_object_validate_and_process_keys(
                               repr->anjay, repr)) {
                err = avs_errno(AVS_EPROTO);
            }
            if (avs_is_err(err)) {
                _standalone_sec_destroy_instances(&repr->instances, true);
                repr->instances = backup.instances;
            } else {
                _standalone_sec_destroy_instances(&backup.instances, true);
                _standalone_sec_clear_modified(repr);
                persistence_log(INFO, _("Security Object state restored"));
            }
        }
    }
    return err;
}

#else // AVS_COMMONS_WITH_AVS_PERSISTENCE

avs_error_t standalone_security_object_persist(anjay_t *anjay,
                                               avs_stream_t *out_stream) {
    (void) anjay;
    (void) out_stream;
    persistence_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

avs_error_t standalone_security_object_restore(anjay_t *anjay,
                                               avs_stream_t *in_stream) {
    (void) anjay;
    (void) in_stream;
    persistence_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

#endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
