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

#include <anjay/download.h>
#include <anjay/fw_update.h>

#include <anjay_modules/dm/modules.h>
#include <anjay_modules/downloader.h>
#include <anjay_modules/io_utils.h>
#include <anjay_modules/sched.h>
#include <anjay_modules/utils_core.h>

#include <avsystem/commons/errno.h>
#include <avsystem/commons/utils.h>

VISIBILITY_SOURCE_BEGIN

#define fw_log(level, ...) _anjay_log(fw_update, level, __VA_ARGS__)

#define FW_OID 5

#define FW_RES_PACKAGE 0
#define FW_RES_PACKAGE_URI 1
#define FW_RES_UPDATE 2
#define FW_RES_STATE 3
#define FW_RES_UPDATE_RESULT 5
#define FW_RES_PKG_NAME 6
#define FW_RES_PKG_VERSION 7
#define FW_RES_UPDATE_PROTOCOL_SUPPORT 8
#define FW_RES_UPDATE_DELIVERY_METHOD 9

typedef enum {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_DOWNLOADED,
    UPDATE_STATE_UPDATING
} fw_update_state_t;

typedef enum {
    UPDATE_RESULT_INITIAL = 0,
    UPDATE_RESULT_SUCCESS = 1,
    UPDATE_RESULT_NOT_ENOUGH_SPACE = 2,
    UPDATE_RESULT_OUT_OF_MEMORY = 3,
    UPDATE_RESULT_CONNECTION_LOST = 4,
    UPDATE_RESULT_INTEGRITY_FAILURE = 5,
    UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE = 6,
    UPDATE_RESULT_INVALID_URI = 7,
    UPDATE_RESULT_FAILED = 8,
    UPDATE_RESULT_UNSUPPORTED_PROTOCOL = 9
} fw_update_result_t;

typedef struct {
    const anjay_fw_update_handlers_t *handlers;
    void *arg;
    fw_update_state_t state;
} fw_user_state_t;

typedef struct fw_repr {
    const anjay_dm_object_def_t *def;

    fw_user_state_t user_state;
    avs_net_security_info_t *security_from_dm;

    fw_update_state_t state;
    fw_update_result_t result;
    const char *package_uri;
    bool retry_download_on_expired;
    anjay_sched_handle_t update_job;
} fw_repr_t;

static inline fw_repr_t *get_fw(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, fw_repr_t, def);
}

static int
user_state_ensure_stream_open(fw_user_state_t *user,
                              const char *package_uri,
                              const struct anjay_etag *package_etag) {
    if (user->state == UPDATE_STATE_DOWNLOADING) {
        return 0;
    }
    assert(user->state == UPDATE_STATE_IDLE);
    int result =
            user->handlers->stream_open(user->arg, package_uri, package_etag);
    if (!result) {
        user->state = UPDATE_STATE_DOWNLOADING;
    }
    return result;
}

static int user_state_stream_write(fw_user_state_t *user,
                                   const void *data,
                                   size_t length) {
    assert(user->state == UPDATE_STATE_DOWNLOADING);
    return user->handlers->stream_write(user->arg, data, length);
}

static const char *user_state_get_name(fw_user_state_t *user) {
    if (!user->handlers->get_name || user->state != UPDATE_STATE_DOWNLOADED) {
        return NULL;
    }
    return user->handlers->get_name(user->arg);
}

static const char *user_state_get_version(fw_user_state_t *user) {
    if (!user->handlers->get_version
            || user->state != UPDATE_STATE_DOWNLOADED) {
        return NULL;
    }
    return user->handlers->get_version(user->arg);
}

static int user_state_perform_upgrade(fw_user_state_t *user) {
    assert(user->state == UPDATE_STATE_DOWNLOADED);
    int result = user->handlers->perform_upgrade(user->arg);
    if (!result) {
        user->state = UPDATE_STATE_UPDATING;
    }
    return result;
}

