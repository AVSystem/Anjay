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

#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H
#include <config.h>

#include <anjay/anjay.h>
#include <anjay/server.h>

#include <avsystem/commons/log.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    SERV_RES_SSID = 0,
    SERV_RES_LIFETIME = 1,
    SERV_RES_DEFAULT_MIN_PERIOD = 2,
    SERV_RES_DEFAULT_MAX_PERIOD = 3,
    SERV_RES_DISABLE = 4,
    SERV_RES_DISABLE_TIMEOUT = 5,
    SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE = 6,
    SERV_RES_BINDING = 7,
    SERV_RES_REGISTRATION_UPDATE_TRIGGER = 8,

    _SERV_RID_BOUND
} server_rid_t;

typedef struct {
    anjay_iid_t iid;
    anjay_server_instance_t data;
    /* mandatory resources */
    bool has_ssid;
    bool has_binding;
    bool has_lifetime;
    bool has_notification_storing;
} server_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(server_instance_t) instances;
    AVS_LIST(server_instance_t) saved_instances;
} server_repr_t;

#define server_log(level, ...) avs_log(server, level, __VA_ARGS__)

VISIBILITY_PRIVATE_HEADER_END

#endif /* SERVER_SERVER_H */
