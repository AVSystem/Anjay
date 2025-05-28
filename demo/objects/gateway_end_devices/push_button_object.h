/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */
#ifndef PUSH_BUTTON_OBJECT_H
#define PUSH_BUTTON_OBJECT_H

#include <anjay/anjay.h>

const anjay_dm_object_def_t **push_button_object_create(anjay_iid_t id);
void push_button_object_release(const anjay_dm_object_def_t **def);
void push_button_press(anjay_t *anjay, const anjay_dm_object_def_t **def);
void push_button_release(anjay_t *anjay, const anjay_dm_object_def_t **def);

#endif // PUSH_BUTTON_OBJECT_H