static int finish_user_stream(fw_repr_t *fw) {
    assert(fw->user_state.state == UPDATE_STATE_DOWNLOADING);
    int result = fw->user_state.handlers->stream_finish(fw->user_state.arg);
    if (result) {
        fw->user_state.state = UPDATE_STATE_IDLE;
        avs_free(fw->security_from_dm);
        fw->security_from_dm = NULL;
    } else {
        fw->user_state.state = UPDATE_STATE_DOWNLOADED;
    }
    return result;
}

static void reset_user_state(fw_repr_t *fw) {
    fw->user_state.handlers->reset(fw->user_state.arg);
    fw->user_state.state = UPDATE_STATE_IDLE;
    avs_free(fw->security_from_dm);
    fw->security_from_dm = NULL;
}

static int get_security_info(anjay_t *anjay,
                             fw_repr_t *fw,
                             avs_net_security_info_t *out_security_info) {
    assert(fw->user_state.state == UPDATE_STATE_IDLE);
    if (fw->user_state.handlers->get_security_info) {
        return fw->user_state.handlers->get_security_info(
                fw->user_state.arg, out_security_info, fw->package_uri);
    } else {
        assert(!fw->security_from_dm);
        if (!(fw->security_from_dm = anjay_fw_update_load_security_from_dm(
                      anjay, fw->package_uri))) {
            return -1;
        }
        *out_security_info = *fw->security_from_dm;
        return 0;
    }
}

static int get_coap_tx_params(fw_repr_t *fw,
                              avs_coap_tx_params_t *out_tx_params) {
    if (fw->user_state.handlers->get_coap_tx_params) {
        *out_tx_params =
                fw->user_state.handlers->get_coap_tx_params(fw->user_state.arg,
                                                            fw->package_uri);
        return 0;
    }
    return -1;
}

static void set_update_result(anjay_t *anjay,
                              fw_repr_t *fw,
                              fw_update_result_t new_result) {
    if (fw->result != new_result) {
        fw->result = new_result;
        anjay_notify_changed(anjay, FW_OID, 0, FW_RES_UPDATE_RESULT);
    }
}

static void
set_state(anjay_t *anjay, fw_repr_t *fw, fw_update_state_t new_state) {
    if (fw->state != new_state) {
        fw->state = new_state;
        anjay_notify_changed(anjay, FW_OID, 0, FW_RES_STATE);
    }
}

static void handle_err_result(anjay_t *anjay,
                              fw_repr_t *fw,
                              fw_update_state_t new_state,
                              int result,
                              fw_update_result_t default_result) {
    fw_update_result_t new_result;
    switch (result) {
    case -UPDATE_RESULT_NOT_ENOUGH_SPACE:
    case -UPDATE_RESULT_OUT_OF_MEMORY:
    case -UPDATE_RESULT_INTEGRITY_FAILURE:
    case -UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE:
        new_result = (fw_update_result_t) -result;
        break;
    default:
        new_result = default_result;
    }
    set_state(anjay, fw, new_state);
    set_update_result(anjay, fw, new_result);
}

static void reset(anjay_t *anjay, fw_repr_t *fw) {
    reset_user_state(fw);
    set_state(anjay, fw, UPDATE_STATE_IDLE);
    set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
    fw_log(INFO, "Firmware Object state reset");
}

static int fw_res_present(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid) {
    (void) anjay;
    (void) iid;
    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_PKG_NAME:
        return user_state_get_name(&fw->user_state) != NULL;
    case FW_RES_PKG_VERSION:
        return user_state_get_version(&fw->user_state) != NULL;
    default:
        return 1;
    }
}

