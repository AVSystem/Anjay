#include <inttypes.h>
#include <string.h>

#include "standalone_server_transaction.h"
#include "standalone_server_utils.h"

#ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
#    include <avsystem/commons/avs_persistence.h>
#endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
#include <avsystem/commons/avs_utils.h>

#define persistence_log(level, ...) \
    avs_log(server_persistence, level, __VA_ARGS__)

#ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE

typedef enum {
    PERSISTENCE_VERSION_0,

    /** Binding resource as string instead of enum */
    PERSISTENCE_VERSION_1,

    /**
     * New resources:
     * - 11: TLS-DTLS Alert Code
     * - 12: Last Bootstrapped
     * - 16: Bootstrap on Registration Failure
     * - 17: Communication Retry Count
     * - 18: Communication Retry Timer
     * - 19: Communication Sequence Delay Timer
     * - 20: Communication Sequence Retry Count
     * - 22: Preferred Transport
     * - 23: Mute Send
     */
    PERSISTENCE_VERSION_2,

    /**
     * New resource: Trigger
     */
    PERSISTENCE_VERSION_3
} server_persistence_version_t;

typedef char magic_t[4];
static const magic_t MAGIC_V0 = { 'S', 'R', 'V', PERSISTENCE_VERSION_0 };
static const magic_t MAGIC_V1 = { 'S', 'R', 'V', PERSISTENCE_VERSION_1 };
static const magic_t MAGIC_V2 = { 'S', 'R', 'V', PERSISTENCE_VERSION_2 };
static const magic_t MAGIC_V3 = { 'S', 'R', 'V', PERSISTENCE_VERSION_3 };

static avs_error_t handle_v0_v1_sized_fields(avs_persistence_context_t *ctx,
                                             server_instance_t *element) {
    assert(avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE);
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->iid)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources[SERV_RES_SSID])))
            || avs_is_err(
                       (err = avs_persistence_bool(
                                ctx,
                                &element->present_resources[SERV_RES_BINDING])))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources[SERV_RES_LIFETIME])))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources
                                        [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE])))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->ssid)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx, (uint32_t *) &element->lifetime)))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx, (uint32_t *) &element->default_min_period)))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx, (uint32_t *) &element->default_max_period)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx,
#    ifndef ANJAY_WITHOUT_DEREGISTER
                                   (uint32_t *) &element->disable_timeout
#    else  // ANJAY_WITHOUT_DEREGISTER
                                   (uint32_t *) &(int32_t) { -1 }
#    endif // ANJAY_WITHOUT_DEREGISTER
                                   )))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->notification_storing))));
    if (avs_is_ok(err)) {
        element->present_resources[SERV_RES_DEFAULT_MIN_PERIOD] =
                (element->default_min_period >= 0);
        element->present_resources[SERV_RES_DEFAULT_MAX_PERIOD] =
                (element->default_max_period >= 0);
#    ifndef ANJAY_WITHOUT_DEREGISTER
        element->present_resources[SERV_RES_DISABLE_TIMEOUT] =
                (element->disable_timeout >= 0);
#    endif // ANJAY_WITHOUT_DEREGISTER
        element->present_resources
                [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE] = true;
    }
    return err;
}

