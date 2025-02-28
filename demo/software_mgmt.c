/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "software_mgmt.h"
#include "demo.h"
#include "demo_utils.h"

#include <errno.h>
#include <inttypes.h>
#include <unistd.h>

#include <sys/stat.h>

#include <avsystem/commons/avs_stream_file.h>
#include <avsystem/commons/avs_utils.h>

#define FORCE_ERROR_FAILED_INSTALL 1
#define FORCE_DELAYED_SUCCESS_INSTALL 2
#define FORCE_DELAYED_ERROR_FAILED_INSTALL 3
#define FORCE_SET_SUCCESS_FROM_PERFORM_INSTALL 4
#define FORCE_SET_SUCCESS_FROM_PERFORM_INSTALL_ACTIVATE 5
#define FORCE_SET_FAILURE_FROM_PERFORM_INSTALL 6
#define FORCE_SET_FAILURE_FROM_PERFORM_UNINSTALL 7
#define FORCE_SET_FAILURE_FROM_PERFORM_ACTIVATION 8
#define FORCE_SET_FAILURE_FROM_PERFORM_DEACTIVATION 9
#define FORCE_SET_FAILURE_FROM_PREPARE_FOR_UPDATE 10
#define FORCE_DO_NOTHING_SW 11

#define DEFAULT_INSTANCE_COUNT 2

static const char *SW_NAME[SW_MGMT_PACKAGE_COUNT] = {
    [0] = "Cute software 0",
    [1] = "Cute software 1",
    [2] = "Secret software"
};

void sw_mgmt_set_package_path(sw_mgmt_logic_t *sw_mgmt, const char *path) {
    if (sw_mgmt->stream) {
        demo_log(ERROR,
                 "cannot set software package path while a download is in "
                 "progress");
        return;
    }
    char *new_target_path = avs_strdup(path);
    if (!new_target_path) {
        demo_log(ERROR, "out of memory");
        return;
    }
    avs_free(sw_mgmt->administratively_set_target_path);

    sw_mgmt->administratively_set_target_path = new_target_path;
    demo_log(INFO, "software package path set to %s",
             sw_mgmt->administratively_set_target_path);
}

static int maybe_create_software_file(sw_mgmt_logic_t *sw_mgmt) {
    if (sw_mgmt->next_target_path) {
        return 0;
    }
    if (sw_mgmt->administratively_set_target_path) {
        sw_mgmt->next_target_path =
                avs_strdup(sw_mgmt->administratively_set_target_path);
    } else {
        sw_mgmt->next_target_path = generate_random_target_filepath();
    }
    if (!sw_mgmt->next_target_path) {
        return -1;
    }
    demo_log(INFO, "Created %s", sw_mgmt->next_target_path);
    return 0;
}

static void maybe_delete_software_file(sw_mgmt_logic_t *sw_mgmt) {
    if (sw_mgmt->next_target_path) {
        unlink(sw_mgmt->next_target_path);
        demo_log(INFO, "Deleted %s", sw_mgmt->next_target_path);
        avs_free(sw_mgmt->next_target_path);
        sw_mgmt->next_target_path = NULL;
    }
}

static void fix_sw_meta_endianness(sw_metadata_t *meta) {
    meta->version = avs_convert_be16(meta->version);
    meta->force_error_case = avs_convert_be16(meta->force_error_case);
    meta->crc = avs_convert_be32(meta->crc);
}

static int read_sw_meta_from_file(FILE *f, sw_metadata_t *out_metadata) {
    sw_metadata_t m;
    memset(&m, 0, sizeof(m));

    if (fread(m.magic, sizeof(m.magic), 1, f) != 1
            || fread(&m.version, sizeof(m.version), 1, f) != 1
            || fread(&m.force_error_case, sizeof(m.force_error_case), 1, f) != 1
            || fread(&m.crc, sizeof(m.crc), 1, f) != 1) {
        demo_log(ERROR, "could not read software metadata");
        return -1;
    }

    fix_sw_meta_endianness(&m);
    *out_metadata = m;
    return 0;
}

