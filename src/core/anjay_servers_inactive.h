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

#ifndef ANJAY_SERVERS_INACTIVE_H
#define ANJAY_SERVERS_INACTIVE_H

#include "anjay_servers_private.h"

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_OBSERVE_SOURCE) \
        && !defined(ANJAY_TEST)
#    error "servers_inactive.h shall only be included from " \
           "servers/ or from the Observe subsystem"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

bool _anjay_server_active(anjay_server_info_t *server);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_INACTIVE_H
