/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ACCESS_CONTROL_H
#define	ACCESS_CONTROL_H

#include "anjay.h"
#include "dm.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_ssid_t ssid;
    anjay_request_action_t action;
} anjay_action_info_t;

#ifdef WITH_ACCESS_CONTROL

bool _anjay_access_control_action_allowed(anjay_t *anjay,
                                          const anjay_action_info_t* info);

#else

#define _anjay_access_control_action_allowed(anjay, info) ((void) (info), true)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif	/* ACCESS_CONTROL_H */