static int fw_read(anjay_t *anjay,
                   const anjay_dm_object_def_t *const *obj_ptr,
                   anjay_iid_t iid,
                   anjay_rid_t rid,
                   anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) iid;
    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_PACKAGE_URI:
        return anjay_ret_string(ctx, fw->package_uri ? fw->package_uri : "");
    case FW_RES_STATE:
        return anjay_ret_i32(ctx, (int32_t) fw->state);
    case FW_RES_UPDATE_RESULT:
        return anjay_ret_i32(ctx, (int32_t) fw->result);
    case FW_RES_PKG_NAME: {
        const char *name = user_state_get_name(&fw->user_state);
        if (name) {
            return anjay_ret_string(ctx, name);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case FW_RES_PKG_VERSION: {
        const char *version = user_state_get_version(&fw->user_state);
        if (version) {
            return anjay_ret_string(ctx, version);
        } else {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    case FW_RES_UPDATE_PROTOCOL_SUPPORT: {
        anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
        if (!array) {
            return ANJAY_ERR_INTERNAL;
        }
        static const int32_t SUPPORTED_PROTOCOLS[] = {
#ifdef WITH_BLOCK_DOWNLOAD
            0, /* CoAP */
            1, /* CoAPS */
#endif         // WITH_BLOCK_DOWNLOAD
#ifdef WITH_HTTP_DOWNLOAD
            2, /* HTTP 1.1 */
            3, /* HTTPS 1.1 */
#endif         // WITH_HTTP_DOWNLOAD
            -1
        };
        size_t index = 0;
        while (SUPPORTED_PROTOCOLS[index] >= 0) {
            if (anjay_ret_array_index(array, (anjay_riid_t) index)
                    || anjay_ret_i32(array, SUPPORTED_PROTOCOLS[index])) {
                anjay_ret_array_finish(array);
                return ANJAY_ERR_INTERNAL;
            }
            index++;
        }
        return anjay_ret_array_finish(array);
    }
    case FW_RES_UPDATE_DELIVERY_METHOD:
#ifdef WITH_BLOCK_RECEIVE
#    ifdef WITH_DOWNLOADER
        // 2 -> pull && push
        return anjay_ret_i32(ctx, 2);
#    else                      // WITH_DOWNLOADER
                               // 1 -> push only
        return anjay_ret_i32(ctx, 1);
#    endif                     // WITH_DOWNLOADER
#elif defined(WITH_DOWNLOADER) // WITH_BLOCK_RECEIVE
        // 0 -> pull only
        return anjay_ret_i32(ctx, 0);
#else                          // WITH_DOWNLOADER
#    error "Firmware Update requires at least WITH_DOWNLOADER or WITH_BLOCK_RECEIVE"
#endif // WITH_DOWNLOADER
    case FW_RES_PACKAGE:
    case FW_RES_UPDATE:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

#ifdef WITH_DOWNLOADER
static anjay_downloader_protocol_class_t classify_protocol(const char *uri) {
    char buf[6] = "";
    const char *colon = strchr(uri, ':');
    size_t proto_length = colon ? (size_t) (colon - uri) : 0;
    if (proto_length >= sizeof(buf)) {
        return ANJAY_DOWNLOADER_PROTO_UNSUPPORTED;
    }
    memcpy(buf, uri, proto_length);
    return _anjay_downloader_classify_protocol(buf);
}

static int download_write_block(anjay_t *anjay,
                                const uint8_t *data,
                                size_t data_size,
                                const anjay_etag_t *etag,
                                void *fw_) {
    (void) anjay;
    (void) etag;

    fw_repr_t *fw = (fw_repr_t *) fw_;
    int result = user_state_ensure_stream_open(&fw->user_state, fw->package_uri,
                                               etag);
    if (!result && data_size > 0) {
        result = user_state_stream_write(&fw->user_state, data, data_size);
    }
    if (result) {
        fw_log(ERROR, "could not write firmware");
        handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                          UPDATE_RESULT_NOT_ENOUGH_SPACE);
        return -1;
    }

    return 0;
}

static int schedule_background_anjay_download(anjay_t *anjay,
                                              fw_repr_t *fw,
                                              size_t start_offset,
                                              const anjay_etag_t *etag);

static void download_finished(anjay_t *anjay, int result, void *fw_) {
    (void) anjay;

    fw_repr_t *fw = (fw_repr_t *) fw_;
    if (fw->state != UPDATE_STATE_DOWNLOADING) {
        // something already failed in download_write_block()
        reset_user_state(fw);
    } else if (result) {
        fw_update_result_t update_result = UPDATE_RESULT_CONNECTION_LOST;
        if (errno == ENOMEM) {
            update_result = UPDATE_RESULT_OUT_OF_MEMORY;
        } else if (errno == EADDRNOTAVAIL) {
            update_result = UPDATE_RESULT_INVALID_URI;
        } else if (errno == ECONNREFUSED) {
            if (result == ANJAY_ERR_NOT_FOUND || result == 404) {
                update_result = UPDATE_RESULT_INVALID_URI;
            }
        }
        reset_user_state(fw);
        if (fw->retry_download_on_expired
                && result == ANJAY_DOWNLOAD_ERR_EXPIRED) {
            fw_log(INFO,
                   "Could not resume firmware download (result = %d), "
                   "retrying from the beginning",
                   result);
            if (schedule_background_anjay_download(anjay, fw, 0, NULL)) {
                fw_log(WARNING, "Could not retry firmware download");
                set_state(anjay, fw, UPDATE_STATE_IDLE);
            }
        } else {
            fw_log(ERROR, "download failed: result = %d", result);
            set_state(anjay, fw, UPDATE_STATE_IDLE);
            set_update_result(anjay, fw, update_result);
        }
    } else if ((result = user_state_ensure_stream_open(&fw->user_state,
                                                       fw->package_uri, NULL))
               || (result = finish_user_stream(fw))) {
        handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                          UPDATE_RESULT_NOT_ENOUGH_SPACE);
    } else {
        set_state(anjay, fw, UPDATE_STATE_DOWNLOADED);
        set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
    }
}

static int schedule_background_anjay_download(anjay_t *anjay,
                                              fw_repr_t *fw,
                                              size_t start_offset,
                                              const anjay_etag_t *etag) {
    anjay_download_config_t cfg = {
        .url = fw->package_uri,
        .start_offset = start_offset,
        .etag = etag,
        .on_next_block = download_write_block,
        .on_download_finished = download_finished,
        .user_data = fw
    };

    if (classify_protocol(fw->package_uri)
            == ANJAY_DOWNLOADER_PROTO_ENCRYPTED) {
        int result = get_security_info(anjay, fw, &cfg.security_info);
        if (result) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                              UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
            return -1;
        }
    }

    avs_coap_tx_params_t tx_params;
    if (!get_coap_tx_params(fw, &tx_params)) {
        cfg.coap_tx_params = &tx_params;
    }

    anjay_download_handle_t handle = anjay_download(anjay, &cfg);
    if (!handle) {
        fw_update_result_t update_result;
        if (errno == EADDRNOTAVAIL || errno == EINVAL) {
            update_result = UPDATE_RESULT_INVALID_URI;
        } else if (errno == ENOMEM) {
            update_result = UPDATE_RESULT_OUT_OF_MEMORY;
        } else if (errno == EPROTONOSUPPORT) {
            update_result = UPDATE_RESULT_UNSUPPORTED_PROTOCOL;
        } else {
            update_result = UPDATE_RESULT_CONNECTION_LOST;
        }
        reset_user_state(fw);
        set_update_result(anjay, fw, update_result);
        return -1;
    }

    fw->retry_download_on_expired = (etag != NULL);
    set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
    set_state(anjay, fw, UPDATE_STATE_DOWNLOADING);
    fw_log(INFO, "download started: %s", fw->package_uri);
    return 0;
}
#endif // WITH_DOWNLOADER

static int write_firmware_to_stream(anjay_t *anjay,
                                    fw_repr_t *fw,
                                    anjay_input_ctx_t *ctx,
                                    bool *out_is_reset_request) {
    int result = 0;
    size_t written = 0;
    bool finished = false;
    int first_byte = EOF;

    *out_is_reset_request = false;
    while (!finished) {
        size_t bytes_read;
        char buffer[1024];
        if ((result = anjay_get_bytes(ctx, &bytes_read, &finished, buffer,
                                      sizeof(buffer)))) {
            fw_log(ERROR, "anjay_get_bytes() failed");

            set_state(anjay, fw, UPDATE_STATE_IDLE);
            set_update_result(anjay, fw, UPDATE_RESULT_CONNECTION_LOST);
            return result;
        }

        if (bytes_read > 0) {
            if (first_byte == EOF) {
                first_byte = (unsigned char) buffer[0];
            }
            result = user_state_stream_write(&fw->user_state, buffer,
                                             bytes_read);
        }
        if (result) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE, result,
                              UPDATE_RESULT_NOT_ENOUGH_SPACE);
            return ANJAY_ERR_INTERNAL;
        }
        written += bytes_read;
    }

    // FU object may be reset either by writing a single nullbyte to Package
    // resource or setting it to an empty value.
    *out_is_reset_request = (written == 1 && first_byte == '\0');

    fw_log(INFO, "write finished, %lu B written", (unsigned long) written);
    return 0;
}

