/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_FW_UPDATE

#    if !defined(ANJAY_WITH_DOWNLOADER) \
            && defined(ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE)
#        error "ANJAY_WITH_MODULE_FW_UPDATE requires at least one of PUSH or PULL modes to be possible: please either disable ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE or enable ANJAY_WITH_DOWNLOADER"
#    endif // !defined(ANJAY_WITH_DOWNLOADER) &&
           // defined(ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE)

#    include <string.h>

#    include <anjay/download.h>
#    include <anjay/fw_update.h>

#    ifdef ANJAY_WITH_SEND
#        include <anjay/lwm2m_send.h>
#    endif // ANJAY_WITH_SEND

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_io_utils.h>
#    include <anjay_modules/anjay_sched.h>
#    include <anjay_modules/anjay_utils_core.h>
#    include <anjay_modules/dm/anjay_modules.h>

#    include <avsystem/coap/code.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_url.h>
#    include <avsystem/commons/avs_utils.h>

VISIBILITY_SOURCE_BEGIN

#    define fw_log(level, ...) _anjay_log(fw_update, level, __VA_ARGS__)

#    define FW_OID 5

#    define FW_RES_PACKAGE 0
#    define FW_RES_PACKAGE_URI 1
#    define FW_RES_UPDATE 2
#    define FW_RES_STATE 3
#    define FW_RES_UPDATE_RESULT 5
#    define FW_RES_PKG_NAME 6
#    define FW_RES_PKG_VERSION 7
#    define FW_RES_UPDATE_PROTOCOL_SUPPORT 8
#    define FW_RES_UPDATE_DELIVERY_METHOD 9

typedef enum {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_DOWNLOADED,
    UPDATE_STATE_UPDATING
} fw_update_state_t;

typedef struct {
    const anjay_fw_update_handlers_t *handlers;
    void *arg;
    fw_update_state_t state;
} fw_user_state_t;

typedef struct fw_repr {
    anjay_dm_installed_object_t def_ptr;
    const anjay_unlocked_dm_object_def_t *def;

    fw_user_state_t user_state;

    fw_update_state_t state;
    anjay_fw_update_result_t result;
    const char *package_uri;
    bool retry_download_on_expired;
    anjay_download_handle_t download_handle;
    avs_sched_handle_t update_job;
    bool prefer_same_socket_downloads;
    avs_sched_handle_t resume_download_job;
    avs_time_monotonic_t resume_download_deadline;
#    ifdef ANJAY_WITH_SEND
    bool use_lwm2m_send;
#    endif // ANJAY_WITH_SEND
} fw_repr_t;

static inline fw_repr_t *get_fw(const anjay_dm_installed_object_t obj_ptr) {
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(&obj_ptr),
                            fw_repr_t, def);
}

#    ifdef ANJAY_WITH_SEND
#        define SEND_RES_PATH(Oid, Iid, Rid) \
            {                                \
                .oid = (Oid),                \
                .iid = (Iid),                \
                .rid = (Rid)                 \
            }

#        define SEND_FW_RES_PATH(Res) SEND_RES_PATH(FW_OID, 0, FW_RES_##Res)

static int perform_send(anjay_unlocked_t *anjay,
                        const anjay_dm_installed_object_t *obj,
                        anjay_iid_t iid,
                        void *batch) {
    (void) obj;
    anjay_ssid_t ssid;
    const anjay_uri_path_t ssid_path =
            MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, iid,
                               ANJAY_DM_RID_SERVER_SSID);

    if (_anjay_dm_read_resource_u16(anjay, &ssid_path, &ssid)) {
        return 0;
    }

    if (_anjay_send_deferrable_unlocked(anjay, (anjay_ssid_t) ssid,
                                        (anjay_send_batch_t *) batch, NULL,
                                        NULL)
            != ANJAY_SEND_OK) {
        fw_log(WARNING, _("failed to perform Send, SSID: ") "%d", ssid);
    }

    return 0;
}

static void send_batch_to_all_servers(anjay_unlocked_t *anjay,
                                      anjay_send_batch_t *batch) {
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);

    if (_anjay_dm_foreach_instance(anjay, obj, perform_send, batch)) {
        fw_log(ERROR, _("failed to perform Send to all servers"));
    }
}

static void perform_lwm2m_send(anjay_unlocked_t *anjay,
                               const anjay_send_resource_path_t *paths,
                               size_t paths_len) {
    assert(paths);

    anjay_send_batch_builder_t *batch_builder = anjay_send_batch_builder_new();
    if (!batch_builder) {
        fw_log(ERROR, _("out of memory"));
        return;
    }

    if (_anjay_send_batch_data_add_current_multiple_unlocked(
                batch_builder, anjay, paths, paths_len, true)) {
        fw_log(ERROR, _("failed to add data to batch"));
        anjay_send_batch_builder_cleanup(&batch_builder);
        return;
    }

    anjay_send_batch_t *batch =
            anjay_send_batch_builder_compile(&batch_builder);
    if (!batch) {
        anjay_send_batch_builder_cleanup(&batch_builder);
        fw_log(ERROR, _("out of memory"));
        return;
    }

    send_batch_to_all_servers(anjay, batch);

    anjay_send_batch_release(&batch);
}

static void send_state_and_update_result(anjay_unlocked_t *anjay,
                                         const fw_repr_t *fw) {
    if (!fw->use_lwm2m_send) {
        return;
    }

    const anjay_send_resource_path_t paths[] = {
        SEND_FW_RES_PATH(STATE), SEND_FW_RES_PATH(UPDATE_RESULT)
    };
    perform_lwm2m_send(anjay, paths, AVS_ARRAY_SIZE(paths));
}
#    endif // ANJAY_WITH_SEND

