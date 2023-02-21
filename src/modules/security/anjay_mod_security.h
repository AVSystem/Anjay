/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SECURITY_SECURITY_H
#define SECURITY_SECURITY_H
#include <anjay_init.h>

#include <anjay/security.h>

#include <anjay_modules/anjay_raw_buffer.h>
#include <anjay_modules/dm/anjay_modules.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    SEC_RES_LWM2M_SERVER_URI = 0,
    SEC_RES_BOOTSTRAP_SERVER = 1,
    SEC_RES_SECURITY_MODE = 2,
    SEC_RES_PK_OR_IDENTITY = 3,
    SEC_RES_SERVER_PK = 4,
    SEC_RES_SECRET_KEY = 5,
    SEC_RES_SHORT_SERVER_ID = 10,
    SEC_RES_CLIENT_HOLD_OFF_TIME = 11,
    SEC_RES_BOOTSTRAP_TIMEOUT = 12,
#ifdef ANJAY_WITH_LWM2M11
    SEC_RES_MATCHING_TYPE = 13,
    SEC_RES_SNI = 14,
    SEC_RES_CERTIFICATE_USAGE = 15,
    SEC_RES_DTLS_TLS_CIPHERSUITE = 16,
#endif // ANJAY_WITH_LWM2M11
    _SEC_RES_COUNT
} security_rid_t;

typedef struct {
    anjay_riid_t riid;
    uint32_t cipher_id;
} sec_cipher_instance_t;

typedef enum {
    SEC_KEY_AS_DATA,
    SEC_KEY_AS_KEY_EXTERNAL,
    SEC_KEY_AS_KEY_OWNED
} sec_key_or_data_type_t;

typedef struct sec_key_or_data_struct sec_key_or_data_t;
struct sec_key_or_data_struct {
    sec_key_or_data_type_t type;
    union {
        anjay_raw_buffer_t data;
#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
        struct {
            avs_crypto_security_info_union_t info;
            void *heap_buf;
        } key;
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) || \
          defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) */
    } value;

    // HERE GOES MAGIC.
    //
    // sec_key_or_data_t is, in a way, semantically something like a
    // shared_ptr<variant<anjay_raw_buffer_t, security_info_and_heap_buf>>.
    // Note that the instances of sec_key_or_data_t itself are NOT individually
    // allocated on the heap, as they are fields in sec_instance_t.
    //
    // These two fields organize multiple instances of sec_key_or_data_t that
    // refer to the same heap buffer (either via value.data.data or
    // value.key.heap_buf) in a doubly linked list. That way, when multiple
    // instances referring to the same buffer exist, and one of them is to be
    // cleaned up, that cleaned up instance can be removed from the list without
    // needing any other pointers (which wouldn't work if that was a singly
    // linked list).
    //
    // When the last (or only) instance referring to a given buffer is being
    // cleaned up, both prev_ref and next_ref will be NULL, which is a signal
    // to actually free the resources.
    //
    // These pointers are manipulated in _anjay_sec_key_or_data_cleanup() and
    // sec_key_or_data_create_ref(), so see there for the actual implementation.
    // Also note that in practice, it is not expected for more than two
    // references (one in instances and one in saved_instances) to the same
    // buffer to exist, but a generic solution isn't more complicated, so...
    sec_key_or_data_t *prev_ref;
    sec_key_or_data_t *next_ref;
};

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

#ifdef ANJAY_WITH_LWM2M11
    int8_t matching_type;
    char *server_name_indication;
    int8_t certificate_usage;
    AVS_LIST(sec_cipher_instance_t) enabled_ciphersuites;
#endif // ANJAY_WITH_LWM2M11

    bool present_resources[_SEC_RES_COUNT];
} sec_instance_t;

typedef struct {
    anjay_dm_installed_object_t def_ptr;
    const anjay_unlocked_dm_object_def_t *def;
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

void _anjay_sec_instance_update_resource_presence(sec_instance_t *inst);

#define security_log(level, ...) _anjay_log(security, level, __VA_ARGS__)

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_SECURITY_H */
