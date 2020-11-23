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

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_notify.h>

#include "coap/anjay_content_format.h"

#include "anjay_access_utils_private.h"
#include "anjay_core.h"
#include "anjay_servers_utils.h"
#include "observe/anjay_observe_core.h"

VISIBILITY_SOURCE_BEGIN

#ifdef ANJAY_WITH_OBSERVE
static int observe_notify(anjay_t *anjay, anjay_notify_queue_t queue) {
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (it->instance_set_changes.instance_set_changed) {
            _anjay_update_ret(
                    &ret,
                    _anjay_observe_notify(anjay, &MAKE_OBJECT_PATH(it->oid),
                                          _anjay_dm_current_ssid(anjay), true));
        } else {
            AVS_LIST(anjay_notify_queue_resource_entry_t) it2;
            AVS_LIST_FOREACH(it2, it->resources_changed) {
                _anjay_update_ret(&ret,
                                  _anjay_observe_notify(
                                          anjay,
                                          &MAKE_RESOURCE_PATH(it->oid, it2->iid,
                                                              it2->rid),
                                          _anjay_dm_current_ssid(anjay), true));
            }
        }
    }
    return ret;
}
#else // ANJAY_WITH_OBSERVE
#    define observe_notify(anjay, queue) (0)
#endif // ANJAY_WITH_OBSERVE

static int
security_modified_notify(anjay_t *anjay,
                         anjay_notify_queue_object_entry_t *security) {
    int ret = 0;
    int32_t last_iid = -1;
    AVS_LIST(anjay_notify_queue_resource_entry_t) it;
    AVS_LIST_FOREACH(it, security->resources_changed) {
        if (it->iid != last_iid) {
            _anjay_update_ret(&ret,
                              _anjay_schedule_socket_update(anjay, it->iid));
            last_iid = it->iid;
        }
    }
    if (security->instance_set_changes.instance_set_changed) {
        _anjay_update_ret(&ret, _anjay_schedule_reload_servers(anjay));
    }
    return ret;
}

static int server_modified_notify(anjay_t *anjay,
                                  anjay_notify_queue_object_entry_t *server) {
    int ret = 0;
    if (server->instance_set_changes.instance_set_changed) {
        _anjay_update_ret(&ret, _anjay_schedule_reload_servers(anjay));
    } else {
        AVS_LIST(anjay_notify_queue_resource_entry_t) it;
        AVS_LIST_FOREACH(it, server->resources_changed) {
            if (it->rid != ANJAY_DM_RID_SERVER_BINDING
                    && it->rid != ANJAY_DM_RID_SERVER_LIFETIME) {
                continue;
            }
            const anjay_uri_path_t path =
                    MAKE_RESOURCE_PATH(ANJAY_DM_OID_SERVER, it->iid,
                                       ANJAY_DM_RID_SERVER_SSID);
            int64_t ssid;
            if (_anjay_dm_read_resource_i64(anjay, &path, &ssid) || ssid <= 0
                    || ssid >= UINT16_MAX) {
                _anjay_update_ret(&ret, -1);
            } else if (_anjay_servers_find_active(anjay, (anjay_ssid_t) ssid)) {
                _anjay_update_ret(&ret,
                                  anjay_schedule_registration_update(
                                          anjay, (anjay_ssid_t) ssid));
            }
        }
    }
    return ret;
}

static int anjay_notify_perform_impl(anjay_t *anjay,
                                     anjay_notify_queue_t queue,
                                     bool server_notify) {
    if (!queue) {
        return 0;
    }
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (it->oid > ANJAY_DM_OID_SERVER) {
            break;
        } else if (it->oid == ANJAY_DM_OID_SECURITY) {
            _anjay_update_ret(&ret, security_modified_notify(anjay, it));
        } else if (server_notify && it->oid == ANJAY_DM_OID_SERVER) {
            _anjay_update_ret(&ret, server_modified_notify(anjay, it));
        }
    }
    _anjay_update_ret(&ret, observe_notify(anjay, queue));
    _anjay_update_ret(&ret, _anjay_sync_access_control(anjay, queue));
    AVS_LIST(anjay_dm_installed_module_t) module;
    AVS_LIST_FOREACH(module, anjay->dm.modules) {
        if (module->def->notify_callback) {
            _anjay_update_ret(&ret,
                              module->def->notify_callback(anjay, queue,
                                                           module->arg));
        }
    }
    return ret;
}

