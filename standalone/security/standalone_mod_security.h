#ifndef ANJAY_STANDALONE_SECURITY_SECURITY_H
#define ANJAY_STANDALONE_SECURITY_SECURITY_H

#include <avsystem/commons/avs_log.h>

#include "standalone_security.h"

typedef enum {
    SEC_RES_LWM2M_SERVER_URI = 0,
    SEC_RES_BOOTSTRAP_SERVER = 1,
    SEC_RES_SECURITY_MODE = 2,
    SEC_RES_PK_OR_IDENTITY = 3,
    SEC_RES_SERVER_PK = 4,
    SEC_RES_SECRET_KEY = 5,
#ifdef ANJAY_WITH_SMS
    SEC_RES_SMS_SECURITY_MODE = 6,
    SEC_RES_SMS_BINDING_KEY_PARAMS = 7,
    SEC_RES_SMS_BINDING_SECRET_KEYS = 8,
    SEC_RES_SERVER_SMS_NUMBER = 9,
#endif // ANJAY_WITH_SMS
    SEC_RES_SHORT_SERVER_ID = 10,
    SEC_RES_CLIENT_HOLD_OFF_TIME = 11,
    SEC_RES_BOOTSTRAP_TIMEOUT = 12,
#ifdef ANJAY_WITH_LWM2M11
    SEC_RES_MATCHING_TYPE = 13,
    SEC_RES_SNI = 14,
    SEC_RES_CERTIFICATE_USAGE = 15,
    SEC_RES_DTLS_TLS_CIPHERSUITE = 16,
#endif // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_COAP_OSCORE
    SEC_RES_OSCORE_SECURITY_MODE = 17,
#endif // ANJAY_WITH_COAP_OSCORE
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

typedef struct {
    void *data;
    /** Amount of bytes currently stored in the buffer. */
    size_t size;
    /** Amount of bytes that might be stored in the buffer. */
    size_t capacity;
} standalone_raw_buffer_t;

typedef struct sec_key_or_data_struct sec_key_or_data_t;
struct sec_key_or_data_struct {
    sec_key_or_data_type_t type;
    union {
        standalone_raw_buffer_t data;
#if defined(ANJAY_WITH_SECURITY_STRUCTURED) \
        || defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT)
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
    // shared_ptr<variant<standalone_raw_buffer_t, security_info_and_heap_buf>>.
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
    // These pointers are manipulated in _standalone_sec_key_or_data_cleanup()
    // and sec_key_or_data_create_ref(), so see there for the actual
    // implementation. Also note that in practice, it is not expected for more
    // than two references (one in instances and one in saved_instances) to the
    // same buffer to exist, but a generic solution isn't more complicated,
    // so...
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
    standalone_raw_buffer_t server_public_key;

    anjay_ssid_t ssid;
    int32_t holdoff_s;
    int32_t bs_timeout_s;

#ifdef ANJAY_WITH_SMS
    anjay_sms_security_mode_t sms_security_mode;
    sec_key_or_data_t sms_key_params;
    sec_key_or_data_t sms_secret_key;
    char *sms_number;
#endif // ANJAY_WITH_SMS

#ifdef ANJAY_WITH_LWM2M11
    int8_t matching_type;
    char *server_name_indication;
    int8_t certificate_usage;
    AVS_LIST(sec_cipher_instance_t) enabled_ciphersuites;
#    ifdef ANJAY_WITH_COAP_OSCORE
    anjay_iid_t oscore_iid;
#    endif // ANJAY_WITH_COAP_OSCORE
#endif     // ANJAY_WITH_LWM2M11

    bool present_resources[_SEC_RES_COUNT];
} sec_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    anjay_t *anjay;
    AVS_LIST(sec_instance_t) instances;
    AVS_LIST(sec_instance_t) saved_instances;
    bool modified_since_persist;
    bool saved_modified_since_persist;
    bool in_transaction;
#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
    standalone_security_hsm_configuration_t hsm_config;
    avs_crypto_prng_ctx_t *prng_ctx;
    bool prng_allocated_by_user;
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
} sec_repr_t;

static inline void _standalone_sec_mark_modified(sec_repr_t *repr) {
    repr->modified_since_persist = true;
}

static inline void _standalone_sec_clear_modified(sec_repr_t *repr) {
    repr->modified_since_persist = false;
}

void _standalone_sec_instance_update_resource_presence(sec_instance_t *inst);

#define security_log(level, ...) avs_log(security, level, __VA_ARGS__)
#define _(Arg) AVS_DISPOSABLE_LOG(Arg)

#endif /* ANJAY_STANDALONE_SECURITY_SECURITY_H */
