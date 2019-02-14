/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#include <anjay_modules/dm_utils.h>
#include <anjay_modules/notify.h>

#include "coap/content_format.h"

#include "anjay_core.h"
#include "observe/observe_core.h"
#include "servers_utils.h"

VISIBILITY_SOURCE_BEGIN

#ifdef WITH_OBSERVE
static int observe_notify(anjay_t *anjay, anjay_notify_queue_t queue) {
    anjay_observe_key_t observe_key = {
        .connection = {
            .ssid = _anjay_dm_current_ssid(anjay),
            .type = ANJAY_CONNECTION_UNSET
        },
        .format = AVS_COAP_FORMAT_NONE
    };
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        observe_key.oid = it->oid;
        if (it->instance_set_changes.instance_set_changed) {
            observe_key.iid = ANJAY_IID_INVALID;
            observe_key.rid = ANJAY_RID_EMPTY;
            _anjay_update_ret(&ret,
                              _anjay_observe_notify(anjay, &observe_key, true));
        } else {
            AVS_LIST(anjay_notify_queue_resource_entry_t) it2;
            AVS_LIST_FOREACH(it2, it->resources_changed) {
                observe_key.iid = it2->iid;
                observe_key.rid = it2->rid;
                _anjay_update_ret(
                        &ret, _anjay_observe_notify(anjay, &observe_key, true));
            }
        }
    }
    return ret;
}
#else // WITH_OBSERVE
#    define observe_notify(anjay, queue) (0)
#endif // WITH_OBSERVE

static int
security_modified_notify(anjay_t *anjay,
                         anjay_notify_queue_object_entry_t *security) {
    if (anjay_is_offline(anjay)) {
        return 0;
    }
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
        if (_anjay_dm_res_read_i64(anjay, &path, &ssid) || ssid <= 0
                || ssid >= UINT16_MAX) {
            _anjay_update_ret(&ret, -1);
        } else if (_anjay_servers_find_active(anjay, (anjay_ssid_t) ssid)) {
            _anjay_update_ret(&ret,
                              anjay_schedule_registration_update(
                                      anjay, (anjay_ssid_t) ssid));
        }
    }
    return ret;
}

int _anjay_notify_perform(anjay_t *anjay, anjay_notify_queue_t queue) {
    if (!queue) {
        return 0;
    }
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (it->oid > 1) {
            break;
        } else if (it->oid == ANJAY_DM_OID_SECURITY) {
            _anjay_update_ret(&ret, security_modified_notify(anjay, it));
        } else if (it->oid == ANJAY_DM_OID_SERVER) {
            _anjay_update_ret(&ret, server_modified_notify(anjay, it));
        }
    }
    _anjay_update_ret(&ret, observe_notify(anjay, queue));
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
    assert(!(*entry_ptr)->instance_set_changes.known_removed_iids);
    AVS_LIST_DELETE(entry_ptr);
}

int _anjay_notify_queue_instance_created(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
    if (add_entry_to_iid_set(
                &(*entry_ptr)->instance_set_changes.known_added_iids, iid)) {
        anjay_log(ERROR, "Out of memory");
        delete_notify_queue_object_entry_if_empty(entry_ptr);
        return -1;
    }
    remove_entry_from_iid_set(
            &(*entry_ptr)->instance_set_changes.known_removed_iids, iid);
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

int _anjay_notify_queue_instance_removed(anjay_notify_queue_t *out_queue,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
    if (add_entry_to_iid_set(
                &(*entry_ptr)->instance_set_changes.known_removed_iids, iid)) {
        anjay_log(ERROR, "Out of memory");
        delete_notify_queue_object_entry_if_empty(entry_ptr);
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
        anjay_log(ERROR, "Out of memory");
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
        anjay_log(ERROR, "Out of memory");
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
        anjay_log(ERROR, "Out of memory");
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
        AVS_LIST_CLEAR(&(*out_queue)->instance_set_changes.known_removed_iids);
        AVS_LIST_CLEAR(&(*out_queue)->resources_changed);
    }
}

static void notify_clb(anjay_t *anjay, const void *dummy) {
    (void) dummy;
    _anjay_notify_flush(anjay, &anjay->scheduled_notify.queue);
}

static int reschedule_notify(anjay_t *anjay) {
    if (anjay->scheduled_notify.handle) {
        return 0;
    }
    return _anjay_sched_now(anjay->sched, &anjay->scheduled_notify.handle,
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

