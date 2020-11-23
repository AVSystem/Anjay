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

#ifndef SERVER_MOD_SERVER_H
#define SERVER_MOD_SERVER_H
#include <anjay_init.h>

#include <anjay/core.h>
#include <anjay/server.h>

#include <anjay_modules/anjay_utils_core.h>

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
} server_rid_t;

typedef struct {
    /* mandatory resources */
    anjay_ssid_t ssid;
    bool has_ssid;
    anjay_binding_mode_t binding;
    bool has_binding;
    int32_t lifetime;
    bool has_lifetime;
    int32_t default_min_period;
    bool has_default_min_period;
    int32_t default_max_period;
    bool has_default_max_period;
#ifndef ANJAY_WITHOUT_DEREGISTER
    int32_t disable_timeout;
    bool has_disable_timeout;
#endif // ANJAY_WITHOUT_DEREGISTER
    bool notification_storing;
    bool has_notification_storing;

    anjay_iid_t iid;

} server_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(server_instance_t) instances;
    AVS_LIST(server_instance_t) saved_instances;
    bool modified_since_persist;
    bool saved_modified_since_persist;
    bool in_transaction;
} server_repr_t;

static inline void _anjay_serv_mark_modified(server_repr_t *repr) {
    repr->modified_since_persist = true;
}

static inline void _anjay_serv_clear_modified(server_repr_t *repr) {
    repr->modified_since_persist = false;
}

size_t _anjay_server_object_get_instances_count(anjay_t *anjay);

#define server_log(level, ...) _anjay_log(server, level, __VA_ARGS__)

VISIBILITY_PRIVATE_HEADER_END

#endif /* SERVER_MOD_SERVER_H */
