/*
 * Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
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

#    include <string.h>

#    include <avsystem/commons/avs_utils.h>

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

int _anjay_sec_validate_sms_security_mode(int32_t security_mode) {
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

int _anjay_sec_fetch_sms_security_mode(anjay_unlocked_input_ctx_t *ctx,
                                       anjay_sms_security_mode_t *out) {
    int32_t value;
    int retval = _anjay_get_i32_unlocked(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_sms_security_mode(value);
    }
    if (!retval) {
        *out = (anjay_sms_security_mode_t) value;
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

void _anjay_sec_key_or_data_cleanup(sec_key_or_data_t *value,
                                    bool remove_from_engine) {
    if (!value->prev_ref && !value->next_ref) {
        switch (value->type) {
        case SEC_KEY_AS_DATA:
            memset(value->value.data.data, 0, value->value.data.capacity);
            _anjay_raw_buffer_clear(&value->value.data);
            break;
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
    _anjay_sec_key_or_data_cleanup(&instance->sms_key_params,
                                   remove_from_engine);
    _anjay_sec_key_or_data_cleanup(&instance->sms_secret_key,
                                   remove_from_engine);
    avs_free((char *) (intptr_t) instance->sms_number);
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
