/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */
#ifndef TEMPERATURE_OBJECT_H
#define TEMPERATURE_OBJECT_H

#include <anjay/dm.h>

const anjay_dm_object_def_t **temperature_object_create(anjay_iid_t id);
void temperature_object_release(const anjay_dm_object_def_t **def);
void temperature_object_update_value(anjay_t *anjay,
                                     const anjay_dm_object_def_t **def);

#endif // TEMPERATURE_OBJECT_H
