/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "firmware_update.h"
#include "demo.h"
#include "demo_utils.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <avsystem/commons/avs_persistence.h>

#include <avsystem/commons/avs_memory.h>
#include <avsystem/commons/avs_stream_file.h>
#include <avsystem/commons/avs_utils.h>

#define FORCE_ERROR_OUT_OF_MEMORY 1
#define FORCE_ERROR_FAILED_UPDATE 2
#define FORCE_DELAYED_SUCCESS 3
#define FORCE_DELAYED_ERROR_FAILED_UPDATE 4
#define FORCE_SET_SUCCESS_FROM_PERFORM_UPGRADE 5
#define FORCE_SET_FAILURE_FROM_PERFORM_UPGRADE 6
#define FORCE_DO_NOTHING 7

static int maybe_create_firmware_file(fw_update_logic_t *fw) {
    if (fw->next_target_path) {
        return 0;
    }
    if (fw->administratively_set_target_path) {
        fw->next_target_path = avs_strdup(fw->administratively_set_target_path);
    } else {
        fw->next_target_path = generate_random_target_filepath();
    }
    if (!fw->next_target_path) {
        return -1;
    }
    demo_log(INFO, "Created %s", fw->next_target_path);
    return 0;
}

static void maybe_delete_firmware_file(fw_update_logic_t *fw) {
    if (fw->next_target_path) {
        unlink(fw->next_target_path);
        demo_log(INFO, "Deleted %s", fw->next_target_path);
        avs_free(fw->next_target_path);
        fw->next_target_path = NULL;
    }
}

void firmware_update_set_package_path(fw_update_logic_t *fw, const char *path) {
    if (fw->stream) {
        demo_log(ERROR,
                 "cannot set package path while a download is in progress");
        return;
    }
    char *new_target_path = avs_strdup(path);
    if (!new_target_path) {
        demo_log(ERROR, "out of memory");
        return;
    }

    avs_free(fw->administratively_set_target_path);
    fw->administratively_set_target_path = new_target_path;
    demo_log(INFO, "firmware package path set to %s",
             fw->administratively_set_target_path);
}

static void fix_fw_meta_endianness(fw_metadata_t *meta) {
    meta->version = avs_convert_be16(meta->version);
    meta->force_error_case = avs_convert_be16(meta->force_error_case);
    meta->crc = avs_convert_be32(meta->crc);
}

static int read_fw_meta_from_file(FILE *f, fw_metadata_t *out_metadata) {
    fw_metadata_t m;
    memset(&m, 0, sizeof(m));

    if (fread(m.magic, sizeof(m.magic), 1, f) != 1
            || fread(&m.version, sizeof(m.version), 1, f) != 1
            || fread(&m.force_error_case, sizeof(m.force_error_case), 1, f) != 1
            || fread(&m.crc, sizeof(m.crc), 1, f) != 1) {
        demo_log(ERROR, "could not read firmware metadata");
        return -1;
    }

    fix_fw_meta_endianness(&m);
    *out_metadata = m;
    return 0;
}

static int unpack_fw_to_file(const char *fw_pkg_path,
                             const char *target_path,
                             fw_metadata_t *out_metadata) {
    int result = -1;
    FILE *fw = fopen(fw_pkg_path, "rb");
    FILE *tmp = NULL;

    if (!fw) {
        demo_log(ERROR, "could not open file: %s", fw_pkg_path);
        goto cleanup;
    }

    tmp = fopen(target_path, "wb");
    if (!tmp) {
        demo_log(ERROR, "could not open file: %s", target_path);
        goto cleanup;
    }

    result = read_fw_meta_from_file(fw, out_metadata);
    if (result) {
        demo_log(ERROR, "could not read metadata from file: %s", fw_pkg_path);
        goto cleanup;
    }
    result = copy_file_contents(tmp, fw);
    if (result) {
        demo_log(ERROR, "could not copy firmware from %s to %s", fw_pkg_path,
                 target_path);
        goto cleanup;
    }

    result = 0;

cleanup:
    if (fw) {
        fclose(fw);
    }
    if (tmp) {
        fclose(tmp);
    }
    return result;
}