static int unpack_sw_to_file(const char *sw_pkg_path,
                             const char *target_path,
                             sw_metadata_t *out_metadata) {
    int result = -1;
    FILE *sw = fopen(sw_pkg_path, "rb");
    FILE *tmp = NULL;

    if (!sw) {
        demo_log(ERROR, "could not open file: %s", sw_pkg_path);
        goto cleanup;
    }

    tmp = fopen(target_path, "wb");
    if (!tmp) {
        demo_log(ERROR, "could not open file: %s", target_path);
        goto cleanup;
    }

    result = read_sw_meta_from_file(sw, out_metadata);
    if (result) {
        demo_log(ERROR, "could not read metadata from file: %s", sw_pkg_path);
        goto cleanup;
    }
    result = copy_file_contents(tmp, sw);
    if (result) {
        demo_log(ERROR, "could not copy software from %s to %s", sw_pkg_path,
                 target_path);
        goto cleanup;
    }

    result = 0;

cleanup:
    if (sw) {
        fclose(sw);
    }
    if (tmp) {
        fclose(tmp);
    }
    return result;
}

static int unpack_software_in_place(sw_mgmt_logic_t *sw_mgmt) {
    char *tmp_path = generate_random_target_filepath();
    if (!tmp_path) {
        return -1;
    }

    int result = unpack_sw_to_file(sw_mgmt->next_target_path, tmp_path,
                                   &sw_mgmt->metadata);
    if (result) {
        goto cleanup;
    }

    if ((result = rename(tmp_path, sw_mgmt->next_target_path)) == -1) {
        demo_log(ERROR, "could not rename %s to %s: %s", tmp_path,
                 sw_mgmt->next_target_path, strerror(errno));
        goto cleanup;
    }

    if ((result = chmod(sw_mgmt->next_target_path, 0700)) == -1) {
        demo_log(ERROR, "could not set permissions for %s: %s",
                 sw_mgmt->next_target_path, strerror(errno));
        goto cleanup;
    }

cleanup:
    unlink(tmp_path);
    avs_free(tmp_path);
    if (result) {
        maybe_delete_software_file(sw_mgmt);
    }

    return result;
}

static bool sw_magic_valid(const sw_metadata_t *meta) {
    if (memcmp(meta->magic, "ANJAY_SW", sizeof(meta->magic))) {
        demo_log(ERROR, "invalid software magic");
        return false;
    }

    return true;
}

static bool sw_version_supported(const sw_metadata_t *meta) {
    if (meta->version != 1) {
        demo_log(ERROR, "unsupported software version: %" PRIu16,
                 meta->version);
        return false;
    }

    return true;
}

static int validate_software(sw_mgmt_logic_t *sw_mgmt) {
    if (!sw_magic_valid(&sw_mgmt->metadata)
            || !sw_version_supported(&sw_mgmt->metadata)) {
        return ANJAY_SW_MGMT_ERR_UNSUPPORTED_PACKAGE_TYPE;
    }

    uint32_t actual_crc;
    int result = calc_file_crc32(sw_mgmt->next_target_path, &actual_crc);

    if (result) {
        demo_log(WARNING, "unable to check software CRC");
        return ANJAY_SW_MGMT_ERR_INTEGRITY_FAILURE;
    }

    if (sw_mgmt->metadata.crc != actual_crc) {
        demo_log(WARNING, "CRC mismatch: expected %08x != %08x actual",
                 sw_mgmt->metadata.crc, actual_crc);
        return ANJAY_SW_MGMT_ERR_INTEGRITY_FAILURE;
    }

    return 0;
}

static void sw_mgmt_destroy_inst(sw_mgmt_logic_t *sw_mgmt) {
    if (sw_mgmt->stream) {
        fclose(sw_mgmt->stream);
    }
    avs_free(sw_mgmt->administratively_set_target_path);
    avs_free(sw_mgmt->next_target_path);
}