static avs_error_t
handle_v2_lwm2m11_sized_fields(avs_persistence_context_t *ctx,
                               server_instance_t *element) {
#    ifndef ANJAY_WITH_LWM2M11
    enum {
        SERV_RES_TLS_DTLS_ALERT_CODE,
        SERV_RES_LAST_BOOTSTRAPPED,
        SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT,
        SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER,
        SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT,
        SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER,
        SERV_RES_PREFERRED_TRANSPORT,
        _FAKE_RESOURCES_NUM
    };

    (void) element;
    struct {
        int64_t last_bootstrapped_timestamp;
        uint8_t last_alert;
        bool bootstrap_on_registration_failure;
        uint32_t server_communication_retry_count;
        uint32_t server_communication_retry_timer;
        uint32_t server_communication_sequence_retry_count;
        uint32_t server_communication_sequence_delay_timer;
        char preferred_transport;
        bool mute_send;

        bool present_resources[_FAKE_RESOURCES_NUM];
    } dummy_element = {
        .bootstrap_on_registration_failure = true
    };
#        define element (&dummy_element)
#    endif // ANJAY_WITH_LWM2M11

    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_bool(
                                ctx,
                                &element->present_resources
                                         [SERV_RES_TLS_DTLS_ALERT_CODE])))
            || avs_is_err((err = avs_persistence_u8(ctx, &element->last_alert)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources
                                            [SERV_RES_LAST_BOOTSTRAPPED])))
            || avs_is_err((err = avs_persistence_i64(
                                   ctx, &element->last_bootstrapped_timestamp)))
            || avs_is_err(
                       (err = avs_persistence_bool(
                                ctx,
                                &element->bootstrap_on_registration_failure)))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources
                                        [SERV_RES_SERVER_COMMUNICATION_RETRY_COUNT])))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx,
                                   &element->server_communication_retry_count)))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources
                                        [SERV_RES_SERVER_COMMUNICATION_RETRY_TIMER])))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx,

                                   &element->server_communication_retry_timer)))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources
                                        [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER])))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx,
                               &element->server_communication_sequence_delay_timer)))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources
                                        [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT])))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx,
                               &element->server_communication_sequence_retry_count)))
            || avs_is_err((
                       err = avs_persistence_u8(
                               ctx, (uint8_t *) &element->preferred_transport)))
            || avs_is_err((err = avs_persistence_bool(ctx,
#    ifdef ANJAY_WITH_SEND
                                                      &element->mute_send
#    else  // ANJAY_WITH_SEND
                                                      &(bool) { false }
#    endif // ANJAY_WITH_SEND
                                                      ))));
    if (avs_is_ok(err)) {
        element->present_resources[SERV_RES_PREFERRED_TRANSPORT] =
                !!element->preferred_transport;
    }
    return err;
#    undef element
}

static avs_error_t handle_v2_sized_fields(avs_persistence_context_t *ctx,
                                          server_instance_t *element) {
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->iid)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources[SERV_RES_SSID])))
            || avs_is_err(
                       (err = avs_persistence_bool(
                                ctx,
                                &element->present_resources[SERV_RES_BINDING])))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources[SERV_RES_LIFETIME])))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources
                                            [SERV_RES_DEFAULT_MIN_PERIOD])))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
                                   &element->present_resources
                                            [SERV_RES_DEFAULT_MAX_PERIOD])))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
#    ifndef ANJAY_WITHOUT_DEREGISTER
                                   &element->present_resources
                                            [SERV_RES_DISABLE_TIMEOUT]
#    else  // ANJAY_WITHOUT_DEREGISTER
                                   &(bool) { false }
#    endif // ANJAY_WITHOUT_DEREGISTER
                                   )))
            || avs_is_err((
                       err = avs_persistence_bool(
                               ctx,
                               &element->present_resources
                                        [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE])))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->ssid)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx, (uint32_t *) &element->lifetime)))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx, (uint32_t *) &element->default_min_period)))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx, (uint32_t *) &element->default_max_period)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx,
#    ifndef ANJAY_WITHOUT_DEREGISTER
                                   (uint32_t *) &element->disable_timeout
#    else  // ANJAY_WITHOUT_DEREGISTER
                                   (uint32_t *) &(int32_t) { -1 }
#    endif // ANJAY_WITHOUT_DEREGISTER
                                   )))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->notification_storing)))
            || avs_is_err(
                       (err = handle_v2_lwm2m11_sized_fields(ctx, element))));
    return err;
}

static avs_error_t handle_v3_sized_fields(avs_persistence_context_t *ctx,
                                          server_instance_t *element) {
    avs_error_t err;
    (void) (avs_is_err((err = handle_v2_sized_fields(ctx, element)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx,
#    if defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_SMS)
                                   &element->present_resources[SERV_RES_TRIGGER]
#    else  // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_SMS)
                                   &(bool) { false }
#    endif // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_SMS)
                                   )))
            || avs_is_err((err = avs_persistence_bool(ctx,
#    if defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_SMS)
                                                      &element->trigger
#    else  // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_SMS)
                                                      &(bool) { false }
#    endif // defined(ANJAY_WITH_LWM2M11) && defined(ANJAY_WITH_SMS)
                                                      ))));
    return err;
}