static int unpack_firmware_in_place(fw_update_logic_t *fw) {
    char *tmp_path = generate_random_target_filepath();
    if (!tmp_path) {
        return -1;
    }

    int result =
            unpack_fw_to_file(fw->next_target_path, tmp_path, &fw->metadata);
    if (result) {
        goto cleanup;
    }

    if ((result = rename(tmp_path, fw->next_target_path)) == -1) {
        demo_log(ERROR, "could not rename %s to %s: %s", tmp_path,
                 fw->next_target_path, strerror(errno));
        goto cleanup;
    }
    if ((result = chmod(fw->next_target_path, 0700)) == -1) {
        demo_log(ERROR, "could not set permissions for %s: %s",
                 fw->next_target_path, strerror(errno));
        goto cleanup;
    }

cleanup:
    unlink(tmp_path);
    avs_free(tmp_path);
    if (result) {
        maybe_delete_firmware_file(fw);
    }

    return result;
}

static bool fw_magic_valid(const fw_metadata_t *meta) {
    if (memcmp(meta->magic, "ANJAY_FW", sizeof(meta->magic))) {
        demo_log(ERROR, "invalid firmware magic");
        return false;
    }

    return true;
}

static bool fw_version_supported(const fw_metadata_t *meta) {
    if (meta->version != 1) {
        demo_log(ERROR, "unsupported firmware version: %u", meta->version);
        return false;
    }

    return true;
}

static int validate_firmware(fw_update_logic_t *fw) {
    if (!fw_magic_valid(&fw->metadata)
            || !fw_version_supported(&fw->metadata)) {
        return ANJAY_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE;
    }

    uint32_t actual_crc;
    int result = calc_file_crc32(fw->next_target_path, &actual_crc);

    if (result) {
        demo_log(WARNING, "unable to check firmware CRC");
        return ANJAY_FW_UPDATE_ERR_INTEGRITY_FAILURE;
    }

    if (fw->metadata.crc != actual_crc) {
        demo_log(WARNING, "CRC mismatch: expected %08x != %08x actual",
                 fw->metadata.crc, actual_crc);
        return ANJAY_FW_UPDATE_ERR_INTEGRITY_FAILURE;
    }

    switch (fw->metadata.force_error_case) {
    case FORCE_ERROR_OUT_OF_MEMORY:
        return ANJAY_FW_UPDATE_ERR_OUT_OF_MEMORY;
    default:
        break;
    }

    return 0;
}

static int preprocess_firmware(fw_update_logic_t *fw) {
    if (unpack_firmware_in_place(fw)) {
        return ANJAY_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE;
    }

    int result = validate_firmware(fw);
    if (!result) {
        demo_log(INFO, "firmware downloaded successfully");
    }
    return result;
}

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)

static int write_persistence_file(const char *path,
                                  anjay_fw_update_initial_result_t result,
                                  const char *uri,
                                  char *download_file,
                                  bool filename_administratively_set,
                                  const anjay_etag_t *etag) {
    avs_stream_t *stream = avs_stream_file_create(path, AVS_STREAM_FILE_WRITE);
    avs_persistence_context_t ctx =
            avs_persistence_store_context_create(stream);
    int8_t result8 = (int8_t) result;
    int retval = 0;
    if (!stream
            || avs_is_err(avs_persistence_bytes(&ctx, (uint8_t *) &result8, 1))
            || avs_is_err(
                       avs_persistence_string(&ctx, (char **) (intptr_t) &uri))
            || avs_is_err(avs_persistence_string(&ctx, &download_file))
            || avs_is_err(avs_persistence_bool(&ctx,
                                               &filename_administratively_set))
            || avs_is_err(store_etag(&ctx, etag))) {
        demo_log(ERROR, "Could not write firmware state persistence file");
        retval = -1;
    }
    if (stream) {
        avs_stream_cleanup(&stream);
    }
    if (retval) {
        unlink(path);
    }
    return retval;
}

static void delete_persistence_file(const fw_update_logic_t *fw) {
    unlink(fw->persistence_file);
}
#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
static int write_persistence_file(const char *path,
                                  anjay_fw_update_initial_result_t result,
                                  const char *uri,
                                  char *download_file,
                                  bool filename_administratively_set,
                                  const anjay_etag_t *etag) {
    (void) path;
    (void) result;
    (void) uri;
    (void) download_file;
    (void) filename_administratively_set;
    (void) etag;
    demo_log(WARNING, "Persistence not compiled in");
    return 0;
}

static void delete_persistence_file(const fw_update_logic_t *fw) {
    (void) fw;
    demo_log(WARNING, "Persistence not compiled in");
}
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

static void fw_reset(void *fw_) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;
    if (fw->stream) {
        fclose(fw->stream);
        fw->stream = NULL;
    }
    avs_free(fw->package_uri);
    fw->package_uri = NULL;
    maybe_delete_firmware_file(fw);
    delete_persistence_file(fw);
    if (fw->auto_suspend) {
        anjay_fw_update_pull_suspend(fw->anjay);
    }
}

