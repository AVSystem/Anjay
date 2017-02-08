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

#ifndef ANJAY_DM_QUERY_H
#define ANJAY_DM_QUERY_H

#include <anjay/anjay.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_find_server_iid(anjay_t *anjay,
                           anjay_ssid_t ssid,
                           anjay_iid_t *out_iid);

int _anjay_find_security_iid(anjay_t *anjay,
                             anjay_ssid_t ssid,
                             anjay_iid_t *out_iid);

int _anjay_ssid_from_server_iid(anjay_t *anjay,
                                anjay_iid_t server_iid,
                                anjay_ssid_t *out_ssid);

int _anjay_ssid_from_security_iid(anjay_t *anjay,
                                  anjay_iid_t security_iid,
                                  uint16_t *out_ssid);

#ifdef WITH_BOOTSTRAP
bool _anjay_is_bootstrap_security_instance(anjay_t *anjay,
                                           anjay_iid_t security_iid);
#else
#define _anjay_is_bootstrap_security_instance(...) (false)
#endif

struct timespec _anjay_disable_timeout_from_server_iid(anjay_t *anjay,
                                                       anjay_iid_t server_iid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_DM_QUERY_H
