/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_LITE_OBJS_H
#define ANJAY_LITE_OBJS_H

#include <stdint.h>

#include <anj/sdm_io.h>

#include <anjay_lite/anjay_lite.h>

sdm_obj_t *anjay_lite_security_obj_setup(uint16_t ssid,
                                         char *uri,
                                         anjay_security_mode_t sec_mode);

sdm_obj_t *anjay_lite_server_obj_setup(uint16_t ssid,
                                       uint32_t lifetime,
                                       fluf_binding_type_t binding);

uint32_t anjay_lite_server_obj_get_lifetime(void);

bool anjay_lite_server_obj_update_trigger_active(void);

#endif // ANJAY_LITE_OBJS_H
