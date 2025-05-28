/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */
#ifndef GW_BINARY_APP_DATA_CONTAINER_H
#define GW_BINARY_APP_DATA_CONTAINER_H

#include <anjay/anjay.h>

#include <avsystem/commons/avs_list.h>

const anjay_dm_object_def_t **
gw_binary_app_data_container_object_create(anjay_iid_t id);
void gw_binary_app_data_container_object_release(
        const anjay_dm_object_def_t **def);
int gw_binary_app_data_container_write(anjay_t *anjay,
                                       const anjay_dm_object_def_t **def,
                                       anjay_iid_t iid,
                                       anjay_riid_t riid,
                                       const char *value);

#endif // GW_BINARY_APP_DATA_CONTAINER_H
