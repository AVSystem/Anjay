/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <anj/sdm_io.h>

int fw_update_object_install(sdm_data_model_t *dm,
                             const char *firmware_version,
                             const char *endpoint_name);

void fw_update_check(void);

#endif // FIRMWARE_UPDATE_H
