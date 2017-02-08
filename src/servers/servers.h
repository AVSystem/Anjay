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

#ifndef ANJAY_SERVERS_SERVERS_H
#define ANJAY_SERVERS_SERVERS_H

#include <anjay/anjay.h>

#include "../servers.h"

#ifndef ANJAY_SERVERS_INTERNALS
#error "Headers from servers/ are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * Retryable job backoff configuration for retryable server jobs
 * (Register/Update).
 */
#define ANJAY_SERVER_RETRYABLE_BACKOFF \
    ((anjay_sched_retryable_backoff_t){ \
        .delay = { 1, 0 }, \
        .max_delay = { 120, 0 } \
     })

/**
 * Cleans up server data. Does not send De-Register message.
 */
void _anjay_server_cleanup(anjay_t *anjay, anjay_active_server_info_t *server);

/**
 * Returns Security IID for given @p ssid . Handles ANJAY_SSID_BOOTSTRAP
 * constant as if it were an actual SSID.
 */
int _anjay_server_security_iid(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               anjay_iid_t *out_iid);

AVS_LIST(anjay_active_server_info_t) *
_anjay_servers_find_active_insert_ptr(anjay_servers_t *servers,
                                      anjay_ssid_t ssid);

AVS_LIST(anjay_inactive_server_info_t) *
_anjay_servers_find_inactive_insert_ptr(anjay_servers_t *servers,
                                        anjay_ssid_t ssid);

AVS_LIST(anjay_active_server_info_t) *
_anjay_servers_find_active_ptr(anjay_servers_t *servers,
                               anjay_ssid_t ssid);

AVS_LIST(anjay_inactive_server_info_t) *
_anjay_servers_find_inactive_ptr(anjay_servers_t *servers,
                                 anjay_ssid_t ssid);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_SERVERS_H
