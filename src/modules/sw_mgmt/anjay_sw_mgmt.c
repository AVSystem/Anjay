/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_SW_MGMT

#    include <inttypes.h>

#    include <anjay/sw_mgmt.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_io_utils.h>
#    include <anjay_modules/anjay_sched.h>
#    include <anjay_modules/anjay_utils_core.h>
#    include <anjay_modules/dm/anjay_modules.h>

#    ifdef ANJAY_WITH_DOWNLOADER
#        include <anjay/download.h>

#        include <avsystem/coap/code.h>

#        include <avsystem/commons/avs_errno.h>
#        include <avsystem/commons/avs_url.h>
#        include <avsystem/commons/avs_utils.h>
#    endif // ANJAY_WITH_DOWNLOADER

VISIBILITY_SOURCE_BEGIN

#    define sw_mgmt_log(level, ...) _anjay_log(sw_mgmt, level, __VA_ARGS__)

#    define sw_mgmt_log_inst(level, inst, ...)                             \
        _anjay_log(sw_mgmt, level,                                         \
                   _("[iid=") "%" PRIu16 _("] ") AVS_VARARG0(__VA_ARGS__), \
                   inst AVS_VARARG_REST(__VA_ARGS__))

#    define OID 9

#    define RID_PKGNAME 0
#    define RID_PKGVERSION 1
#    define RID_PACKAGE 2
#    define RID_PACKAGE_URI 3
#    define RID_INSTALL 4
#    define RID_UNINSTALL 6
#    define RID_UPDATE_STATE 7
#    define RID_UPDATE_RESULT 9
#    define RID_ACTIVATE 10
#    define RID_DEACTIVATE 11
#    define RID_ACTIVATION_STATE 12

#    define UNINSTALL_ARG_UNINSTALL 0
#    define UNINSTALL_ARG_FOR_UPDATE 1

#    define UNLOCK_FOR_SW_MGMT_CALLBACK(AnjayLockedVar, AnjayUnlockedVar, \
                                        SwMgmtInstancePtr)                \
        (SwMgmtInstancePtr)->cannot_delete = true;                        \
        ANJAY_MUTEX_UNLOCK_FOR_CALLBACK((AnjayLockedVar), (AnjayUnlockedVar))
#    define LOCK_AFTER_SW_MGMT_CALLBACK(AnjayLockedVar, SwMgmtInstancePtr) \
        ANJAY_MUTEX_LOCK_AFTER_CALLBACK((AnjayLockedVar));                 \
        (SwMgmtInstancePtr)->cannot_delete = false

typedef enum {
    SW_MGMT_INTERNAL_STATE_IDLE,
    SW_MGMT_INTERNAL_STATE_DOWNLOADING,
    SW_MGMT_INTERNAL_STATE_DOWNLOADED,
    SW_MGMT_INTERNAL_STATE_DELIVERED,
    SW_MGMT_INTERNAL_STATE_INSTALLING,
    SW_MGMT_INTERNAL_STATE_INSTALLED_DEACTIVATED,
    SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED
} sw_mgmt_internal_state_t;

typedef enum {
    SW_MGMT_UPDATE_STATE_INITIAL = 0,
    SW_MGMT_UPDATE_STATE_DOWNLOAD_STARTED = 1,
    SW_MGMT_UPDATE_STATE_DOWNLOADED = 2,
    SW_MGMT_UPDATE_STATE_DELIVERED = 3,
    SW_MGMT_UPDATE_STATE_INSTALLED = 4
} sw_mgmt_update_state_t;

typedef struct sw_mgmt_instance_struct {
    anjay_iid_t iid;
    void *inst_ctx;

    sw_mgmt_internal_state_t internal_state;
    anjay_sw_mgmt_update_result_t update_result;

    avs_sched_handle_t install_and_integrity_jobs_handle;

    bool cannot_delete;

#    ifdef ANJAY_WITH_DOWNLOADER
    anjay_download_handle_t pull_download_handle;
    bool pull_download_stream_opened;
#    endif // ANJAY_WITH_DOWNLOADER
} sw_mgmt_instance_t;

typedef struct sw_mgmt_object_struct {
    anjay_dm_installed_object_t def_ptr;
    const anjay_unlocked_dm_object_def_t *def;

    const anjay_sw_mgmt_handlers_t *handlers;
    void *obj_ctx;

    AVS_LIST(sw_mgmt_instance_t) instances;

#    if defined(ANJAY_WITH_DOWNLOADER)
    bool prefer_same_socket_downloads;
    bool downloads_suspended;
#    endif // defined(ANJAY_WITH_DOWNLOADER)
} sw_mgmt_object_t;

static anjay_dm_module_deleter_t sw_mgmt_delete;

static inline sw_mgmt_internal_state_t
initial_state_to_internal_state(anjay_sw_mgmt_initial_state_t initial_state) {
    // clang-format off
    static const sw_mgmt_internal_state_t map[] = {
        [ANJAY_SW_MGMT_INITIAL_STATE_IDLE] =
                SW_MGMT_INTERNAL_STATE_IDLE,
        [ANJAY_SW_MGMT_INITIAL_STATE_DOWNLOADED] =
                SW_MGMT_INTERNAL_STATE_DOWNLOADED,
        [ANJAY_SW_MGMT_INITIAL_STATE_DELIVERED] =
                SW_MGMT_INTERNAL_STATE_DELIVERED,
        [ANJAY_SW_MGMT_INITIAL_STATE_INSTALLING] =
                SW_MGMT_INTERNAL_STATE_INSTALLING,
        [ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED] =
                SW_MGMT_INTERNAL_STATE_INSTALLED_DEACTIVATED,
        [ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_ACTIVATED] =
                SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED,
    };
    // clang-format on
    assert(initial_state >= 0);
    assert(initial_state < AVS_ARRAY_SIZE(map));
    return map[initial_state];
}

static inline anjay_sw_mgmt_update_result_t
initial_state_to_update_result(anjay_sw_mgmt_initial_state_t initial_state) {
    // clang-format off
    static const anjay_sw_mgmt_update_result_t map[] = {
        [ANJAY_SW_MGMT_INITIAL_STATE_IDLE] =
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL,
        [ANJAY_SW_MGMT_INITIAL_STATE_DOWNLOADED] =
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL,
        [ANJAY_SW_MGMT_INITIAL_STATE_DELIVERED] =
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL,
        [ANJAY_SW_MGMT_INITIAL_STATE_INSTALLING] =
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL,
        [ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED] =
                ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLED,
        [ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_ACTIVATED] =
                ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLED,
    };
    // clang-format on
    assert(initial_state >= 0);
    assert(initial_state < AVS_ARRAY_SIZE(map));
    return map[initial_state];
}

