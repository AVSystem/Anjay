/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SOFTWARE_MGMT_H
#define SOFTWARE_MGMT_H

#include <stdbool.h>
#include <stdint.h>

#include <anjay/anjay.h>
#include <anjay/sw_mgmt.h>

#define SW_MGMT_PACKAGE_COUNT 3

typedef struct software_metadata {
    uint8_t magic[8];
    uint16_t version;
    uint16_t force_error_case;
    uint32_t crc;
} sw_metadata_t;

typedef struct {
    FILE *stream;
    char *administratively_set_target_path;
    char *next_target_path;
    sw_metadata_t metadata;
} sw_mgmt_logic_t;

typedef struct {
    anjay_t *anjay;
    const char *persistence_file;
    avs_net_security_info_t *security_info;
    avs_coap_udp_tx_params_t *coap_tx_params;
    avs_time_duration_t *tcp_request_timeout;
    bool auto_suspend;
    bool terminate_after_downloading;
    bool disable_repeated_activation_deactivation;
    sw_mgmt_logic_t *sw_mgmt_table;
} sw_mgmt_common_logic_t;

int sw_mgmt_install(anjay_t *anjay,
                    sw_mgmt_common_logic_t *sw_mgmt_common,
                    sw_mgmt_logic_t *sw_table,
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
);

void sw_mgmt_update_destroy(sw_mgmt_logic_t *sw_mgmt_table);

void sw_mgmt_set_package_path(sw_mgmt_logic_t *sw_mgmt, const char *path);

#endif // SOFTWARE_MGMT_H
