/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE

#    include <inttypes.h>

#    include <avsystem/commons/avs_log.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_url.h>

#    include <avsystem/coap/code.h>

#    include <anjay/dm.h>

#    ifdef ANJAY_WITH_DOWNLOADER
#        include <anjay/download.h>
#    endif // ANJAY_WITH_DOWNLOADER

#    ifdef ANJAY_WITH_SEND
#        include <anjay/lwm2m_send.h>
#    endif // ANJAY_WITH_SEND

#    include <anjay/advanced_fw_update.h>
#    include <anjay_modules/anjay_io_utils.h>
#    include <anjay_modules/anjay_sched.h>
#    include <anjay_modules/anjay_utils_core.h>
#    include <anjay_modules/dm/anjay_modules.h>

#    define fw_log(level, ...) avs_log(advanced_fw_update, level, __VA_ARGS__)
#    define _(Arg) AVS_DISPOSABLE_LOG(Arg)

#    define ADV_FW_RES_PACKAGE 0
#    define ADV_FW_RES_PACKAGE_URI 1
#    define ADV_FW_RES_UPDATE 2
#    define ADV_FW_RES_STATE 3
#    define ADV_FW_RES_UPDATE_RESULT 5
#    define ADV_FW_RES_PKG_NAME 6
#    define ADV_FW_RES_PKG_VERSION 7
#    define ADV_FW_RES_UPDATE_PROTOCOL_SUPPORT 8
#    define ADV_FW_RES_UPDATE_DELIVERY_METHOD 9
#    define ADV_FW_RES_CANCEL 10
#    define ADV_FW_RES_SEVERITY 11
#    define ADV_FW_RES_LAST_STATE_CHANGE_TIME 12
#    define ADV_FW_RES_MAX_DEFER_PERIOD 13
#    define ADV_FW_RES_COMPONENT_NAME 14
#    define ADV_FW_RES_CURRENT_VERSION 15
#    define ADV_FW_RES_LINKED_INSTANCES 16
#    define ADV_FW_RES_CONFLICTING_INSTANCES 17

VISIBILITY_SOURCE_BEGIN

typedef struct {
    const anjay_advanced_fw_update_handlers_t *handlers;
    void *arg;
    anjay_advanced_fw_update_state_t state;
} advanced_fw_user_state_t;

typedef struct {
    anjay_iid_t iid;

    const char *component_name;

    advanced_fw_user_state_t user_state;

    anjay_advanced_fw_update_state_t state;
    anjay_advanced_fw_update_result_t result;
    const char *package_uri;
    avs_sched_handle_t update_job;
#    ifdef ANJAY_WITH_DOWNLOADER
    bool retry_download_on_expired;
    avs_sched_handle_t resume_download_job;
    avs_time_monotonic_t resume_download_deadline;
#    endif // ANJAY_WITH_DOWNLOADER
    anjay_advanced_fw_update_severity_t severity;
    avs_time_real_t last_state_change_time;
    int max_defer_period;
    avs_time_real_t update_deadline;

    anjay_iid_t *linked_instances;
    size_t linked_instances_count;

    anjay_iid_t *conflicting_instances;
    size_t conflicting_instances_count;
} advanced_fw_instance_t;

typedef struct {
    anjay_iid_t iid;
    anjay_download_handle_t download_handle;
} current_download_t;

typedef struct {
    anjay_dm_installed_object_t def_ptr;
    const anjay_unlocked_dm_object_def_t *def;

#    ifdef ANJAY_WITH_DOWNLOADER
    bool prefer_same_socket_downloads;
#    endif // ANJAY_WITH_DOWNLOADER
#    ifdef ANJAY_WITH_SEND
    bool use_lwm2m_send;
#    endif // ANJAY_WITH_SEND

    anjay_iid_t *supplemental_iid_cache;
    size_t supplemental_iid_cache_count;

#    ifdef ANJAY_WITH_DOWNLOADER
    current_download_t current_download;
    bool downloads_suspended;
    AVS_LIST(anjay_download_config_t) download_queue;
#    endif // ANJAY_WITH_DOWNLOADER

    AVS_LIST(advanced_fw_instance_t) instances;
} advanced_fw_repr_t;

static inline advanced_fw_repr_t *
get_fw(const anjay_dm_installed_object_t obj_ptr) {
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(&obj_ptr),
                            advanced_fw_repr_t, def);
}

#    ifdef ANJAY_WITH_SEND
#        define SEND_RES_PATH(Oid, Iid, Rid) \
            {                                \
                .oid = (Oid),                \
                .iid = (Iid),                \
                .rid = (Rid)                 \
            }

#        define SEND_FW_RES_PATH(Iid, Res) \
            SEND_RES_PATH(ANJAY_ADVANCED_FW_UPDATE_OID, Iid, ADV_FW_RES_##Res)

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

    if (_anjay_send_deferrable_unlocked(
                anjay, ssid, (anjay_send_batch_t *) batch, NULL, NULL)
            != ANJAY_SEND_OK) {
        fw_log(WARNING, _("failed to perform Send, SSID: ") "%" PRIu16, ssid);
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
        _anjay_log_oom();
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
        _anjay_log_oom();
        return;
    }
    send_batch_to_all_servers(anjay, batch);
    anjay_send_batch_release(&batch);
}

static void send_state_and_update_result(anjay_unlocked_t *anjay,
                                         const advanced_fw_repr_t *fw,
                                         anjay_iid_t iid,
                                         bool with_version_info) {
    if (!fw->use_lwm2m_send) {
        return;
    }

    anjay_send_resource_path_t paths[5] = { SEND_FW_RES_PATH(iid, STATE),
                                            SEND_FW_RES_PATH(iid,
                                                             UPDATE_RESULT) };
    size_t path_count = 2;
    if (with_version_info) {
        paths[path_count++] =
                (anjay_send_resource_path_t) SEND_FW_RES_PATH(iid,
                                                              CURRENT_VERSION);
        paths[path_count++] =
                (anjay_send_resource_path_t) SEND_RES_PATH(3, 0, 3);
        paths[path_count++] =
                (anjay_send_resource_path_t) SEND_RES_PATH(3, 0, 19);
    }
    perform_lwm2m_send(anjay, paths, path_count);
}
#    endif // ANJAY_WITH_SEND

static int set_update_result(anjay_unlocked_t *anjay,
                             advanced_fw_instance_t *inst,
                             anjay_advanced_fw_update_result_t new_result) {
    if (inst->result != new_result) {
        fw_log(DEBUG,
               _("Advanced Firmware Update Instance ") "%" PRIu16 _(
                       " Result change: ") "%d" _(" -> ") "%d",
               inst->iid, (int) inst->result, (int) new_result);
        inst->result = new_result;
        _anjay_notify_changed_unlocked(anjay, ANJAY_ADVANCED_FW_UPDATE_OID,
                                       inst->iid, ADV_FW_RES_UPDATE_RESULT);
        return 0;
    }
    return -1;
}

static int set_state(anjay_unlocked_t *anjay,
                     advanced_fw_instance_t *inst,
                     anjay_advanced_fw_update_state_t new_state) {
    if (inst->state != new_state) {
        inst->last_state_change_time = avs_time_real_now();
        fw_log(DEBUG,
               _("Advanced Firmware Update Instance ") "%" PRIu16 _(
                       " State change: ") "%d" _(" -> ") "%d",
               inst->iid, (int) inst->state, (int) new_state);
        inst->state = new_state;
        _anjay_notify_changed_unlocked(anjay, ANJAY_ADVANCED_FW_UPDATE_OID,
                                       inst->iid, ADV_FW_RES_STATE);
        return 0;
    }
    return -1;
}

static void
update_state_and_update_result(anjay_unlocked_t *anjay,
                               advanced_fw_repr_t *fw,
                               advanced_fw_instance_t *inst,
                               anjay_advanced_fw_update_state_t new_state,
                               anjay_advanced_fw_update_result_t new_result) {
    int set_res = set_update_result(anjay, inst, new_result);
    int set_st = set_state(anjay, inst, new_state);
#    ifdef ANJAY_WITH_SEND
    bool send_version_info =
            inst->state == ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING
            && new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
            && new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS;
    if (!set_res || !set_st) {
        send_state_and_update_result(anjay, fw, inst->iid, send_version_info);
    }
#    else
    (void) fw;
    (void) set_res;
    (void) set_st;
#    endif // ANJAY_WITH_SEND
}

