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

#include "transaction.h"
#include "utils.h"

VISIBILITY_SOURCE_BEGIN

static int ssid_cmp(const void *a, const void *b, size_t size) {
    assert(size == sizeof(anjay_ssid_t));
    return *((const anjay_ssid_t *) a) - *((const anjay_ssid_t *) b);
}

int _anjay_serv_object_validate(server_repr_t *repr) {
    if (!repr->instances) {
        return 0;
    }
    int result = 0;

    AVS_LIST(anjay_ssid_t) seen_ssids = NULL;
    AVS_LIST(server_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (!it->has_ssid
                || !it->has_binding
                || !it->has_lifetime
                || !it->has_notification_storing) {
            result = -1;
            break;
        }
        if (it->data.lifetime <= 0
                || it->data.default_max_period == 0
                || it->data.binding == ANJAY_BINDING_NONE) {
            result = -1;
            break;
        }
        if (!AVS_LIST_INSERT_NEW(anjay_ssid_t, &seen_ssids)) {
            result = -1;
            break;
        }
        *seen_ssids = it->data.ssid;
    }

    /* Test for SSID duplication */
    if (!result) {
        AVS_LIST_SORT(&seen_ssids, ssid_cmp);
        AVS_LIST(anjay_ssid_t) prev = seen_ssids;
        AVS_LIST(anjay_ssid_t) next = AVS_LIST_NEXT(seen_ssids);
        while (next) {
            if (*prev == *next) {
                /* Duplicate found */
                result = -1;
                break;
            }
            prev = next;
            next = AVS_LIST_NEXT(next);
        }
    }
    AVS_LIST_CLEAR(&seen_ssids);
    return result;
}

int _anjay_serv_transaction_begin_impl(server_repr_t *repr) {
    assert(!repr->saved_instances);
    if (!repr->instances) {
        return 0;
    }
    repr->saved_instances = _anjay_serv_clone_instances(repr);
    if (!repr->saved_instances) {
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

int _anjay_serv_transaction_commit_impl(server_repr_t *repr) {
    _anjay_serv_destroy_instances(&repr->saved_instances);
    return 0;
}

int _anjay_serv_transaction_validate_impl(server_repr_t *repr) {
    return _anjay_serv_object_validate(repr);
}

int _anjay_serv_transaction_rollback_impl(server_repr_t *repr) {
    _anjay_serv_destroy_instances(&repr->instances);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    return 0;
}
