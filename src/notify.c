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

#include <config.h>

#include <anjay_modules/dm.h>
#include <anjay_modules/notify.h>

#include "anjay.h"
#include "observe.h"

VISIBILITY_SOURCE_BEGIN

static void update_ret(int *var, int new_retval) {
    if (!*var) {
        *var = new_retval;
    }
}

#ifdef WITH_OBSERVE
static int observe_notify(anjay_t *anjay,
                          anjay_ssid_t origin_ssid,
                          anjay_notify_queue_t queue) {
    anjay_observe_key_t observe_key = {
        .connection = {
            .ssid = origin_ssid,
            .type = ANJAY_CONNECTION_WILDCARD
        },
        .format = ANJAY_COAP_FORMAT_NONE
    };
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        observe_key.oid = it->oid;
        if (it->instance_set_changes.instance_set_changed) {
            observe_key.iid = ANJAY_IID_INVALID;
            observe_key.rid = ANJAY_RID_EMPTY;
            update_ret(&ret, _anjay_observe_notify(anjay, &observe_key, true));
        } else {
            AVS_LIST(anjay_notify_queue_resource_entry_t) it2;
            AVS_LIST_FOREACH(it2, it->resources_changed) {
                observe_key.iid = it2->iid;
                observe_key.rid = it2->rid;
                update_ret(&ret,
                           _anjay_observe_notify(anjay, &observe_key, true));
            }
        }
    }
    return ret;
}
#else // WITH_OBSERVE
#define observe_notify(anjay, origin_ssid, queue) ((void) (origin_ssid), 0)
#endif // WITH_OBSERVE

static int security_modified_notify(
        anjay_t *anjay, anjay_notify_queue_object_entry_t *security) {
    if (anjay_is_offline(anjay)) {
        return 0;
    }
    int ret = 0;
    int32_t last_iid = -1;
    AVS_LIST(anjay_notify_queue_resource_entry_t) it;
    AVS_LIST_FOREACH(it, security->resources_changed) {
        if (it->iid != last_iid) {
            update_ret(&ret, _anjay_schedule_socket_update(anjay, it->iid));
            last_iid = it->iid;
        }
    }
    if (security->instance_set_changes.instance_set_changed) {
        update_ret(&ret, _anjay_schedule_reload_sockets(anjay));
    }
    return ret;
}

static int server_modified_notify(anjay_t *anjay,
                                  anjay_notify_queue_object_entry_t *server) {
    int ret = 0;
    AVS_LIST(anjay_notify_queue_resource_entry_t) it;
    AVS_LIST_FOREACH(it, server->resources_changed) {
        if (it->rid != ANJAY_DM_RID_SERVER_BINDING) {
            continue;
        }
        const anjay_resource_path_t path = {
            ANJAY_DM_OID_SERVER, it->iid, ANJAY_DM_RID_SERVER_SSID
        };
        int64_t ssid;
        if (_anjay_dm_res_read_i64(anjay, &path, &ssid)
                || ssid <= 0 || ssid >= UINT16_MAX) {
            update_ret(&ret, -1);
        } else if (_anjay_servers_find_active(&anjay->servers,
                                              (anjay_ssid_t) ssid)) {
            update_ret(&ret,
                       anjay_schedule_registration_update(anjay,
                                                          (anjay_ssid_t) ssid));
        }
    }
    return ret;
}

int _anjay_notify_perform(anjay_t *anjay,
                          anjay_ssid_t origin_ssid,
                          anjay_notify_queue_t queue) {
    assert(origin_ssid != ANJAY_SSID_ANY);
    int ret = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) it;
    AVS_LIST_FOREACH(it, queue) {
        if (it->oid > 1) {
            break;
        } else if (it->oid == ANJAY_DM_OID_SECURITY) {
            update_ret(&ret, security_modified_notify(anjay, it));
        } else if (it->oid == ANJAY_DM_OID_SERVER) {
            update_ret(&ret, server_modified_notify(anjay, it));
        }
    }
    update_ret(&ret, observe_notify(anjay, origin_ssid, queue));
    AVS_LIST(anjay_notify_callback_entry_t) clb;
    AVS_LIST_FOREACH(clb, anjay->notify_callbacks) {
        update_ret(&ret, clb->callback(anjay, origin_ssid, queue, clb->data));
    }
    return ret;
}