static inline sw_mgmt_update_state_t
internal_state_to_update_state(sw_mgmt_internal_state_t internal_state) {
    // clang-format off
    static const sw_mgmt_update_state_t map[] = {
        [SW_MGMT_INTERNAL_STATE_IDLE] =
                SW_MGMT_UPDATE_STATE_INITIAL,
        [SW_MGMT_INTERNAL_STATE_DOWNLOADING] =
                SW_MGMT_UPDATE_STATE_DOWNLOAD_STARTED,
        [SW_MGMT_INTERNAL_STATE_DOWNLOADED] =
                SW_MGMT_UPDATE_STATE_DOWNLOADED,
        [SW_MGMT_INTERNAL_STATE_DELIVERED] =
                SW_MGMT_UPDATE_STATE_DELIVERED,
        [SW_MGMT_INTERNAL_STATE_INSTALLING] =
                SW_MGMT_UPDATE_STATE_DELIVERED,
        [SW_MGMT_INTERNAL_STATE_INSTALLED_DEACTIVATED] =
                SW_MGMT_UPDATE_STATE_INSTALLED,
        [SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED] =
                SW_MGMT_UPDATE_STATE_INSTALLED
    };
    // clang-format on
    assert(internal_state >= 0);
    assert(internal_state < AVS_ARRAY_SIZE(map));
    return map[internal_state];
}

static inline bool
internal_state_is_delivered(sw_mgmt_internal_state_t internal_state) {
    return internal_state_to_update_state(internal_state)
           == SW_MGMT_UPDATE_STATE_DELIVERED;
}

static inline bool
internal_state_is_installed(sw_mgmt_internal_state_t internal_state) {
    return internal_state_to_update_state(internal_state)
           == SW_MGMT_UPDATE_STATE_INSTALLED;
}

static inline bool
internal_state_is_activated(sw_mgmt_internal_state_t internal_state) {
    return internal_state == SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED;
}

static inline bool package_available(sw_mgmt_internal_state_t internal_state) {
    return internal_state_is_delivered(internal_state)
           || internal_state_is_installed(internal_state);
}

static inline anjay_sw_mgmt_update_result_t
retval_to_update_result(int retval) {
    switch (retval) {
    case ANJAY_SW_MGMT_ERR_NOT_ENOUGH_SPACE:
    case ANJAY_SW_MGMT_ERR_OUT_OF_MEMORY:
    case ANJAY_SW_MGMT_ERR_INTEGRITY_FAILURE:
    case ANJAY_SW_MGMT_ERR_UNSUPPORTED_PACKAGE_TYPE:
        return (anjay_sw_mgmt_update_result_t) -retval;
    default:
        return ANJAY_SW_MGMT_UPDATE_RESULT_UPDATE_ERROR;
    }
}

static void change_internal_state_and_update_result(
        anjay_unlocked_t *anjay,
        sw_mgmt_instance_t *inst,
        sw_mgmt_internal_state_t internal_state,
        anjay_sw_mgmt_update_result_t update_result) {
    // clang-format off
    sw_mgmt_log_inst(DEBUG, inst->iid, _("fsm state and update result change")
                     _(" from ") "%d" _(", ") "%d"
                     _(" to ") "%d" _(", ") "%d",
                     (int) inst->internal_state, (int) inst->update_result,
                     (int) internal_state, (int) update_result);
    // clang-format on

    if (internal_state != inst->internal_state) {
        sw_mgmt_internal_state_t old_internal_state = inst->internal_state;
        inst->internal_state = internal_state;

        // internal_state controls Update State and Activation State resources
        if (internal_state_to_update_state(inst->internal_state)
                != internal_state_to_update_state(old_internal_state)) {
            _anjay_notify_changed_unlocked(anjay, OID, inst->iid,
                                           RID_UPDATE_STATE);
        }

        if (internal_state_is_activated(inst->internal_state)
                != internal_state_is_activated(old_internal_state)) {
            _anjay_notify_changed_unlocked(anjay, OID, inst->iid,
                                           RID_ACTIVATION_STATE);
        }

        // version and name are available only when package is available
        if (package_available(inst->internal_state)
                != package_available(old_internal_state)) {
            _anjay_notify_changed_unlocked(anjay, OID, inst->iid, RID_PKGNAME);
            _anjay_notify_changed_unlocked(anjay, OID, inst->iid,
                                           RID_PKGVERSION);
        }
    }
    if (update_result != inst->update_result) {
        inst->update_result = update_result;
        _anjay_notify_changed_unlocked(anjay, OID, inst->iid,
                                       RID_UPDATE_RESULT);
    }
}

static inline sw_mgmt_object_t *
get_obj(const anjay_dm_installed_object_t *obj_ptr) {
    return AVS_CONTAINER_OF(_anjay_dm_installed_object_get_unlocked(obj_ptr),
                            sw_mgmt_object_t, def);
}

static sw_mgmt_instance_t *find_instance(const sw_mgmt_object_t *obj,
                                         anjay_iid_t iid) {
    AVS_LIST(sw_mgmt_instance_t) it;
    AVS_LIST_FOREACH(it, obj->instances) {
        if (it->iid == iid) {
            return it;
        } else if (it->iid > iid) {
            break;
        }
    }

    return NULL;
}

static inline int call_stream_open(anjay_unlocked_t *anjay,
                                   sw_mgmt_object_t *obj,
                                   sw_mgmt_instance_t *inst) {
    int result = -1;
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
    result =
            obj->handlers->stream_open(obj->obj_ctx, inst->iid, inst->inst_ctx);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
    return result;
}

static inline int call_stream_write(anjay_unlocked_t *anjay,
                                    sw_mgmt_object_t *obj,
                                    sw_mgmt_instance_t *inst,
                                    const void *data,
                                    size_t length) {
    int result = -1;
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
    result = obj->handlers->stream_write(obj->obj_ctx, inst->iid,
                                         inst->inst_ctx, data, length);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
    return result;
}

static inline int call_stream_finish(anjay_unlocked_t *anjay,
                                     sw_mgmt_object_t *obj,
                                     sw_mgmt_instance_t *inst) {
    int result = -1;
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
    result = obj->handlers->stream_finish(obj->obj_ctx, inst->iid,
                                          inst->inst_ctx);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
    return result;
}

static inline void call_reset(anjay_unlocked_t *anjay,
                              sw_mgmt_object_t *obj,
                              sw_mgmt_instance_t *inst) {
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
    obj->handlers->reset(obj->obj_ctx, inst->iid, inst->inst_ctx);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
}

