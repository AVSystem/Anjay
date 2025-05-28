/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include "advanced_firmware_update.h"
#include "demo_utils.h"

#include <unistd.h>

static advanced_fw_update_logic_t *fw_global;

static int fw_stream_open(anjay_iid_t iid, void *fw_) {
    (void) iid;

    return fw_update_common_open(iid, fw_);
}

static int compare_files(FILE *s1, FILE *s2) {
    while (!feof(s1)) {
        char buf_1[1024];
        char buf_2[1024];
        size_t bytes_read_1 = fread(buf_1, 1, sizeof(buf_1), s1);
        size_t bytes_read_2 = fread(buf_2, 1, sizeof(buf_2), s2);
        if (bytes_read_1 != bytes_read_2) {
            return -1;
        }
        if (memcmp(buf_1, buf_2, bytes_read_1)) {
            return -1;
        }
    }
    return 0;
}

static int compare_images(const char *file_path_1, const char *file_path_2) {
    int result = -1;
    FILE *stream1 = fopen(file_path_1, "r");
    FILE *stream2 = NULL;

    if (!stream1) {
        demo_log(ERROR, "could not open file: %s", file_path_1);
        goto cleanup;
    }

    stream2 = fopen(file_path_2, "r");
    if (!stream2) {
        demo_log(ERROR, "could not open file: %s", file_path_2);
        goto cleanup;
    }

    result = compare_files(stream1, stream2);
cleanup:
    if (stream1) {
        fclose(stream1);
    }
    if (stream2) {
        fclose(stream2);
    }
    return result;
}

static int prepare_and_validate_update(advanced_fw_update_logic_t *fw) {
    demo_log(INFO,
             "Checking image of " AVS_QUOTE_MACRO(
                     ANJAY_ADVANCED_FW_UPDATE_OID) "/%u instance",
             fw->iid);
    if (!compare_images(fw->original_img_file_path, fw->next_target_path)) {
        demo_log(INFO, "Image check success");
        return 0;
    }
    demo_log(ERROR, "Image check failure");
    anjay_advanced_fw_update_set_state_and_result(
            fw->anjay, fw->iid, ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
            ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED);
    return -1;
}

static int update(advanced_fw_update_logic_t *fw) {
    demo_log(INFO, "*** FIRMWARE UPDATE: %s ***", fw->next_target_path);
    demo_log(INFO,
             "Update success for " AVS_QUOTE_MACRO(
                     ANJAY_ADVANCED_FW_UPDATE_OID) "/%u instance",
             fw->iid);
    memcpy(fw->current_ver, fw->metadata.pkg_ver, fw->metadata.pkg_ver_len);
    anjay_advanced_fw_update_set_state_and_result(
            fw->anjay, fw->iid, ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE,
            ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS);
    return 0;
}

static anjay_advanced_fw_update_handlers_t handlers = {
    .stream_open = fw_stream_open,
    .stream_write = fw_update_common_write,
    .stream_finish = fw_update_common_finish,
    .reset = fw_update_common_reset,
    .get_pkg_version = fw_update_common_get_pkg_version,
    .get_current_version = fw_update_common_get_current_version,
    .perform_upgrade = fw_update_common_perform_upgrade
};

int advanced_firmware_update_additional_image_install(
        anjay_t *anjay,
        anjay_iid_t iid,
        advanced_fw_update_logic_t *fw_table,
        anjay_advanced_fw_update_initial_state_t *init_state,
        const avs_net_security_info_t *security_info,
        const char *component_name) {
    advanced_fw_update_logic_t *fw_logic = &fw_table[iid];
    memcpy(fw_logic->current_ver, VER_DEFAULT, sizeof(VER_DEFAULT));
    fw_global = fw_logic;
    if (security_info) {
        memcpy(&fw_logic->security_info, security_info,
               sizeof(fw_logic->security_info));
        handlers.get_security_config =
                advanced_firmware_update_get_security_config;
    } else {
        handlers.get_security_config = NULL;
    }
    int result =
            anjay_advanced_fw_update_instance_add(anjay, fw_logic->iid,
                                                  component_name, &handlers,
                                                  fw_table, init_state);
    if (!result) {
        fw_logic->check_yourself = prepare_and_validate_update;
        fw_logic->update_yourself = update;
    }
    if (result) {
        memset(fw_global, 0x00, sizeof(advanced_fw_update_logic_t));
    }
    return result;
}
