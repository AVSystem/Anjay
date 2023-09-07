#include <string.h>

#include "standalone_security_utils.h"

int _standalone_sec_validate_security_mode(int32_t security_mode) {
    switch (security_mode) {
    case ANJAY_SECURITY_NOSEC:
    case ANJAY_SECURITY_PSK:
    case ANJAY_SECURITY_CERTIFICATE:
    case ANJAY_SECURITY_EST:
        return 0;
    case ANJAY_SECURITY_RPK:
        security_log(ERROR, _("Raw Public Key mode not supported"));
        return ANJAY_ERR_NOT_IMPLEMENTED;
    default:
        security_log(ERROR, _("Invalid Security Mode"));
        return ANJAY_ERR_BAD_REQUEST;
    }
}

int _standalone_sec_fetch_security_mode(anjay_input_ctx_t *ctx,
                                        anjay_security_mode_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _standalone_sec_validate_security_mode(value);
    }
    if (!retval) {
        *out = (anjay_security_mode_t) value;
    }
    return retval;
}

#ifdef ANJAY_WITH_SMS
int _standalone_sec_validate_sms_security_mode(int32_t security_mode) {
    switch (security_mode) {
    case ANJAY_SMS_SECURITY_DTLS_PSK:
    case ANJAY_SMS_SECURITY_NOSEC:
        return 0;
    case ANJAY_SMS_SECURITY_SECURE_PACKET:
        security_log(DEBUG, _("Secure Packet mode not supported"));
        return ANJAY_ERR_NOT_IMPLEMENTED;
    default:
        security_log(DEBUG, _("Invalid SMS Security Mode"));
        return ANJAY_ERR_BAD_REQUEST;
    }
}

int _standalone_sec_fetch_sms_security_mode(anjay_input_ctx_t *ctx,
                                            anjay_sms_security_mode_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _standalone_sec_validate_sms_security_mode(value);
    }
    if (!retval) {
        *out = (anjay_sms_security_mode_t) value;
    }
    return retval;
}
#endif // ANJAY_WITH_SMS

static int _standalone_sec_validate_short_server_id(int32_t ssid) {
    return ssid > 0 && ssid <= UINT16_MAX ? 0 : -1;
}

int _standalone_sec_fetch_short_server_id(anjay_input_ctx_t *ctx,
                                          anjay_ssid_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _standalone_sec_validate_short_server_id(value);
    }
    if (!retval) {
        *out = (anjay_ssid_t) value;
    }
    return retval;
}

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)                    \
        || (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) \
            && defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE))
int _standalone_sec_init_certificate_chain_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_certificate_chain_info_t *in_value) {
    avs_crypto_certificate_chain_info_t *array = NULL;
    size_t array_element_count;
    if (avs_is_err(avs_crypto_certificate_chain_info_copy_as_array(
                &array, &array_element_count, *in_value))) {
        return -1;
    }
    assert(array || !array_element_count);
    assert(!out_resource->prev_ref);
    assert(!out_resource->next_ref);
    assert(type == SEC_KEY_AS_KEY_EXTERNAL || type == SEC_KEY_AS_KEY_OWNED);
    out_resource->type = type;
    out_resource->value.key.info =
            avs_crypto_certificate_chain_info_from_array(array,
                                                         array_element_count)
                    .desc;
    out_resource->value.key.heap_buf = array;
    return 0;
}

int _standalone_sec_init_private_key_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_private_key_info_t *in_value) {
    avs_crypto_private_key_info_t *private_key = NULL;
    if (avs_is_err(avs_crypto_private_key_info_copy(&private_key, *in_value))) {
        return -1;
    }
    assert(private_key);
    assert(!out_resource->prev_ref);
    assert(!out_resource->next_ref);
    assert(type == SEC_KEY_AS_KEY_EXTERNAL || type == SEC_KEY_AS_KEY_OWNED);
    out_resource->type = type;
    out_resource->value.key.info = private_key->desc;
    out_resource->value.key.heap_buf = private_key;
    return 0;
}
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
          (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
          defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE)) */

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)                    \
        || (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) \
            && defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE))
