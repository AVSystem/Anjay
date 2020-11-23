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

#ifndef SECURITY_SECURITY_H
#define SECURITY_SECURITY_H
#include <anjay_init.h>

#include <anjay/core.h>

#include <avsystem/commons/avs_list.h>

#include <anjay/security.h>

#include <anjay_modules/anjay_raw_buffer.h>
#include <anjay_modules/anjay_utils_core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    SEC_RES_LWM2M_SERVER_URI = 0,
    SEC_RES_BOOTSTRAP_SERVER = 1,
    SEC_RES_SECURITY_MODE = 2,
    SEC_RES_PK_OR_IDENTITY = 3,
    SEC_RES_SERVER_PK = 4,
    SEC_RES_SECRET_KEY = 5,
    SEC_RES_SMS_SECURITY_MODE = 6,
    SEC_RES_SMS_BINDING_KEY_PARAMS = 7,
    SEC_RES_SMS_BINDING_SECRET_KEYS = 8,
    SEC_RES_SERVER_SMS_NUMBER = 9,
    SEC_RES_SHORT_SERVER_ID = 10,
    SEC_RES_CLIENT_HOLD_OFF_TIME = 11,
    SEC_RES_BOOTSTRAP_TIMEOUT = 12,
} security_resource_t;

typedef enum { SEC_KEY_AS_DATA, SEC_KEY_AS_KEY } sec_key_or_data_type_t;

typedef struct {
    sec_key_or_data_type_t type;
    union {
        anjay_raw_buffer_t data;
    } value;
} sec_key_or_data_t;

typedef struct {
    anjay_iid_t iid;
    char *server_uri;
    bool is_bootstrap;
    anjay_security_mode_t security_mode;
    sec_key_or_data_t public_cert_or_psk_identity;
    sec_key_or_data_t private_cert_or_psk_key;
    anjay_raw_buffer_t server_public_key;

    anjay_ssid_t ssid;
    int32_t holdoff_s;
    int32_t bs_timeout_s;

    anjay_sms_security_mode_t sms_security_mode;
    anjay_raw_buffer_t sms_key_params;
    anjay_raw_buffer_t sms_secret_key;
    char *sms_number;

    bool has_is_bootstrap;
    bool has_security_mode;
    bool has_ssid;
    bool has_sms_security_mode;
    bool has_sms_key_params;
    bool has_sms_secret_key;

} sec_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(sec_instance_t) instances;
    AVS_LIST(sec_instance_t) saved_instances;
    bool modified_since_persist;
    bool saved_modified_since_persist;
    bool in_transaction;
} sec_repr_t;

static inline void _anjay_sec_mark_modified(sec_repr_t *repr) {
    repr->modified_since_persist = true;
}

static inline void _anjay_sec_clear_modified(sec_repr_t *repr) {
    repr->modified_since_persist = false;
}

#define security_log(level, ...) _anjay_log(security, level, __VA_ARGS__)

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_SECURITY_H */