static void set_user_state(advanced_fw_user_state_t *user,
                           anjay_advanced_fw_update_state_t new_state) {
    fw_log(DEBUG, _("user->state change: ") "%d" _(" -> ") "%d",
           (int) user->state, (int) new_state);
    user->state = new_state;
}

static int user_state_ensure_stream_open(anjay_unlocked_t *anjay,
                                         advanced_fw_instance_t *inst) {
    advanced_fw_user_state_t *const user = &inst->user_state;

    if (user->state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING) {
        return 0;
    }
    assert(user->state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE);

    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->stream_open(inst->iid, user->arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    if (!result) {
        set_user_state(user, ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING);
    }
    return result;
}

static int user_state_stream_write(anjay_unlocked_t *anjay,
                                   advanced_fw_instance_t *inst,
                                   const void *data,
                                   size_t length) {
    assert(inst->user_state.state
           == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = inst->user_state.handlers->stream_write(
            inst->iid, inst->user_state.arg, data, length);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static const char *user_state_get_pkg_name(anjay_unlocked_t *anjay,
                                           advanced_fw_instance_t *inst) {
    if (!inst->user_state.handlers->get_pkg_name
            || inst->user_state.state
                           != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED) {
        return NULL;
    }
    const char *result = NULL;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = inst->user_state.handlers->get_pkg_name(inst->iid,
                                                     inst->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static const char *user_state_get_pkg_version(anjay_unlocked_t *anjay,
                                              advanced_fw_instance_t *inst) {
    if (!inst->user_state.handlers->get_pkg_version
            || inst->user_state.state
                           != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED) {
        return NULL;
    }
    const char *result = NULL;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = inst->user_state.handlers->get_pkg_version(inst->iid,
                                                        inst->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static const char *
user_state_get_current_version(anjay_unlocked_t *anjay,
                               advanced_fw_instance_t *inst) {
    if (!inst->user_state.handlers->get_current_version) {
        return NULL;
    }
    const char *result = NULL;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = inst->user_state.handlers->get_current_version(
            inst->iid, inst->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return result;
}

static int user_state_perform_upgrade(anjay_unlocked_t *anjay,
                                      advanced_fw_instance_t *inst,
                                      const anjay_iid_t *supplemental_iids,
                                      size_t supplemental_iids_count) {
    advanced_fw_user_state_t *user = &inst->user_state;

    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = user->handlers->perform_upgrade(
            inst->iid, user->arg, supplemental_iids, supplemental_iids_count);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    // If the state was changed during perform_upgrade handler, this means
    // @ref anjay_advanced_fw_update_set_state_and_result was called and
    // has overwritten the State and Result. In that case, change State to
    // Updating if update was not deferred or to Downloaded if failed due to
    // dependency error.
    if (!result && user->state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED
            && inst->result != ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED
            && inst->result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR) {
        set_user_state(user, ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING);
    }
    return result;
}

static int finish_user_stream(anjay_unlocked_t *anjay,
                              advanced_fw_instance_t *inst) {
    assert(inst->user_state.state
           == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING);
    int result = -1;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    result = inst->user_state.handlers->stream_finish(inst->iid,
                                                      inst->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    if (result) {
        set_user_state(&inst->user_state, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE);
    } else {
        set_user_state(&inst->user_state,
                       ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED);
    }
    return result;
}

static void reset_user_state(anjay_unlocked_t *anjay,
                             advanced_fw_instance_t *inst) {
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    inst->user_state.handlers->reset(inst->iid, inst->user_state.arg);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    set_user_state(&inst->user_state, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE);
}

#    ifdef ANJAY_WITH_DOWNLOADER

static int get_security_config(anjay_unlocked_t *anjay,
                               advanced_fw_instance_t *inst,
                               anjay_security_config_t *out_security_config) {
    assert(inst->user_state.state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
           || inst->user_state.state
                      == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING);
    if (inst->user_state.handlers->get_security_config) {
        int result = -1;
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
        result = inst->user_state.handlers->get_security_config(
                inst->iid, inst->user_state.arg, out_security_config,
                inst->package_uri);
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
        return result;
    } else {
        if (!_anjay_security_config_from_dm_unlocked(anjay, out_security_config,
                                                     inst->package_uri)) {
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
                              advanced_fw_instance_t *inst,
                              avs_coap_udp_tx_params_t *out_tx_params) {
    if (inst->user_state.handlers->get_coap_tx_params) {
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
        *out_tx_params = inst->user_state.handlers->get_coap_tx_params(
                inst->iid, inst->user_state.arg, inst->package_uri);
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
        return 0;
    }
    return -1;
}

static avs_time_duration_t
get_tcp_request_timeout(anjay_unlocked_t *anjay, advanced_fw_instance_t *inst) {
    avs_time_duration_t result = AVS_TIME_DURATION_INVALID;
    if (inst->user_state.handlers->get_tcp_request_timeout) {
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
        result = inst->user_state.handlers->get_tcp_request_timeout(
                inst->iid, inst->user_state.arg, inst->package_uri);
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    }
    return result;
}
#    endif // ANJAY_WITH_DOWNLOADER

static void
handle_err_result(anjay_unlocked_t *anjay,
                  advanced_fw_repr_t *fw,
                  advanced_fw_instance_t *inst,
                  anjay_advanced_fw_update_state_t new_state,
                  int result,
                  anjay_advanced_fw_update_result_t default_result) {
    anjay_advanced_fw_update_result_t new_result;

    switch (result) {
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE:
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY:
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_INTEGRITY_FAILURE:
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE:
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED:
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_CONFLICTING_STATE:
    case -ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR:
        new_result = (anjay_advanced_fw_update_result_t) (-result);
        break;
    default:
        new_result = default_result;
    }
    update_state_and_update_result(anjay, fw, inst, new_state, new_result);
}

static void reset_state(anjay_unlocked_t *anjay,
                        advanced_fw_repr_t *fw,
                        advanced_fw_instance_t *inst) {
    reset_user_state(anjay, inst);
    update_state_and_update_result(anjay, fw, inst,
                                   ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                                   ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
    fw_log(INFO,
           _("Advanced Firmware Object Instance ") "%" PRIu16 _(" state reset"),
           inst->iid);
}

static int fw_list_instances(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_ptr,
                             anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    advanced_fw_repr_t *fw = get_fw(obj_ptr);
    AVS_LIST(advanced_fw_instance_t) inst;
    AVS_LIST_FOREACH(inst, fw->instances) {
        _anjay_dm_emit_unlocked(ctx, inst->iid);
    }
    return 0;
}

static advanced_fw_instance_t *get_fw_instance(advanced_fw_repr_t *fw,
                                               anjay_iid_t iid) {
    assert(fw);

    AVS_LIST(advanced_fw_instance_t) inst;
    AVS_LIST_FOREACH(inst, fw->instances) {
        if (inst->iid > iid) {
            break;
        } else if (inst->iid == iid) {
            return inst;
        }
    }
    return NULL;
}

static int fw_list_resources(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_ptr,
                             anjay_iid_t iid,
                             anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    advanced_fw_repr_t *fw = get_fw(obj_ptr);
    advanced_fw_instance_t *inst = get_fw_instance(fw, iid);
    assert(inst);

    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_PACKAGE, ANJAY_DM_RES_W,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_PACKAGE_URI, ANJAY_DM_RES_RW,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_UPDATE, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_STATE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_UPDATE_RESULT, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_PKG_NAME, ANJAY_DM_RES_R,
                                user_state_get_pkg_name(anjay, inst)
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_PKG_VERSION, ANJAY_DM_RES_R,
                                user_state_get_pkg_version(anjay, inst)
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_UPDATE_PROTOCOL_SUPPORT,
                                ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_UPDATE_DELIVERY_METHOD,
                                ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_CANCEL, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_SEVERITY, ANJAY_DM_RES_RW,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_LAST_STATE_CHANGE_TIME,
                                ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_MAX_DEFER_PERIOD,
                                ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_COMPONENT_NAME, ANJAY_DM_RES_R,
                                inst->component_name ? ANJAY_DM_RES_PRESENT
                                                     : ANJAY_DM_RES_ABSENT);
    _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_CURRENT_VERSION, ANJAY_DM_RES_R,
                                user_state_get_current_version(anjay, inst)
                                        ? ANJAY_DM_RES_PRESENT
                                        : ANJAY_DM_RES_ABSENT);
    if (AVS_LIST_NEXT(fw->instances)) {
        // at least 2 instances exist
        _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_LINKED_INSTANCES,
                                    ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
        _anjay_dm_emit_res_unlocked(ctx, ADV_FW_RES_CONFLICTING_INSTANCES,
                                    ANJAY_DM_RES_RM, ANJAY_DM_RES_PRESENT);
    }
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
    advanced_fw_repr_t *fw = get_fw(obj_ptr);
    advanced_fw_instance_t *inst = get_fw_instance(fw, iid);
    assert(inst);
    switch (rid) {
    case ADV_FW_RES_PACKAGE_URI:
        return _anjay_ret_string_unlocked(
                ctx, inst->package_uri ? inst->package_uri : "");
    case ADV_FW_RES_STATE:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) inst->state);
    case ADV_FW_RES_UPDATE_RESULT:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) inst->result);
    case ADV_FW_RES_PKG_NAME: {
        const char *name = user_state_get_pkg_name(anjay, inst);

        if (name) {
            return _anjay_ret_string_unlocked(ctx, name);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case ADV_FW_RES_PKG_VERSION: {
        const char *version = user_state_get_pkg_version(anjay, inst);

        if (version) {
            return _anjay_ret_string_unlocked(ctx, version);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case ADV_FW_RES_UPDATE_PROTOCOL_SUPPORT:
        assert(riid < AVS_ARRAY_SIZE(SUPPORTED_PROTOCOLS));
        return _anjay_ret_i64_unlocked(ctx, SUPPORTED_PROTOCOLS[riid]);
    case ADV_FW_RES_UPDATE_DELIVERY_METHOD:
#    ifdef ANJAY_WITH_DOWNLOADER
        return _anjay_ret_i64_unlocked(ctx, 2); // 2 -> pull && push
#    else                                       // ANJAY_WITH_DOWNLOADER
        return _anjay_ret_i64_unlocked(ctx, 1); // 1 -> push only
#    endif                                      // ANJAY_WITH_DOWNLOADER
    case ADV_FW_RES_SEVERITY:
        return _anjay_ret_i64_unlocked(ctx, (int32_t) inst->severity);
    case ADV_FW_RES_LAST_STATE_CHANGE_TIME: {
        int64_t last_state_change_timestamp = 0;

        avs_time_real_to_scalar(&last_state_change_timestamp, AVS_TIME_S,
                                inst->last_state_change_time);
        return _anjay_ret_i64_unlocked(ctx, last_state_change_timestamp);
    }
    case ADV_FW_RES_MAX_DEFER_PERIOD:
        return _anjay_ret_i64_unlocked(ctx, inst->max_defer_period);
    case ADV_FW_RES_COMPONENT_NAME:
        return _anjay_ret_string_unlocked(ctx, inst->component_name);
    case ADV_FW_RES_CURRENT_VERSION: {
        const char *version = user_state_get_current_version(anjay, inst);

        if (version) {
            return _anjay_ret_string_unlocked(ctx, version);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case ADV_FW_RES_LINKED_INSTANCES:
        return _anjay_ret_objlnk_unlocked(ctx, ANJAY_ADVANCED_FW_UPDATE_OID,
                                          riid);
    case ADV_FW_RES_CONFLICTING_INSTANCES:
        return _anjay_ret_objlnk_unlocked(ctx, ANJAY_ADVANCED_FW_UPDATE_OID,
                                          riid);
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

static void set_update_deadline(advanced_fw_instance_t *inst) {
    if (inst->max_defer_period <= 0) {
        inst->update_deadline = AVS_TIME_REAL_INVALID;
        return;
    }
    inst->update_deadline = avs_time_real_add(
            avs_time_real_now(),
            avs_time_duration_from_scalar(inst->max_defer_period, AVS_TIME_S));
}

#    ifdef ANJAY_WITH_DOWNLOADER

static avs_error_t download_write_block(anjay_t *anjay_locked,
                                        const uint8_t *data,
                                        size_t data_size,
                                        const anjay_etag_t *etag,
                                        void *inst_) {
    (void) etag;
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);
        advanced_fw_instance_t *inst = (advanced_fw_instance_t *) inst_;
        result = user_state_ensure_stream_open(anjay, inst);
        if (!result && data_size > 0) {
            result = user_state_stream_write(anjay, inst, data, data_size);
        }
        if (result) {
            fw_log(ERROR, _("could not write firmware"));

            handle_err_result(anjay, fw, inst,
                              ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE, result,
                              ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result ? avs_errno(AVS_UNKNOWN_ERROR) : AVS_OK;
}

static int schedule_background_anjay_download(anjay_unlocked_t *anjay,
                                              advanced_fw_repr_t *fw,
                                              advanced_fw_instance_t *inst);

static int schedule_download_now(anjay_unlocked_t *anjay,
                                 advanced_fw_repr_t *fw,
                                 advanced_fw_instance_t *inst,
                                 anjay_download_config_t *cfg) {
    if (transport_security_from_uri(cfg->url) == ANJAY_TRANSPORT_ENCRYPTED) {
        int result = get_security_config(anjay, inst, &cfg->security_config);

        if (result) {
            handle_err_result(
                    anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                    result,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
            return -1;
        }
    }
    avs_error_t err =
            _anjay_download_unlocked(anjay, cfg,
                                     &fw->current_download.download_handle);
    if (avs_is_err(err)) {
        anjay_advanced_fw_update_result_t update_result =
                ANJAY_ADVANCED_FW_UPDATE_RESULT_CONNECTION_LOST;
        if (err.category == AVS_ERRNO_CATEGORY) {
            switch (err.code) {
            case AVS_EADDRNOTAVAIL:
            case AVS_EINVAL:
                update_result = ANJAY_ADVANCED_FW_UPDATE_RESULT_INVALID_URI;
                break;
            case AVS_ENOMEM:
                update_result = ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY;
                break;
            case AVS_EPROTONOSUPPORT:
                update_result =
                        ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL;
                break;
            }
        }
        reset_user_state(anjay, inst);
        set_update_result(anjay, inst, update_result);
#        ifdef ANJAY_WITH_SEND
        send_state_and_update_result(anjay, fw, inst->iid, false);
#        endif // ANJAY_WITH_SEND
        return -1;
    }
    fw->current_download.iid = inst->iid;
    if (fw->downloads_suspended) {
        _anjay_download_suspend_unlocked(anjay,
                                         fw->current_download.download_handle);
    }
    inst->retry_download_on_expired = (false);
    update_state_and_update_result(anjay, fw, inst,
                                   ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING,
                                   ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
    fw_log(INFO, _("IID ") "%" PRIu16 _(": download started: ") "%s", inst->iid,
           inst->package_uri);
    return 0;
}

static void start_next_download_if_waiting(anjay_unlocked_t *anjay,
                                           advanced_fw_repr_t *fw) {
    if (fw->download_queue != NULL) {
        advanced_fw_instance_t *inst =
                (advanced_fw_instance_t *) fw->download_queue->user_data;
        if (schedule_download_now(anjay, fw, inst, fw->download_queue)) {
            fw_log(WARNING, _("Scheduling next waiting download failed"));
        }
        fw_log(TRACE, _("Scheduled download for instance %") PRIu16, inst->iid);
        avs_free((void *) (intptr_t) fw->download_queue->url);
        avs_free((void *) fw->download_queue->coap_tx_params);
        AVS_LIST_DELETE(&fw->download_queue);
    }
}

static void download_finished(anjay_t *anjay_locked,
                              anjay_download_status_t status,
                              void *inst_) {
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);
        advanced_fw_instance_t *inst = (advanced_fw_instance_t *) inst_;
        fw->current_download.download_handle = NULL;
        fw->current_download.iid = ANJAY_ID_INVALID;
        if (inst->state != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING) {
            // something already failed in download_write_block()
            reset_user_state(anjay, inst);
            start_next_download_if_waiting(anjay, fw);
        } else if (status.result != ANJAY_DOWNLOAD_FINISHED) {
            anjay_advanced_fw_update_result_t update_result =
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_CONNECTION_LOST;

            if (status.result == ANJAY_DOWNLOAD_ERR_FAILED) {
                if (status.details.error.category == AVS_ERRNO_CATEGORY) {
                    if (status.details.error.code == AVS_ENOMEM) {
                        update_result =
                                ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY;
                    } else if (status.details.error.code == AVS_EADDRNOTAVAIL) {
                        update_result =
                                ANJAY_ADVANCED_FW_UPDATE_RESULT_INVALID_URI;
                    }
                }
            } else if (status.result == ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE
                       && (status.details.status_code == AVS_COAP_CODE_NOT_FOUND
                           || status.details.status_code == 404)) {
                // NOTE: We should only check for the status code appropriate
                // for the download protocol, but 132 (AVS_COAP_CODE_NOT_FOUND)
                // is unlikely as a HTTP status code, and 12.20 (404 according
                // to CoAP convention) is not representable on a single byte, so
                // this is good enough.
                update_result = ANJAY_ADVANCED_FW_UPDATE_RESULT_INVALID_URI;
            }
            reset_user_state(anjay, inst);
            if (inst->retry_download_on_expired
                    && status.result == ANJAY_DOWNLOAD_ERR_EXPIRED) {
                fw_log(INFO,
                       _("Could not resume firmware download (result = ") "%"
                                                                          "d" _("), retrying from the beginning"),
                       (int) status.result);
                if (schedule_background_anjay_download(anjay, fw, inst)) {
                    fw_log(WARNING, _("Could not retry firmware download"));
                    set_state(anjay, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE);
#        ifdef ANJAY_WITH_SEND
                    send_state_and_update_result(anjay, fw, inst->iid, false);
#        endif // ANJAY_WITH_SEND
                }
            } else {
                fw_log(WARNING, _("download aborted: result = ") "%d",
                       (int) status.result);
                update_state_and_update_result(
                        anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                        update_result);
            }
        } else {
            int result = user_state_ensure_stream_open(anjay, inst);

            if (!result) {
                result = finish_user_stream(anjay, inst);
            }
            if (result) {
                handle_err_result(
                        anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                        result,
                        ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
            } else {
                update_state_and_update_result(
                        anjay, fw, inst,
                        ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
                        ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
            }
            start_next_download_if_waiting(anjay, fw);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static bool is_any_download_in_progress(advanced_fw_repr_t *fw) {
    return fw->current_download.download_handle || fw->download_queue;
}

static int enqueue_download(anjay_unlocked_t *anjay,
                            advanced_fw_repr_t *fw,
                            advanced_fw_instance_t *inst,
                            anjay_download_config_t *cfg) {
#        ifndef NDEBUG
    AVS_LIST(anjay_download_config_t) queued_cfg;
    AVS_LIST_FOREACH(queued_cfg, fw->download_queue) {
        advanced_fw_instance_t *queued_inst =
                (advanced_fw_instance_t *) queued_cfg->user_data;
        assert(queued_inst->iid != inst->iid);
    }
#        endif // NDEBUG
    AVS_LIST(anjay_download_config_t) new_download =
            AVS_LIST_APPEND_NEW(anjay_download_config_t, &fw->download_queue);
    if (!new_download) {
        goto cleanup;
    }
    memcpy(new_download, cfg, sizeof(anjay_download_config_t));
    new_download->url = avs_strdup(cfg->url);
    if (!new_download->url) {
        goto cleanup;
    }
    if (cfg->coap_tx_params) {
        new_download->coap_tx_params = (avs_coap_udp_tx_params_t *) avs_malloc(
                sizeof(avs_coap_udp_tx_params_t));
        if (!new_download->coap_tx_params) {
            goto cleanup;
        }
        memcpy(new_download->coap_tx_params, cfg->coap_tx_params,
               sizeof(avs_coap_udp_tx_params_t));
    }

    update_state_and_update_result(anjay, fw, inst,
                                   ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING,
                                   ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
    fw_log(INFO,
           _("There is a download in progress. New download from ") "%s" _(
                   " added to queue"),
           inst->package_uri);
    return 0;

cleanup:
    _anjay_log_oom();
    if (new_download) {
        avs_free((void *) (intptr_t) new_download->url);
        AVS_LIST_DELETE(&new_download);
    }
    return -1;
}

static int schedule_download(anjay_unlocked_t *anjay,
                             advanced_fw_repr_t *fw,
                             advanced_fw_instance_t *inst) {
    anjay_download_config_t cfg = {
        .url = inst->package_uri,
        .on_next_block = download_write_block,
        .on_download_finished = download_finished,
        .user_data = inst,
        .prefer_same_socket_downloads = fw->prefer_same_socket_downloads
    };
    avs_coap_udp_tx_params_t tx_params;
    if (!get_coap_tx_params(anjay, inst, &tx_params)) {
        cfg.coap_tx_params = &tx_params;
    }
    cfg.tcp_request_timeout = get_tcp_request_timeout(anjay, inst);
    if (is_any_download_in_progress(fw)) {
        return enqueue_download(anjay, fw, inst, &cfg);
    }
    return schedule_download_now(anjay, fw, inst, &cfg);
}

struct schedule_download_args {
    anjay_t *anjay;
    advanced_fw_repr_t *fw;
    advanced_fw_instance_t *inst;
    size_t start_offset;
    // actually a FAM
};

static int schedule_background_anjay_download(anjay_unlocked_t *anjay,
                                              advanced_fw_repr_t *fw,
                                              advanced_fw_instance_t *inst) {
    return schedule_download(anjay, fw, inst);
}
#    endif // ANJAY_WITH_DOWNLOADER

static int write_firmware_to_stream(anjay_unlocked_t *anjay,
                                    advanced_fw_repr_t *fw,
                                    advanced_fw_instance_t *inst,
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

        result = _anjay_get_bytes_unlocked(ctx, &bytes_read, &finished, buffer,
                                           sizeof(buffer));
        if (result) {
            fw_log(ERROR, _("anjay_get_bytes() failed"));

            update_state_and_update_result(
                    anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_CONNECTION_LOST);
            return result;
        }
        if (bytes_read > 0) {
            if (first_byte == EOF) {
                first_byte = (unsigned char) buffer[0];
            }
            result = user_state_stream_write(anjay, inst, buffer, bytes_read);
        }
        if (result) {
            handle_err_result(anjay, fw, inst,
                              ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE, result,
                              ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
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
                          advanced_fw_repr_t *fw,
                          advanced_fw_instance_t *inst,
                          anjay_unlocked_input_ctx_t *ctx,
                          bool *out_is_reset_request) {
    assert(inst->state != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING);
    if (user_state_ensure_stream_open(anjay, inst)) {
        return -1;
    }

    int result = write_firmware_to_stream(anjay, fw, inst, ctx,
                                          out_is_reset_request);

    if (result) {
        reset_user_state(anjay, inst);
    } else if (!*out_is_reset_request) {
        // stream_finish_result deliberately not propagated up:
        // write itself succeeded
        int stream_finish_result = finish_user_stream(anjay, inst);

        if (stream_finish_result) {
            handle_err_result(anjay, fw, inst,
                              ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                              stream_finish_result,
                              ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE);
        } else {
            update_state_and_update_result(
                    anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
        }
    }
    return result;
}

#    ifdef ANJAY_WITH_DOWNLOADER
static void download_queue_entry_cleanup(anjay_download_config_t *cfg) {
    avs_free((void *) (intptr_t) cfg->url);
    avs_free((void *) cfg->coap_tx_params);
}

static void
cancel_existing_download_if_in_progress(anjay_unlocked_t *anjay,
                                        advanced_fw_repr_t *fw,
                                        advanced_fw_instance_t *inst) {
    if (inst->state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING) {
        if (fw->current_download.download_handle
                && fw->current_download.iid == inst->iid) {
            _anjay_download_abort_unlocked(
                    anjay, fw->current_download.download_handle);
            assert(!fw->current_download.download_handle);
            fw->current_download.iid = ANJAY_ID_INVALID;
            fw_log(TRACE,
                   _("Aborted ongoing download for instance ") "%" PRIu16,
                   inst->iid);
            start_next_download_if_waiting(anjay, fw);
            return;
        }
        AVS_LIST(anjay_download_config_t) *queued_cfg;
        AVS_LIST_FOREACH_PTR(queued_cfg, &fw->download_queue) {
            advanced_fw_instance_t *queued_inst =
                    (advanced_fw_instance_t *) (*queued_cfg)->user_data;
            if (queued_inst->iid == inst->iid) {
                download_queue_entry_cleanup(*queued_cfg);
                AVS_LIST_DELETE(queued_cfg);
                fw_log(TRACE,
                       _("Removed instance ") "%" PRIu16 _(
                               " from download queue"),
                       inst->iid);
                return;
            }
        }
    }
}
#    endif // ANJAY_WITH_DOWNLOADER

static int fw_write(anjay_unlocked_t *anjay,
                    const anjay_dm_installed_object_t obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_riid_t riid,
                    anjay_unlocked_input_ctx_t *ctx) {
    (void) riid;

    advanced_fw_repr_t *fw = get_fw(obj_ptr);
    advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

    assert(inst);

    switch (rid) {
    case ADV_FW_RES_PACKAGE: {
        assert(riid == ANJAY_ID_INVALID);

        int result = 0;
#    ifdef ANJAY_WITH_DOWNLOADER
        bool is_any_in_progress = is_any_download_in_progress(fw);
#    endif // ANJAY_WITH_DOWNLOADER

        if (inst->state == ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING) {
            fw_log(WARNING, _("cannot set Package resource while updating"));
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        } else if (inst->state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
#    ifdef ANJAY_WITH_DOWNLOADER
                   && !is_any_in_progress
#    endif // ANJAY_WITH_DOWNLOADER
        ) {
            bool is_reset_request = false;

            result = write_firmware(anjay, fw, inst, ctx, &is_reset_request);
            if (!result && is_reset_request) {
                reset_state(anjay, fw, inst);
            }
        } else {
            result = expect_single_nullbyte(ctx);
            if (!result) {
#    ifdef ANJAY_WITH_DOWNLOADER
                cancel_existing_download_if_in_progress(anjay, fw, inst);
#    endif // ANJAY_WITH_DOWNLOADER
                reset_state(anjay, fw, inst);
            }
#    ifdef ANJAY_WITH_DOWNLOADER
            else if (is_any_in_progress) {
                fw_log(ERROR, _("There is a download already in progress or in "
                                "queue. Rejecting push mode download due do "
                                "implementation limitation"));
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            }
#    endif // ANJAY_WITH_DOWNLOADER
        }
        return result;
    }
    case ADV_FW_RES_PACKAGE_URI: {
        assert(riid == ANJAY_ID_INVALID);
        char *new_uri = NULL;
        int result = _anjay_io_fetch_string(ctx, &new_uri);
        size_t len = (new_uri ? strlen(new_uri) : 0);

        if (!result && len == 0) {
            avs_free(new_uri);

            if (inst->state == ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING) {
                fw_log(WARNING,
                       _("cannot set Package URI resource while updating"));
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            }

#    ifdef ANJAY_WITH_DOWNLOADER
            cancel_existing_download_if_in_progress(anjay, fw, inst);
#    endif // ANJAY_WITH_DOWNLOADER

            avs_free((void *) (intptr_t) inst->package_uri);
            inst->package_uri = NULL;
            reset_state(anjay, fw, inst);
            return 0;
        }

        if (!result && inst->state != ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE) {
            result = ANJAY_ERR_BAD_REQUEST;
        }

        if (!result
                && transport_security_from_uri(new_uri)
                               == ANJAY_TRANSPORT_SECURITY_UNDEFINED) {
            fw_log(WARNING,
                   _("unsupported download protocol required for uri ") "%s",
                   new_uri);
            set_update_result(
                    anjay, inst,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
#    ifdef ANJAY_WITH_SEND
            send_state_and_update_result(anjay, fw, iid, false);
#    endif // ANJAY_WITH_SEND
            result = ANJAY_ERR_BAD_REQUEST;
        }

#    ifdef ANJAY_WITH_DOWNLOADER
        if (!result) {
            avs_free((void *) (intptr_t) inst->package_uri);
            inst->package_uri = new_uri;
            new_uri = NULL;

            int dl_res = schedule_background_anjay_download(anjay, fw, inst);

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
    case ADV_FW_RES_SEVERITY: {
        assert(riid == ANJAY_ID_INVALID);

        int32_t severity = ANJAY_ADVANCED_FW_UPDATE_SEVERITY_MANDATORY;

        if (_anjay_get_i32_unlocked(ctx, &severity)
                || severity < (int32_t)
                                      ANJAY_ADVANCED_FW_UPDATE_SEVERITY_CRITICAL
                || severity > (int32_t)
                                      ANJAY_ADVANCED_FW_UPDATE_SEVERITY_OPTIONAL) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->severity = (anjay_advanced_fw_update_severity_t) severity;
        return 0;
    }
    case ADV_FW_RES_MAX_DEFER_PERIOD: {
        assert(riid == ANJAY_ID_INVALID);

        int max_defer_period = 0;

        if (_anjay_get_i32_unlocked(ctx, &max_defer_period)
                || max_defer_period < 0) {
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->max_defer_period = max_defer_period;
        return 0;
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
    advanced_fw_repr_t *fw = get_fw(obj_ptr);
    advanced_fw_instance_t *inst = get_fw_instance(fw, iid);
    assert(inst);

    switch (rid) {
    case ADV_FW_RES_UPDATE_PROTOCOL_SUPPORT: {
        for (anjay_riid_t i = 0; i < AVS_ARRAY_SIZE(SUPPORTED_PROTOCOLS); ++i) {
            _anjay_dm_emit_unlocked(ctx, i);
        }
        return 0;
    }
    case ADV_FW_RES_LINKED_INSTANCES: {
        for (size_t i = 0; i < inst->linked_instances_count; ++i) {
            _anjay_dm_emit_unlocked(ctx, inst->linked_instances[i]);
        }
        return 0;
    }
    case ADV_FW_RES_CONFLICTING_INSTANCES: {
        for (size_t i = 0; i < inst->conflicting_instances_count; ++i) {
            _anjay_dm_emit_unlocked(ctx, inst->conflicting_instances[i]);
        }
        return 0;
    }
    default:
        AVS_UNREACHABLE(
                "Attempted to list instances in a single-instance resource");
        return ANJAY_ERR_INTERNAL;
    }
}

static void reset_supplemental_iid_cache(advanced_fw_repr_t *fw) {
    avs_free(fw->supplemental_iid_cache);
    fw->supplemental_iid_cache = NULL;
    fw->supplemental_iid_cache_count = 0;
}

struct upgrade_job_args {
    advanced_fw_repr_t *fw;
    advanced_fw_instance_t *inst;
};

static void perform_upgrade(avs_sched_t *sched, const void *args_) {
    struct upgrade_job_args *args =
            (struct upgrade_job_args *) (intptr_t) args_;

    set_update_deadline(args->inst);
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    int result =
            user_state_perform_upgrade(anjay,
                                       args->inst,
                                       args->fw->supplemental_iid_cache,
                                       args->fw->supplemental_iid_cache_count);
    reset_supplemental_iid_cache(args->fw);
    if (result) {
        fw_log(ERROR, _("user_state_perform_upgrade() failed: ") "%d", result);
        handle_err_result(anjay, args->fw, args->inst,
                          ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED, result,
                          ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static void schedule_upgrade(avs_sched_t *sched, const void *args_) {
    struct upgrade_job_args *args =
            (struct upgrade_job_args *) (intptr_t) args_;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    // Let's defer actually performing the upgrade to yet another scheduler run
    // - the notification for the UPDATING state is probably being scheduled in
    // the current one.
    if (args->inst->state == ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING
            && args->inst->user_state.state
                           != ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING
            && AVS_SCHED_NOW(sched, &args->inst->update_job, perform_upgrade,
                             args, sizeof(*args))) {
        reset_supplemental_iid_cache(args->fw);
        update_state_and_update_result(
                anjay, args->fw, args->inst,
                ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
                ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int sort_supplemental_iid_cache(advanced_fw_repr_t *fw) {
    // Simple insertion sort.
    // The count should be usually low enough for it to not be a performance
    // hog.
    for (size_t i = 1; i < fw->supplemental_iid_cache_count; ++i) {
        for (ptrdiff_t j = (ptrdiff_t) i; j > 0; --j) {
            if (fw->supplemental_iid_cache[j - 1]
                    == fw->supplemental_iid_cache[j]) {
                fw_log(ERROR, _("Duplicate instances specified in "
                                "Firmare Update arguments"));
                return ANJAY_ERR_BAD_REQUEST;
            } else if (fw->supplemental_iid_cache[j - 1]
                       < fw->supplemental_iid_cache[j]) {
                break;
            }

            anjay_iid_t tmp = fw->supplemental_iid_cache[j - 1];

            fw->supplemental_iid_cache[j - 1] = fw->supplemental_iid_cache[j];
            fw->supplemental_iid_cache[j] = tmp;
        }
    }
    return 0;
}

static int handle_fw_execute_args(advanced_fw_repr_t *fw,
                                  anjay_iid_t main_iid,
                                  anjay_unlocked_execute_ctx_t *ctx) {
    int arg;
    bool arg_has_value;

    reset_supplemental_iid_cache(fw);

    if (_anjay_execute_get_next_arg_unlocked(ctx, &arg, &arg_has_value)) {
        return 0;
    }

    if (arg != 0) {
        fw_log(ERROR, _("Invalid Advanced Firmware Update argument: ") "%d",
               arg);
        return ANJAY_ERR_BAD_REQUEST;
    }

    avs_stream_t *supplemental_iid_cache_membuf = avs_stream_membuf_create();

    if (!supplemental_iid_cache_membuf) {
        _anjay_log_oom();
        return ANJAY_ERR_INTERNAL;
    }

    char arg_buf[sizeof("</" AVS_QUOTE_MACRO(ADV_FW_OID) "/65535>,")];
    size_t arg_buf_offset = 0;
    size_t arg_buf_bytes_read;
    int result = ANJAY_BUFFER_TOO_SHORT;
    void *supplemental_iid_cache = NULL;
    size_t supplemental_iid_cache_size = 0;

    while (true) {
        if (result == ANJAY_BUFFER_TOO_SHORT
                && sizeof(arg_buf) - arg_buf_offset > 1) {
            result = _anjay_execute_get_arg_value_unlocked(
                    ctx, &arg_buf_bytes_read, arg_buf + arg_buf_offset,
                    sizeof(arg_buf) - arg_buf_offset);
            if (result && result != ANJAY_BUFFER_TOO_SHORT) {
                fw_log(ERROR,
                       _("Error while reading Advanced Firmware Update "
                         "arguments"));
                goto finish;
            }
        }

        if (arg_buf_offset == 0 && arg_buf_bytes_read == 0) {
            break;
        }

        anjay_oid_t supplemental_oid;
        anjay_iid_t supplemental_iid;
        advanced_fw_instance_t *supplemental_inst = NULL;
        int char_count = 0;
        int scanf_result =
                sscanf(arg_buf, "</%" SCNu16 "/%" SCNu16 ">%n",
                       &supplemental_oid, &supplemental_iid, &char_count);

        if (scanf_result == 2 && char_count
                && supplemental_oid == ANJAY_ADVANCED_FW_UPDATE_OID
                && supplemental_iid != main_iid
                && supplemental_iid != ANJAY_ID_INVALID) {
            supplemental_inst = get_fw_instance(fw, supplemental_iid);
        }

        if (!supplemental_inst) {
            fw_log(ERROR, _("Invalid argument for Advanced Firmware Update"));
            result = ANJAY_ERR_BAD_REQUEST;
            goto finish;
        }

        if (supplemental_inst->state
                != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED) {
            fw_log(WARNING,
                   _("Advanced Firmware Update including supplemental "
                     "instance ") "%" PRIu16 _(" requested, but firmware not "
                                               "yet downloaded (state "
                                               "= ") "%d" _(")"),
                   supplemental_iid, supplemental_inst->state);
            result = ANJAY_ERR_METHOD_NOT_ALLOWED;
            goto finish;
        }

        if (avs_is_err(avs_stream_write(supplemental_iid_cache_membuf,
                                        &supplemental_iid,
                                        sizeof(supplemental_iid)))) {
            _anjay_log_oom();
            result = ANJAY_ERR_INTERNAL;
            goto finish;
        }

        if (!result && !arg_buf[char_count]) {
            break;
        } else if (arg_buf[char_count] == ',') {
            memmove(arg_buf, arg_buf + char_count + 1,
                    sizeof(arg_buf) - (size_t) char_count - 1);
            arg_buf_offset += arg_buf_bytes_read;
            arg_buf_offset -= (size_t) char_count + 1;
        } else {
            fw_log(ERROR, _("Invalid argument for Advanced Firmware Update"));
            result = ANJAY_ERR_BAD_REQUEST;
            goto finish;
        }
    }

    if (!fw->supplemental_iid_cache_count) {
        // empty list is different from non-existing one in this case
        // let's allocated a dummy byte so that the resulting pointer is not
        // NULL
        avs_stream_write(supplemental_iid_cache_membuf, &(uint8_t) { 0 }, 1);
    }

    avs_stream_membuf_fit(supplemental_iid_cache_membuf);

    if (avs_is_err(
                avs_stream_membuf_take_ownership(supplemental_iid_cache_membuf,
                                                 &supplemental_iid_cache,
                                                 &supplemental_iid_cache_size))
            || !supplemental_iid_cache) {
        fw_log(ERROR, _("Could not take ownership of the buffer"));
        result = ANJAY_ERR_INTERNAL;
        goto finish;
    }

    fw->supplemental_iid_cache = (anjay_iid_t *) supplemental_iid_cache;
    fw->supplemental_iid_cache_count =
            supplemental_iid_cache_size / sizeof(anjay_iid_t);

    result = sort_supplemental_iid_cache(fw);

    if (!result
            && _anjay_execute_get_next_arg_unlocked(ctx, &arg, &arg_has_value)
                           != ANJAY_EXECUTE_GET_ARG_END) {
        fw_log(ERROR, _("Superfluous Advanced Firmware Update argument: ") "%d",
               arg);
        result = ANJAY_ERR_BAD_REQUEST;
    }

finish:
    avs_stream_cleanup(&supplemental_iid_cache_membuf);

    if (result) {
        reset_supplemental_iid_cache(fw);
    }
    return result;
}

static int fw_execute(anjay_unlocked_t *anjay,
                      const anjay_dm_installed_object_t obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_unlocked_execute_ctx_t *ctx) {
    advanced_fw_repr_t *fw = get_fw(obj_ptr);
    advanced_fw_instance_t *inst = get_fw_instance(fw, iid);
    assert(inst);

    switch (rid) {
    case ADV_FW_RES_UPDATE: {
        if (inst->state != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED) {
            fw_log(WARNING,
                   _("Advanced Firmware Update for instance ") "%" PRIu16 _(
                           " requested, but firmware not yet downloaded "
                           "(state = ") "%d" _(")"),
                   iid, inst->state);
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        int result = handle_fw_execute_args(fw, iid, ctx);

        if (result) {
            return result;
        }

        update_state_and_update_result(anjay, fw, inst,
                                       ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING,
                                       ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
        // NOTE: This has to be called after update_state_and_update_result(),
        // to make sure that schedule_upgrade() is called after notify_clb()
        // and consequently, perform_upgrade() is called after trigger_observe()
        // (if it's not delayed due to pmin).
        const struct upgrade_job_args upgrade_job_args = {
            .fw = fw,
            .inst = inst
        };

        if (AVS_SCHED_NOW(_anjay_get_scheduler_unlocked(anjay),
                          &inst->update_job, schedule_upgrade,
                          &upgrade_job_args, sizeof(upgrade_job_args))) {
            fw_log(WARNING, _("Could not schedule the upgrade job"));
            update_state_and_update_result(
                    anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
                    ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY);
            reset_supplemental_iid_cache(fw);
            return ANJAY_ERR_INTERNAL;
        }
        return 0;
    }
    case ADV_FW_RES_CANCEL:
        if (inst->state != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING
                && inst->state != ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED) {
            fw_log(WARNING,
                   _("Advanced Firmware Update Cancel requested, but the "
                     "firmware is being installed or has already been "
                     "installed (state = ") "%d" _(")"),
                   inst->state);
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
#    ifdef ANJAY_WITH_DOWNLOADER
        cancel_existing_download_if_in_progress(anjay, fw, inst);
#    endif // ANJAY_WITH_DOWNLOADER

        reset_user_state(anjay, inst);
        update_state_and_update_result(
                anjay, fw, inst, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
                ANJAY_ADVANCED_FW_UPDATE_RESULT_UPDATE_CANCELLED);
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
    .oid = ANJAY_ADVANCED_FW_UPDATE_OID,
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

static AVS_LIST(advanced_fw_instance_t) initialize_fw_instance(
        anjay_unlocked_t *anjay,
        advanced_fw_repr_t *fw,
        anjay_iid_t iid,
        const char *component_name,
        const anjay_advanced_fw_update_handlers_t *handlers,
        void *user_arg,
        const anjay_advanced_fw_update_initial_state_t *initial_state) {
    AVS_LIST(advanced_fw_instance_t) inst =
            AVS_LIST_NEW_ELEMENT(advanced_fw_instance_t);

    if (!inst) {
        _anjay_log_oom();
        return NULL;
    }

    inst->iid = iid;
    inst->component_name = component_name;
    inst->user_state.handlers = handlers;
    inst->user_state.arg = user_arg;

    if (!initial_state) {
        return inst;
    }

    inst->severity = initial_state->persisted_severity;
    inst->last_state_change_time =
            initial_state->persisted_last_state_change_time;
    inst->update_deadline = initial_state->persisted_update_deadline;

    if ((initial_state->state != ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
         && initial_state->result != ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL)
            || (initial_state->state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
                && initial_state->result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL
                && initial_state->result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS
                && initial_state->result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_INTEGRITY_FAILURE
                && initial_state->result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED
                && initial_state->result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR)) {
        fw_log(ERROR,
               _("Invalid initial_state->result for the specified "
                 "initial_state->state"));
        goto fail;
    }

    switch (initial_state->state) {
    case ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE:
        inst->result = initial_state->result;
        return inst;
    case ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING: {
#    ifdef ANJAY_WITH_DOWNLOADER
        inst->user_state.state = ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING;
        reset_user_state(anjay, inst);
        if (inst->result == ANJAY_ADVANCED_FW_UPDATE_RESULT_CONNECTION_LOST
                && schedule_background_anjay_download(anjay, fw, inst)) {
            fw_log(WARNING, _("Could not retry firmware download"));
        }
#    else  // ANJAY_WITH_DOWNLOADER
        (void) anjay;
        (void) fw;
        fw_log(WARNING,
               _("Unable to resume download: PULL download not supported"));
#    endif // ANJAY_WITH_DOWNLOADER
        return inst;
    }
    case ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED:
        inst->user_state.state = ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED;
        inst->state = ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED;
        return inst;
    case ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING:
        inst->user_state.state = ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING;
        inst->state = ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING;
        inst->result = ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL;
        return inst;
    }

    fw_log(ERROR, _("Invalid initial_state->state"));
fail:
    AVS_LIST_DELETE(&inst);
    return NULL;
}

static void fw_delete(void *fw_) {
    advanced_fw_repr_t *fw = (advanced_fw_repr_t *) fw_;
    AVS_LIST_CLEAR(&fw->instances) {
        AVS_LIST(advanced_fw_instance_t) inst = fw->instances;
        avs_sched_del(&inst->update_job);
#    ifdef ANJAY_WITH_DOWNLOADER
        avs_sched_del(&inst->resume_download_job);
#    endif // ANJAY_WITH_DOWNLOADER
        avs_free(inst->linked_instances);
        avs_free(inst->conflicting_instances);
        avs_free((void *) (intptr_t) inst->package_uri);
    }
#    ifdef ANJAY_WITH_DOWNLOADER
    AVS_LIST_CLEAR(&fw->download_queue) {
        download_queue_entry_cleanup(fw->download_queue);
    }
#    endif // ANJAY_WITH_DOWNLOADER
    // NOTE: fw itself will be freed when cleaning the objects list
}

int anjay_advanced_fw_update_install(
        anjay_t *anjay_locked,
        const anjay_advanced_fw_update_global_config_t *config) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    AVS_LIST(advanced_fw_repr_t) repr =
            AVS_LIST_NEW_ELEMENT(advanced_fw_repr_t);
    if (!repr) {
        _anjay_log_oom();
    } else {
        repr->def = &FIRMWARE_UPDATE;
        repr->current_download.iid = ANJAY_ID_INVALID;
        if (config) {
#    ifdef ANJAY_WITH_DOWNLOADER
            repr->prefer_same_socket_downloads =
                    config->prefer_same_socket_downloads;
#    endif // ANJAY_WITH_DOWNLOADER
#    ifdef ANJAY_WITH_SEND
            repr->use_lwm2m_send = config->use_lwm2m_send;
#    endif // ANJAY_WITH_SEND
        }
        _anjay_dm_installed_object_init_unlocked(&repr->def_ptr, &repr->def);
        if (!_anjay_dm_module_install(anjay, fw_delete, repr)) {
            AVS_STATIC_ASSERT(offsetof(advanced_fw_repr_t, def_ptr) == 0,
                              def_ptr_is_first_field);
            AVS_LIST(anjay_dm_installed_object_t) entry = &repr->def_ptr;
            if (_anjay_register_object_unlocked(anjay, &entry)) {
                result = _anjay_dm_module_uninstall(anjay, fw_delete);
                assert(!result);
                result = -1;
            } else {
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

int anjay_advanced_fw_update_instance_add(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const char *component_name,
        const anjay_advanced_fw_update_handlers_t *handlers,
        void *user_arg,
        const anjay_advanced_fw_update_initial_state_t *initial_state) {
    assert(anjay_locked);
    assert(handlers);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t **inst_insert_ptr = &fw->instances;

        AVS_LIST_ITERATE_PTR(inst_insert_ptr) {
            if ((*inst_insert_ptr)->iid >= iid) {
                break;
            }
        }

        if (*inst_insert_ptr && (*inst_insert_ptr)->iid == iid) {
            fw_log(ERROR, _("Instance already initialized"));
        } else if ((fw->instances || iid != 0)
                   && (!component_name || !handlers->get_current_version)) {
            fw_log(ERROR,
                   _("Component Name and Current Version is mandatory if "
                     "multiple instances are present"));
        } else {
            AVS_LIST(advanced_fw_instance_t) inst =
                    initialize_fw_instance(anjay, fw, iid, component_name,
                                           handlers, user_arg, initial_state);

            if (inst) {
                AVS_LIST_INSERT(inst_insert_ptr, inst);
                retval = 0;

#    ifdef ANJAY_WITH_SEND
                if (initial_state->state != ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
                        || initial_state->result
                                       != ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL) {
                    send_state_and_update_result(anjay, fw, iid, true);
                }
#    endif // ANJAY_WITH_SEND
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

static bool
is_state_change_allowed(anjay_advanced_fw_update_state_t current_state,
                        anjay_advanced_fw_update_state_t new_state,
                        anjay_advanced_fw_update_result_t new_result) {
    switch (current_state) {
    case ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE:
        return (((new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING
                  || new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED)
                 && new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL)
                || (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED
                    && new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED));
    case ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING:
        return (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
                && new_result != ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS
                && new_result != ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED)
               || (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED
                   && (new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL
                       || new_result
                                  == ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED));
    case ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED:
        return (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
                && (new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL
                    || new_result
                               == ANJAY_ADVANCED_FW_UPDATE_RESULT_UPDATE_CANCELLED))
               || (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED
                   && new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED)
               || (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING
                   && new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL);
    case ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING:
        return (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE
                && new_result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_UPDATE_CANCELLED
                && new_result != ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED
                && new_result
                           != ANJAY_ADVANCED_FW_UPDATE_RESULT_CONFLICTING_STATE)
               || (new_state == ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED
                   && (new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED
                       || new_result == ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED
                       || new_result
                                  == ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR));
    }
    AVS_UNREACHABLE("invalid enum value");
    return false;
}

int anjay_advanced_fw_update_set_state_and_result(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        anjay_advanced_fw_update_state_t state,
        anjay_advanced_fw_update_result_t result) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else if (!is_state_change_allowed(inst->state, state, result)) {
            fw_log(WARNING,
                   _("Advanced Firmware Update State and Result change "
                     "from ") "%d/%d" _(" to ") "%d/%d" _(" is not "
                                                          "allowed"),
                   (int) inst->state, (int) inst->result, (int) state,
                   (int) result);
        } else {
            if (state == ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE) {
                reset_user_state(anjay, inst);
            }
            update_state_and_update_result(anjay, fw, inst, state, result);
            retval = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

int anjay_advanced_fw_update_get_state(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        anjay_advanced_fw_update_state_t *out_state) {
    assert(anjay_locked);
    assert(out_state);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            *out_state = inst->state;
            retval = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

int anjay_advanced_fw_update_get_result(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        anjay_advanced_fw_update_result_t *out_result) {
    assert(anjay_locked);
    assert(out_result);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            *out_result = inst->result;
            retval = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

static int validate_target_iid_list(advanced_fw_repr_t *fw,
                                    anjay_iid_t iid,
                                    const anjay_iid_t *target_iids,
                                    size_t target_iids_count) {
    for (size_t i = 1; i < target_iids_count; ++i) {
        if (target_iids[i - 1] == target_iids[i]) {
            fw_log(ERROR, _("Duplicate target instance"));
            return -1;
        } else if (target_iids[i - 1] > target_iids[i]) {
            fw_log(ERROR, _("Target instance list not sorted"));
            return -1;
        }
    }

    AVS_LIST(advanced_fw_instance_t) inst = fw->instances;

    for (size_t i = 0; i < target_iids_count; ++i) {
        if (target_iids[i] == iid) {
            fw_log(ERROR, _("Linked Instances or Conflicting Instances "
                            "cannot reference self"));
            return -1;
        }
        while (inst && inst->iid < target_iids[i]) {
            AVS_LIST_ADVANCE(&inst);
        }
        if (!inst || inst->iid != target_iids[i]) {
            fw_log(ERROR, _("Target instance does not exist"));
            return -1;
        }
    }

    return 0;
}

static int copy_target_iid_list(anjay_iid_t **out_instances,
                                size_t *out_size,
                                const anjay_iid_t *target_iids,
                                size_t target_iids_count) {
    anjay_iid_t *new_ptr = NULL;

    if (!target_iids_count) {
        avs_free(*out_instances);
    } else {
        new_ptr = (anjay_iid_t *) avs_realloc(
                *out_instances, target_iids_count * sizeof(anjay_iid_t));
    }

    if (target_iids_count && !new_ptr) {
        _anjay_log_oom();
        return -1;
    }

    *out_instances = new_ptr;
    *out_size = target_iids_count;
    if (target_iids_count) {
        memcpy(*out_instances, target_iids,
               target_iids_count * sizeof(anjay_iid_t));
    }

    return 0;
}

int anjay_advanced_fw_update_set_linked_instances(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const anjay_iid_t *target_iids,
        size_t target_iids_count) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            retval = validate_target_iid_list(fw, iid, target_iids,
                                              target_iids_count);

            if (!retval) {
                retval = copy_target_iid_list(&inst->linked_instances,
                                              &inst->linked_instances_count,
                                              target_iids, target_iids_count);
            }

            if (!retval) {
                _anjay_notify_changed_unlocked(anjay,
                                               ANJAY_ADVANCED_FW_UPDATE_OID,
                                               iid,
                                               ADV_FW_RES_LINKED_INSTANCES);
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

int anjay_advanced_fw_update_get_linked_instances(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const anjay_iid_t **out_target_iids,
        size_t *out_target_iids_count) {
    assert(anjay_locked);

    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            *out_target_iids = inst->linked_instances;
            *out_target_iids_count = inst->linked_instances_count;
            retval = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

int anjay_advanced_fw_update_set_conflicting_instances(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const anjay_iid_t *target_iids,
        size_t target_iids_count) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            retval = validate_target_iid_list(fw, iid, target_iids,
                                              target_iids_count);

            if (!retval) {
                retval =
                        copy_target_iid_list(&inst->conflicting_instances,
                                             &inst->conflicting_instances_count,
                                             target_iids, target_iids_count);
            }

            if (!retval) {
                _anjay_notify_changed_unlocked(
                        anjay, ANJAY_ADVANCED_FW_UPDATE_OID, iid,
                        ADV_FW_RES_CONFLICTING_INSTANCES);
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

int anjay_advanced_fw_update_get_conflicting_instances(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        const anjay_iid_t **out_target_iids,
        size_t *out_target_iids_count) {
    assert(anjay_locked);
    int retval = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            *out_target_iids = inst->conflicting_instances;
            *out_target_iids_count = inst->conflicting_instances_count;
            retval = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return retval;
}

avs_time_real_t anjay_advanced_fw_update_get_deadline(anjay_t *anjay_locked,
                                                      anjay_iid_t iid) {
    assert(anjay_locked);
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            result = inst->update_deadline;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

anjay_advanced_fw_update_severity_t
anjay_advanced_fw_update_get_severity(anjay_t *anjay_locked, anjay_iid_t iid) {
    assert(anjay_locked);
    anjay_advanced_fw_update_severity_t result =
            ANJAY_ADVANCED_FW_UPDATE_SEVERITY_MANDATORY;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            result = inst->severity;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

avs_time_real_t
anjay_advanced_fw_update_get_last_state_change_time(anjay_t *anjay_locked,
                                                    anjay_iid_t iid) {
    assert(anjay_locked);
    avs_time_real_t result = AVS_TIME_REAL_INVALID;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);

        assert(fw);
        advanced_fw_instance_t *inst = get_fw_instance(fw, iid);

        if (!inst) {
            fw_log(ERROR, _("Instance does not exist"));
        } else {
            result = inst->last_state_change_time;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#    ifdef ANJAY_WITH_DOWNLOADER
void anjay_advanced_fw_update_pull_suspend(anjay_t *anjay_locked) {
    assert(anjay_locked);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);
        assert(fw);
        if (fw->current_download.download_handle) {
            _anjay_download_suspend_unlocked(
                    anjay, fw->current_download.download_handle);
        }
        fw->downloads_suspended = true;
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

int anjay_advanced_fw_update_pull_reconnect(anjay_t *anjay_locked) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_ADVANCED_FW_UPDATE_OID);
    if (!obj) {
        fw_log(WARNING, _("Advanced Firmware Update object not installed"));
    } else {
        advanced_fw_repr_t *fw = get_fw(*obj);
        assert(fw);
        fw->downloads_suspended = false;
        if (fw->current_download.download_handle) {
            result = _anjay_download_reconnect_unlocked(
                    anjay, fw->current_download.download_handle);
        } else {
            result = 0;
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}
#    endif // ANJAY_WITH_DOWNLOADER

#endif // ANJAY_WITH_MODULE_ADVANCED_FW_UPDATE
