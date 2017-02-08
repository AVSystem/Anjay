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

static int ssid_cmp(const void *a, const void *b, size_t element_size) {
    assert(element_size == sizeof(anjay_ssid_t));
    return *((const anjay_ssid_t *) a) - *((const anjay_ssid_t *) b);
}

static int validate_instance(sec_instance_t *it) {
    if (!it->server_uri
            || !it->has_is_bootstrap
            || !it->has_security_mode
            || (!it->is_bootstrap && !it->has_ssid)) {
        return -1;
    }
    if (_anjay_sec_validate_security_mode(it->security_mode)) {
        security_log(ERROR, "Security mode %d not supported",
                     (int) it->security_mode);
        return -1;
    }
    if (it->security_mode != ANJAY_UDP_SECURITY_NOSEC) {
        if (!it->public_cert_or_psk_identity.data
                || !it->private_cert_or_psk_key.data) {
            return -1;
        }
    }
    return 0;
}

int _anjay_sec_object_validate(sec_repr_t *repr) {
    AVS_LIST(anjay_ssid_t) seen_ssids = NULL;
    AVS_LIST(sec_instance_t) it;
    int result = 0;
    bool bootstrap_server_present = false;
    AVS_LIST_FOREACH(it, repr->instances) {
        /* Assume something will go wrong */
        result = -1;
        if (validate_instance(it)) {
            goto finish;
        }

        if (it->is_bootstrap) {
            if (bootstrap_server_present) {
                goto finish;
            }
            bootstrap_server_present = true;
        } else {
            if (!AVS_LIST_INSERT_NEW(anjay_ssid_t, &seen_ssids)) {
                goto finish;
            }
            *seen_ssids = it->ssid;
        }
        /* We are still there - nothing went wrong, continue */
        result = 0;
    }

    if (!result && seen_ssids) {
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
finish:
    AVS_LIST_CLEAR(&seen_ssids);
    return result;
}

int _anjay_sec_transaction_begin_impl(sec_repr_t *repr) {
    assert(!repr->saved_instances);
    if (!repr->instances) {
        return 0;
    }
    repr->saved_instances = _anjay_sec_clone_instances(repr);
    if (!repr->saved_instances) {
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

int _anjay_sec_transaction_commit_impl(sec_repr_t *repr) {
    _anjay_sec_destroy_instances(&repr->saved_instances);
    return 0;
}

int _anjay_sec_transaction_validate_impl(sec_repr_t *repr) {
    return _anjay_sec_object_validate(repr);
}

int _anjay_sec_transaction_rollback_impl(sec_repr_t *repr) {
    _anjay_sec_destroy_instances(&repr->instances);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    return 0;
}
