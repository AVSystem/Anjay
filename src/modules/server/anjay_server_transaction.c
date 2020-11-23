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

#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_SERVER

#    include <assert.h>
#    include <inttypes.h>
#    include <string.h>

#    include "anjay_server_transaction.h"
#    include "anjay_server_utils.h"

#    include <anjay_modules/anjay_dm_utils.h>

VISIBILITY_SOURCE_BEGIN

static int ssid_cmp(const void *a, const void *b, size_t size) {
    assert(size == sizeof(anjay_ssid_t));
    (void) size;
    return *((const anjay_ssid_t *) a) - *((const anjay_ssid_t *) b);
}

#    define LOG_VALIDATION_FAILED(ServInstance, ...)                    \
        do {                                                            \
            char buffer[128];                                           \
            int offset = snprintf(buffer, sizeof(buffer),               \
                                  "/%u/%u: ", ANJAY_DM_OID_SERVER,      \
                                  (unsigned) (ServInstance)->iid);      \
            if (offset < 0) {                                           \
                offset = 0;                                             \
            }                                                           \
            snprintf(&buffer[offset], sizeof(buffer) - (size_t) offset, \
                     __VA_ARGS__);                                      \
            server_log(WARNING, "%s", buffer);                          \
        } while (0)

static int validate_instance(server_instance_t *it) {
    if (!it->has_ssid) {
        LOG_VALIDATION_FAILED(
                it, _("missing mandatory 'Short Server ID' resource value"));
        return -1;
    }
    if (it->ssid < 1 || it->ssid >= UINT16_MAX) {
        LOG_VALIDATION_FAILED(
                it, _("invalid 'Short Server ID' resource value: ") "%" PRIu16,
                it->ssid);
        return -1;
    }
    if (!it->has_binding) {
        LOG_VALIDATION_FAILED(it,
                              _("missing mandatory 'Binding' resource value"));
        return -1;
    }
    if (!it->has_lifetime) {
        LOG_VALIDATION_FAILED(it,
                              _("missing mandatory 'Lifetime' resource value"));
        return -1;
    }
    if (!it->has_notification_storing) {
        LOG_VALIDATION_FAILED(it,
                              _("missing mandatory 'Notification Storing "
                                "when disabled or offline' resource value"));
        return -1;
    }

    if (it->lifetime <= 0) {
        LOG_VALIDATION_FAILED(it,
                              _("Lifetime value is non-positive: ") "%" PRId32,
                              it->lifetime);
        return -1;
    }
    if (it->has_default_max_period && it->default_max_period <= 0) {
        LOG_VALIDATION_FAILED(it, _("Default Max Period is non-positive"));
        return -1;
    }
    if (it->has_default_min_period && it->default_min_period < 0) {
        LOG_VALIDATION_FAILED(it, _("Default Min Period is negative"));
        return -1;
    }
#    ifndef ANJAY_WITHOUT_DEREGISTER
    if (it->has_disable_timeout && it->disable_timeout < 0) {
        LOG_VALIDATION_FAILED(it, _("Disable Timeout is negative"));
        return -1;
    }
#    endif // ANJAY_WITHOUT_DEREGISTER
    if (!anjay_binding_mode_valid(it->binding)) {
        LOG_VALIDATION_FAILED(it, _("Incorrect binding mode ") "%s",
                              it->binding);
        return -1;
    }

    return 0;
}

int _anjay_serv_object_validate(server_repr_t *repr) {
    if (!repr->instances) {
        return 0;
    }
    int result = 0;

    AVS_LIST(anjay_ssid_t) seen_ssids = NULL;
    AVS_LIST(server_instance_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (validate_instance(it)) {
            result = ANJAY_ERR_BAD_REQUEST;
            break;
        }

        if (!AVS_LIST_INSERT_NEW(anjay_ssid_t, &seen_ssids)) {
            result = ANJAY_ERR_INTERNAL;
            break;
        }
        *seen_ssids = it->ssid;
    }

    /* Test for SSID duplication */
    if (!result) {
        AVS_LIST_SORT(&seen_ssids, ssid_cmp);
        AVS_LIST(anjay_ssid_t) prev = seen_ssids;
        AVS_LIST(anjay_ssid_t) next = AVS_LIST_NEXT(seen_ssids);
        while (next) {
            if (*prev == *next) {
                /* Duplicate found */
                result = ANJAY_ERR_BAD_REQUEST;
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
    assert(!repr->in_transaction);
    repr->saved_instances = _anjay_serv_clone_instances(repr);
    if (!repr->saved_instances && repr->instances) {
        return ANJAY_ERR_INTERNAL;
    }
    repr->saved_modified_since_persist = repr->modified_since_persist;
    repr->in_transaction = true;
    return 0;
}

int _anjay_serv_transaction_commit_impl(server_repr_t *repr) {
    assert(repr->in_transaction);
    _anjay_serv_destroy_instances(&repr->saved_instances);
    repr->in_transaction = false;
    return 0;
}

int _anjay_serv_transaction_validate_impl(server_repr_t *repr) {
    assert(repr->in_transaction);
    return _anjay_serv_object_validate(repr);
}

int _anjay_serv_transaction_rollback_impl(server_repr_t *repr) {
    assert(repr->in_transaction);
    _anjay_serv_destroy_instances(&repr->instances);
    repr->modified_since_persist = repr->saved_modified_since_persist;
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    repr->in_transaction = false;
    return 0;
}

#endif // ANJAY_WITH_MODULE_SERVER