static int expect_single_nullbyte(anjay_input_ctx_t *ctx) {
    char bytes[2];
    size_t bytes_read;
    bool finished = false;
    if (anjay_get_bytes(ctx, &bytes_read, &finished, bytes, sizeof(bytes))) {
        fw_log(ERROR, "anjay_get_bytes() failed");
        return ANJAY_ERR_INTERNAL;
    } else if (bytes_read != 1 || !finished || bytes[0] != '\0') {
        return ANJAY_ERR_BAD_REQUEST;
    }
    return 0;
}

static int write_firmware(anjay_t *anjay,
                          fw_repr_t *fw,
                          anjay_input_ctx_t *ctx,
                          bool *out_is_reset_request) {
    if (fw->state == UPDATE_STATE_DOWNLOADING) {
        fw_log(ERROR, "cannot set Package resource while downloading");
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }

    if (user_state_ensure_stream_open(&fw->user_state, NULL, NULL)) {
        return -1;
    }

    int result = write_firmware_to_stream(anjay, fw, ctx, out_is_reset_request);
    if (result) {
        reset_user_state(fw);
    } else if (!*out_is_reset_request) {
        // stream_finish_result deliberately not propagated up:
        // write itself succeeded
        int stream_finish_result = finish_user_stream(fw);
        if (stream_finish_result) {
            handle_err_result(anjay, fw, UPDATE_STATE_IDLE,
                              stream_finish_result,
                              UPDATE_RESULT_NOT_ENOUGH_SPACE);
        } else {
            set_state(anjay, fw, UPDATE_STATE_DOWNLOADED);
            set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
        }
    }
    return result;
}