int _standalone_sec_init_psk_identity_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_psk_identity_info_t *in_value) {
    avs_crypto_psk_identity_info_t *psk_identity = NULL;
    if (avs_is_err(
                avs_crypto_psk_identity_info_copy(&psk_identity, *in_value))) {
        return -1;
    }
    assert(psk_identity);
    assert(!out_resource->prev_ref);
    assert(!out_resource->next_ref);
    assert(type == SEC_KEY_AS_KEY_EXTERNAL || type == SEC_KEY_AS_KEY_OWNED);
    out_resource->type = type;
    out_resource->value.key.info = psk_identity->desc;
    out_resource->value.key.heap_buf = psk_identity;
    return 0;
}

int _standalone_sec_init_psk_key_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_psk_key_info_t *in_value) {
    avs_crypto_psk_key_info_t *psk_key = NULL;
    if (avs_is_err(avs_crypto_psk_key_info_copy(&psk_key, *in_value))) {
        return -1;
    }
    assert(psk_key);
    assert(!out_resource->prev_ref);
    assert(!out_resource->next_ref);
    assert(type == SEC_KEY_AS_KEY_EXTERNAL || type == SEC_KEY_AS_KEY_OWNED);
    out_resource->type = type;
    out_resource->value.key.info = psk_key->desc;
    out_resource->value.key.heap_buf = psk_key;
    return 0;
}
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
          (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
          defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)) */

#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
static void
remove_sec_key_from_engine(const avs_crypto_security_info_union_t *desc) {
    assert(desc->source != AVS_CRYPTO_DATA_SOURCE_LIST);
    if (desc->source == AVS_CRYPTO_DATA_SOURCE_ENGINE) {
        avs_error_t err;
        switch (desc->type) {
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
        case AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN:
            err = avs_crypto_pki_engine_certificate_rm(desc->info.engine.query);
            break;
        case AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY:
            err = avs_crypto_pki_engine_key_rm(desc->info.engine.query);
            break;
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE
#    ifdef AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
        case AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY:
            err = avs_crypto_psk_engine_identity_rm(desc->info.engine.query);
            break;
        case AVS_CRYPTO_SECURITY_INFO_PSK_KEY:
            err = avs_crypto_psk_engine_key_rm(desc->info.engine.query);
            break;
#    endif // AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE
        default:
            err = avs_errno(AVS_EINVAL);
        }
        if (avs_is_err(err)) {
            security_log(WARNING,
                         _("could not remove ") "%s" _(
                                 " from the engine storage"),
                         desc->info.engine.query);
        }
    } else if (desc->source == AVS_CRYPTO_DATA_SOURCE_ARRAY) {
        for (size_t i = 0; i < desc->info.array.element_count; ++i) {
            remove_sec_key_from_engine(&desc->info.array.array_ptr[i]);
        }
    }
}
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT

void _standalone_sec_key_or_data_cleanup(sec_key_or_data_t *value,
                                         bool remove_from_engine) {
    (void) remove_from_engine;
    if (!value->prev_ref && !value->next_ref) {
        switch (value->type) {
        case SEC_KEY_AS_DATA:
            memset(value->value.data.data, 0, value->value.data.capacity);
            _standalone_raw_buffer_clear(&value->value.data);
            break;
#if defined(ANJAY_WITH_SECURITY_STRUCTURED) \
        || defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT)
        case SEC_KEY_AS_KEY_OWNED:
#    ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
            if (remove_from_engine) {
                remove_sec_key_from_engine(&value->value.key.info);
            }
#    endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
           // fall-through
        case SEC_KEY_AS_KEY_EXTERNAL:
            avs_free(value->value.key.heap_buf);
            break;
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
        default:
            AVS_UNREACHABLE("invalid value of sec_key_or_data_type_t");
        }
    } else {
        if (value->prev_ref) {
            value->prev_ref->next_ref = value->next_ref;
        }
        if (value->next_ref) {
            value->next_ref->prev_ref = value->prev_ref;
        }
    }
    memset(value, 0, sizeof(*value));
    assert(value->type == SEC_KEY_AS_DATA);
}