void sw_mgmt_update_destroy(sw_mgmt_logic_t *sw_mgmt_table) {
    assert(sw_mgmt_table);

    for (size_t iid = 0; iid < SW_MGMT_PACKAGE_COUNT; iid++) {
        sw_mgmt_destroy_inst(&sw_mgmt_table[iid]);
    }
}

typedef struct {
    anjay_sw_mgmt_initial_state_t result[SW_MGMT_PACKAGE_COUNT];
    char *download_file[SW_MGMT_PACKAGE_COUNT];
    bool filename_administratively_set[SW_MGMT_PACKAGE_COUNT];
    bool exists[SW_MGMT_PACKAGE_COUNT];
} persistence_file_data_t;

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
static bool is_valid_result(uint8_t result) {
    switch (result) {
    case ANJAY_SW_MGMT_INITIAL_STATE_IDLE:
    case ANJAY_SW_MGMT_INITIAL_STATE_DOWNLOADED:
    case ANJAY_SW_MGMT_INITIAL_STATE_DELIVERED:
    case ANJAY_SW_MGMT_INITIAL_STATE_INSTALLING:
    case ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED:
    case ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_ACTIVATED:
        return true;
    default:
        return false;
    }
}

static int write_persistence_file(const char *path,
                                  persistence_file_data_t *data) {
    avs_stream_t *stream = avs_stream_file_create(path, AVS_STREAM_FILE_WRITE);
    avs_persistence_context_t ctx =
            avs_persistence_store_context_create(stream);
    int retval = 0;

    for (size_t iid = 0; iid < SW_MGMT_PACKAGE_COUNT; iid++) {
        uint8_t result8 = (uint8_t) data->result[iid];
        if (!stream || avs_is_err(avs_persistence_bytes(&ctx, &result8, 1))
                || avs_is_err(avs_persistence_string(&ctx,
                                                     &data->download_file[iid]))
                || avs_is_err(avs_persistence_bool(
                           &ctx, &data->filename_administratively_set[iid]))
                || avs_is_err(avs_persistence_bool(&ctx, &data->exists[iid]))) {
            demo_log(ERROR, "Could not write software state persistence file");
            retval = -1;
            break;
        }
    }
    if (stream) {
        avs_stream_cleanup(&stream);
    }
    if (retval) {
        unlink(path);
    }
    return retval;
}
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

static int read_persistence_file(const char *path,
                                 persistence_file_data_t *data) {
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
    int ret = 0;
    memset(data, 0, sizeof(*data));
    avs_stream_t *stream = NULL;
    uint8_t result8 = (uint8_t) ANJAY_SW_MGMT_INITIAL_STATE_IDLE;
    uint8_t result8_first = (uint8_t) ANJAY_SW_MGMT_INITIAL_STATE_IDLE;

    if ((stream = avs_stream_file_create(path, AVS_STREAM_FILE_READ))) {
        // invalid or empty but existing file still signifies success but only
        // for first instance
        result8_first = (uint8_t) ANJAY_SW_MGMT_INITIAL_STATE_INSTALLING;
    }
    avs_persistence_context_t ctx =
            avs_persistence_restore_context_create(stream);

    for (size_t iid = 0; iid < SW_MGMT_PACKAGE_COUNT; iid++) {
        if (!stream || avs_is_err(avs_persistence_bytes(&ctx, &result8, 1))
                || !is_valid_result(result8)
                || avs_is_err(avs_persistence_string(&ctx,
                                                     &data->download_file[iid]))
                || avs_is_err(avs_persistence_bool(
                           &ctx, &data->filename_administratively_set[iid]))
                || avs_is_err(avs_persistence_bool(&ctx, &data->exists[iid]))) {
            demo_log(WARNING,
                     "Invalid data in the software state persistence file %s",
                     path);
            for (size_t i = 0; i < SW_MGMT_PACKAGE_COUNT; i++) {
                avs_free(data->download_file[i]);
            }
            memset(data, 0, sizeof(*data));
            data->result[0] = (anjay_sw_mgmt_initial_state_t) result8_first;
            ret = -1;
            break;
        }
        data->result[iid] = (anjay_sw_mgmt_initial_state_t) result8;
    }
    if (stream) {
        avs_stream_cleanup(&stream);
    }
    return ret;
#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
    (void) path;
    (void) data;
    demo_log(WARNING, "Persistence not compiled in");
    return 0;
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
}

