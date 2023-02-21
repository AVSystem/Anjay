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

#    include <string.h>

#    include <anjay_modules/anjay_dm_utils.h>

#    include "anjay_security_utils.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_sec_validate_security_mode(int32_t security_mode) {
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

int _anjay_sec_fetch_security_mode(anjay_unlocked_input_ctx_t *ctx,
                                   anjay_security_mode_t *out) {
    int32_t value;
    int retval = _anjay_get_i32_unlocked(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_security_mode(value);
    }
    if (!retval) {
        *out = (anjay_security_mode_t) value;
    }
    return retval;
}

static int _anjay_sec_validate_short_server_id(int32_t ssid) {
    return ssid > 0 && ssid <= UINT16_MAX ? 0 : -1;
}

int _anjay_sec_fetch_short_server_id(anjay_unlocked_input_ctx_t *ctx,
                                     anjay_ssid_t *out) {
    int32_t value;
    int retval = _anjay_get_i32_unlocked(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_short_server_id(value);
    }
    if (!retval) {
        *out = (anjay_ssid_t) value;
    }
    return retval;
}

#    if defined(ANJAY_WITH_SECURITY_STRUCTURED)
int _anjay_sec_init_certificate_chain_resource(
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

int _anjay_sec_init_private_key_resource(
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
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
              (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
              defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE)) */

#    if defined(ANJAY_WITH_SECURITY_STRUCTURED)
int _anjay_sec_init_psk_identity_resource(
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

int _anjay_sec_init_psk_key_resource(
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
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
              (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
              defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)) */

void _anjay_sec_key_or_data_cleanup(sec_key_or_data_t *value,
                                    bool remove_from_engine) {
    (void) remove_from_engine;
    if (!value->prev_ref && !value->next_ref) {
        switch (value->type) {
        case SEC_KEY_AS_DATA:
            memset(value->value.data.data, 0, value->value.data.capacity);
            _anjay_raw_buffer_clear(&value->value.data);
            break;
#    if defined(ANJAY_WITH_SECURITY_STRUCTURED)
        case SEC_KEY_AS_KEY_OWNED:
            // fall-through
        case SEC_KEY_AS_KEY_EXTERNAL:
            avs_free(value->value.key.heap_buf);
            break;
#    endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
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

void _anjay_sec_destroy_instance_fields(sec_instance_t *instance,
                                        bool remove_from_engine) {
    if (!instance) {
        return;
    }
    avs_free((char *) (intptr_t) instance->server_uri);
    _anjay_sec_key_or_data_cleanup(&instance->public_cert_or_psk_identity,
                                   remove_from_engine);
    _anjay_sec_key_or_data_cleanup(&instance->private_cert_or_psk_key,
                                   remove_from_engine);
    _anjay_raw_buffer_clear(&instance->server_public_key);
#    ifdef ANJAY_WITH_LWM2M11
    AVS_LIST_CLEAR(&instance->enabled_ciphersuites);
    avs_free(instance->server_name_indication);
#    endif // ANJAY_WITH_LWM2M11
}

void _anjay_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr,
                                  bool remove_from_engine) {
    AVS_LIST_CLEAR(instances_ptr) {
        _anjay_sec_destroy_instance_fields(*instances_ptr, remove_from_engine);
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

static int _anjay_sec_clone_instance(sec_instance_t *dest,
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

    dest->server_public_key = ANJAY_RAW_BUFFER_EMPTY;
    if (_anjay_raw_buffer_clone(&dest->server_public_key,
                                &src->server_public_key)) {
        security_log(ERROR, _("Cannot clone Server Public Key resource"));
        return -1;
    }

#    ifdef ANJAY_WITH_LWM2M11
    dest->server_name_indication = NULL;
    if (src->server_name_indication
            && !(dest->server_name_indication =
                         avs_strdup(src->server_name_indication))) {
        security_log(ERROR, _("Cannot clone SNI resource"));
        return -1;
    }
#    endif // ANJAY_WITH_LWM2M11

    return 0;
}

AVS_LIST(sec_instance_t) _anjay_sec_clone_instances(const sec_repr_t *repr) {
    AVS_LIST(sec_instance_t) retval = NULL;
    AVS_LIST(sec_instance_t) current;
    AVS_LIST(sec_instance_t) *last;
    last = &retval;

    AVS_LIST_FOREACH(current, repr->instances) {
        if (AVS_LIST_INSERT_NEW(sec_instance_t, last)) {
            if (_anjay_sec_clone_instance(*last, current)) {
                security_log(ERROR,
                             _("Cannot clone Security Object Instances"));
                _anjay_sec_destroy_instances(&retval, false);
                return NULL;
            }
            AVS_LIST_ADVANCE_PTR(&last);
        }
    }
    return retval;
}

#endif // ANJAY_WITH_MODULE_SECURITY