void _standalone_sec_destroy_instance_fields(sec_instance_t *instance,
                                             bool remove_from_engine) {
    if (!instance) {
        return;
    }
    avs_free((char *) (intptr_t) instance->server_uri);
    _standalone_sec_key_or_data_cleanup(&instance->public_cert_or_psk_identity,
                                        remove_from_engine);
    _standalone_sec_key_or_data_cleanup(&instance->private_cert_or_psk_key,
                                        remove_from_engine);
    _standalone_raw_buffer_clear(&instance->server_public_key);
#ifdef ANJAY_WITH_LWM2M11
    AVS_LIST_CLEAR(&instance->enabled_ciphersuites);
    avs_free(instance->server_name_indication);
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_SMS
    _standalone_sec_key_or_data_cleanup(&instance->sms_key_params,
                                        remove_from_engine);
    _standalone_sec_key_or_data_cleanup(&instance->sms_secret_key,
                                        remove_from_engine);
    avs_free((char *) (intptr_t) instance->sms_number);
#endif // ANJAY_WITH_SMS
}

void _standalone_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr,
                                       bool remove_from_engine) {
    AVS_LIST_CLEAR(instances_ptr) {
        _standalone_sec_destroy_instance_fields(*instances_ptr,
                                                remove_from_engine);
    }
}

static void sec_key_or_data_create_ref(sec_key_or_data_t *dest,
                                       sec_key_or_data_t *src) {
    *dest = *src;
    dest->prev_ref = src;
    dest->next_ref = src->next_ref;
    if (src->next_ref) {
        src->next_ref->prev_ref = dest;
    }
    src->next_ref = dest;
}

static int _standalone_sec_clone_instance(sec_instance_t *dest,
                                          sec_instance_t *src) {
    *dest = *src;

    assert(src->server_uri);
    dest->server_uri = avs_strdup(src->server_uri);
    if (!dest->server_uri) {
        security_log(ERROR, _("Cannot clone Server Uri resource"));
        return -1;
    }

    sec_key_or_data_create_ref(&dest->public_cert_or_psk_identity,
                               &src->public_cert_or_psk_identity);
    sec_key_or_data_create_ref(&dest->private_cert_or_psk_key,
                               &src->private_cert_or_psk_key);

    memset(&dest->server_public_key, 0, sizeof(dest->server_public_key));
    if (_standalone_raw_buffer_clone(&dest->server_public_key,
                                     &src->server_public_key)) {
        security_log(ERROR, _("Cannot clone Server Public Key resource"));
        return -1;
    }

#ifdef ANJAY_WITH_LWM2M11
    dest->server_name_indication = NULL;
    if (src->server_name_indication
            && !(dest->server_name_indication =
                         avs_strdup(src->server_name_indication))) {
        security_log(ERROR, _("Cannot clone SNI resource"));
        return -1;
    }
#endif // ANJAY_WITH_LWM2M11

#ifdef ANJAY_WITH_SMS
    sec_key_or_data_create_ref(&dest->sms_key_params, &src->sms_key_params);
    sec_key_or_data_create_ref(&dest->sms_secret_key, &src->sms_secret_key);

    dest->sms_number = NULL;
    if (src->sms_number) {
        dest->sms_number = avs_strdup(src->sms_number);
        if (!dest->sms_number) {
            security_log(ERROR, _("Cannot clone Server SMS Number resource"));
            return -1;
        }
    }
#endif // ANJAY_WITH_SMS

    return 0;
}