static int fw_stream_open(void *fw_,
                          const char *package_uri,
                          const struct anjay_etag *package_etag) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;

    assert(!fw->stream);

    char *uri = NULL;
    if (package_uri && !(uri = avs_strdup(package_uri))) {
        demo_log(ERROR, "Out of memory");
        return -1;
    }

    if (maybe_create_firmware_file(fw)) {
        avs_free(uri);
        return -1;
    }

    if (!(fw->stream = fopen(fw->next_target_path, "wb"))) {
        demo_log(ERROR, "could not open file: %s", fw->next_target_path);
        avs_free(uri);
        return -1;
    }

    avs_free(fw->package_uri);
    fw->package_uri = uri;
    if (write_persistence_file(
                fw->persistence_file, ANJAY_FW_UPDATE_INITIAL_DOWNLOADING,
                package_uri, fw->next_target_path,
                !!fw->administratively_set_target_path, package_etag)) {
        fw_reset(fw_);
        return -1;
    }

    return 0;
}

static int fw_stream_write(void *fw_, const void *data, size_t length) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;
    if (!fw->stream) {
        demo_log(ERROR, "stream not open");
        return -1;
    }
    if (length
            && (fwrite(data, length, 1, fw->stream) != 1
                // Firmware update integration tests measure download
                // progress by checking file size, so avoiding buffering
                // is required.
                || fflush(fw->stream) != 0)) {
        demo_log(ERROR, "fwrite or fflush failed: %s", strerror(errno));
        return ANJAY_FW_UPDATE_ERR_NOT_ENOUGH_SPACE;
    }

    return 0;
}

static int fw_stream_finish(void *fw_) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;
    if (fw->auto_suspend) {
        anjay_fw_update_pull_suspend(fw->anjay);
    }
    if (!fw->stream) {
        demo_log(ERROR, "stream not open");
        return -1;
    }
    fclose(fw->stream);
    fw->stream = NULL;

    int result;
    if ((result = preprocess_firmware(fw))
            || (result = write_persistence_file(
                        fw->persistence_file,
                        ANJAY_FW_UPDATE_INITIAL_DOWNLOADED, fw->package_uri,
                        fw->next_target_path,
                        !!fw->administratively_set_target_path, NULL))) {
        fw_reset(fw);
    }
    return result;
}

static const char *fw_get_name(void *fw) {
    (void) fw;
    return "Cute Firmware";
}

static const char *fw_get_version(void *fw) {
    (void) fw;
    return "1.0";
}

static int fw_perform_upgrade(void *fw_) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;

    if (write_persistence_file(fw->persistence_file,
                               ANJAY_FW_UPDATE_INITIAL_SUCCESS, NULL,
                               fw->next_target_path,
                               !!fw->administratively_set_target_path, NULL)) {
        delete_persistence_file(fw);
        return -1;
    }

    demo_log(INFO, "*** FIRMWARE UPDATE: %s ***", fw->next_target_path);
    switch (fw->metadata.force_error_case) {
    case FORCE_ERROR_FAILED_UPDATE:
        demo_log(ERROR, "update failed");
        delete_persistence_file(fw);
        return -1;
    case FORCE_DELAYED_SUCCESS:
        if (argv_append("--delayed-upgrade-result") || argv_append("1")) {
            demo_log(ERROR, "could not append delayed result to argv");
            return -1;
        }
        break;
    case FORCE_DELAYED_ERROR_FAILED_UPDATE:
        if (argv_append("--delayed-upgrade-result") || argv_append("8")) {
            demo_log(ERROR, "could not append delayed result to argv");
            return -1;
        }
        break;
    case FORCE_SET_SUCCESS_FROM_PERFORM_UPGRADE:
        if (anjay_fw_update_set_result(fw->anjay,
                                       ANJAY_FW_UPDATE_RESULT_SUCCESS)) {
            demo_log(ERROR, "anjay_fw_update_set_result failed");
            return -1;
        }
        return 0;
    case FORCE_SET_FAILURE_FROM_PERFORM_UPGRADE:
        if (anjay_fw_update_set_result(fw->anjay,
                                       ANJAY_FW_UPDATE_RESULT_FAILED)) {
            demo_log(ERROR, "anjay_fw_update_set_result failed");
            return -1;
        }
        return 0;
    case FORCE_DO_NOTHING:
        return 0;
    default:
        break;
    }

    execv(fw->next_target_path, argv_get());

    demo_log(ERROR, "execv failed (%s)", strerror(errno));
    delete_persistence_file(fw);
    return -1;
}