static void pkg_install_job(avs_sched_t *sched, const void *inst_ptr) {
    sw_mgmt_instance_t *inst = *(sw_mgmt_instance_t *const *) inst_ptr;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_INSTALLING);
    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);

    int result = -1;
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_relocked, anjay, inst);
    result =
            obj->handlers->pkg_install(obj->obj_ctx, inst->iid, inst->inst_ctx);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_relocked, inst);

    if (result) {
        sw_mgmt_log_inst(WARNING, inst->iid, _("pkg_install() failed: ") "%d",
                         result);
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_DELIVERED,
                ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLATION_FAILURE);
    } else {
        sw_mgmt_log_inst(DEBUG, inst->iid, _("package installed successfully"));
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static void check_integrity_job(avs_sched_t *sched, const void *inst_ptr) {
    sw_mgmt_instance_t *inst = *(sw_mgmt_instance_t *const *) inst_ptr;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_DOWNLOADED);
    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);

    int result = -1;
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_relocked, anjay, inst);
    result = obj->handlers->check_integrity(obj->obj_ctx, inst->iid,
                                            inst->inst_ctx);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_relocked, inst);

    if (result) {
        sw_mgmt_log_inst(WARNING, inst->iid,
                         _("check_integrity() failed: ") "%d", result);
        call_reset(anjay, obj, inst);
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                retval_to_update_result(result));
    } else {
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_DELIVERED,
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL);
        sw_mgmt_log_inst(DEBUG, inst->iid, _("integrity checked successfully"));
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

/**
 * Schedule this additional intermediate function to extend the time for sending
 * a potential notification related to resource 9/x/7 and
 * SW_MGMT_UPDATE_STATE_DOWNLOADED state. However, it may not be enough because
 * of e.g. pmin attribute value.
 */
static void schedule_check_integrity_job(avs_sched_t *sched,
                                         const void *inst_ptr) {
    sw_mgmt_instance_t *inst = *(sw_mgmt_instance_t *const *) inst_ptr;
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    if (AVS_SCHED_NOW(_anjay_get_scheduler_unlocked(anjay),
                      &inst->install_and_integrity_jobs_handle,
                      check_integrity_job, &inst, sizeof(inst))) {
        sw_mgmt_object_t *obj =
                (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                              sw_mgmt_delete);
        if (obj) {
            call_reset(anjay, obj, inst);
        }
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                ANJAY_SW_MGMT_UPDATE_RESULT_OUT_OF_MEMORY);
        sw_mgmt_log_inst(WARNING, inst->iid,
                         _("Could not schedule check_integrity_job"));
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static void ensure_not_stalled_in_downloaded_state(anjay_unlocked_t *anjay,
                                                   sw_mgmt_object_t *obj,
                                                   sw_mgmt_instance_t *inst) {
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_DOWNLOADED);

    // check if integrity check is not already scheduled
    if (!inst->install_and_integrity_jobs_handle) {
        if (AVS_SCHED_NOW(_anjay_get_scheduler_unlocked(anjay),
                          &inst->install_and_integrity_jobs_handle,
                          schedule_check_integrity_job, &inst, sizeof(inst))) {
            call_reset(anjay, obj, inst);
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    ANJAY_SW_MGMT_UPDATE_RESULT_OUT_OF_MEMORY);
            sw_mgmt_log_inst(WARNING, inst->iid,
                             _("Could not schedule "
                               "schedule_check_integrity_job"));
        }
    }
}

static inline void possibly_schedule_integrity_check(anjay_unlocked_t *anjay,
                                                     sw_mgmt_object_t *obj,
                                                     sw_mgmt_instance_t *inst) {
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_DOWNLOADING);

    if (obj->handlers->check_integrity) {
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_DOWNLOADED,
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL);
        ensure_not_stalled_in_downloaded_state(anjay, obj, inst);
    } else {
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_DELIVERED,
                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL);
    }
}

static int package_push_download(anjay_unlocked_t *anjay,
                                 sw_mgmt_object_t *obj,
                                 sw_mgmt_instance_t *inst,
                                 anjay_unlocked_input_ctx_t *ctx) {
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_IDLE);

    if (call_stream_open(anjay, obj, inst)) {
        return ANJAY_ERR_INTERNAL;
    }

    // nobody's gonna notice that in PUSH mode, but let's adhere to the spec...
    change_internal_state_and_update_result(
            anjay, inst, SW_MGMT_INTERNAL_STATE_DOWNLOADING,
            ANJAY_SW_MGMT_UPDATE_RESULT_DOWNLOADING);

    int result;
    size_t written = 0;
    bool finished = false;

    while (!finished) {
        size_t bytes_read;
        char buffer[1024];

        result = _anjay_get_bytes_unlocked(ctx, &bytes_read, &finished, buffer,
                                           sizeof(buffer));
        if (result) {
            call_reset(anjay, obj, inst);
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    ANJAY_SW_MGMT_UPDATE_RESULT_CONNECTION_LOST);
            return result;
        }

        if (bytes_read > 0) {
            result = call_stream_write(anjay, obj, inst, buffer, bytes_read);
        }
        if (result) {
            call_reset(anjay, obj, inst);
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    retval_to_update_result(result));
            return ANJAY_ERR_INTERNAL;
        }
        written += bytes_read;
    }

    result = call_stream_finish(anjay, obj, inst);
    if (result) {
        call_reset(anjay, obj, inst);
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                retval_to_update_result(result));
        return ANJAY_ERR_INTERNAL;
    }

    sw_mgmt_log_inst(DEBUG, inst->iid,
                     _("stream write successfully finished, ") "%zu" _(
                             " B written"),
                     written);

    possibly_schedule_integrity_check(anjay, obj, inst);
    return 0;
}

#    ifdef ANJAY_WITH_DOWNLOADER
#        if defined(ANJAY_WITH_COAP_DOWNLOAD) \
                || defined(ANJAY_WITH_HTTP_DOWNLOAD)
static anjay_transport_security_t
transport_security_from_protocol(const char *protocol) {
#            ifdef ANJAY_WITH_HTTP_DOWNLOAD
    if (avs_strcasecmp(protocol, "http") == 0) {
        return ANJAY_TRANSPORT_NOSEC;
    }
    if (avs_strcasecmp(protocol, "https") == 0) {
        return ANJAY_TRANSPORT_ENCRYPTED;
    }
#            endif // ANJAY_WITH_HTTP_DOWNLOAD

#            ifdef ANJAY_WITH_COAP_DOWNLOAD
    const anjay_transport_info_t *info =
            _anjay_transport_info_by_uri_scheme(protocol);
    if (info) {
        return info->security;
    }
#            endif // ANJAY_WITH_COAP_DOWNLOAD

    return ANJAY_TRANSPORT_SECURITY_UNDEFINED;
}