static void delete_persistence_file(const sw_mgmt_common_logic_t *sw) {
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
    unlink(sw->persistence_file);
#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
    // defined(AVS_COMMONS_STREAM_WITH_FILE)
    (void) sw;
    demo_log(WARNING, "Persistence not compiled in");
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
    // defined(AVS_COMMONS_STREAM_WITH_FILE)
}

static int update_persistence_file(const char *path,
                                   sw_mgmt_logic_t *sw,
                                   anjay_sw_mgmt_initial_state_t result,
                                   anjay_iid_t iid) {
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
    int res = 0;
    persistence_file_data_t data;
    read_persistence_file(path, &data);
    avs_free(data.download_file[iid]);
    data.result[iid] = result;
    data.download_file[iid] = sw->next_target_path;
    data.filename_administratively_set[iid] =
            sw->administratively_set_target_path;
    data.exists[iid] = true;
    res = write_persistence_file(path, &data);
    for (size_t i = 0; i < SW_MGMT_PACKAGE_COUNT; i++) {
        if (i != iid) {
            avs_free(data.download_file[i]);
        }
    }
    return res;
#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
    (void) path;
    (void) sw;
    (void) result;
    (void) iid;
    return 0;
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
}

static int update_persisted_instance_existence(const char *path,
                                               bool exists,
                                               anjay_iid_t iid) {
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
    int res = 0;
    persistence_file_data_t data;
    read_persistence_file(path, &data);
    data.exists[iid] = exists;
    res = write_persistence_file(path, &data);
    for (size_t i = 0; i < SW_MGMT_PACKAGE_COUNT; i++) {
        avs_free(data.download_file[i]);
    }
    return res;
#else  // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
    (void) path;
    (void) exists;
    (void) iid;
    return 0;
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
}

static int sw_mgmt_stream_open(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    (void) obj_ctx;
    (void) iid;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;
    assert(!sw->stream);

    if (maybe_create_software_file(sw)) {
        return -1;
    }

    if (!(sw->stream = fopen(sw->next_target_path, "wb"))) {
        demo_log(ERROR, "could not open file: %s", sw->next_target_path);
        return -1;
    }

    return 0;
}

static int sw_mgmt_stream_write(void *obj_ctx,
                                anjay_iid_t iid,
                                void *inst_ctx,
                                const void *data,
                                size_t length) {
    (void) obj_ctx;
    (void) iid;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;

    if (!sw->stream) {
        demo_log(ERROR, "stream not open");
        return -1;
    }

    if (length
            && (fwrite(data, length, 1, sw->stream) != 1
                // Software management integration tests measure download
                // progress by checking file size, so avoiding buffering
                // is required.
                || fflush(sw->stream) != 0)) {
        demo_log(ERROR, "fwrite or fflush failed: %s", strerror(errno));
        return ANJAY_SW_MGMT_ERR_NOT_ENOUGH_SPACE;
    }

    return 0;
}