int _anjay_notify_perform(anjay_t *anjay, anjay_notify_queue_t queue) {
    return anjay_notify_perform_impl(anjay, queue, true);
}

int _anjay_notify_perform_without_servers(anjay_t *anjay,
                                          anjay_notify_queue_t queue) {
    return anjay_notify_perform_impl(anjay, queue, false);
}

int _anjay_notify_flush(anjay_t *anjay, anjay_notify_queue_t *queue_ptr) {
    int result = _anjay_notify_perform(anjay, *queue_ptr);
    _anjay_notify_clear_queue(queue_ptr);
    return result;
}

static AVS_LIST(anjay_notify_queue_object_entry_t) *
find_or_create_object_entry(anjay_notify_queue_t *out_queue, anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *it;
    AVS_LIST_FOREACH_PTR(it, out_queue) {
        if ((*it)->oid == oid) {
            return it;
        } else if ((*it)->oid > oid) {
            break;
        }
    }
    if (AVS_LIST_INSERT_NEW(anjay_notify_queue_object_entry_t, it)) {
        (*it)->oid = oid;
        return it;
    } else {
        return NULL;
    }
}

static int add_entry_to_iid_set(AVS_LIST(anjay_iid_t) *iid_set_ptr,
                                anjay_iid_t iid) {
    AVS_LIST_ITERATE_PTR(iid_set_ptr) {
        if (**iid_set_ptr == iid) {
            return 0;
        } else if (**iid_set_ptr > iid) {
            break;
        }
    }
    if (AVS_LIST_INSERT_NEW(anjay_iid_t, iid_set_ptr)) {
        **iid_set_ptr = iid;
        return 0;
    } else {
        return -1;
    }
}

static void remove_entry_from_iid_set(AVS_LIST(anjay_iid_t) *iid_set_ptr,
                                      anjay_iid_t iid) {
    AVS_LIST_ITERATE_PTR(iid_set_ptr) {
        if (**iid_set_ptr >= iid) {
            if (**iid_set_ptr == iid) {
                AVS_LIST_DELETE(iid_set_ptr);
            }
            return;
        }
    }
}

static void delete_notify_queue_object_entry_if_empty(
        AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr) {
    if (!entry_ptr || !*entry_ptr) {
        return;
    }
    if ((*entry_ptr)->instance_set_changes.instance_set_changed
            || (*entry_ptr)->resources_changed) {
        // entry not empty
        return;
    }
    assert(!(*entry_ptr)->instance_set_changes.known_added_iids);
    AVS_LIST_DELETE(entry_ptr);
}