#        endif // defined(ANJAY_WITH_COAP_DOWNLOAD) ||
               // defined(ANJAY_WITH_HTTP_DOWNLOAD)
static anjay_transport_security_t transport_security_from_uri(const char *uri) {
#        if defined(ANJAY_WITH_COAP_DOWNLOAD) \
                || defined(ANJAY_WITH_HTTP_DOWNLOAD)
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
#        else  // defined(ANJAY_WITH_COAP_DOWNLOAD) ||
               // defined(ANJAY_WITH_HTTP_DOWNLOAD)
    (void) uri;
    return ANJAY_TRANSPORT_SECURITY_UNDEFINED;
#        endif // defined(ANJAY_WITH_COAP_DOWNLOAD) ||
               // defined(ANJAY_WITH_HTTP_DOWNLOAD)
}

static int get_security_config(anjay_unlocked_t *anjay,
                               sw_mgmt_object_t *obj,
                               sw_mgmt_instance_t *inst,
                               const char *package_uri,
                               anjay_security_config_t *out_security_config) {
    if (obj->handlers->get_security_config) {
        int result = -1;
        UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
        result = obj->handlers->get_security_config(obj->obj_ctx, inst->iid,
                                                    inst->inst_ctx, package_uri,
                                                    out_security_config);
        LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
        return result;
    } else {
        if (!_anjay_security_config_from_dm_unlocked(anjay, out_security_config,
                                                     package_uri)) {
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
                              sw_mgmt_object_t *obj,
                              sw_mgmt_instance_t *inst,
                              const char *package_uri,
                              avs_coap_udp_tx_params_t *out_tx_params) {
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_IDLE);
    if (obj->handlers->get_coap_tx_params) {
        UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
        *out_tx_params =
                obj->handlers->get_coap_tx_params(obj->obj_ctx, inst->iid,
                                                  inst->inst_ctx, package_uri);
        LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
        return 0;
    }
    return -1;
}

static avs_time_duration_t get_tcp_request_timeout(anjay_unlocked_t *anjay,
                                                   sw_mgmt_object_t *obj,
                                                   sw_mgmt_instance_t *inst,
                                                   const char *package_uri) {
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_IDLE);
    avs_time_duration_t result = AVS_TIME_DURATION_INVALID;
    if (obj->handlers->get_tcp_request_timeout) {
        UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
        result = obj->handlers->get_tcp_request_timeout(
                obj->obj_ctx, inst->iid, inst->inst_ctx, package_uri);
        LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
    }
    return result;
}

static inline int pull_download_ensure_stream_opened(anjay_unlocked_t *anjay,
                                                     sw_mgmt_object_t *obj,
                                                     sw_mgmt_instance_t *inst) {
    if (!inst->pull_download_stream_opened) {
        if (call_stream_open(anjay, obj, inst)) {
            sw_mgmt_log_inst(ERROR, inst->iid, _("could not open package"));
            return -1;
        }
        inst->pull_download_stream_opened = true;
    }
    return 0;
}

static avs_error_t pull_download_on_next_block(anjay_t *anjay_locked,
                                               const uint8_t *data,
                                               size_t data_size,
                                               const anjay_etag_t *etag,
                                               void *inst_) {
    (void) etag;

    int result = 0;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    sw_mgmt_instance_t *inst = (sw_mgmt_instance_t *) inst_;

    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_DOWNLOADING);

    if (!pull_download_ensure_stream_opened(anjay, obj, inst)) {
        if (data_size > 0) {
            result = call_stream_write(anjay, obj, inst, data, data_size);
        }

        if (result) {
            sw_mgmt_log_inst(ERROR, inst->iid, _("could not write package"));
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    retval_to_update_result(result));
        }
    } else {
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                ANJAY_SW_MGMT_UPDATE_RESULT_UPDATE_ERROR);
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);

    return result ? avs_errno(AVS_UNKNOWN_ERROR) : AVS_OK;
}

static inline anjay_sw_mgmt_update_result_t
handle_downloader_error(anjay_download_status_t status) {
    anjay_sw_mgmt_update_result_t update_result =
            ANJAY_SW_MGMT_UPDATE_RESULT_UPDATE_ERROR;

    switch (status.result) {
    case ANJAY_DOWNLOAD_ERR_FAILED:
        if (status.details.error.category == AVS_ERRNO_CATEGORY) {
            switch (status.details.error.code) {
            case AVS_EADDRNOTAVAIL:
                update_result = ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI;
                break;
            case AVS_EPROTO:
            case AVS_ECONNABORTED:
            case AVS_ECONNREFUSED:
            case AVS_ETIMEDOUT:
                update_result = ANJAY_SW_MGMT_UPDATE_RESULT_CONNECTION_LOST;
                break;
            case AVS_ENOMEM:
                update_result = ANJAY_SW_MGMT_UPDATE_RESULT_OUT_OF_MEMORY;
                break;
            }
        } else if (status.details.error.category
                   == AVS_NET_SSL_ALERT_CATEGORY) {
            update_result = ANJAY_SW_MGMT_UPDATE_RESULT_CONNECTION_LOST;
        }
        break;
    case ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE:
#        ifdef ANJAY_WITH_COAP_DOWNLOAD
        if (status.details.error.code == AVS_COAP_CODE_NOT_FOUND) {
            update_result = ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI;
        }
#        endif // ANJAY_WITH_COAP_DOWNLOAD
#        ifdef ANJAY_WITH_HTTP_DOWNLOAD
        if (status.details.error.code == 404) {
            update_result = ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI;
        }
#        endif // ANJAY_WITH_HTTP_DOWNLOAD
        break;
    case ANJAY_DOWNLOAD_ERR_EXPIRED:
        update_result = ANJAY_SW_MGMT_UPDATE_RESULT_CONNECTION_LOST;
        break;
    default:
        break;
    }

    return update_result;
}