static int fw_get_security_config(void *fw_,
                                  anjay_security_config_t *out_security_config,
                                  const char *download_uri) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;
    (void) download_uri;
    memset(out_security_config, 0, sizeof(*out_security_config));
    out_security_config->security_info = fw->security_info;
    return 0;
}

static avs_coap_udp_tx_params_t
fw_get_coap_tx_params(void *fw_, const char *download_uri) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;
    (void) download_uri;
    if (fw->auto_suspend) {
        anjay_fw_update_pull_reconnect(fw->anjay);
    }
    return fw->coap_tx_params;
}

static avs_time_duration_t
fw_get_tcp_request_timeout(void *fw_, const char *download_uri) {
    fw_update_logic_t *fw = (fw_update_logic_t *) fw_;
    (void) download_uri;
    return fw->tcp_request_timeout;
}

static anjay_fw_update_handlers_t FW_UPDATE_HANDLERS = {
    .stream_open = fw_stream_open,
    .stream_write = fw_stream_write,
    .stream_finish = fw_stream_finish,
    .reset = fw_reset,
    .get_name = fw_get_name,
    .get_version = fw_get_version,
    .perform_upgrade = fw_perform_upgrade
};

static bool is_valid_result(int8_t result) {
    switch (result) {
    case ANJAY_FW_UPDATE_INITIAL_DOWNLOADED:
    case ANJAY_FW_UPDATE_INITIAL_DOWNLOADING:
    case ANJAY_FW_UPDATE_INITIAL_NEUTRAL:
    case ANJAY_FW_UPDATE_INITIAL_SUCCESS:
    case ANJAY_FW_UPDATE_INITIAL_INTEGRITY_FAILURE:
    case ANJAY_FW_UPDATE_INITIAL_FAILED:
        return true;
    default:
        return false;
    }
}

typedef struct {
    anjay_fw_update_initial_result_t result;
    char *uri;
    char *download_file;
    bool filename_administratively_set;
    anjay_etag_t *etag;
} persistence_file_data_t;

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
static persistence_file_data_t read_persistence_file(const char *path) {
    persistence_file_data_t data;
    memset(&data, 0, sizeof(data));
    avs_stream_t *stream = NULL;
    int8_t result8 = (int8_t) ANJAY_FW_UPDATE_INITIAL_NEUTRAL;
    if ((stream = avs_stream_file_create(path, AVS_STREAM_FILE_READ))) {
        // invalid or empty but existing file still signifies success
        result8 = (int8_t) ANJAY_FW_UPDATE_INITIAL_SUCCESS;
    }
    avs_persistence_context_t ctx =
            avs_persistence_restore_context_create(stream);
    if (!stream
            || avs_is_err(avs_persistence_bytes(&ctx, (uint8_t *) &result8, 1))
            || !is_valid_result(result8)
            || avs_is_err(avs_persistence_string(&ctx, &data.uri))
            || avs_is_err(avs_persistence_string(&ctx, &data.download_file))
            || avs_is_err(avs_persistence_bool(
                       &ctx, &data.filename_administratively_set))
            || avs_is_err(restore_etag(&ctx, &data.etag))) {
        demo_log(WARNING,
                 "Invalid data in the firmware state persistence file");
        avs_free(data.uri);
        avs_free(data.download_file);
        memset(&data, 0, sizeof(data));
    }
    data.result = (anjay_fw_update_initial_result_t) result8;
    if (stream) {
        avs_stream_cleanup(&stream);
    }
    return data;
}
#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
static persistence_file_data_t read_persistence_file(const char *path) {
    (void) path;
    demo_log(WARNING, "Persistence not compiled in");
    persistence_file_data_t retval;
    memset(&retval, 0, sizeof(retval));
    return retval;
}
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

typedef struct {
    anjay_t *anjay;
    anjay_fw_update_result_t delayed_result;
} set_delayed_fw_update_result_args_t;

static void set_delayed_fw_update_result(avs_sched_t *sched, const void *arg) {
    (void) sched;
    const set_delayed_fw_update_result_args_t *args =
            (const set_delayed_fw_update_result_args_t *) arg;

    anjay_fw_update_set_result(args->anjay, args->delayed_result);
}