int _anjay_notify_flush(anjay_t *anjay,
                        anjay_ssid_t origin_ssid,
                        anjay_notify_queue_t *queue_ptr) {
    int result = _anjay_notify_perform(anjay, origin_ssid, *queue_ptr);
    _anjay_notify_clear_queue(queue_ptr);
    return result;
}

static AVS_LIST(anjay_notify_queue_object_entry_t) *
find_or_create_object_entry(anjay_notify_queue_t *out_queue,
                            anjay_oid_t oid) {
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
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
}

int _anjay_notify_queue_instance_set_unknown_change(
        anjay_notify_queue_t *out_queue,
        anjay_oid_t oid) {
    AVS_LIST(anjay_notify_queue_object_entry_t) *entry_ptr =
            find_or_create_object_entry(out_queue, oid);
    if (!entry_ptr) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
    (*entry_ptr)->instance_set_changes.instance_set_changed = true;
    return 0;
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
    AVS_LIST(anjay_notify_queue_resource_entry_t) *res_entry_ptr;
    AVS_LIST_FOREACH_PTR(res_entry_ptr, &(*obj_entry_ptr)->resources_changed) {
        if ((*res_entry_ptr)->iid == iid) {
            if ((*res_entry_ptr)->rid == rid) {
                return 0;
            } else if ((*res_entry_ptr)->rid > rid) {
                break;
            }
        } else if ((*res_entry_ptr)->rid > rid) {
            break;
        }
    }
    if (!AVS_LIST_INSERT_NEW(anjay_notify_queue_resource_entry_t, res_entry_ptr)) {
        anjay_log(ERROR, "Out of memory");
        if (!(*obj_entry_ptr)->instance_set_changes.instance_set_changed
                && !(*obj_entry_ptr)->resources_changed) {
            AVS_LIST_DELETE(obj_entry_ptr);
        }
        return -1;
    }
    (*res_entry_ptr)->iid = iid;
    (*res_entry_ptr)->rid = rid;
    return 0;
}

void _anjay_notify_clear_queue(anjay_notify_queue_t *out_queue) {
    AVS_LIST_CLEAR(out_queue) {
        AVS_LIST_CLEAR(&(*out_queue)->instance_set_changes.known_added_iids);
        AVS_LIST_CLEAR(&(*out_queue)->instance_set_changes.known_removed_iids);
        AVS_LIST_CLEAR(&(*out_queue)->resources_changed);
    }
}

static inline bool
is_notify_callback_registered(anjay_t *anjay,
                              anjay_notify_callback_t *callback) {
    AVS_LIST(anjay_notify_callback_entry_t) it;
    AVS_LIST_FOREACH(it, anjay->notify_callbacks) {
        if (it->callback == callback) {
            return true;
        }
    }
    return false;
}

int _anjay_notify_register_callback(anjay_t *anjay,
                                    anjay_notify_callback_t *callback,
                                    void *callback_data) {
    assert(callback);
    if ((void *) (intptr_t) callback == NULL) {
        return -1;
    }
    assert(!is_notify_callback_registered(anjay, callback));
    AVS_LIST(anjay_notify_callback_entry_t) elem =
        AVS_LIST_NEW_ELEMENT(anjay_notify_callback_entry_t);
    if (!elem) {
        anjay_log(ERROR, "Out of memory");
        return -1;
    }
    *elem = (anjay_notify_callback_entry_t) {
        .callback = callback,
        .data = callback_data
    };
    AVS_LIST_INSERT(&anjay->notify_callbacks, elem);
    return 0;
}

static int notify_clb(anjay_t *anjay, void *dummy) {
    (void) dummy;
    return _anjay_notify_flush(anjay, ANJAY_SSID_BOOTSTRAP,
                               &anjay->scheduled_notify.queue);
}

static int reschedule_notify(anjay_t *anjay) {
    if (anjay->scheduled_notify.handle) {
        return 0;
    }
    return _anjay_sched_now(anjay->sched, &anjay->scheduled_notify.handle,
                            notify_clb, NULL);
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