static int
sw_mgmt_stream_finish(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;
    if (sw_common->auto_suspend) {
        anjay_sw_mgmt_pull_suspend(sw_common->anjay);
    }
    if (!sw->stream) {
        demo_log(ERROR, "stream not open");
        return -1;
    }
    fclose(sw->stream);
    sw->stream = NULL;

    (void) update_persistence_file(sw_common->persistence_file, sw,
                                   ANJAY_SW_MGMT_INITIAL_STATE_DOWNLOADED, iid);

    if (sw_common->terminate_after_downloading && iid == 0) {
        anjay_event_loop_interrupt(sw_common->anjay);
    }

    return 0;
}

static int
sw_mgmt_check_integrity(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    int result = 0;
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;
    if (!(sw_common->terminate_after_downloading && iid == 0)) {

        if (unpack_software_in_place(sw)) {
            return ANJAY_SW_MGMT_ERR_UNSUPPORTED_PACKAGE_TYPE;
        }

        result = validate_software(sw);
        if (!result) {
            demo_log(INFO, "software downloaded successfully");
            (void) update_persistence_file(
                    sw_common->persistence_file, sw,
                    ANJAY_SW_MGMT_INITIAL_STATE_DELIVERED, iid);
        }
    }
    return result;
}

static void sw_mgmt_reset(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    (void) iid;
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;

    if (sw->stream) {
        fclose(sw->stream);
        sw->stream = NULL;
    }

    maybe_delete_software_file(sw);
    delete_persistence_file(sw_common);
    if (sw_common->auto_suspend) {
        anjay_sw_mgmt_pull_suspend(sw_common->anjay);
    }
}

static const char *
sw_mgmt_get_name(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    (void) obj_ctx;
    (void) inst_ctx;
    return SW_NAME[iid];
}

static const char *
sw_mgmt_get_version(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    (void) obj_ctx;
    (void) iid;
    (void) inst_ctx;
    return "1.0";
}

static int sw_mgmt_pkg_install(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;

    demo_log(INFO, "*** SOFTWARE INSTALL: %s ***", sw->next_target_path);
    switch (sw->metadata.force_error_case) {
    case FORCE_ERROR_FAILED_INSTALL:
        demo_log(ERROR, "install failed");
        delete_persistence_file(sw_common);
        return -1;
    case FORCE_DELAYED_SUCCESS_INSTALL:
        if (argv_append("--delayed-sw-mgmt-result") || argv_append("1")) {
            demo_log(ERROR, "could not append delayed result to argv");
            return -1;
        }
        break;
    case FORCE_DELAYED_ERROR_FAILED_INSTALL:
        if (argv_append("--delayed-sw-mgmt-result") || argv_append("0")) {
            demo_log(ERROR, "could not append delayed result to argv");
            return -1;
        }
        break;
    case FORCE_SET_SUCCESS_FROM_PERFORM_INSTALL:
        if (anjay_sw_mgmt_finish_pkg_install(
                    sw_common->anjay, iid,
                    ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_INACTIVE)) {
            demo_log(ERROR, "anjay_sw_mgmt_finish_pkg_install failed");
            return -1;
        }
        return 0;
    case FORCE_SET_SUCCESS_FROM_PERFORM_INSTALL_ACTIVATE:
        if (anjay_sw_mgmt_finish_pkg_install(
                    sw_common->anjay, iid,
                    ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_ACTIVE)) {
            demo_log(ERROR, "anjay_sw_mgmt_finish_pkg_install failed");
            return -1;
        }
        return 0;
    case FORCE_SET_FAILURE_FROM_PERFORM_INSTALL:
        if (anjay_sw_mgmt_finish_pkg_install(
                    sw_common->anjay, iid,
                    ANJAY_SW_MGMT_FINISH_PKG_INSTALL_FAILURE)) {
            demo_log(ERROR, "anjay_sw_mgmt_finish_pkg_install failed");
            return -1;
        }
        return 0;
    case FORCE_DO_NOTHING_SW:
        return 0;
    default:
        break;
    }

    if (sw->metadata.force_error_case == FORCE_DELAYED_SUCCESS_INSTALL
            || sw->metadata.force_error_case
                           == FORCE_DELAYED_ERROR_FAILED_INSTALL) {
        execv(sw->next_target_path, argv_get());
        demo_log(ERROR, "execv failed (%s)", strerror(errno));
        delete_persistence_file(sw_common);
        return -1;
    } else {
        if (system(sw->next_target_path)) {
            demo_log(ERROR, "execution of shell command failed");
            return -1;
        }
        anjay_sw_mgmt_finish_pkg_install(
                sw_common->anjay, iid,
                ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_INACTIVE);
        (void) update_persistence_file(
                sw_common->persistence_file, sw,
                ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED, iid);
        return 0;
    }
}