static void set_update_result(anjay_unlocked_t *anjay,
                              fw_repr_t *fw,
                              anjay_fw_update_result_t new_result) {
    if (fw->result != new_result) {
        fw_log(DEBUG, _("Firmware Update Result change: ") "%d" _(" -> ") "%d",
               (int) fw->result, (int) new_result);
        fw->result = new_result;
        _anjay_notify_changed_unlocked(anjay, FW_OID, 0, FW_RES_UPDATE_RESULT);
    }
}

static void
set_state(anjay_unlocked_t *anjay, fw_repr_t *fw, fw_update_state_t new_state) {
    if (fw->state != new_state) {
        fw_log(DEBUG, _("Firmware Update State change: ") "%d" _(" -> ") "%d",
               (int) fw->state, (int) new_state);
        fw->state = new_state;
        _anjay_notify_changed_unlocked(anjay, FW_OID, 0, FW_RES_STATE);
    }
}

static void
update_state_and_update_result(anjay_unlocked_t *anjay,
                               fw_repr_t *fw,
                               fw_update_state_t new_state,
                               anjay_fw_update_result_t new_result) {
    set_update_result(anjay, fw, new_result);
    set_state(anjay, fw, new_state);
#    ifdef ANJAY_WITH_SEND
    send_state_and_update_result(anjay, fw);
#    endif // ANJAY_WITH_SEND
}

static void set_user_state(fw_user_state_t *user, fw_update_state_t new_state) {
    fw_log(DEBUG, _("user->state change: ") "%d" _(" -> ") "%d",
           (int) user->state, (int) new_state);
    user->state = new_state;
}

static int
user_state_ensure_stream_open(anjay_unlocked_t *anjay,
                              fw_user_state_t *user,
                              const char *package_uri,
                              const struct anjay_etag *package_etag) {
    if (user->state == UPDATE_STATE_DOWNLOADING) {
        return 0;
    }
    assert(user->state == UPDATE_STATE_IDLE);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->stream_open(user->arg, package_uri, package_etag);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    if (!result) {
        set_user_state(user, UPDATE_STATE_DOWNLOADING);
    }
    return result;
}

