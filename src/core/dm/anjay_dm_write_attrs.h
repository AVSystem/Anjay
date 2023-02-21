/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_WRITE_ATTRS_CORE_H
#define ANJAY_WRITE_ATTRS_CORE_H

#include <anjay/core.h>

#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

bool _anjay_dm_resource_specific_request_attrs_empty(
        const anjay_request_attributes_t *attrs);
bool _anjay_dm_request_attrs_empty(const anjay_request_attributes_t *attrs);

void _anjay_update_r_attrs(anjay_dm_r_attributes_t *attrs_ptr,
                           const anjay_request_attributes_t *request_attrs);

bool _anjay_r_attrs_valid(const anjay_dm_r_attributes_t *attrs);

int _anjay_dm_write_attributes(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj,
                               const anjay_request_t *request,
                               anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_WRITE_ATTRS_CORE_H
