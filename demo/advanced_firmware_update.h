/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ADVANCED_FIRMWARE_UPDATE_H
#define ADVANCED_FIRMWARE_UPDATE_H

#include <stddef.h>
#include <stdio.h>

#include "demo_utils.h"
#include <anjay/advanced_fw_update.h>
#include <anjay/anjay_config.h>

#define IMG_VER_STR_MAX_LEN (sizeof("255.255.65535.4294967295") - 1)
#define VER_DEFAULT "1.0"

#define FW_UPDATE_IID_APP 0
#define FW_UPDATE_IID_TEE 1
#define FW_UPDATE_IID_BOOT 2
#define FW_UPDATE_IID_MODEM 3
#define FW_UPDATE_IID_IMAGE_SLOTS 4
#define METADATA_LINKED_SLOTS 8

#define FORCE_ERROR_OUT_OF_MEMORY 1
#define FORCE_ERROR_FAILED_UPDATE 2
#define FORCE_DELAYED_SUCCESS 3
#define FORCE_DELAYED_ERROR_FAILED_UPDATE 4
#define FORCE_SET_SUCCESS_FROM_PERFORM_UPGRADE 5
#define FORCE_SET_FAILURE_FROM_PERFORM_UPGRADE 6
#define FORCE_DO_NOTHING 7
#define FORCE_DEFER 8

enum target_image_t {
    TARGET_IMAGE_TYPE_APPLICATION = 0,
    TARGET_IMAGE_TYPE_ADDITIONAL_IMAGE,
};

typedef struct advanced_firmware_metadata {
    uint8_t magic[8];
    uint16_t version;
    uint16_t force_error_case;
    uint32_t crc;
    uint8_t linked[METADATA_LINKED_SLOTS];
    uint8_t pkg_ver_len;
    uint8_t pkg_ver[IMG_VER_STR_MAX_LEN + 1];
} advanced_fw_metadata_t;

typedef struct unpacked_imgs_info {
    char *path;
    advanced_fw_metadata_t meta;
} unpacked_imgs_info_t;

typedef struct advanced_firmware_multipkg_metadata {
    uint8_t magic[8];
    uint16_t version;
    uint16_t packages_count;
    uint32_t package_len[FW_UPDATE_IID_IMAGE_SLOTS];
} advanced_fw_multipkg_metadata_t;

struct advanced_fw_update_logic {
    anjay_iid_t iid;
    const char *original_img_file_path;
    char current_ver[IMG_VER_STR_MAX_LEN + 1];
    anjay_t *anjay;
    advanced_fw_metadata_t metadata;
    char *administratively_set_target_path;
    char *next_target_path;
    const char *persistence_file;
    FILE *stream;
    avs_net_security_info_t security_info;
    int (*check_yourself)(struct advanced_fw_update_logic *);
    int (*update_yourself)(struct advanced_fw_update_logic *);
    avs_sched_handle_t update_job;
};
typedef struct advanced_fw_update_logic advanced_fw_update_logic_t;

int advanced_firmware_update_application_install(
        anjay_t *anjay,
        advanced_fw_update_logic_t *fw_logic,
        anjay_advanced_fw_update_initial_state_t *init_state,
        const avs_net_security_info_t *security_info,
        const avs_coap_udp_tx_params_t *tx_params);
int advanced_firmware_update_app_perform(advanced_fw_update_logic_t *fw);
const char *advanced_firmware_update_app_get_pkg_version(anjay_iid_t iid,
                                                         void *fw_);
const char *advanced_firmware_update_app_get_current_version(anjay_iid_t iid,
                                                             void *fw_);

int advanced_firmware_update_additional_image_install(
        anjay_t *anjay,
        anjay_iid_t iid,
        advanced_fw_update_logic_t *fw_table,
        anjay_advanced_fw_update_initial_state_t *init_state,
        const char *component_name);

const char *
advanced_firmware_update_additional_image_get_pkg_version(anjay_iid_t iid,
                                                          void *fw_);
const char *
advanced_firmware_update_additional_image_get_current_version(anjay_iid_t iid,
                                                              void *fw_);

int advanced_firmware_update_install(
        anjay_t *anjay,
        advanced_fw_update_logic_t *fw_table,
        const char *persistence_file,
        const avs_net_security_info_t *security_info,
        const avs_coap_udp_tx_params_t *tx_params,
        anjay_advanced_fw_update_result_t delayed_result,
        bool prefer_same_socket_downloads,
        const char *original_img_file_path
#ifdef ANJAY_WITH_SEND
        ,
        bool use_lwm2m_send
#endif // ANJAY_WITH_SEND
);

void advanced_firmware_update_uninstall(advanced_fw_update_logic_t *fw_table);
int fw_update_common_open(anjay_iid_t iid, void *fw_);
int fw_update_common_write(anjay_iid_t iid,
                           void *user_ptr,
                           const void *data,
                           size_t length);
const char *fw_update_common_get_current_version(anjay_iid_t iid, void *fw_);
const char *fw_update_common_get_pkg_version(anjay_iid_t iid, void *fw_);
int fw_update_common_finish(anjay_iid_t iid, void *fw_);
void fw_update_common_reset(anjay_iid_t iid, void *fw_);
int fw_update_common_perform_upgrade(
        anjay_iid_t iid,
        void *fw_,
        const anjay_iid_t *requested_supplemental_iids,
        size_t requested_supplemental_iids_count);
int fw_update_common_maybe_create_firmware_file(advanced_fw_update_logic_t *fw);

typedef struct {
    anjay_advanced_fw_update_state_t inst_states[FW_UPDATE_IID_IMAGE_SLOTS];
    anjay_advanced_fw_update_result_t inst_results[FW_UPDATE_IID_IMAGE_SLOTS];
    char *next_target_paths[FW_UPDATE_IID_IMAGE_SLOTS];
} states_results_paths_t;

int advanced_firmware_update_read_states_results_paths(
        advanced_fw_update_logic_t *fw_table,
        states_results_paths_t *out_states_results_paths);

int advanced_firmware_update_write_persistence_file(
        const char *path,
        states_results_paths_t *states_results_paths,
        anjay_advanced_fw_update_severity_t severity,
        avs_time_real_t last_state_change_time,
        avs_time_real_t update_deadline,
        const char *current_ver);

void advanced_firmware_update_delete_persistence_file(
        const advanced_fw_update_logic_t *fw);

void advanced_firmware_update_set_package_path(
        advanced_fw_update_logic_t *fw_logic, const char *path);

#endif // ADVANCED_FIRMWARE_UPDATE_H