static avs_error_t handle_v1_v2_v3_binding_mode(avs_persistence_context_t *ctx,
                                                server_instance_t *element) {
    avs_error_t err = avs_persistence_bytes(ctx, element->binding.data,
                                            sizeof(element->binding.data));
    if (avs_is_err(err)) {
        return err;
    }
    if (!memchr(element->binding.data, '\0', sizeof(element->binding.data))
            || !anjay_binding_mode_valid(element->binding.data)) {
        return avs_errno(AVS_EBADMSG);
    }
    return AVS_OK;
}

static avs_error_t restore_v0_binding_mode(avs_persistence_context_t *ctx,
                                           server_instance_t *element) {
    assert(avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE);
    uint32_t binding;
    avs_error_t err = avs_persistence_u32(ctx, &binding);
    if (avs_is_err(err)) {
        return err;
    }

    enum {
        V0_BINDING_NONE,
        V0_BINDING_U,
        V0_BINDING_UQ,
        V0_BINDING_S,
        V0_BINDING_SQ,
        V0_BINDING_US,
        V0_BINDING_UQS
    };

    const char *binding_str = "";
    switch (binding) {
    case V0_BINDING_NONE:
        binding_str = "";
        break;
    case V0_BINDING_U:
        binding_str = "U";
        break;
    case V0_BINDING_UQ:
        binding_str = "UQ";
        break;
    case V0_BINDING_S:
        binding_str = "S";
        break;
    case V0_BINDING_SQ:
        binding_str = "SQ";
        break;
    case V0_BINDING_US:
        binding_str = "US";
        break;
    case V0_BINDING_UQS:
        binding_str = "UQS";
        break;
    default:
        persistence_log(WARNING, _("Invalid binding mode: ") "%" PRIu32,
                        binding);
        err = avs_errno(AVS_EBADMSG);
        break;
    }
    if (avs_is_ok(err)
            && avs_simple_snprintf(element->binding.data,
                                   sizeof(element->binding.data), "%s",
                                   binding_str)
                           < 0) {
        persistence_log(WARNING, _("Could not restore binding: ") "%s",
                        binding_str);
        err = avs_errno(AVS_EBADMSG);
    }
    return err;
}

static avs_error_t server_instance_persistence_handler(
        avs_persistence_context_t *ctx, void *element_, void *version_) {
    server_instance_t *element = (server_instance_t *) element_;
    server_persistence_version_t *version =
            (server_persistence_version_t *) version_;
    AVS_ASSERT(avs_persistence_direction(ctx) != AVS_PERSISTENCE_STORE
                       || *version == PERSISTENCE_VERSION_3,
               "persistence storing is impossible in legacy mode");

    // Ensure every field initialized regardless of persistence version
    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        _standalone_serv_reset_instance(element);
    }

    avs_error_t err;
    switch (*version) {
    case PERSISTENCE_VERSION_0:
        (void) (avs_is_err((err = handle_v0_v1_sized_fields(ctx, element)))
                || avs_is_err((err = restore_v0_binding_mode(ctx, element))));
        break;
    case PERSISTENCE_VERSION_1:
        (void) (avs_is_err((err = handle_v0_v1_sized_fields(ctx, element)))
                || avs_is_err(
                           (err = handle_v1_v2_v3_binding_mode(ctx, element))));
        break;
    case PERSISTENCE_VERSION_2:
        (void) (avs_is_err((err = handle_v2_sized_fields(ctx, element)))
                || avs_is_err(
                           (err = handle_v1_v2_v3_binding_mode(ctx, element))));
        break;
    case PERSISTENCE_VERSION_3:
        (void) (avs_is_err((err = handle_v3_sized_fields(ctx, element)))
                || avs_is_err(
                           (err = handle_v1_v2_v3_binding_mode(ctx, element))));
        break;
    default:
        AVS_UNREACHABLE("invalid enum value");
    }
    return err;
}

