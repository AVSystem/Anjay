/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <stddef.h>
#include <stdio.h>

#include <anjay/anjay_config.h>
#include <anjay/fw_update.h>

#include "iosched.h"

typedef struct firmware_metadata {
    uint8_t magic[8]; // "ANJAY_FW"
    uint16_t version;
    uint16_t force_error_case;
    uint32_t crc;
} fw_metadata_t;

typedef struct {
    anjay_t *anjay;
    fw_metadata_t metadata;
    char *administratively_set_target_path;
    char *next_target_path;
    char *package_uri;
    const char *persistence_file;
    FILE *stream;
    avs_net_security_info_t security_info;
} fw_update_logic_t;

int firmware_update_install(anjay_t *anjay,
                            iosched_t *iosched,
                            fw_update_logic_t *fw,
                            const char *persistence_file,
                            const avs_net_security_info_t *security_info,
                            const avs_coap_udp_tx_params_t *tx_params,
                            anjay_fw_update_result_t delayed_result);

void firmware_update_destroy(fw_update_logic_t *fw_update);

void firmware_update_set_package_path(fw_update_logic_t *fw_update,
                                      const char *path);

#endif /* FIRMWARE_UPDATE_H */
