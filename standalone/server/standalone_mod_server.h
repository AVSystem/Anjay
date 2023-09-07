#ifndef ANJAY_STANDALONE_SERVER_MOD_SERVER_H
#define ANJAY_STANDALONE_SERVER_MOD_SERVER_H

#include <avsystem/commons/avs_log.h>

#include "standalone_server.h"

typedef struct {
    char data[8];
} standalone_binding_mode_t;

typedef enum {
    SERV_RES_SSID = 0,
    SERV_RES_LIFETIME = 1,
    SERV_RES_DEFAULT_MIN_PERIOD = 2,
    SERV_RES_DEFAULT_MAX_PERIOD = 3,
#ifndef ANJAY_WITHOUT_DEREGISTER
    SERV_RES_DISABLE = 4,
    SERV_RES_DISABLE_TIMEOUT = 5,
#endif // ANJAY_WITHOUT_DEREGISTER
    SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE = 6,
    SERV_RES_BINDING = 7,
    SERV_RES_REGISTRATION_UPDATE_TRIGGER = 8,
#ifdef ANJAY_WITH_LWM2M11
    SERV_RES_BOOTSTRAP_REQUEST_TRIGGER = 9,
    SERV_RES_TLS_DTLS_ALERT_CODE = 11,
    SERV_RES_LAST_BOOTSTRAPPED = 12,
    SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE = 16,
    SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT = 17,
    SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER = 18,
    SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER = 19,
    SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT = 20,
#    ifdef ANJAY_WITH_SMS
    SERV_RES_TRIGGER = 21,
#    endif // ANJAY_WITH_SMS
    SERV_RES_PREFERRED_TRANSPORT = 22,
#    ifdef ANJAY_WITH_SEND
    SERV_RES_MUTE_SEND = 23,
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
    _SERV_RES_COUNT
} server_rid_t;

typedef struct {
    /* mandatory resources */
    anjay_ssid_t ssid;
    standalone_binding_mode_t binding;
    int32_t lifetime;
    int32_t default_min_period;
    int32_t default_max_period;
#ifndef ANJAY_WITHOUT_DEREGISTER
    int32_t disable_timeout;
#endif // ANJAY_WITHOUT_DEREGISTER
    bool notification_storing;

    anjay_iid_t iid;

#ifdef ANJAY_WITH_LWM2M11
    int64_t last_bootstrapped_timestamp;
    uint8_t last_alert;
    bool bootstrap_on_registration_failure;
    uint32_t server_communication_retry_count;
    uint32_t server_communication_retry_timer;
    uint32_t server_communication_sequence_retry_count;
    uint32_t server_communication_sequence_delay_timer;
#    ifdef ANJAY_WITH_SMS
    bool trigger;
#    endif // ANJAY_WITH_SMS
    char preferred_transport;
#    ifdef ANJAY_WITH_SEND
    bool mute_send;
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11

    bool present_resources[_SERV_RES_COUNT];
} server_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    anjay_t *anjay;
    AVS_LIST(server_instance_t) instances;
    AVS_LIST(server_instance_t) saved_instances;
    bool modified_since_persist;
    bool saved_modified_since_persist;
    bool in_transaction;
} server_repr_t;

static inline void _standalone_serv_mark_modified(server_repr_t *repr) {
    repr->modified_since_persist = true;
}

static inline void _standalone_serv_clear_modified(server_repr_t *repr) {
    repr->modified_since_persist = false;
}

#define server_log(level, ...) avs_log(server, level, __VA_ARGS__)
#define _(Arg) AVS_DISPOSABLE_LOG(Arg)

#endif /* ANJAY_STANDALONE_SERVER_MOD_SERVER_H */