AVS_LIST(sec_instance_t)
_standalone_sec_clone_instances(const sec_repr_t *repr) {
    AVS_LIST(sec_instance_t) retval = NULL;
    AVS_LIST(sec_instance_t) current;
    AVS_LIST(sec_instance_t) *last;
    last = &retval;

    AVS_LIST_FOREACH(current, repr->instances) {
        if (AVS_LIST_INSERT_NEW(sec_instance_t, last)) {
            if (_standalone_sec_clone_instance(*last, current)) {
                security_log(ERROR,
                             _("Cannot clone Security Object Instances"));
                _standalone_sec_destroy_instances(&retval, false);
                return NULL;
            }
            AVS_LIST_ADVANCE_PTR(&last);
        }
    }
    return retval;
}

void _standalone_raw_buffer_clear(standalone_raw_buffer_t *buffer) {
    avs_free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

int _standalone_raw_buffer_clone(standalone_raw_buffer_t *dst,
                                 const standalone_raw_buffer_t *src) {
    assert(!dst->data && !dst->size);
    if (!src->size) {
        return 0;
    }
    dst->data = avs_malloc(src->size);
    if (!dst->data) {
        return -1;
    }
    dst->capacity = src->size;
    dst->size = src->size;
    memcpy(dst->data, src->data, src->size);
    return 0;
}

typedef int chunk_getter_t(anjay_input_ctx_t *ctx,
                           char *out,
                           size_t out_size,
                           bool *out_finished,
                           size_t *out_bytes_read);

static int bytes_getter(anjay_input_ctx_t *ctx,
                        char *out,
                        size_t size,
                        bool *out_finished,
                        size_t *out_bytes_read) {
    return anjay_get_bytes(ctx, out_bytes_read, out_finished, out, size);
}

static int string_getter(anjay_input_ctx_t *ctx,
                         char *out,
                         size_t size,
                         bool *out_finished,
                         size_t *out_bytes_read) {
    int result = anjay_get_string(ctx, out, size);
    if (result < 0) {
        return result;
    }
    *out_finished = true;
    *out_bytes_read = strlen(out) + 1;
    if (result == ANJAY_BUFFER_TOO_SHORT) {
        *out_finished = false;
        /**
         * We don't want null terminator, because we're still in the phase of
         * string chunk concatenation (and null terminators in the middle of
         * the string are rather bad).
         */
        --*out_bytes_read;
    }
    return 0;
}

static int generic_getter(anjay_input_ctx_t *ctx,
                          char **out,
                          size_t *out_bytes_read,
                          chunk_getter_t *getter) {
    char tmp[128];
    bool finished = false;
    char *buffer = NULL;
    size_t buffer_size = 0;
    int result;
    do {
        size_t chunk_bytes_read = 0;
        if ((result = getter(ctx, tmp, sizeof(tmp), &finished,
                             &chunk_bytes_read))) {
            goto error;
        }
        if (chunk_bytes_read > 0) {
            char *bigger_buffer =
                    (char *) avs_realloc(buffer,
                                         buffer_size + chunk_bytes_read);
            if (!bigger_buffer) {
                result = ANJAY_ERR_INTERNAL;
                goto error;
            }
            memcpy(bigger_buffer + buffer_size, tmp, chunk_bytes_read);
            buffer = bigger_buffer;
            buffer_size += chunk_bytes_read;
        }
    } while (!finished);
    *out = buffer;
    *out_bytes_read = buffer_size;
    return 0;
error:
    avs_free(buffer);
    return result;
}

int _standalone_io_fetch_bytes(anjay_input_ctx_t *ctx,
                               standalone_raw_buffer_t *buffer) {
    _standalone_raw_buffer_clear(buffer);
    int retval = generic_getter(ctx, (char **) &buffer->data, &buffer->size,
                                bytes_getter);
    buffer->capacity = buffer->size;
    return retval;
}

int _standalone_io_fetch_string(anjay_input_ctx_t *ctx, char **out) {
    avs_free(*out);
    *out = NULL;
    size_t bytes_read = 0;
    return generic_getter(ctx, out, &bytes_read, string_getter);
}
