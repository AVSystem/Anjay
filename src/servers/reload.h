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

#ifndef ANJAY_SERVERS_RELOAD_H
#define ANJAY_SERVERS_RELOAD_H

#include <anjay/core.h>

#if !defined(ANJAY_SERVERS_INTERNALS) && !defined(ANJAY_TEST)
#    error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_schedule_delayed_reload_servers(anjay_t *anjay);

int _anjay_schedule_refresh_server(anjay_server_info_t *server,
                                   avs_time_duration_t delay);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_RELOAD_H