int firmware_update_install(anjay_t *anjay,
                            fw_update_logic_t *fw,
                            const char *persistence_file,
                            const avs_net_security_info_t *security_info,
                            const avs_coap_udp_tx_params_t *tx_params,
                            avs_time_duration_t tcp_request_timeout,
                            anjay_fw_update_result_t delayed_result,
                            bool prefer_same_socket_downloads,
#ifdef ANJAY_WITH_SEND
                            bool use_lwm2m_send,
#endif // ANJAY_WITH_SEND
                            bool auto_suspend) {
    int result = -1;

    fw->anjay = anjay;
    fw->persistence_file = persistence_file;
    if (security_info) {
        memcpy(&fw->security_info, security_info, sizeof(fw->security_info));
        FW_UPDATE_HANDLERS.get_security_config = fw_get_security_config;
    } else {
        FW_UPDATE_HANDLERS.get_security_config = NULL;
    }

    if (tx_params || auto_suspend) {
        if (tx_params) {
            fw->coap_tx_params = *tx_params;
        } else {
            fw->coap_tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;
        }
        fw->auto_suspend = auto_suspend;
        FW_UPDATE_HANDLERS.get_coap_tx_params = fw_get_coap_tx_params;
    } else {
        FW_UPDATE_HANDLERS.get_coap_tx_params = NULL;
    }

    if (avs_time_duration_valid(tcp_request_timeout)) {
        fw->tcp_request_timeout = tcp_request_timeout;
        FW_UPDATE_HANDLERS.get_tcp_request_timeout = fw_get_tcp_request_timeout;
    } else {
        FW_UPDATE_HANDLERS.get_tcp_request_timeout = NULL;
    }

    persistence_file_data_t data = read_persistence_file(persistence_file);
    delete_persistence_file(fw);
    demo_log(INFO, "Initial firmware upgrade state result: %d",
             (int) data.result);
    if ((fw->next_target_path = data.download_file)
            && data.filename_administratively_set
            && !(fw->administratively_set_target_path =
                         avs_strdup(data.download_file))) {
        demo_log(WARNING, "Could not administratively set firmware path");
    }
    anjay_fw_update_initial_state_t state = {
        .result = data.result,
        .persisted_uri = data.uri,
        .resume_offset = 0,
        .resume_etag = data.etag,
        .prefer_same_socket_downloads = prefer_same_socket_downloads,
#ifdef ANJAY_WITH_SEND
        .use_lwm2m_send = use_lwm2m_send,
#endif // ANJAY_WITH_SEND
    };

    if (delayed_result != ANJAY_FW_UPDATE_RESULT_INITIAL) {
        demo_log(INFO,
                 "delayed_result == %d; initializing Firmware Update in "
                 "UPDATING state",
                 (int) delayed_result);
        state.result = ANJAY_FW_UPDATE_INITIAL_UPDATING;

        // Simulate FOTA process that finishes after the LwM2M client starts by
        // changing the Update Result later at runtime
        set_delayed_fw_update_result_args_t args = {
            .anjay = anjay,
            .delayed_result = delayed_result
        };
        if (AVS_SCHED_NOW(anjay_get_scheduler(anjay), NULL,
                          set_delayed_fw_update_result, &args, sizeof(args))) {
            goto exit;
        }
    }

    if (state.result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING) {
        long offset;
        if (!fw->next_target_path
                || !(fw->stream = fopen(fw->next_target_path, "ab"))
                || (offset = ftell(fw->stream)) < 0) {
            if (fw->stream) {
                fclose(fw->stream);
                fw->stream = NULL;
            }
            state.result = ANJAY_FW_UPDATE_INITIAL_NEUTRAL;
        } else {
            state.resume_offset = (size_t) offset;
        }
    }
    if (state.result >= 0) {
        // we're initializing in the "Idle" state, so the firmware file is not
        // supposed to exist; delete it if we have it for any weird reason
        maybe_delete_firmware_file(fw);
    }

    result = anjay_fw_update_install(anjay, &FW_UPDATE_HANDLERS, fw, &state);
    if (!result && auto_suspend) {
        anjay_fw_update_pull_suspend(anjay);
    }

exit:
    avs_free(data.uri);
    avs_free(data.etag);
    if (result) {
        firmware_update_destroy(fw);
    }
    return result;
}

void firmware_update_destroy(fw_update_logic_t *fw_update) {
    assert(fw_update);
    if (fw_update->stream) {
        fclose(fw_update->stream);
    }
    avs_free(fw_update->package_uri);
    avs_free(fw_update->administratively_set_target_path);
    avs_free(fw_update->next_target_path);
}