static int
sw_mgmt_pkg_uninstall(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;

    if (sw->metadata.force_error_case
            == FORCE_SET_FAILURE_FROM_PERFORM_UNINSTALL) {
        return -1;
    } else {
        (void) update_persistence_file(sw_common->persistence_file, sw,
                                       ANJAY_SW_MGMT_INITIAL_STATE_IDLE, iid);
    }

    return 0;
}

static int
sw_mgmt_prepare_for_update(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;

    if (sw->metadata.force_error_case
            == FORCE_SET_FAILURE_FROM_PREPARE_FOR_UPDATE) {
        return -1;
    } else {
        (void) update_persistence_file(sw_common->persistence_file, sw,
                                       ANJAY_SW_MGMT_INITIAL_STATE_IDLE, iid);
    }

    return 0;
}

static int sw_mgmt_activate(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;
    bool activate;

    if ((sw_common->disable_repeated_activation_deactivation
         && (anjay_sw_mgmt_get_activation_state(sw_common->anjay, iid,
                                                &activate)
             || activate))
            || sw->metadata.force_error_case
                           == FORCE_SET_FAILURE_FROM_PERFORM_ACTIVATION) {
        return -1;
    } else {
        (void) update_persistence_file(
                sw_common->persistence_file, sw,
                ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_ACTIVATED, iid);
    }

    return 0;
}

static int sw_mgmt_deactivate(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;
    bool activate;

    if ((sw_common->disable_repeated_activation_deactivation
         && (anjay_sw_mgmt_get_activation_state(sw_common->anjay, iid,
                                                &activate)
             || !activate))
            || sw->metadata.force_error_case
                           == FORCE_SET_FAILURE_FROM_PERFORM_DEACTIVATION) {
        return -1;
    } else {
        (void) update_persistence_file(
                sw_common->persistence_file, sw,
                ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED, iid);
    }

    return 0;
}

#ifdef ANJAY_WITH_DOWNLOADER
static int
sw_mgmt_get_security_config(void *obj_ctx,
                            anjay_iid_t iid,
                            void *inst_ctx,
                            const char *download_uri,
                            anjay_security_config_t *out_security_info) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    (void) download_uri;
    (void) iid;
    (void) inst_ctx;
    memset(out_security_info, 0, sizeof(*out_security_info));
    out_security_info->security_info = *sw_common->security_info;
    return 0;
}

static avs_time_duration_t
sw_mgmt_get_tcp_request_timeout(void *obj_ctx,
                                anjay_iid_t iid,
                                void *inst_ctx,
                                const char *download_uri) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    (void) download_uri;
    (void) iid;
    (void) inst_ctx;
    return *sw_common->tcp_request_timeout;
}
#    ifdef ANJAY_WITH_COAP_DOWNLOAD
static avs_coap_udp_tx_params_t
sw_mgmt_get_coap_tx_params(void *obj_ctx,
                           anjay_iid_t iid,
                           void *inst_ctx,
                           const char *download_uri) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    (void) download_uri;
    (void) iid;
    (void) inst_ctx;
    if (sw_common->auto_suspend) {
        anjay_sw_mgmt_pull_reconnect(sw_common->anjay);
    }
    return sw_common->coap_tx_params != NULL ? *sw_common->coap_tx_params
                                             : AVS_COAP_DEFAULT_UDP_TX_PARAMS;
}
#    endif // ANJAY_WITH_COAP_DOWNLOAD
#endif     // ANJAY_WITH_DOWNLOADER