static int user_state_stream_write(anjay_unlocked_t *anjay,
                                   fw_user_state_t *user,
                                   const void *data,
                                   size_t length) {
    assert(user->state == UPDATE_STATE_DOWNLOADING);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->stream_write(user->arg, data, length);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static const char *user_state_get_name(anjay_unlocked_t *anjay,
                                       fw_user_state_t *user) {
    if (!user->handlers->get_name || user->state != UPDATE_STATE_DOWNLOADED) {
        return NULL;
    }
    const char *result = NULL;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->get_name(user->arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static const char *user_state_get_version(anjay_unlocked_t *anjay,
                                          fw_user_state_t *user) {
    if (!user->handlers->get_version
            || user->state != UPDATE_STATE_DOWNLOADED) {
        return NULL;
    }
    const char *result = NULL;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->get_version(user->arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int user_state_perform_upgrade(anjay_unlocked_t *anjay, fw_repr_t *fw) {
    fw_user_state_t *user = &fw->user_state;
    if (user->state != UPDATE_STATE_DOWNLOADED) {
        fw_log(WARNING,
               _("Update State ") "%d" _(" != ") "%d" _(
                       " (DOWNLOADED); aborting"),
               (int) user->state, (int) UPDATE_STATE_DOWNLOADED);
        return -1;
    }

    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->perform_upgrade(user->arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    // If the state was changed during perform_upgrade handler, this means
    // @ref anjay_fw_update_set_result was called and has overwritten the
    // State and Result. In that case, change State to Updating if update was
    // not deferred.
    if (!result && user->state == UPDATE_STATE_DOWNLOADED) {
        set_user_state(user, UPDATE_STATE_UPDATING);
    }
    return result;
}

static int finish_user_stream(anjay_unlocked_t *anjay, fw_repr_t *fw) {
    assert(fw->user_state.state == UPDATE_STATE_DOWNLOADING);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = fw->user_state.handlers->stream_finish(fw->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    if (result) {
        set_user_state(&fw->user_state, UPDATE_STATE_IDLE);
    } else {
        set_user_state(&fw->user_state, UPDATE_STATE_DOWNLOADED);
    }
    return result;
}

static void reset_user_state(anjay_unlocked_t *anjay, fw_repr_t *fw) {
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    fw->user_state.handlers->reset(fw->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    set_user_state(&fw->user_state, UPDATE_STATE_IDLE);
}

#    ifdef ANJAY_WITH_DOWNLOADER
static int get_security_config(anjay_unlocked_t *anjay,
                               fw_repr_t *fw,
                               anjay_security_config_t *out_security_config) {
    assert(fw->user_state.state == UPDATE_STATE_IDLE
           || fw->user_state.state == UPDATE_STATE_DOWNLOADING);
    if (fw->user_state.handlers->get_security_config) {
        int result = -1;
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
        result = fw->user_state.handlers->get_security_config(
                fw->user_state.arg, out_security_config, fw->package_uri);
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
        return result;
    } else {
        if (!_anjay_security_config_from_dm_unlocked(anjay, out_security_config,
                                                     fw->package_uri)) {
            return 0;
        }
#        ifdef ANJAY_WITH_LWM2M11
        *out_security_config = _anjay_security_config_pkix_unlocked(anjay);
        if (out_security_config->security_info.data.cert
                    .server_cert_validation) {
            return 0;
        }
#        endif // ANJAY_WITH_LWM2M11
        return -1;
    }
}

static int get_coap_tx_params(anjay_unlocked_t *anjay,
                              fw_repr_t *fw,
                              avs_coap_udp_tx_params_t *out_tx_params) {
    if (fw->user_state.handlers->get_coap_tx_params) {
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
        *out_tx_params =
                fw->user_state.handlers->get_coap_tx_params(fw->user_state.arg,
                                                            fw->package_uri);
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
        return 0;
    }
    return -1;
}
#    endif // ANJAY_WITH_DOWNLOADER

static void handle_err_result(anjay_unlocked_t *anjay,
                              fw_repr_t *fw,
                              fw_update_state_t new_state,
                              int result,
                              anjay_fw_update_result_t default_result) {
    anjay_fw_update_result_t new_result;
    switch (result) {
    case -ANJAY_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE:
    case -ANJAY_FW_UPDATE_RESULT_OUT_OF_MEMORY:
    case -ANJAY_FW_UPDATE_RESULT_INTEGRITY_FAILURE:
    case -ANJAY_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE:
        new_result = (anjay_fw_update_result_t) -result;
        break;
    default:
        new_result = default_result;
    }
    update_state_and_update_result(anjay, fw, new_state, new_result);
}

static void reset(anjay_unlocked_t *anjay, fw_repr_t *fw) {
    reset_user_state(anjay, fw);
    update_state_and_update_result(anjay, fw, UPDATE_STATE_IDLE,
                                   ANJAY_FW_UPDATE_RESULT_INITIAL);
    fw_log(INFO, _("Firmware Object state reset"));
}

static int fw_list_instances(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_ptr,
                             anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    _anjay_dm_emit_unlocked(ctx, 0);
    return 0;
}

static int fw_list_resources(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_ptr,
                             anjay_iid_t iid,
                             anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) iid;
    fw_repr_t *fw = get_fw(obj_ptr);

    _anjay_dm_emit_res_unlocked(ctx, FW_RES_PACKAGE, ANJAY_DM_RES_W,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_PACKAGE_URI, ANJAY_DM_RES_RW,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_UPDATE, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_STATE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_UPDATE_RESULT, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_PKG_NAME, ANJAY_DM_RES_R,
                                user_state_get_name(anjay, &fw->user_state)
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_PKG_VERSION, ANJAY_DM_RES_R,
                                user_state_get_version(anjay, &fw->user_state)
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_UPDATE_PROTOCOL_SUPPORT,
                                ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, FW_RES_UPDATE_DELIVERY_METHOD,
                                ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    return 0;
}

static const int32_t SUPPORTED_PROTOCOLS[] = {
#    ifdef WITH_AVS_COAP_UDP
    0, /* CoAP */
#        ifndef AVS_COMMONS_WITHOUT_TLS
    1,         /* CoAPS */
#        endif // AVS_COMMONS_WITHOUT_TLS
#    endif     // WITH_AVS_COAP_UDP
#    ifdef ANJAY_WITH_HTTP_DOWNLOAD
    2, /* HTTP 1.1 */
#        ifndef AVS_COMMONS_WITHOUT_TLS
    3,         /* HTTPS 1.1 */
#        endif // AVS_COMMONS_WITHOUT_TLS
#    endif     // ANJAY_WITH_HTTP_DOWNLOAD
#    ifdef WITH_AVS_COAP_TCP
    4, /* CoAP over TCP */
#        ifndef AVS_COMMONS_WITHOUT_TLS
    5,         /* CoAP over TLS */
#        endif // AVS_COMMONS_WITHOUT_TLS
#    endif     // WITH_AVS_COAP_TCP
};

static int fw_read(anjay_unlocked_t *anjay,
                   const anjay_dm_installed_object_t obj_ptr,
                   anjay_iid_t iid,
                   anjay_rid_t rid,
                   anjay_riid_t riid,
                   anjay_unlocked_output_ctx_t *ctx) {
    (void) iid;
    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_PACKAGE_URI:
        return _anjay_ret_string_unlocked(ctx, fw->package_uri ? fw->package_uri
                                                               : "");
    case FW_RES_STATE:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) fw->state);
    case FW_RES_UPDATE_RESULT:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) fw->result);
    case FW_RES_PKG_NAME: {
        const char *name = user_state_get_name(anjay, &fw->user_state);
        if (name) {
            return _anjay_ret_string_unlocked(ctx, name);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case FW_RES_PKG_VERSION: {
        const char *version = user_state_get_version(anjay, &fw->user_state);
        if (version) {
            return _anjay_ret_string_unlocked(ctx, version);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case FW_RES_UPDATE_PROTOCOL_SUPPORT:
        assert(riid < AVS_ARRAY_SIZE(SUPPORTED_PROTOCOLS));
        return _anjay_ret_i64_unlocked(ctx, SUPPORTED_PROTOCOLS[riid]);
    case FW_RES_UPDATE_DELIVERY_METHOD:
#    ifdef ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
        // 0 -> pull only
        return _anjay_ret_i64_unlocked(ctx, 0);
#    else // ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
#        ifdef ANJAY_WITH_DOWNLOADER
        // 2 -> pull && push
        return _anjay_ret_i64_unlocked(ctx, 2);
#        else  // ANJAY_WITH_DOWNLOADER
        // 1 -> push only
        return _anjay_ret_i64_unlocked(ctx, 1);
#        endif // ANJAY_WITH_DOWNLOADER
#    endif     // ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
    default:
        AVS_UNREACHABLE("Read called on unknown or non-readable Firmware "
                        "Update resource");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

#    if defined(ANJAY_WITH_COAP_DOWNLOAD) || defined(ANJAY_WITH_HTTP_DOWNLOAD)
static anjay_transport_security_t
transport_security_from_protocol(const char *protocol) {
#        ifdef ANJAY_WITH_COAP_DOWNLOAD
    const anjay_transport_info_t *info =
            _anjay_transport_info_by_uri_scheme(protocol);
    if (info) {
        return info->security;
    }
#        endif // ANJAY_WITH_COAP_DOWNLOAD

#        ifdef ANJAY_WITH_HTTP_DOWNLOAD
    if (avs_strcasecmp(protocol, "http") == 0) {
        return ANJAY_TRANSPORT_NOSEC;
    }
    if (avs_strcasecmp(protocol, "https") == 0) {
        return ANJAY_TRANSPORT_ENCRYPTED;
    }
#        endif // ANJAY_WITH_HTTP_DOWNLOAD

    return ANJAY_TRANSPORT_SECURITY_UNDEFINED;
}

static anjay_transport_security_t transport_security_from_uri(const char *uri) {
    avs_url_t *parsed_url = avs_url_parse_lenient(uri);
    if (!parsed_url) {
        return ANJAY_TRANSPORT_SECURITY_UNDEFINED;
    }
    anjay_transport_security_t result = ANJAY_TRANSPORT_SECURITY_UNDEFINED;

    const char *protocol = avs_url_protocol(parsed_url);
    if (protocol) {
        result = transport_security_from_protocol(protocol);
    }
    avs_url_free(parsed_url);
    return result;
}
#    else  // ANJAY_WITH_COAP_DOWNLOAD || ANJAY_WITH_HTTP_DOWNLOAD
static anjay_transport_security_t transport_security_from_uri(const char *uri) {
    (void) uri;
    return ANJAY_TRANSPORT_SECURITY_UNDEFINED;
}
#    endif // ANJAY_WITH_COAP_DOWNLOAD || ANJAY_WITH_HTTP_DOWNLOAD

#    ifdef ANJAY_WITH_DOWNLOADER
static avs_error_t download_write_block(anjay_t *anjay_locked,
                                        const uint8_t *data,
                                        size_t data_size,
                                        const anjay_etag_t *etag,
                                        void *fw_) {
    (void) etag;
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    fw_repr_t *fw = (fw_repr_t *) fw_;
    result = user_state_ensure_stream_open(anjay, &fw->user_state,
                                           fw->package_uri, etag);
    if (!result && data_size > 0) {
        result = user_state_stream_write(anjay, &fw->user_state, data,
                                         data_size);
    }
    if (result) {
        fw_log(ERROR, _("could not write firmware"));
        handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                          ANJAY_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result ? avs_errno(AVS_UNKNOWN_ERROR) : AVS_OK;
}

static int schedule_background_anjay_download(anjay_unlocked_t *anjay,
                                              fw_repr_t *fw,
                                              size_t start_offset,
                                              const anjay_etag_t *etag);

static void download_finished(anjay_t *anjay_locked,
                              anjay_download_status_t status,
                              void *fw_) {
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    fw_repr_t *fw = (fw_repr_t *) fw_;
    fw->download_handle = NULL;
    if (fw->state != UPDATE_STATE_DOWNLOADING) {
        // something already failed in download_write_block()
        reset_user_state(anjay, fw);
    } else if (status.result != ANJAY_DOWNLOAD_FINISHED) {
        anjay_fw_update_result_t update_result =
                ANJAY_FW_UPDATE_RESULT_CONNECTION_LOST;
        if (status.result == ANJAY_DOWNLOAD_ERR_FAILED) {
            if (status.details.error.category == AVS_ERRNO_CATEGORY) {
                if (status.details.error.code == AVS_ENOMEM) {
                    update_result = ANJAY_FW_UPDATE_RESULT_OUT_OF_MEMORY;
                } else if (status.details.error.code == AVS_EADDRNOTAVAIL) {
                    update_result = ANJAY_FW_UPDATE_RESULT_INVALID_URI;
                }
            }
        } else if (status.result == ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE
                   && (status.details.status_code == AVS_COAP_CODE_NOT_FOUND
                       || status.details.status_code == 404)) {
            // NOTE: We should only check for the status code appropriate for
            // the download protocol, but 132 (AVS_COAP_CODE_NOT_FOUND) is
            // unlikely as a HTTP status code, and 12.20 (404 according to CoAP
            // convention) is not representable on a single byte, so this is
            // good enough.
            update_result = ANJAY_FW_UPDATE_RESULT_INVALID_URI;
        }
        reset_user_state(anjay, fw);
        if (fw->retry_download_on_expired
                && status.result == ANJAY_DOWNLOAD_ERR_EXPIRED) {
            fw_log(INFO,
                   _("Could not resume firmware download (result = ") "%d" _(
                           "), retrying from the beginning"),
                   (int) status.result);
            if (schedule_background_anjay_download(anjay, fw, 0, NULL)) {
                fw_log(WARNING, _("Could not retry firmware download"));
                set_state(anjay, fw, UPDATE_STATE_IDLE);
#        ifdef ANJAY_WITH_SEND
                send_state_and_update_result(anjay, fw);
#        endif // ANJAY_WITH_SEND
            }
        } else {
            fw_log(WARNING, _("download aborted: result = ") "%d",
                   (int) status.result);
            update_state_and_update_result(anjay, fw, UPDATE_STATE_IDLE,
                                           update_result);
        }
    } else {
        int result;
        if ((result = user_state_ensure_stream_open(anjay, &fw->user_state,
                                                    fw->package_uri, NULL))
                || (result = finish_user_stream(anjay, fw))) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                              ANJAY_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
        } else {
            update_state_and_update_result(anjay, fw, UPDATE_STATE_DOWNLOADED,
                                           ANJAY_FW_UPDATE_RESULT_INITIAL);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int schedule_download(anjay_unlocked_t *anjay,
                             fw_repr_t *fw,
                             size_t start_offset,
                             const anjay_etag_t *etag) {
    anjay_download_config_t cfg = {
        .url = fw->package_uri,
        .start_offset = start_offset,
        .etag = etag,
        .on_next_block = download_write_block,
        .on_download_finished = download_finished,
        .user_data = fw,
        .prefer_same_socket_downloads = fw->prefer_same_socket_downloads
    };

    if (transport_security_from_uri(fw->package_uri)
            == ANJAY_TRANSPORT_ENCRYPTED) {
        int result = get_security_config(anjay, fw, &cfg.security_config);
        if (result) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                              ANJAY_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
            return -1;
        }
    }

    avs_coap_udp_tx_params_t tx_params;
    if (!get_coap_tx_params(anjay, fw, &tx_params)) {
        cfg.coap_tx_params = &tx_params;
    }

    assert(!fw->download_handle);
    avs_error_t err =
            _anjay_download_unlocked(anjay, &cfg, &fw->download_handle);
    if (!fw->download_handle) {
        anjay_fw_update_result_t update_result =
                ANJAY_FW_UPDATE_RESULT_CONNECTION_LOST;
        if (avs_is_err(err) && err.category == AVS_ERRNO_CATEGORY) {
            switch (err.code) {
            case AVS_EADDRNOTAVAIL:
            case AVS_EINVAL:
                update_result = ANJAY_FW_UPDATE_RESULT_INVALID_URI;
                break;
            case AVS_ENOMEM:
                update_result = ANJAY_FW_UPDATE_RESULT_OUT_OF_MEMORY;
                break;
            case AVS_EPROTONOSUPPORT:
                update_result = ANJAY_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL;
                break;
            }
        }
        reset_user_state(anjay, fw);
        set_update_result(anjay, fw, update_result);
#        ifdef ANJAY_WITH_SEND
        send_state_and_update_result(anjay, fw);
#        endif // ANJAY_WITH_SEND
        return -1;
    }

    fw->retry_download_on_expired = (etag != NULL);
    update_state_and_update_result(anjay, fw, UPDATE_STATE_DOWNLOADING,
                                   ANJAY_FW_UPDATE_RESULT_INITIAL);
    fw_log(INFO, _("download started: ") "%s", fw->package_uri);
    return 0;
}

typedef struct {
    fw_repr_t *fw;
    size_t start_offset;
    // actually a FAM
    anjay_etag_t etag;
} schedule_download_args_t;

static size_t schedule_download_args_size(size_t etag_length) {
    return offsetof(schedule_download_args_t, etag)
           + offsetof(anjay_etag_t, value) + etag_length;
}

static void resume_download_job(avs_sched_t *sched, const void *args_) {
    schedule_download_args_t *args =
            (schedule_download_args_t *) (intptr_t) args_;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    if (!_anjay_ongoing_registration_exists_unlocked(anjay)) {
        fw_log(DEBUG,
               _("all registrations settled down, scheduling download "
                 "resumption"));
        if (schedule_download(anjay, args->fw, args->start_offset,
                              &args->etag)) {
            fw_log(ERROR, _("could not resume firmware download"));
        }
    } else if (avs_time_monotonic_before(args->fw->resume_download_deadline,
                                         avs_time_monotonic_now())) {
        fw_log(DEBUG,
               _("registrations not settled within 5 minutes, canceling "
                 "download resumption"));
        reset_user_state(anjay, args->fw);
        update_state_and_update_result(anjay, args->fw, UPDATE_STATE_IDLE,
                                       ANJAY_FW_UPDATE_RESULT_CONNECTION_LOST);
    } else {
        fw_log(DEBUG,
               _("ongoing registration exists, delaying download resumption"));
        if (AVS_SCHED_DELAYED(sched, &args->fw->resume_download_job,
                              avs_time_duration_from_scalar(1, AVS_TIME_S),
                              resume_download_job, args,
                              schedule_download_args_size(args->etag.size))) {
            fw_log(WARNING, _("could not schedule another resumption attempt"));
            reset_user_state(anjay, args->fw);
            update_state_and_update_result(
                    anjay, args->fw, UPDATE_STATE_IDLE,
                    ANJAY_FW_UPDATE_RESULT_OUT_OF_MEMORY);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int schedule_background_anjay_download(anjay_unlocked_t *anjay,
                                              fw_repr_t *fw,
                                              size_t start_offset,
                                              const anjay_etag_t *etag) {
    if (fw->prefer_same_socket_downloads && etag) {
        avs_sched_t *sched = _anjay_get_scheduler_unlocked(anjay);
        assert(sched);
        schedule_download_args_t *args =
                (schedule_download_args_t *) avs_malloc(
                        schedule_download_args_size(etag->size));
        if (args) {
            args->fw = fw;
            args->start_offset = start_offset;
            args->etag.size = etag->size;
            memcpy(args->etag.value, etag->value, etag->size);

            fw->resume_download_deadline = avs_time_monotonic_add(
                    avs_time_monotonic_now(),
                    avs_time_duration_from_scalar(5, AVS_TIME_MIN));
            if (!AVS_SCHED_NOW(sched, &fw->resume_download_job,
                               resume_download_job, args,
                               schedule_download_args_size(etag->size))) {
                fw_log(DEBUG,
                       _("same socket download initiated, waiting for "
                         "server registrations to settle down"));
                update_state_and_update_result(anjay, fw,
                                               UPDATE_STATE_DOWNLOADING,
                                               ANJAY_FW_UPDATE_RESULT_INITIAL);
                avs_free(args);
                return 0;
            }
        }
        avs_free(args);
        fw_log(WARNING, _("could not resume download on the same socket"));
    }
    return schedule_download(anjay, fw, start_offset, etag);
}
#    endif // ANJAY_WITH_DOWNLOADER

#    ifndef ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
static int write_firmware_to_stream(anjay_unlocked_t *anjay,
                                    fw_repr_t *fw,
                                    anjay_unlocked_input_ctx_t *ctx,
                                    bool *out_is_reset_request) {
    int result = 0;
    size_t written = 0;
    bool finished = false;
    int first_byte = EOF;

    *out_is_reset_request = false;
    while (!finished) {
        size_t bytes_read;
        char buffer[1024];
        if ((result = _anjay_get_bytes_unlocked(ctx, &bytes_read, &finished,
                                                buffer, sizeof(buffer)))) {
            fw_log(ERROR, _("anjay_get_bytes() failed"));

            update_state_and_update_result(
                    anjay, fw, UPDATE_STATE_IDLE,
                    ANJAY_FW_UPDATE_RESULT_CONNECTION_LOST);
            return result;
        }

        if (bytes_read > 0) {
            if (first_byte == EOF) {
                first_byte = (unsigned char) buffer[0];
            }
            result = user_state_stream_write(anjay, &fw->user_state, buffer,
                                             bytes_read);
        }
        if (result) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                              ANJAY_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
            return ANJAY_ERR_INTERNAL;
        }
        written += bytes_read;
    }

    *out_is_reset_request = (written == 1 && first_byte == '\0');

    fw_log(INFO, _("write finished, ") "%lu" _(" B written"),
           (unsigned long) written);
    return 0;
}

static int expect_single_nullbyte(anjay_unlocked_input_ctx_t *ctx) {
    char bytes[2];
    size_t bytes_read;
    bool finished = false;
    if (_anjay_get_bytes_unlocked(ctx, &bytes_read, &finished, bytes,
                                  sizeof(bytes))) {
        fw_log(ERROR, _("anjay_get_bytes() failed"));
        return ANJAY_ERR_INTERNAL;
    } else if (bytes_read != 1 || !finished || bytes[0] != '\0') {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int write_firmware(anjay_unlocked_t *anjay,
                          fw_repr_t *fw,
                          anjay_unlocked_input_ctx_t *ctx,
                          bool *out_is_reset_request) {
    assert(fw->state != UPDATE_STATE_DOWNLOADING);
    if (user_state_ensure_stream_open(anjay, &fw->user_state, NULL, NULL)) {
        return -1;
    }

    int result = write_firmware_to_stream(anjay, fw, ctx, out_is_reset_request);
    if (result) {
        reset_user_state(anjay, fw);
    } else if (!*out_is_reset_request) {
        // stream_finish_result deliberately not propagated up:
        // write itself succeeded
        int stream_finish_result = finish_user_stream(anjay, fw);
        if (stream_finish_result) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE,
                              stream_finish_result,
                              ANJAY_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
        } else {
            update_state_and_update_result(anjay, fw, UPDATE_STATE_DOWNLOADED,
                                           ANJAY_FW_UPDATE_RESULT_INITIAL);
        }
    }
    return result;
}
#    endif // ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE

static void cancel_existing_download_if_in_progress(anjay_unlocked_t *anjay,
                                                    fw_repr_t *fw) {
    if (fw->state == UPDATE_STATE_DOWNLOADING) {
        if (fw->resume_download_job) {
            assert(!fw->download_handle);
            avs_sched_del(&fw->resume_download_job);
            return;
        }
        AVS_ASSERT(fw->download_handle,
                   "download_handle is NULL - another Write handler called "
                   "during a PUSH-mode download?!");
        _anjay_download_abort_unlocked(anjay, fw->download_handle);
        assert(!fw->download_handle);
    }
}

static int fw_write(anjay_unlocked_t *anjay,
                    const anjay_dm_installed_object_t obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_unlocked_input_ctx_t *ctx) {
    (void) iid;
    (void) riid;

    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_PACKAGE: {
#    ifdef ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
        return ANJAY_ERR_BAD_REQUEST;
#    else  // ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
        assert(riid == ANJAY_ID_INVALID);
        int result = 0;
        if (fw->state == UPDATE_STATE_UPDATING) {
            fw_log(WARNING, _("cannot set Package resource while updating"));
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        } else if (fw->state == UPDATE_STATE_IDLE) {
            bool is_reset_request = false;
            result = write_firmware(anjay, fw, ctx, &is_reset_request);
            if (!result && is_reset_request) {
                reset(anjay, fw);
            }
        } else {
            result = expect_single_nullbyte(ctx);
            if (!result) {
                cancel_existing_download_if_in_progress(anjay, fw);
                reset(anjay, fw);
            }
        }
        return result;
#    endif // ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE
    }
    case FW_RES_PACKAGE_URI: {
        assert(riid == ANJAY_ID_INVALID);
        char *new_uri = NULL;
        int result = _anjay_io_fetch_string(ctx, &new_uri);
        size_t len = (new_uri ? strlen(new_uri) : 0);

        if (!result && len == 0) {
            avs_free(new_uri);

            if (fw->state == UPDATE_STATE_UPDATING) {
                fw_log(WARNING,
                       _("cannot set Package URI resource while updating"));
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            }

            cancel_existing_download_if_in_progress(anjay, fw);

            avs_free((void *) (intptr_t) fw->package_uri);
            fw->package_uri = NULL;
            reset(anjay, fw);
            return 0;
        }

        if (!result && fw->state != UPDATE_STATE_IDLE) {
            result = ANJAY_ERR_BAD_REQUEST;
        }

        if (!result
                && transport_security_from_uri(new_uri)
                               == ANJAY_TRANSPORT_SECURITY_UNDEFINED) {
            fw_log(WARNING,
                   _("unsupported download protocol required for uri ") "%s",
                   new_uri);
            set_update_result(anjay, fw,
                              ANJAY_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
#    ifdef ANJAY_WITH_SEND
            send_state_and_update_result(anjay, fw);
#    endif // ANJAY_WITH_SEND
            result = ANJAY_ERR_BAD_REQUEST;
        }

#    ifdef ANJAY_WITH_DOWNLOADER
        if (!result) {
            avs_free((void *) (intptr_t) fw->package_uri);
            fw->package_uri = new_uri;
            new_uri = NULL;

            int dl_res = schedule_background_anjay_download(anjay, fw, 0, NULL);
            if (dl_res) {
                fw_log(WARNING,
                       _("schedule_download_in_background failed: ") "%d",
                       dl_res);
            }
            // write itself succeeded; do not propagate error
        }
#    endif // ANJAY_WITH_DOWNLOADER

        avs_free(new_uri);

        return result;
    }
    default:
        // Bootstrap Server may try to write to other resources,
        // so no AVS_UNREACHABLE() here
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int fw_resource_instances(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    switch (rid) {
    case FW_RES_UPDATE_PROTOCOL_SUPPORT:
        for (anjay_riid_t i = 0; i < AVS_ARRAY_SIZE(SUPPORTED_PROTOCOLS); ++i) {
            _anjay_dm_emit_unlocked(ctx, i);
        }
        return 0;
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

static void perform_upgrade(avs_sched_t *sched, const void *fw_ptr) {
    fw_repr_t *fw = *(fw_repr_t *const *) fw_ptr;

    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    int result = user_state_perform_upgrade(anjay, fw);
    if (result) {
        fw_log(ERROR, _("user_state_perform_upgrade() failed: ") "%d", result);
        handle_err_result(anjay, fw, UPDATE_STATE_DOWNLOADED, result,
                          ANJAY_FW_UPDATE_RESULT_FAILED);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static void schedule_upgrade(avs_sched_t *sched, const void *fw_ptr) {
    fw_repr_t *fw = *(fw_repr_t *const *) fw_ptr;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    // Let's defer actually performing the upgrade to yet another scheduler run
    // - the notification for the UPDATING state is probably being scheduled in
    // the current one.
    if (fw->state == UPDATE_STATE_UPDATING
            && fw->user_state.state != UPDATE_STATE_UPDATING
            && AVS_SCHED_NOW(sched, &fw->update_job, perform_upgrade, &fw,
                             sizeof(fw))) {
        update_state_and_update_result(anjay, fw, UPDATE_STATE_DOWNLOADED,
                                       ANJAY_FW_UPDATE_RESULT_OUT_OF_MEMORY);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int fw_execute(anjay_unlocked_t *anjay,
                      const anjay_dm_installed_object_t obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_unlocked_execute_ctx_t *ctx) {
    (void) iid;
    (void) ctx;

    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_UPDATE:
        if (fw->state != UPDATE_STATE_DOWNLOADED) {
            fw_log(WARNING,
                   _("Firmware Update requested, but firmware not yet "
                     "downloaded (state = ") "%d" _(")"),
                   fw->state);
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
        update_state_and_update_result(anjay, fw, UPDATE_STATE_UPDATING,
                                       ANJAY_FW_UPDATE_RESULT_INITIAL);
        // NOTE: This has to be called after update_state_and_update_result(),
        // to make sure that schedule_upgrade() is called after notify_clb()
        // and consequently, perform_upgrade() is called after trigger_observe()
        // (if it's not delayed due to pmin).
        if (AVS_SCHED_NOW(_anjay_get_scheduler_unlocked(anjay), &fw->update_job,
                          schedule_upgrade, &fw, sizeof(fw))) {
            fw_log(WARNING, _("Could not schedule the upgrade job"));
            update_state_and_update_result(
                    anjay, fw, UPDATE_STATE_DOWNLOADED,
                    ANJAY_FW_UPDATE_RESULT_OUT_OF_MEMORY);
            return ANJAY_ERR_INTERNAL;
        }
        return 0;
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int fw_transaction_noop(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    (void) obj_ptr;
    return 0;
}

static const anjay_unlocked_dm_object_def_t FIRMWARE_UPDATE = {
    .oid = FW_OID,
    .handlers = {
        .list_instances = fw_list_instances,
        .list_resources = fw_list_resources,
        .resource_read = fw_read,
        .resource_write = fw_write,
        .list_resource_instances = fw_resource_instances,
        .resource_execute = fw_execute,
        .transaction_begin = fw_transaction_noop,
        .transaction_validate = fw_transaction_noop,
        .transaction_commit = fw_transaction_noop,
        .transaction_rollback = fw_transaction_noop
    }
};

#    ifdef ANJAY_WITH_SEND
static void send_result_after_fw_update(anjay_unlocked_t *anjay,
                                        fw_repr_t *fw) {
    if (!fw->use_lwm2m_send) {
        return;
    }

    const anjay_send_resource_path_t paths[] = {
        SEND_FW_RES_PATH(STATE), SEND_FW_RES_PATH(UPDATE_RESULT),
        SEND_RES_PATH(ANJAY_DM_OID_DEVICE, 0,
                      ANJAY_DM_RID_DEVICE_FIRMWARE_VERSION),
        SEND_RES_PATH(ANJAY_DM_OID_DEVICE, 0,
                      ANJAY_DM_RID_DEVICE_SOFTWARE_VERSION)
    };
    perform_lwm2m_send(anjay, paths, AVS_ARRAY_SIZE(paths));
}
#    endif // ANJAY_WITH_SEND

static void fw_delete(void *fw_) {
    fw_repr_t *fw = (fw_repr_t *) fw_;
    avs_sched_del(&fw->update_job);
    avs_sched_del(&fw->resume_download_job);
    avs_free((void *) (intptr_t) fw->package_uri);
    // NOTE: fw itself will be freed when cleaning the objects list
}

static int
initialize_fw_repr(anjay_unlocked_t *anjay,
                   fw_repr_t *repr,
                   const anjay_fw_update_initial_state_t *initial_state) {
    if (!initial_state) {
        return 0;
    }
    repr->prefer_same_socket_downloads =
            initial_state->prefer_same_socket_downloads;
#    ifdef ANJAY_WITH_SEND
    repr->use_lwm2m_send = initial_state->use_lwm2m_send;
#    endif // ANJAY_WITH_SEND

    switch (initial_state->result) {
    case ANJAY_FW_UPDATE_INITIAL_DOWNLOADED:
        if (initial_state->persisted_uri
                && !(repr->package_uri =
                             avs_strdup(initial_state->persisted_uri))) {
            fw_log(WARNING, _("Could not copy the persisted Package URI"));
        }
        repr->user_state.state = UPDATE_STATE_DOWNLOADED;
        repr->state = UPDATE_STATE_DOWNLOADED;
        return 0;
    case ANJAY_FW_UPDATE_INITIAL_DOWNLOADING: {
#    ifdef ANJAY_WITH_DOWNLOADER
        repr->user_state.state = UPDATE_STATE_DOWNLOADING;
        size_t resume_offset = initial_state->resume_offset;
        if (resume_offset > 0 && !initial_state->resume_etag) {
            fw_log(WARNING,
                   _("ETag not set, need to start from the beginning"));
            reset_user_state(anjay, repr);
            resume_offset = 0;
        }
        if (!initial_state->persisted_uri
                || !(repr->package_uri =
                             avs_strdup(initial_state->persisted_uri))) {
            fw_log(WARNING, _("Could not copy the persisted Package URI, not "
                              "resuming firmware download"));
            reset_user_state(anjay, repr);
        } else if (schedule_background_anjay_download(
                           anjay, repr, resume_offset,
                           initial_state->resume_etag)) {
            fw_log(WARNING, _("Could not resume firmware download"));
            reset_user_state(anjay, repr);
            if (repr->result == ANJAY_FW_UPDATE_RESULT_CONNECTION_LOST
                    && initial_state->resume_etag
                    && schedule_background_anjay_download(anjay, repr, 0,
                                                          NULL)) {
                fw_log(WARNING, _("Could not retry firmware download"));
            }
        }
#    else  // ANJAY_WITH_DOWNLOADER
        (void) anjay;
        fw_log(WARNING,
               _("Unable to resume download: PULL download not supported"));
#    endif // ANJAY_WITH_DOWNLOADER
        return 0;
    }
    case ANJAY_FW_UPDATE_INITIAL_UPDATING:
        repr->user_state.state = UPDATE_STATE_UPDATING;
        repr->state = UPDATE_STATE_UPDATING;
        repr->result = ANJAY_FW_UPDATE_RESULT_INITIAL;
        return 0;
    case ANJAY_FW_UPDATE_INITIAL_NEUTRAL:
    case ANJAY_FW_UPDATE_INITIAL_SUCCESS:
    case ANJAY_FW_UPDATE_INITIAL_INTEGRITY_FAILURE:
    case ANJAY_FW_UPDATE_INITIAL_FAILED:
        repr->result = (anjay_fw_update_result_t) initial_state->result;
        return 0;
    default:
        fw_log(ERROR, _("Invalid initial_state->result"));
        return -1;
    }
}

int anjay_fw_update_install(
        anjay_t *anjay_locked,
        const anjay_fw_update_handlers_t *handlers,
        void *user_arg,
        const anjay_fw_update_initial_state_t *initial_state) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    AVS_LIST(fw_repr_t) repr = AVS_LIST_NEW_ELEMENT(fw_repr_t);
    if (!repr) {
        fw_log(ERROR, _("out of memory"));
    } else {
        repr->def = &FIRMWARE_UPDATE;
        _anjay_dm_installed_object_init_unlocked(&repr->def_ptr, &repr->def);
        repr->user_state.handlers = handlers;
        repr->user_state.arg = user_arg;

        if (!initialize_fw_repr(anjay, repr, initial_state)
                && !_anjay_dm_module_install(anjay, fw_delete, repr)) {
            AVS_STATIC_ASSERT(offsetof(fw_repr_t, def_ptr) == 0,
                              def_ptr_is_first_field);
            AVS_LIST(anjay_dm_installed_object_t) entry = &repr->def_ptr;
            if (_anjay_register_object_unlocked(anjay, &entry)) {
                result = _anjay_dm_module_uninstall(anjay, fw_delete);
                assert(!result);
                result = -1;
            } else {
#    ifdef ANJAY_WITH_SEND
                if (initial_state->result != ANJAY_FW_UPDATE_INITIAL_NEUTRAL) {
                    send_result_after_fw_update(anjay, repr);
                }
#    endif // ANJAY_WITH_SEND
                result = 0;
            }
        }
        if (result) {
            AVS_LIST_CLEAR(&repr);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

static bool is_error_result(anjay_fw_update_result_t result) {
    return result != ANJAY_FW_UPDATE_RESULT_INITIAL
           && result != ANJAY_FW_UPDATE_RESULT_SUCCESS;
}

static bool is_result_change_allowed(fw_update_state_t current_state,
                                     anjay_fw_update_result_t new_result) {
    switch (current_state) {
    case UPDATE_STATE_IDLE:
        // changing result while nothing is going on is pointless
        return false;
    case UPDATE_STATE_DOWNLOADING:
    case UPDATE_STATE_DOWNLOADED:
        // FOTA is not supposed to be performed unless requested by the server;
        // failing while downloading should still be an option
        return is_error_result(new_result);
    case UPDATE_STATE_UPDATING:
        // unexpected reset is likely to confuse the server
        return new_result != ANJAY_FW_UPDATE_RESULT_INITIAL;
    }

    AVS_UNREACHABLE("invalid enum value");
    return false;
}

int anjay_fw_update_set_result(anjay_t *anjay_locked,
                               anjay_fw_update_result_t result) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_FIRMWARE_UPDATE);
    if (!obj) {
        fw_log(WARNING, _("Firmware Update object not installed"));
    } else {
        fw_repr_t *fw = get_fw(*obj);
        assert(fw);

        if (!is_result_change_allowed(fw->state, result)) {
            fw_log(WARNING,
                   _("Firmware Update Result change to ") "%d" _(
                           " not allowed in State ") "%d",
                   (int) result, (int) fw->state);
        } else {
            reset_user_state(anjay, fw);
            update_state_and_update_result(anjay, fw, UPDATE_STATE_IDLE,
                                           result);
            retval = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

#endif // ANJAY_WITH_MODULE_FW_UPDATE