static int fw_write(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj_ptr,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    anjay_input_ctx_t *ctx) {
    (void) iid;

    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_PACKAGE: {
        int result = 0;
        if (fw->state == UPDATE_STATE_DOWNLOADED) {
            result = expect_single_nullbyte(ctx);
            if (!result) {
                reset(anjay, fw);
            }
        } else {
            bool is_reset_request = false;
            result = write_firmware(anjay, fw, ctx, &is_reset_request);
            if (!result && is_reset_request) {
                reset(anjay, fw);
            }
        }
        return result;
    }
    case FW_RES_PACKAGE_URI:
#ifdef WITH_DOWNLOADER
    {
        if (fw->state == UPDATE_STATE_DOWNLOADING) {
            fw_log(ERROR, "cannot set Package Uri resource while downloading");
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        char *new_uri = NULL;
        int result = _anjay_io_fetch_string(ctx, &new_uri);
        size_t len = (new_uri ? strlen(new_uri) : 0);

        if (!result && len > 0 && fw->state != UPDATE_STATE_IDLE) {
            result = ANJAY_ERR_BAD_REQUEST;
        }

        if (!result && len > 0
                && classify_protocol(new_uri)
                               == ANJAY_DOWNLOADER_PROTO_UNSUPPORTED) {
            fw_log(ERROR,
                   "unsupported download protocol required for uri %s",
                   new_uri);
            set_update_result(anjay, fw, UPDATE_RESULT_UNSUPPORTED_PROTOCOL);
            result = ANJAY_ERR_BAD_REQUEST;
        }

        if (!result) {
            avs_free((void *) (intptr_t) fw->package_uri);
            fw->package_uri = new_uri;

            if (len == 0) {
                reset(anjay, fw);
            } else {
                int dl_res =
                        schedule_background_anjay_download(anjay, fw, 0, NULL);
                if (dl_res) {
                    fw_log(WARNING,
                           "schedule_download_in_background failed: %d",
                           dl_res);
                }
                // write itself succeeded; do not propagate error
            }
        } else {
            avs_free(new_uri);
        }

        return result;
    }
#endif // WITH_DOWNLOADER
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static void perform_upgrade(anjay_t *anjay, const void *fw_ptr) {
    fw_repr_t *fw = *(fw_repr_t *const *) fw_ptr;

    int result = user_state_perform_upgrade(&fw->user_state);
    if (result) {
        fw_log(ERROR, "user_state_perform_upgrade() failed: %d", result);
        handle_err_result(anjay, fw, UPDATE_STATE_DOWNLOADED, result,
                          UPDATE_RESULT_FAILED);
    }
}

static int fw_execute(anjay_t *anjay,
                      const anjay_dm_object_def_t *const *obj_ptr,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_execute_ctx_t *ctx) {
    (void) iid;
    (void) ctx;

    fw_repr_t *fw = get_fw(obj_ptr);
    switch (rid) {
    case FW_RES_UPDATE: {
        if (fw->state != UPDATE_STATE_DOWNLOADED) {
            fw_log(WARNING,
                   "Firmware Update requested, but firmware not yet downloaded "
                   "(state = %d)",
                   fw->state);
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }

        set_state(anjay, fw, UPDATE_STATE_UPDATING);
        set_update_result(anjay, fw, UPDATE_RESULT_INITIAL);
        // update process will be continued in fw_on_notify
        return 0;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static const anjay_dm_object_def_t FIRMWARE_UPDATE = {
    .oid = FW_OID,
    .supported_rids = ANJAY_DM_SUPPORTED_RIDS(FW_RES_PACKAGE,
                                              FW_RES_PACKAGE_URI,
                                              FW_RES_UPDATE,
                                              FW_RES_STATE,
                                              FW_RES_UPDATE_RESULT,
                                              FW_RES_PKG_NAME,
                                              FW_RES_PKG_VERSION,
                                              FW_RES_UPDATE_PROTOCOL_SUPPORT,
                                              FW_RES_UPDATE_DELIVERY_METHOD),
    .handlers = {
        .instance_it = anjay_dm_instance_it_SINGLE,
        .instance_present = anjay_dm_instance_present_SINGLE,
        .resource_present = fw_res_present,
        .resource_read = fw_read,
        .resource_write = fw_write,
        .resource_execute = fw_execute,
        .transaction_begin = anjay_dm_transaction_NOOP,
        .transaction_validate = anjay_dm_transaction_NOOP,
        .transaction_commit = anjay_dm_transaction_NOOP,
        .transaction_rollback = anjay_dm_transaction_NOOP
    }
};

static int
fw_on_notify(anjay_t *anjay, anjay_notify_queue_t incoming_queue, void *fw_) {
    fw_repr_t *fw = (fw_repr_t *) fw_;
    (void) incoming_queue;
    if (!fw->update_job && fw->state == UPDATE_STATE_UPDATING
            && _anjay_sched_now(_anjay_sched_get(anjay), &fw->update_job,
                                perform_upgrade, &fw, sizeof(fw))) {
        // we don't need to reschedule notifying,
        // we're already in the middle of it
        fw->state = UPDATE_STATE_DOWNLOADED;
        fw->result = UPDATE_RESULT_OUT_OF_MEMORY;
    }
    return 0;
}

static void fw_delete(anjay_t *anjay, void *fw_) {
    (void) anjay;
    fw_repr_t *fw = (fw_repr_t *) fw_;
    _anjay_sched_del(_anjay_sched_get(anjay), &fw->update_job);
    avs_free(fw->security_from_dm);
    avs_free((void *) (intptr_t) fw->package_uri);
    avs_free(fw);
}

static const anjay_dm_module_t FIRMWARE_UPDATE_MODULE = {
    .notify_callback = fw_on_notify,
    .deleter = fw_delete
};

static int
initialize_fw_repr(anjay_t *anjay,
                   fw_repr_t *repr,
                   const anjay_fw_update_initial_state_t *initial_state) {
    if (!initial_state) {
        return 0;
    }
    switch (initial_state->result) {
    case ANJAY_FW_UPDATE_INITIAL_DOWNLOADED:
        if (initial_state->persisted_uri
                && !(repr->package_uri =
                             avs_strdup(initial_state->persisted_uri))) {
            fw_log(WARNING, "Could not copy the persisted Package URI");
        }
        repr->user_state.state = UPDATE_STATE_DOWNLOADED;
        repr->state = UPDATE_STATE_DOWNLOADED;
        return 0;
    case ANJAY_FW_UPDATE_INITIAL_DOWNLOADING: {
#ifdef WITH_DOWNLOADER
        repr->user_state.state = UPDATE_STATE_DOWNLOADING;
        size_t resume_offset = initial_state->resume_offset;
        if (resume_offset > 0 && !initial_state->resume_etag) {
            fw_log(WARNING, "ETag not set, need to start from the beginning");
            reset_user_state(repr);
            resume_offset = 0;
        }
        if (!initial_state->persisted_uri
                || !(repr->package_uri =
                             avs_strdup(initial_state->persisted_uri))) {
            fw_log(WARNING, "Could not copy the persisted Package URI, not "
                            "resuming firmware download");
            reset_user_state(repr);
        } else if (schedule_background_anjay_download(
                           anjay, repr, resume_offset,
                           initial_state->resume_etag)) {
            fw_log(WARNING, "Could not resume firmware download");
            reset_user_state(repr);
            if (repr->result == UPDATE_RESULT_CONNECTION_LOST
                    && initial_state->resume_etag
                    && schedule_background_anjay_download(anjay, repr, 0,
                                                          NULL)) {
                fw_log(WARNING, "Could not retry firmware download");
            }
        }
#else  // WITH_DOWNLOADER
        fw_log(WARNING,
               "Unable to resume download: PULL download not supported");
#endif // WITH_DOWNLOADER
        return 0;
    }
    case ANJAY_FW_UPDATE_INITIAL_NEUTRAL:
    case ANJAY_FW_UPDATE_INITIAL_SUCCESS:
    case ANJAY_FW_UPDATE_INITIAL_INTEGRITY_FAILURE:
    case ANJAY_FW_UPDATE_INITIAL_FAILED:
        repr->result = (fw_update_result_t) initial_state->result;
        return 0;
    default:
        fw_log(ERROR, "Invalid initial_state->result");
        return -1;
    }
}

int anjay_fw_update_install(
        anjay_t *anjay,
        const anjay_fw_update_handlers_t *handlers,
        void *user_arg,
        const anjay_fw_update_initial_state_t *initial_state) {
    assert(anjay);

    fw_repr_t *repr = (fw_repr_t *) avs_calloc(1, sizeof(fw_repr_t));
    if (!repr) {
        fw_log(ERROR, "Out of memory");
        return -1;
    }

    repr->def = &FIRMWARE_UPDATE;
    repr->user_state.handlers = handlers;
    repr->user_state.arg = user_arg;

    if (initialize_fw_repr(anjay, repr, initial_state)
            || _anjay_dm_module_install(anjay, &FIRMWARE_UPDATE_MODULE, repr)) {
        avs_free(repr);
        return -1;
    }

    if (anjay_register_object(anjay, &repr->def)) {
        // this will free repr
        int result = _anjay_dm_module_uninstall(anjay, &FIRMWARE_UPDATE_MODULE);
        assert(!result);
        (void) result;
        return -1;
    }

    return 0;
}