static int
sw_mgmt_add_handler(void *obj_ctx, anjay_iid_t iid, void **out_inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;

    if (iid < SW_MGMT_PACKAGE_COUNT
            && !update_persisted_instance_existence(sw_common->persistence_file,
                                                    true, iid)) {
        *out_inst_ctx = (void *) &sw_common->sw_mgmt_table[iid];
    } else {
        return -1;
    }

    return 0;
}

static int
sw_mgmt_remove_handler(void *obj_ctx, anjay_iid_t iid, void *inst_ctx) {
    sw_mgmt_common_logic_t *sw_common = (sw_mgmt_common_logic_t *) obj_ctx;
    sw_mgmt_logic_t *sw = (sw_mgmt_logic_t *) inst_ctx;

    if (iid < SW_MGMT_PACKAGE_COUNT
            && !update_persisted_instance_existence(sw_common->persistence_file,
                                                    false, iid)) {
        sw_mgmt_destroy_inst(sw);
    } else {
        return -1;
    }

    return 0;
}

static anjay_sw_mgmt_handlers_t g_handlers = {
    .stream_open = sw_mgmt_stream_open,
    .stream_write = sw_mgmt_stream_write,
    .stream_finish = sw_mgmt_stream_finish,
    .check_integrity = sw_mgmt_check_integrity,
    .reset = sw_mgmt_reset,
    .get_name = sw_mgmt_get_name,
    .get_version = sw_mgmt_get_version,
    .pkg_install = sw_mgmt_pkg_install,
    .pkg_uninstall = sw_mgmt_pkg_uninstall,
    .prepare_for_update = sw_mgmt_prepare_for_update,
    .activate = sw_mgmt_activate,
    .deactivate = sw_mgmt_deactivate,
    .add_handler = sw_mgmt_add_handler,
    .remove_handler = sw_mgmt_remove_handler
};

typedef struct {
    anjay_t *anjay;
    bool delayed_result;
} set_delayed_sw_mgmt_update_result_args_t;

static void set_delayed_sw_mgmt_update_result(avs_sched_t *sched,
                                              const void *arg) {
    (void) sched;
    const set_delayed_sw_mgmt_update_result_args_t *args =
            (const set_delayed_sw_mgmt_update_result_args_t *) arg;
    anjay_sw_mgmt_finish_pkg_install(
            args->anjay, 0,
            args->delayed_result
                    ? ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_INACTIVE
                    : ANJAY_SW_MGMT_FINISH_PKG_INSTALL_FAILURE);
}