static void pull_download_on_download_finished(anjay_t *anjay_locked,
                                               anjay_download_status_t status,
                                               void *inst_) {
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    sw_mgmt_instance_t *inst = (sw_mgmt_instance_t *) inst_;

    inst->pull_download_handle = NULL;

    if (inst->internal_state != SW_MGMT_INTERNAL_STATE_DOWNLOADING) {
        // pull_download_on_next_block() already failed
        call_reset(anjay, obj, inst);
    } else if (status.result != ANJAY_DOWNLOAD_FINISHED) {
        call_reset(anjay, obj, inst);
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                handle_downloader_error(status));
    } else {
        // in case the downloaded file is empty
        // stream_open should be called anyways
        if (pull_download_ensure_stream_opened(anjay, obj, inst)
                || call_stream_finish(anjay, obj, inst)) {
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    ANJAY_SW_MGMT_UPDATE_RESULT_UPDATE_ERROR);
        } else {
            possibly_schedule_integrity_check(anjay, obj, inst);
        }
    }
    inst->pull_download_stream_opened = false;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int schedule_package_pull_download(anjay_unlocked_t *anjay,
                                          sw_mgmt_object_t *obj,
                                          sw_mgmt_instance_t *inst,
                                          const char *package_uri) {
    assert(inst->internal_state == SW_MGMT_INTERNAL_STATE_IDLE);

    anjay_download_config_t cfg = {
        .url = package_uri,
        .start_offset = 0,
        .on_next_block = pull_download_on_next_block,
        .on_download_finished = pull_download_on_download_finished,
        .user_data = inst,
        .prefer_same_socket_downloads = obj->prefer_same_socket_downloads
    };

    if (transport_security_from_uri(package_uri) == ANJAY_TRANSPORT_ENCRYPTED) {
        int result = get_security_config(anjay, obj, inst, package_uri,
                                         &cfg.security_config);
        if (result) {
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI);
            return -1;
        }
    }

#        ifdef ANJAY_WITH_COAP_DOWNLOAD
    avs_coap_udp_tx_params_t tx_params;
    if (!get_coap_tx_params(anjay, obj, inst, package_uri, &tx_params)) {
        cfg.coap_tx_params = &tx_params;
    }
#        endif // ANJAY_WITH_COAP_DOWNLOAD
    cfg.tcp_request_timeout =
            get_tcp_request_timeout(anjay, obj, inst, package_uri);

    assert(!inst->pull_download_handle);
    avs_error_t err =
            _anjay_download_unlocked(anjay, &cfg, &inst->pull_download_handle);
    if (!inst->pull_download_handle) {
        anjay_sw_mgmt_update_result_t update_result =
                ANJAY_SW_MGMT_UPDATE_RESULT_UPDATE_ERROR;
        if (avs_is_err(err) && err.category == AVS_ERRNO_CATEGORY) {
            switch (err.code) {
            case AVS_EADDRNOTAVAIL:
            case AVS_EINVAL:
                update_result = ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI;
                break;
            case AVS_ENODEV:
                update_result = ANJAY_SW_MGMT_UPDATE_RESULT_CONNECTION_LOST;
                break;
            case AVS_ENOMEM:
                update_result = ANJAY_SW_MGMT_UPDATE_RESULT_OUT_OF_MEMORY;
                break;
            }
        }
        change_internal_state_and_update_result(
                anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE, update_result);
        return -1;
    }

    if (obj->downloads_suspended) {
        _anjay_download_suspend_unlocked(anjay, inst->pull_download_handle);
    }

    change_internal_state_and_update_result(
            anjay, inst, SW_MGMT_INTERNAL_STATE_DOWNLOADING,
            ANJAY_SW_MGMT_UPDATE_RESULT_DOWNLOADING);
    sw_mgmt_log_inst(INFO, inst->iid, _("download started: ") "%s",
                     package_uri);
    return 0;
}