int _anjay_notify_queue_instance_created(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    if (add_entry_to_iid_set(
                &(*entry_ptr)->instance_set_changes.known_added_iids, iid)) {
        anjay_log(ERROR, _("out of memory"));
        delete_notify_queue_object_entry_if_empty(entry_ptr);
        return -1;
    }
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

int _anjay_notify_queue_instance_removed(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    remove_entry_from_iid_set(
            &(*entry_ptr)->instance_set_changes.known_added_iids, iid);
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

int _anjay_notify_queue_instance_set_unknown_change(
        anjay_notify_queue_t *out_queue, anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

static int
compare_resource_entries(const anjay_notify_queue_resource_entry_t *left,
                         const anjay_notify_queue_resource_entry_t *right) {
    int result = left->iid - right->iid;
    if (!result) {
        result = left->rid - right->rid;
    }
    return result;
}

int _anjay_notify_queue_resource_change(anjay_notify_queue_t *out_queue,
                                        anjay_oid_t oid,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *obj_entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!obj_entry_ptr) {
        anjay_log(ERROR, _("out of memory"));
        return -1;
    }
    anjay_notify_queue_resource_entry_t new_entry = {
        .iid = iid,
        .rid = rid
    };
    AVS_LIST(anjay_notify_queue_resource_entry_t) *res_entry_ptr;
    AVS_LIST_FOREACH_PTR(res_entry_ptr, &(*obj_entry_ptr)->resources_changed) {
        int compare = compare_resource_entries(*res_entry_ptr, &new_entry);
        if (compare == 0) {
            return 0;
        } else if (compare > 0) {
            break;
        }
    }
    if (!AVS_LIST_INSERT_NEW(anjay_notify_queue_resource_entry_t,
                             res_entry_ptr)) {
        anjay_log(ERROR, _("out of memory"));
        if (!(*obj_entry_ptr)->instance_set_changes.instance_set_changed
                && !(*obj_entry_ptr)->resources_changed) {
            AVS_LIST_DELETE(obj_entry_ptr);
        }
        return -1;
    }
    **res_entry_ptr = new_entry;
    return 0;
}

void _anjay_notify_clear_queue(anjay_notify_queue_t *out_queue) {
    AVS_LIST_CLEAR(out_queue) {
        AVS_LIST_CLEAR(&(*out_queue)->instance_set_changes.known_added_iids);
        AVS_LIST_CLEAR(&(*out_queue)->resources_changed);
    }
}

static void notify_clb(avs_sched_t *sched, const void *dummy) {
    (void) dummy;
    anjay_t *anjay = _anjay_get_from_sched(sched);
    _anjay_notify_flush(anjay, &anjay->scheduled_notify.queue);
}

static int reschedule_notify(anjay_t *anjay) {
    if (anjay->scheduled_notify.handle) {
        return 0;
    }
    return AVS_SCHED_NOW(anjay->sched, &anjay->scheduled_notify.handle,
                         notify_clb, NULL, 0);
}

int _anjay_notify_instance_created(anjay_t *anjay,
                                   anjay_oid_t oid,
                                   anjay_iid_t iid) {
    int retval;
    (void) ((retval = _anjay_notify_queue_instance_created(
                     &anjay->scheduled_notify.queue, oid, iid))
            || (retval = reschedule_notify(anjay)));
    return retval;
}

int anjay_notify_changed(anjay_t *anjay,
                         anjay_oid_t oid,
                         anjay_iid_t iid,
                         anjay_rid_t rid) {
    int retval;
    (void) ((retval = _anjay_notify_queue_resource_change(
                     &anjay->scheduled_notify.queue, oid, iid, rid))
            || (retval = reschedule_notify(anjay)));
    return retval;
}

int anjay_notify_instances_changed(anjay_t *anjay, anjay_oid_t oid) {
    int retval;
    (void) ((retval = _anjay_notify_queue_instance_set_unknown_change(
                     &anjay->scheduled_notify.queue, oid))
            || (retval = reschedule_notify(anjay)));
    return retval;
}

#ifdef ANJAY_WITH_OBSERVATION_STATUS
anjay_resource_observation_status_t anjay_resource_observation_status(
        anjay_t *anjay, anjay_oid_t oid, anjay_iid_t iid, anjay_rid_t rid) {
    if (oid == ANJAY_ID_INVALID || iid == ANJAY_ID_INVALID
            || rid == ANJAY_ID_INVALID) {
        return (anjay_resource_observation_status_t) {
            .is_observed = false,
            .min_period = 0,
            .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
        };
    }

    if (oid == ANJAY_DM_OID_SECURITY
            && _anjay_servers_find_active_by_security_iid(anjay, iid)) {
        // All resources in active Security instances are always considered
        // observed, as server connections need to be refreshed if they changed;
        // compare with _anjay_notify_perform()
        return (anjay_resource_observation_status_t) {
            .is_observed = true,
            .min_period = 0,
            .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
        };
    }

    if (oid == ANJAY_DM_OID_SERVER
            && (rid == ANJAY_DM_RID_SERVER_LIFETIME
                || rid == ANJAY_DM_RID_SERVER_BINDING)) {
        // Lifetime and Binding in Server Object are always considered observed,
        // as server connections need to be refreshed if they changed; compare
        // with _anjay_notify_perform()
        return (anjay_resource_observation_status_t) {
            .is_observed = true,
            .min_period = 0,
            .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE
        };
    }

    // Note: some modules may also depend on resource notifications,
    // particularly Firmware Update depends on notifications on /5/0/3, but it
    // also implements that object and generates relevant notifications
    // internally, so there's no need to check that here.

    return _anjay_observe_status(anjay, oid, iid, rid);
}
#endif // ANJAY_WITH_OBSERVATION_STATUS