int sw_mgmt_install(anjay_t *anjay,
                    sw_mgmt_common_logic_t *sw_mgmt_common,
                    sw_mgmt_logic_t *sw_mgmt_table,
                    const char *persistence_file,
                    bool prefer_same_socket_downloads,
                    uint8_t delayed_first_instance_install_result,
                    bool terminate_after_downloading,
                    bool disable_repeated_activation_deactivation
#ifdef ANJAY_WITH_DOWNLOADER
                    ,
                    avs_net_security_info_t *security_info,
                    avs_coap_udp_tx_params_t *tx_params,
                    avs_time_duration_t *tcp_request_timeout,
                    bool auto_suspend
#endif // ANJAY_WITH_DOWNLOADER
) {
    int ret = 0;
    sw_mgmt_logic_t *sw_logic = NULL;
    sw_mgmt_common->anjay = anjay;
    sw_mgmt_common->sw_mgmt_table = sw_mgmt_table;
    sw_mgmt_common->persistence_file = persistence_file;
    sw_mgmt_common->terminate_after_downloading = terminate_after_downloading;
    sw_mgmt_common->disable_repeated_activation_deactivation =
            disable_repeated_activation_deactivation;
#ifdef ANJAY_WITH_DOWNLOADER
    if (security_info) {
        sw_mgmt_common->security_info = security_info;
        g_handlers.get_security_config = sw_mgmt_get_security_config;
    } else {
        g_handlers.get_security_config = NULL;
    }

    if (tx_params || auto_suspend) {
        sw_mgmt_common->auto_suspend = auto_suspend;
        sw_mgmt_common->coap_tx_params = tx_params;
        g_handlers.get_coap_tx_params = sw_mgmt_get_coap_tx_params;
    } else {
        g_handlers.get_coap_tx_params = NULL;
    }

    if (tcp_request_timeout && avs_time_duration_valid(*tcp_request_timeout)) {
        sw_mgmt_common->tcp_request_timeout = tcp_request_timeout;
        g_handlers.get_tcp_request_timeout = sw_mgmt_get_tcp_request_timeout;
    } else {
        g_handlers.get_tcp_request_timeout = NULL;
    }
#endif // ANJAY_WITH_DOWNLOADER

    const anjay_sw_mgmt_settings_t settings = {
        .handlers = &g_handlers,
        .obj_ctx = sw_mgmt_common,
#ifdef ANJAY_WITH_DOWNLOADER
        .prefer_same_socket_downloads = prefer_same_socket_downloads
#endif // ANJAY_WITH_DOWNLOADER
    };

    persistence_file_data_t data;
    if (read_persistence_file(persistence_file, &data)) {
        for (size_t i = 0; i < DEFAULT_INSTANCE_COUNT; i++) {
            data.exists[i] = true;
        }
    }
    delete_persistence_file(sw_mgmt_common);

    if (anjay_sw_mgmt_install(anjay, &settings)) {
        ret = -1;
        goto exit;
    }

    for (anjay_iid_t iid = 0; iid < SW_MGMT_PACKAGE_COUNT; iid++) {
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
        if (data.exists[iid])
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)
        {
            sw_logic = &sw_mgmt_table[iid];

            if ((sw_logic->next_target_path = data.download_file[iid])
                    && data.filename_administratively_set[iid]
                    && !(sw_logic->administratively_set_target_path =
                                 avs_strdup(data.download_file[iid]))) {
                demo_log(WARNING,
                         "Could not administratively set firmware path for %d",
                         iid);
            }

            anjay_sw_mgmt_instance_initializer_t inst_settings = {
                .iid = iid,
                .initial_state = data.result[iid],
                .inst_ctx = sw_logic
            };

            if (iid == 0
                    && (delayed_first_instance_install_result == 0
                        || delayed_first_instance_install_result == 1)) {
                demo_log(INFO,
                         "delayed_result == %d; initializing Software "
                         "Management "
                         "in DELIVERED state",
                         (int) delayed_first_instance_install_result);
                inst_settings.initial_state =
                        ANJAY_SW_MGMT_INITIAL_STATE_INSTALLING;

                // Simulate installing process that finishes after the LwM2M
                // client starts by changing the Update Result later at runtime
                set_delayed_sw_mgmt_update_result_args_t args = {
                    .anjay = anjay,
                    .delayed_result = delayed_first_instance_install_result == 1
                };

                if (AVS_SCHED_DELAYED(anjay_get_scheduler(anjay), NULL,
                                      avs_time_duration_from_scalar(1,
                                                                    AVS_TIME_S),
                                      set_delayed_sw_mgmt_update_result, &args,
                                      sizeof(args))) {
                    ret = -1;
                    goto exit;
                }
            }

            if (anjay_sw_mgmt_add_instance(anjay, &inst_settings)) {
                ret = -1;
                goto exit;
            }

            if (auto_suspend) {
                anjay_sw_mgmt_pull_suspend(anjay);
            }
        }
    }
exit:
    if (ret) {
        sw_mgmt_update_destroy(sw_mgmt_table);
    }
    return ret;
}