void anjay_sw_mgmt_pull_suspend(anjay_t *anjay_locked) {
    assert(anjay_locked);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    if (!obj) {
        sw_mgmt_log(WARNING, _("Software Management object not installed"));
    } else {
        AVS_LIST(sw_mgmt_instance_t) *it;
        AVS_LIST_FOREACH_PTR(it, &obj->instances) {
            if ((*it)->pull_download_handle) {
                _anjay_download_suspend_unlocked(anjay,
                                                 (*it)->pull_download_handle);
            }
        }
        obj->downloads_suspended = true;
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

int anjay_sw_mgmt_pull_reconnect(anjay_t *anjay_locked) {
    assert(anjay_locked);
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    if (!obj) {
        sw_mgmt_log(WARNING, _("Software Management object not installed"));
    } else {
        result = 0;
        obj->downloads_suspended = false;
        AVS_LIST(sw_mgmt_instance_t) *it;
        AVS_LIST_FOREACH_PTR(it, &obj->instances) {
            if ((*it)->pull_download_handle) {
                _anjay_update_ret(&result,
                                  _anjay_download_reconnect_unlocked(
                                          anjay, (*it)->pull_download_handle));
            }
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}
#    endif // ANJAY_WITH_DOWNLOADER

static void initialize_instance(
        sw_mgmt_instance_t *inst,
        const anjay_sw_mgmt_instance_initializer_t *instance_initializer) {
    inst->iid = instance_initializer->iid;
    inst->inst_ctx = instance_initializer->inst_ctx;
    inst->internal_state = initial_state_to_internal_state(
            instance_initializer->initial_state);
    inst->update_result =
            initial_state_to_update_result(instance_initializer->initial_state);
}

static void clean_up_instance(anjay_unlocked_t *anjay,
                              sw_mgmt_instance_t *inst) {
    (void) anjay;
    avs_sched_del(&inst->install_and_integrity_jobs_handle);

#    ifdef ANJAY_WITH_DOWNLOADER
    // HACK: this method is called in sw_mgmt_delete which doesn't have
    // access to anjay, but in case of whole library deinitialization
    // all downloads are already cancelled
    if (anjay) {
        _anjay_download_abort_unlocked(anjay, inst->pull_download_handle);
    }
#    endif // ANJAY_WITH_DOWNLOADER
}

static void insert_instance(sw_mgmt_object_t *obj,
                            sw_mgmt_instance_t *to_insert) {
    AVS_LIST(sw_mgmt_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &obj->instances) {
        assert((*it)->iid != to_insert->iid);
        if ((*it)->iid > to_insert->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(it, to_insert);
}

static int delete_instance(anjay_unlocked_t *anjay,
                           sw_mgmt_object_t *obj,
                           anjay_iid_t iid) {
    AVS_LIST(sw_mgmt_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &obj->instances) {
        if ((*it)->iid == iid) {
            clean_up_instance(anjay, *it);
            AVS_LIST_DELETE(it);
            return 0;
        } else if ((*it)->iid > iid) {
            break;
        }
    }

    return -1;
}

static int sw_mgmt_list_instances(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr,
                                  anjay_unlocked_dm_list_ctx_t *ctx) {
    (void) anjay;

    sw_mgmt_object_t *obj = get_obj(&obj_ptr);
    assert(obj);

    AVS_LIST(sw_mgmt_instance_t) it;
    AVS_LIST_FOREACH(it, obj->instances) {
        _anjay_dm_emit_unlocked(ctx, it->iid);
    }

    return 0;
}

static int sw_mgmt_instance_create(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj_ptr,
                                   anjay_iid_t iid) {
    (void) anjay;

    sw_mgmt_object_t *obj = get_obj(&obj_ptr);
    assert(obj);

    if (!obj->handlers->add_handler) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    AVS_LIST(sw_mgmt_instance_t) created =
            AVS_LIST_NEW_ELEMENT(sw_mgmt_instance_t);
    if (!created) {
        _anjay_log_oom();
        return ANJAY_ERR_INTERNAL;
    }

    anjay_sw_mgmt_instance_initializer_t instance_initializer = {
        .initial_state = ANJAY_SW_MGMT_INITIAL_STATE_IDLE,
        .iid = iid
    };

    initialize_instance(created, &instance_initializer);

    int result = -1;
    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, created);
    result = obj->handlers->add_handler(obj->obj_ctx, iid, &created->inst_ctx);
    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, created);

    if (result) {
        sw_mgmt_log_inst(
                DEBUG, created->iid,
                _("attempt to create sw_mgmt instance rejected by user"));
        clean_up_instance(anjay, created);
        AVS_LIST_CLEAR(&created);
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    insert_instance(obj, created);

    return 0;
}

static int sw_mgmt_instance_remove(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t obj_ptr,
                                   anjay_iid_t iid) {
    (void) anjay;

    sw_mgmt_object_t *obj = get_obj(&obj_ptr);
    assert(obj);

    if (!obj->handlers->remove_handler) {
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    AVS_LIST(sw_mgmt_instance_t) *it;
    AVS_LIST_FOREACH_PTR(it, &obj->instances) {
        if ((*it)->iid == iid) {
            int result = -1;
            UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, *it);
            result = obj->handlers->remove_handler(obj->obj_ctx, iid,
                                                   (*it)->inst_ctx);
            LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, *it);

            if (result) {
                sw_mgmt_log_inst(DEBUG, (*it)->iid,
                                 _("attempt to delete sw_mgmt instance "
                                   "rejected by user"));
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            }

            clean_up_instance(anjay, *it);
            AVS_LIST_DELETE(it);
            return 0;
        } else if ((*it)->iid > iid) {
            break;
        }
    }

    assert(0);
    return ANJAY_ERR_NOT_FOUND;
}

static int sw_mgmt_list_resources(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_unlocked_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;

    _anjay_dm_emit_res_unlocked(ctx, RID_PKGNAME, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_PKGVERSION, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_PACKAGE, ANJAY_DM_RES_W,
                                ANJAY_DM_RES_PRESENT);
#    ifdef ANJAY_WITH_DOWNLOADER
    _anjay_dm_emit_res_unlocked(ctx, RID_PACKAGE_URI, ANJAY_DM_RES_W,
                                ANJAY_DM_RES_PRESENT);
#    endif // ANJAY_WITH_DOWNLOADER
    _anjay_dm_emit_res_unlocked(ctx, RID_INSTALL, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_UNINSTALL, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_UPDATE_STATE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_UPDATE_RESULT, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_ACTIVATE, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_DEACTIVATE, ANJAY_DM_RES_E,
                                ANJAY_DM_RES_PRESENT);
    _anjay_dm_emit_res_unlocked(ctx, RID_ACTIVATION_STATE, ANJAY_DM_RES_R,
                                ANJAY_DM_RES_PRESENT);

    return 0;
}

static int sw_mgmt_resource_read(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 anjay_unlocked_output_ctx_t *ctx) {
    (void) riid;

    sw_mgmt_object_t *obj = get_obj(&obj_ptr);
    assert(obj);

    sw_mgmt_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_PKGNAME: {
        assert(riid == ANJAY_ID_INVALID);

        const char *pkg_name = NULL;
        if (package_available(inst->internal_state)) {
            UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
            pkg_name =
                    obj->handlers->get_name(obj->obj_ctx, iid, inst->inst_ctx);
            LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
        }

        return _anjay_ret_string_unlocked(ctx, pkg_name ? pkg_name : "");
    }
    case RID_PKGVERSION: {
        assert(riid == ANJAY_ID_INVALID);

        const char *pkg_version = NULL;
        if (package_available(inst->internal_state)) {
            UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
            pkg_version = obj->handlers->get_version(obj->obj_ctx, iid,
                                                     inst->inst_ctx);
            LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
        }

        return _anjay_ret_string_unlocked(ctx, pkg_version ? pkg_version : "");
    }
    case RID_UPDATE_STATE: {
        assert(riid == ANJAY_ID_INVALID);

        return _anjay_ret_i64_unlocked(ctx, internal_state_to_update_state(
                                                    inst->internal_state));
    }
    case RID_UPDATE_RESULT: {
        assert(riid == ANJAY_ID_INVALID);

        return _anjay_ret_i64_unlocked(ctx, inst->update_result);
    }
    case RID_ACTIVATION_STATE: {
        assert(riid == ANJAY_ID_INVALID);

        return _anjay_ret_bool_unlocked(ctx, internal_state_is_activated(
                                                     inst->internal_state));
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int sw_mgmt_resource_write(anjay_unlocked_t *anjay,
                                  const anjay_dm_installed_object_t obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_unlocked_input_ctx_t *ctx) {
    (void) anjay;
    (void) riid;

    sw_mgmt_object_t *obj = get_obj(&obj_ptr);
    assert(obj);

    sw_mgmt_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_PACKAGE: {
        assert(riid == ANJAY_ID_INVALID);

        if (inst->internal_state != SW_MGMT_INTERNAL_STATE_IDLE) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        return package_push_download(anjay, obj, inst, ctx);
    }
#    ifdef ANJAY_WITH_DOWNLOADER
    case RID_PACKAGE_URI: {
        assert(riid == ANJAY_ID_INVALID);

        if (inst->internal_state != SW_MGMT_INTERNAL_STATE_IDLE) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        char *uri = NULL;
        if (_anjay_io_fetch_string(ctx, &uri)) {
            return ANJAY_ERR_INTERNAL;
        }

        if (!*uri
                || transport_security_from_uri(uri)
                               == ANJAY_TRANSPORT_SECURITY_UNDEFINED) {
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                    ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI);
            avs_free(uri);
            return ANJAY_ERR_BAD_REQUEST;
        }

        int dl_res = schedule_package_pull_download(anjay, obj, inst, uri);
        if (dl_res) {
            sw_mgmt_log_inst(WARNING, iid,
                             _("schedule_package_pull_download failed: ") "%d",
                             dl_res);
        }

        avs_free(uri);
        return 0;
    }
#    endif // ANJAY_WITH_DOWNLOADER
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int sw_mgmt_resource_execute(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid,
                                    anjay_unlocked_execute_ctx_t *arg_ctx) {
    (void) arg_ctx;

    sw_mgmt_object_t *obj = get_obj(&obj_ptr);
    assert(obj);

    sw_mgmt_instance_t *inst = find_instance(obj, iid);
    assert(inst);

    switch (rid) {
    case RID_INSTALL: {
        if (inst->internal_state != SW_MGMT_INTERNAL_STATE_DELIVERED) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        if (AVS_SCHED_NOW(_anjay_get_scheduler_unlocked(anjay),
                          &inst->install_and_integrity_jobs_handle,
                          pkg_install_job,
                          &inst,
                          sizeof(inst))) {
            sw_mgmt_log_inst(WARNING, inst->iid,
                             _("couldn't schedule pkg_install_job"));
            return ANJAY_ERR_INTERNAL;
        } else {
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_INSTALLING,
                    inst->update_result);
            return 0;
        }
    }
    case RID_UNINSTALL: {
        int arg;
        bool has_value;

        switch (_anjay_execute_get_next_arg_unlocked(arg_ctx, &arg,
                                                     &has_value)) {
        case 0: {
            if (has_value
                    || (arg != UNINSTALL_ARG_UNINSTALL
                        && arg != UNINSTALL_ARG_FOR_UPDATE)) {
                return ANJAY_ERR_BAD_REQUEST;
            }

            // we don't expect more arguments
            int arg_ignored;
            if (_anjay_execute_get_next_arg_unlocked(arg_ctx, &arg_ignored,
                                                     &has_value)
                    != ANJAY_EXECUTE_GET_ARG_END) {
                return ANJAY_ERR_BAD_REQUEST;
            }
            break;
        }
        case ANJAY_EXECUTE_GET_ARG_END: {
            arg = UNINSTALL_ARG_UNINSTALL;
            break;
        }
        default:
            return ANJAY_ERR_BAD_REQUEST;
        }

        if (arg == UNINSTALL_ARG_UNINSTALL) {
            if (internal_state_is_delivered(inst->internal_state)) {
                if (inst->internal_state == SW_MGMT_INTERNAL_STATE_INSTALLING) {
                    // remove potential install job
                    avs_sched_del(&inst->install_and_integrity_jobs_handle);
                }

                call_reset(anjay, obj, inst);
                change_internal_state_and_update_result(
                        anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                        inst->update_result);
                return 0;
            } else if (internal_state_is_installed(inst->internal_state)) {
                int result = -1;
                if (obj->handlers->pkg_uninstall) {
                    UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
                    result = obj->handlers->pkg_uninstall(obj->obj_ctx, iid,
                                                          inst->inst_ctx);
                    LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
                } else {
                    return ANJAY_ERR_METHOD_NOT_ALLOWED;
                }

                if (result) {
                    return ANJAY_ERR_INTERNAL;
                } else {
                    change_internal_state_and_update_result(
                            anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                            ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL);
                    return 0;
                }
            } else {
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            }
        } else {
            int result = -1;
            if (obj->handlers->prepare_for_update
                    && internal_state_is_installed(inst->internal_state)) {
                UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
                result = obj->handlers->prepare_for_update(obj->obj_ctx, iid,
                                                           inst->inst_ctx);
                LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
            }

            if (result) {
                return ANJAY_ERR_METHOD_NOT_ALLOWED;
            } else {
                change_internal_state_and_update_result(
                        anjay, inst, SW_MGMT_INTERNAL_STATE_IDLE,
                        ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL);
                return 0;
            }
        }
    }
    case RID_ACTIVATE: {
        if (!internal_state_is_installed(inst->internal_state)) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        int result = 0;
        if (obj->handlers->activate) {
            UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
            result = obj->handlers->activate(obj->obj_ctx, iid, inst->inst_ctx);
            LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
        }

        if (result) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        } else {
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED,
                    inst->update_result);
            return 0;
        }
    }
    case RID_DEACTIVATE: {
        if (!internal_state_is_installed(inst->internal_state)) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        int result = 0;
        if (obj->handlers->deactivate) {
            UNLOCK_FOR_SW_MGMT_CALLBACK(anjay_locked, anjay, inst);
            result = obj->handlers->deactivate(obj->obj_ctx, iid,
                                               inst->inst_ctx);
            LOCK_AFTER_SW_MGMT_CALLBACK(anjay_locked, inst);
        }

        if (result) {
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        } else {
            change_internal_state_and_update_result(
                    anjay, inst, SW_MGMT_INTERNAL_STATE_INSTALLED_DEACTIVATED,
                    inst->update_result);
            return 0;
        }
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int sw_mgmt_transaction_noop(anjay_unlocked_t *anjay,
                                    const anjay_dm_installed_object_t obj_ptr) {
    (void) anjay;
    (void) obj_ptr;
    return 0;
}

static void sw_mgmt_delete(void *obj_) {
    sw_mgmt_object_t *obj = (sw_mgmt_object_t *) obj_;
    AVS_LIST_CLEAR(&obj->instances) {
        clean_up_instance(NULL, obj->instances);
    }
    // NOTE: object will be freed when cleaning the object list
}

static const anjay_unlocked_dm_object_def_t OBJ_DEF = {
    .oid = OID,
    .handlers = {
        .list_instances = sw_mgmt_list_instances,
        .instance_create = sw_mgmt_instance_create,
        .instance_remove = sw_mgmt_instance_remove,

        .list_resources = sw_mgmt_list_resources,
        .resource_read = sw_mgmt_resource_read,
        .resource_write = sw_mgmt_resource_write,
        .resource_execute = sw_mgmt_resource_execute,

        .transaction_begin = sw_mgmt_transaction_noop,
        .transaction_validate = sw_mgmt_transaction_noop,
        .transaction_commit = sw_mgmt_transaction_noop,
        .transaction_rollback = sw_mgmt_transaction_noop
    }
};

int anjay_sw_mgmt_install(anjay_t *anjay_locked,
                          const anjay_sw_mgmt_settings_t *settings) {
    assert(anjay_locked);
    assert(settings);

    assert(settings->handlers);
    assert(settings->handlers->stream_open);
    assert(settings->handlers->stream_write);
    assert(settings->handlers->stream_finish);
    assert(settings->handlers->reset);
    assert(settings->handlers->get_name);
    assert(settings->handlers->get_version);
    assert(settings->handlers->pkg_install);

    assert(!!settings->handlers->activate == !!settings->handlers->deactivate);

    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    AVS_LIST(sw_mgmt_object_t) obj = AVS_LIST_NEW_ELEMENT(sw_mgmt_object_t);
    if (!obj) {
        _anjay_log_oom();
    } else {
        obj->def = &OBJ_DEF;
        _anjay_dm_installed_object_init_unlocked(&obj->def_ptr, &obj->def);
        obj->handlers = settings->handlers;
        obj->obj_ctx = settings->obj_ctx;
#    if defined(ANJAY_WITH_DOWNLOADER)
        obj->prefer_same_socket_downloads =
                settings->prefer_same_socket_downloads;
#    endif // defined(ANJAY_WITH_DOWNLOADER)

        if (!_anjay_dm_module_install(anjay, sw_mgmt_delete, obj)) {
            AVS_LIST(anjay_dm_installed_object_t) entry = &obj->def_ptr;

            if (_anjay_register_object_unlocked(anjay, &entry)) {
                result = _anjay_dm_module_uninstall(anjay, sw_mgmt_delete);
                assert(!result);
                result = -1;
            } else {
                result = 0;
            }
        }

        if (result) {
            AVS_LIST_CLEAR(&obj);
        }
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_sw_mgmt_get_activation_state(anjay_t *anjay_locked,
                                       anjay_iid_t iid,
                                       bool *out_state) {
    assert(anjay_locked);
    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    if (!obj) {
        sw_mgmt_log(WARNING, _("Software Management object not installed"));
    } else {
        sw_mgmt_instance_t *inst = find_instance(obj, iid);
        if (!inst) {
            sw_mgmt_log_inst(ERROR, iid, _("instance not found"));
        } else {
            if (internal_state_is_installed(inst->internal_state)) {
                result = 0;
                *out_state = inst->internal_state
                             == SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED;
            }
        }
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_sw_mgmt_finish_pkg_install(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        anjay_sw_mgmt_finish_pkg_install_result_t pkg_install_result) {
    assert(anjay_locked);
    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    if (!obj) {
        sw_mgmt_log(WARNING, _("Software Management object not installed"));
    } else {
        sw_mgmt_instance_t *inst = find_instance(obj, iid);
        if (!inst) {
            sw_mgmt_log_inst(ERROR, iid, _("instance not found"));
        } else {
            if (inst->internal_state != SW_MGMT_INTERNAL_STATE_INSTALLING) {
                sw_mgmt_log_inst(
                        ERROR, inst->iid,
                        _("anjay_sw_mgmt_finish_pkg_install may be "
                          "only called when an installation was scheduled"));
            } else if (inst->install_and_integrity_jobs_handle) {
                sw_mgmt_log_inst(ERROR, inst->iid,
                                 _("cannot set installation result before "
                                   "execution of install handler"));
            } else {
                sw_mgmt_internal_state_t state = SW_MGMT_INTERNAL_STATE_IDLE;

                switch (pkg_install_result) {
                case ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_INACTIVE:
                    state = SW_MGMT_INTERNAL_STATE_INSTALLED_DEACTIVATED;
                    break;
                case ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_ACTIVE:
                    state = SW_MGMT_INTERNAL_STATE_INSTALLED_ACTIVATED;
                    break;
                case ANJAY_SW_MGMT_FINISH_PKG_INSTALL_FAILURE:
                    state = SW_MGMT_INTERNAL_STATE_DELIVERED;
                    break;
                default:
                    sw_mgmt_log_inst(ERROR, inst->iid,
                                     _("wrong package install result passed to "
                                       "anjay_sw_mgmt_finish_pkg_install"));
                }

                if (state != SW_MGMT_INTERNAL_STATE_IDLE) {
                    change_internal_state_and_update_result(
                            anjay, inst, state,
                            state == SW_MGMT_INTERNAL_STATE_DELIVERED
                                    ? ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLATION_FAILURE
                                    : ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLED);

                    result = 0;
                }
            }
        }
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_sw_mgmt_add_instance(
        anjay_t *anjay_locked,
        const anjay_sw_mgmt_instance_initializer_t *instance_initializer) {

    assert(anjay_locked);
    assert(instance_initializer);
    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    if (!obj) {
        sw_mgmt_log(WARNING, _("Software Management object not installed"));
    } else {
        anjay_iid_t iid = instance_initializer->iid;
        if (find_instance(obj, iid)) {
            sw_mgmt_log_inst(ERROR, iid, _("instance already in use"));
        } else {
            AVS_LIST(sw_mgmt_instance_t) created =
                    AVS_LIST_NEW_ELEMENT(sw_mgmt_instance_t);

            if (!created) {
                _anjay_log_oom();
            } else {
                initialize_instance(created, instance_initializer);
                insert_instance(obj, created);
                _anjay_notify_instances_changed_unlocked(anjay, OID);
                if (created->internal_state
                        == SW_MGMT_INTERNAL_STATE_DOWNLOADED) {
                    if (obj->handlers->check_integrity) {
                        ensure_not_stalled_in_downloaded_state(anjay, obj,
                                                               created);
                    } else {
                        change_internal_state_and_update_result(
                                anjay, created,
                                SW_MGMT_INTERNAL_STATE_DELIVERED,
                                ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL);
                    }
                }
                result = 0;
            }

            if (result) {
                AVS_LIST_CLEAR(&created);
            }
        }
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_sw_mgmt_remove_instance(anjay_t *anjay_locked, anjay_iid_t iid) {
    assert(anjay_locked);
    int result = -1;

    ANJAY_MUTEX_LOCK(anjay, anjay_locked);

    sw_mgmt_object_t *obj =
            (sw_mgmt_object_t *) _anjay_dm_module_get_arg(anjay,
                                                          sw_mgmt_delete);
    if (!obj) {
        sw_mgmt_log(WARNING, _("Software Management object not installed"));
    } else {
        sw_mgmt_instance_t *inst = find_instance(obj, iid);
        if (!inst) {
            sw_mgmt_log_inst(ERROR, iid, _("instance not found"));
        } else if (inst->cannot_delete) {
            sw_mgmt_log_inst(ERROR, iid,
                             _("some callback associated with this instance is "
                               "currently being executed"));
            result = 1;
        } else {
            result = delete_instance(anjay, obj, iid);
            if (!result) {
                _anjay_notify_instances_changed_unlocked(anjay, OID);
            }
        }
    }

    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#endif // ANJAY_WITH_MODULE_SW_MGMT
