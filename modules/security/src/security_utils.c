/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <string.h>

#include <avsystem/commons/utils.h>

#include "security_utils.h"

VISIBILITY_SOURCE_BEGIN

int _anjay_sec_validate_udp_security_mode(int32_t security_mode) {
    switch (security_mode) {
    case ANJAY_UDP_SECURITY_NOSEC:
    case ANJAY_UDP_SECURITY_PSK:
    case ANJAY_UDP_SECURITY_CERTIFICATE:
        return 0;
    case ANJAY_UDP_SECURITY_RPK:
        security_log(ERROR, "Raw Public Key mode not supported");
        return ANJAY_ERR_NOT_IMPLEMENTED;
    default:
        security_log(ERROR, "Invalid UDP Security Mode");
        return ANJAY_ERR_BAD_REQUEST;
    }
}

int _anjay_sec_fetch_udp_security_mode(anjay_input_ctx_t *ctx,
                                       anjay_udp_security_mode_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_udp_security_mode(value);
    }
    if (!retval) {
        *out = (anjay_udp_security_mode_t) value;
    }
    return retval;
}

int _anjay_sec_validate_sms_security_mode(int32_t security_mode) {
    switch (security_mode) {
    case ANJAY_SMS_SECURITY_DTLS_PSK:
    case ANJAY_SMS_SECURITY_NOSEC:
        return 0;
    case ANJAY_SMS_SECURITY_SECURE_PACKET:
        security_log(DEBUG, "Secure Packet mode not supported");
        return ANJAY_ERR_NOT_IMPLEMENTED;
    default:
        security_log(DEBUG, "Invalid SMS Security Mode");
        return ANJAY_ERR_BAD_REQUEST;
    }
}

int _anjay_sec_fetch_sms_security_mode(anjay_input_ctx_t *ctx,
                                       anjay_sms_security_mode_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
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

int _anjay_sec_fetch_short_server_id(anjay_input_ctx_t *ctx,
                                     anjay_ssid_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_short_server_id(value);
    }
    if (!retval) {
        *out = (anjay_ssid_t) value;
    }
    return retval;
}

void _anjay_sec_destroy_instance_fields(sec_instance_t *instance) {
    if (!instance) {
        return;
    }
    avs_free((char *) (intptr_t) instance->server_uri);
    avs_free((char *) (intptr_t) instance->sms_number);
    _anjay_raw_buffer_clear(&instance->public_cert_or_psk_identity);
    _anjay_raw_buffer_clear(&instance->private_cert_or_psk_key);
    _anjay_raw_buffer_clear(&instance->server_public_key);
    _anjay_raw_buffer_clear(&instance->sms_key_params);
    _anjay_raw_buffer_clear(&instance->sms_secret_key);
}

void _anjay_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr) {
    AVS_LIST_CLEAR(instances_ptr) {
        _anjay_sec_destroy_instance_fields(*instances_ptr);
    }
}

static int _anjay_sec_clone_instance(sec_instance_t *dest,
                                     const sec_instance_t *src) {
    *dest = *src;
    dest->public_cert_or_psk_identity = ANJAY_RAW_BUFFER_EMPTY;
    dest->private_cert_or_psk_key = ANJAY_RAW_BUFFER_EMPTY;
    dest->server_public_key = ANJAY_RAW_BUFFER_EMPTY;
    dest->sms_key_params = ANJAY_RAW_BUFFER_EMPTY;
    dest->sms_secret_key = ANJAY_RAW_BUFFER_EMPTY;
    dest->server_uri = NULL;
    dest->sms_number = NULL;

    dest->server_uri = avs_strdup(src->server_uri);
    if (!dest->server_uri) {
        security_log(ERROR, "Cannot clone Server Uri resource");
        return -1;
    }
    if (src->sms_number) {
        dest->sms_number = avs_strdup(src->sms_number);
        if (!dest->sms_number) {
            security_log(ERROR, "Cannot clone Server SMS Number resource");
            return -1;
        }
    }
    if (_anjay_raw_buffer_clone(&dest->public_cert_or_psk_identity,
                                &src->public_cert_or_psk_identity)) {
        security_log(ERROR, "Cannot clone Pk Or Identity resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->private_cert_or_psk_key,
                                &src->private_cert_or_psk_key)) {
        security_log(ERROR, "Cannot clone Secret Key resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->server_public_key,
                                &src->server_public_key)) {
        security_log(ERROR, "Cannot clone Server Public Key resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->sms_key_params, &src->sms_key_params)) {
        security_log(ERROR, "Cannot clone SMS Binding Key Parameters resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->sms_secret_key, &src->sms_secret_key)) {
        security_log(ERROR, "Cannot clone SMS Binding Secret Key(s) resource");
        return -1;
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
                security_log(ERROR, "Cannot clone Security Object Instances");
                _anjay_sec_destroy_instances(&retval);
                return NULL;
            }
            AVS_LIST_ADVANCE_PTR(&last);
        }
    }
    return retval;
}