avs_error_t
standalone_server_object_persist(const anjay_dm_object_def_t *const *obj_ptr,
                                 avs_stream_t *out_stream) {
    avs_error_t err = avs_errno(AVS_EINVAL);
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    if (!repr) {
        err = avs_errno(AVS_EBADF);
    } else {
        avs_persistence_context_t persist_ctx =
                avs_persistence_store_context_create(out_stream);
        if (avs_is_ok((err = avs_persistence_bytes(&persist_ctx,
                                                   (void *) (intptr_t) MAGIC_V3,
                                                   sizeof(MAGIC_V3))))) {
            server_persistence_version_t persistence_version =
                    PERSISTENCE_VERSION_3;
            err = avs_persistence_list(
                    &persist_ctx,
                    (AVS_LIST(void) *) (repr->in_transaction
                                                ? &repr->saved_instances
                                                : &repr->instances),
                    sizeof(server_instance_t),
                    server_instance_persistence_handler, &persistence_version,
                    NULL);
            if (avs_is_ok(err)) {
                _standalone_serv_clear_modified(repr);
                persistence_log(INFO, _("Server Object state persisted"));
            }
        }
    }
    return err;
}

static int check_magic_header(magic_t magic_header,
                              server_persistence_version_t *out_version) {
    if (!memcmp(magic_header, MAGIC_V0, sizeof(magic_t))) {
        *out_version = PERSISTENCE_VERSION_0;
        return 0;
    }
    if (!memcmp(magic_header, MAGIC_V1, sizeof(magic_t))) {
        *out_version = PERSISTENCE_VERSION_1;
        return 0;
    }
    if (!memcmp(magic_header, MAGIC_V2, sizeof(magic_t))) {
        *out_version = PERSISTENCE_VERSION_2;
        return 0;
    }
    if (!memcmp(magic_header, MAGIC_V3, sizeof(magic_t))) {
        *out_version = PERSISTENCE_VERSION_3;
        return 0;
    }
    return -1;
}

avs_error_t
standalone_server_object_restore(const anjay_dm_object_def_t *const *obj_ptr,
                                 avs_stream_t *in_stream) {
    avs_error_t err = avs_errno(AVS_EINVAL);
    server_repr_t *repr = _standalone_serv_get(obj_ptr);
    if (!repr || repr->in_transaction) {
        err = avs_errno(AVS_EBADF);
    } else {
        server_repr_t backup = *repr;
        avs_persistence_context_t restore_ctx =
                avs_persistence_restore_context_create(in_stream);

        magic_t magic_header;
        if (avs_is_err((err = avs_persistence_bytes(&restore_ctx, magic_header,
                                                    sizeof(magic_header))))) {
            persistence_log(WARNING, _("Could not read Server Object header"));
        } else {
            server_persistence_version_t persistence_version;
            if (check_magic_header(magic_header, &persistence_version)) {
                persistence_log(WARNING, _("Header magic constant mismatch"));
                err = avs_errno(AVS_EBADMSG);
            } else {
                repr->instances = NULL;
                err = avs_persistence_list(&restore_ctx,
                                           (AVS_LIST(void) *) &repr->instances,
                                           sizeof(server_instance_t),
                                           server_instance_persistence_handler,
                                           &persistence_version, NULL);
                if (avs_is_ok(err) && _standalone_serv_object_validate(repr)) {
                    err = avs_errno(AVS_EBADMSG);
                }
                if (avs_is_err(err)) {
                    _standalone_serv_destroy_instances(&repr->instances);
                    repr->instances = backup.instances;
                } else {
                    _standalone_serv_destroy_instances(&backup.instances);
                    _standalone_serv_clear_modified(repr);
                    persistence_log(INFO, _("Server Object state restored"));
                }
            }
        }
    }
    return err;
}

#else // AVS_COMMONS_WITH_AVS_PERSISTENCE

avs_error_t standalone_server_object_persist(anjay_t *anjay,
                                             avs_stream_t *out_stream) {
    (void) anjay;
    (void) out_stream;
    persistence_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

avs_error_t standalone_server_object_restore(anjay_t *anjay,
                                             avs_stream_t *in_stream) {
    (void) anjay;
    (void) in_stream;
    persistence_log(ERROR, _("Persistence not compiled in"));
    return avs_errno(AVS_ENOTSUP);
}

#endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
