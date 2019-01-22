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

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>

#include <avsystem/commons/stream/stream_membuf.h>

#include <anjay_modules/dm_utils.h>
#include <anjay_modules/raw_buffer.h>

#include "mod_attr_storage.h"

VISIBILITY_SOURCE_BEGIN

//// LIFETIME AND OBJECT HANDLING //////////////////////////////////////////////

static anjay_dm_object_read_default_attrs_t object_read_default_attrs;
static anjay_dm_object_write_default_attrs_t object_write_default_attrs;
static anjay_dm_instance_it_t instance_it;
static anjay_dm_instance_present_t instance_present;
static anjay_dm_instance_remove_t instance_remove;
static anjay_dm_instance_read_default_attrs_t instance_read_default_attrs;
static anjay_dm_instance_write_default_attrs_t instance_write_default_attrs;
static anjay_dm_resource_present_t resource_present;
static anjay_dm_resource_read_attrs_t resource_read_attrs;
static anjay_dm_resource_write_attrs_t resource_write_attrs;
static anjay_dm_transaction_begin_t transaction_begin;
static anjay_dm_transaction_commit_t transaction_commit;
static anjay_dm_transaction_rollback_t transaction_rollback;

static void fas_delete(anjay_t *anjay, void *fas_) {
    (void) anjay;
    anjay_attr_storage_t *fas = (anjay_attr_storage_t *) fas_;
    assert(fas);
    _anjay_attr_storage_clear(fas);
    avs_stream_cleanup(&fas->saved_state.persist_data);
    avs_free(fas);
}

const anjay_dm_module_t _anjay_attr_storage_MODULE = {
    .overlay_handlers = {
        .object_read_default_attrs = object_read_default_attrs,
        .object_write_default_attrs = object_write_default_attrs,
        .instance_it = instance_it,
        .instance_present = instance_present,
        .instance_remove = instance_remove,
        .instance_read_default_attrs = instance_read_default_attrs,
        .instance_write_default_attrs = instance_write_default_attrs,
        .resource_present = resource_present,
        .resource_read_attrs = resource_read_attrs,
        .resource_write_attrs = resource_write_attrs,
        .transaction_begin = transaction_begin,
        .transaction_commit = transaction_commit,
        .transaction_rollback = transaction_rollback
    },
    .deleter = fas_delete
};

int anjay_attr_storage_install(anjay_t *anjay) {
    if (!anjay) {
        fas_log(ERROR, "ANJAY object must not be NULL");
        return -1;
    }
    anjay_attr_storage_t *fas =
            (anjay_attr_storage_t *) avs_calloc(1,
                                                sizeof(anjay_attr_storage_t));
    if (!fas) {
        fas_log(ERROR, "out of memory");
        return -1;
    }
    if (!(fas->saved_state.persist_data = avs_stream_membuf_create())
            || _anjay_dm_module_install(anjay, &_anjay_attr_storage_MODULE,
                                        fas)) {
        avs_stream_cleanup(&fas->saved_state.persist_data);
        avs_free(fas);
        return -1;
    }
    return 0;
}

static void reset_it_state(fas_iteration_state_t *it) {
    it->oid = UINT16_MAX;
    AVS_LIST_CLEAR(&it->iids);
    it->last_cookie = NULL;
}

bool anjay_attr_storage_is_modified(anjay_t *anjay) {
    anjay_attr_storage_t *fas = _anjay_attr_storage_get(anjay);
    if (!fas) {
        fas_log(ERROR, "Attribute Storage is not installed");
        return false;
    }
    return fas->modified_since_persist;
}

void _anjay_attr_storage_clear(anjay_attr_storage_t *fas) {
    reset_it_state(&fas->iteration);
    while (fas->objects) {
        remove_object_entry(fas, &fas->objects);
    }
}

void anjay_attr_storage_purge(anjay_t *anjay) {
    anjay_attr_storage_t *fas = _anjay_attr_storage_get(anjay);
    if (!fas) {
        fas_log(ERROR, "Attribute Storage is not installed");
        return;
    }
    _anjay_attr_storage_clear(fas);
    _anjay_attr_storage_mark_modified(fas);
}

//// HELPERS ///////////////////////////////////////////////////////////////////

static bool implements_any_object_default_attrs_handlers(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr) {
    return _anjay_dm_handler_implemented(
                   anjay, obj_ptr, &_anjay_attr_storage_MODULE,
                   offsetof(anjay_dm_handlers_t, object_read_default_attrs))
           || _anjay_dm_handler_implemented(
                      anjay, obj_ptr, &_anjay_attr_storage_MODULE,
                      offsetof(anjay_dm_handlers_t,
                               object_write_default_attrs));
}

static bool implements_any_instance_default_attrs_handlers(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr) {
    return _anjay_dm_handler_implemented(
                   anjay, obj_ptr, &_anjay_attr_storage_MODULE,
                   offsetof(anjay_dm_handlers_t, instance_read_default_attrs))
           || _anjay_dm_handler_implemented(
                      anjay, obj_ptr, &_anjay_attr_storage_MODULE,
                      offsetof(anjay_dm_handlers_t,
                               instance_write_default_attrs));
}

static bool implements_any_resource_attrs_handlers(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr) {
    return _anjay_dm_handler_implemented(
                   anjay, obj_ptr, &_anjay_attr_storage_MODULE,
                   offsetof(anjay_dm_handlers_t, resource_read_attrs))
           || _anjay_dm_handler_implemented(
                      anjay, obj_ptr, &_anjay_attr_storage_MODULE,
                      offsetof(anjay_dm_handlers_t, resource_write_attrs));
}

static void
remove_resource_if_empty(AVS_LIST(fas_resource_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->attrs) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

anjay_attr_storage_t *_anjay_attr_storage_get(anjay_t *anjay) {
    return (anjay_attr_storage_t *) _anjay_dm_module_get_arg(
            anjay, &_anjay_attr_storage_MODULE);
}

static anjay_attr_storage_t *get_fas(anjay_t *anjay) {
    assert(anjay);
    anjay_attr_storage_t *fas = _anjay_attr_storage_get(anjay);
    assert(fas);
    return (anjay_attr_storage_t *) fas;
}

AVS_STATIC_ASSERT(offsetof(fas_object_entry_t, oid) == 0, object_id_offset);
AVS_STATIC_ASSERT(offsetof(fas_instance_entry_t, iid) == 0, instance_id_offset);
AVS_STATIC_ASSERT(offsetof(fas_resource_entry_t, rid) == 0, resource_id_offset);

static AVS_LIST(void) *
find_or_create_entry_impl(AVS_LIST(void) *children_list_ptr,
                          size_t entry_size,
                          uint16_t id,
                          bool allow_create) {
    AVS_LIST(void) *entry_ptr;
    AVS_LIST_FOREACH_PTR(entry_ptr, children_list_ptr) {
        if (*(uint16_t *) *entry_ptr >= id) {
            break;
        }
    }
    if (!*entry_ptr || *(uint16_t *) *entry_ptr != id) {
        if (allow_create) {
            AVS_LIST(void) new_entry = AVS_LIST_NEW_BUFFER(entry_size);
            if (!new_entry) {
                fas_log(ERROR, "Out of memory");
                return NULL;
            }
            *(uint16_t *) new_entry = id;
            AVS_LIST_INSERT(entry_ptr, new_entry);
        } else {
            return NULL;
        }
    }
    return entry_ptr;
}

static inline AVS_LIST(fas_object_entry_t) *
find_object(anjay_attr_storage_t *parent, anjay_oid_t id) {
    return (AVS_LIST(fas_object_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->objects, sizeof(fas_object_entry_t), id,
            false);
}

static inline AVS_LIST(fas_object_entry_t) *
find_or_create_object(anjay_attr_storage_t *parent, anjay_oid_t id) {
    return (AVS_LIST(fas_object_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->objects, sizeof(fas_object_entry_t), id,
            true);
}

static inline AVS_LIST(fas_instance_entry_t) *
find_instance(fas_object_entry_t *parent, anjay_iid_t id) {
    return (AVS_LIST(fas_instance_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->instances, sizeof(fas_instance_entry_t),
            id, false);
}

static inline AVS_LIST(fas_instance_entry_t) *
find_or_create_instance(fas_object_entry_t *parent, anjay_iid_t id) {
    return (AVS_LIST(fas_instance_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->instances, sizeof(fas_instance_entry_t),
            id, true);
}

static inline AVS_LIST(fas_resource_entry_t) *
find_resource(fas_instance_entry_t *parent, anjay_rid_t id) {
    return (AVS_LIST(fas_resource_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->resources, sizeof(fas_resource_entry_t),
            id, false);
}

static inline AVS_LIST(fas_resource_entry_t) *
find_or_create_resource(fas_instance_entry_t *parent, anjay_rid_t id) {
    return (AVS_LIST(fas_resource_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->resources, sizeof(fas_resource_entry_t),
            id, true);
}

static void remove_instance(anjay_attr_storage_t *fas,
                            AVS_LIST(fas_object_entry_t) *object_ptr,
                            anjay_iid_t iid) {
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            find_instance(*object_ptr, iid);
    if (instance_ptr && *instance_ptr) {
        remove_instance_entry(fas, instance_ptr);
    }
    remove_object_if_empty(object_ptr);
}

static void remove_resource(anjay_attr_storage_t *fas,
                            AVS_LIST(fas_object_entry_t) *object_ptr,
                            AVS_LIST(fas_instance_entry_t) *instance_ptr,
                            anjay_rid_t rid) {
    AVS_LIST(fas_resource_entry_t) *resource_ptr =
            find_resource(*instance_ptr, rid);
    if (resource_ptr) {
        remove_resource_entry(fas, resource_ptr);
    }
    remove_instance_if_empty(instance_ptr);
    remove_object_if_empty(object_ptr);
}

static inline bool is_ssid_reference_object(anjay_oid_t oid) {
    return oid == ANJAY_DM_OID_SECURITY || oid == ANJAY_DM_OID_SERVER;
}

static inline anjay_rid_t ssid_rid(anjay_oid_t oid) {
    switch (oid) {
    case ANJAY_DM_OID_SECURITY:
        return ANJAY_DM_RID_SECURITY_SSID;
    case ANJAY_DM_OID_SERVER:
        return ANJAY_DM_RID_SERVER_SSID;
    default:
        AVS_UNREACHABLE("Invalid object for Short Server ID query");
    }
    fas_log(ERROR, "Could not get valid RID");
    return (anjay_rid_t) -1;
}

static anjay_ssid_t
query_ssid(anjay_t *anjay, anjay_oid_t oid, anjay_iid_t iid) {
    if (!is_ssid_reference_object(oid)) {
        return 0;
    }
    int64_t ssid;
    const anjay_uri_path_t uri = MAKE_RESOURCE_PATH(oid, iid, ssid_rid(oid));
    int result = _anjay_dm_res_read_i64(anjay, &uri, &ssid);
    if (result || ssid <= 0 || ssid >= UINT16_MAX) {
        /* Most likely a Bootstrap instance, ignore. */
        return 0;
    }
    return (anjay_ssid_t) ssid;
}

static void remove_attrs_entry(anjay_attr_storage_t *fas,
                               AVS_LIST(void) *attrs_ptr) {
    AVS_LIST_DELETE(attrs_ptr);
    _anjay_attr_storage_mark_modified(fas);
}

static void remove_attrs_for_server(anjay_attr_storage_t *fas,
                                    AVS_LIST(void) *attrs_ptr,
                                    void *ssid_ptr) {
    anjay_ssid_t ssid = *(anjay_ssid_t *) ssid_ptr;
    AVS_LIST_ITERATE_PTR(attrs_ptr) {
        assert(!*AVS_LIST_NEXT_PTR(attrs_ptr)
               || *get_ssid_ptr(*attrs_ptr)
                          < *get_ssid_ptr(*AVS_LIST_NEXT_PTR(attrs_ptr)));
        if (*get_ssid_ptr(*attrs_ptr) == ssid) {
            remove_attrs_entry(fas, attrs_ptr);
            assert(!*attrs_ptr || ssid < *get_ssid_ptr(*attrs_ptr));
            return;
        } else if (*get_ssid_ptr(*attrs_ptr) > ssid) {
            break;
        }
    }
}

static void remove_attrs_for_servers_not_on_list(anjay_attr_storage_t *fas,
                                                 AVS_LIST(void) *attrs_ptr,
                                                 void *ssid_list_ptr) {
    AVS_LIST(anjay_ssid_t) ssid_ptr = *(AVS_LIST(anjay_ssid_t) *) ssid_list_ptr;
    while (*attrs_ptr) {
        if (!ssid_ptr || *get_ssid_ptr(*attrs_ptr) < *ssid_ptr) {
            remove_attrs_entry(fas, attrs_ptr);
        } else {
            while (ssid_ptr && *get_ssid_ptr(*attrs_ptr) > *ssid_ptr) {
                AVS_LIST_ADVANCE(&ssid_ptr);
            }
            if (ssid_ptr && *get_ssid_ptr(*attrs_ptr) == *ssid_ptr) {
                AVS_LIST_ADVANCE(&ssid_ptr);
                AVS_LIST_ADVANCE_PTR(&attrs_ptr);
            }
        }
    }
}

typedef void remove_attrs_func_t(anjay_attr_storage_t *fas,
                                 AVS_LIST(void) *attrs_ptr,
                                 void *ssid_ref_ptr);

static void remove_servers(anjay_attr_storage_t *fas,
                           remove_attrs_func_t *remove_attrs_func,
                           void *ssid_ref) {
    AVS_LIST(fas_object_entry_t) *object_ptr;
    AVS_LIST(fas_object_entry_t) object_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(object_ptr, object_helper, &fas->objects) {
        remove_attrs_func(fas, (AVS_LIST(void) *) &(*object_ptr)->default_attrs,
                          ssid_ref);
        AVS_LIST(fas_instance_entry_t) *instance_ptr;
        AVS_LIST(fas_instance_entry_t) instance_helper;
        AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper,
                                       &(*object_ptr)->instances) {
            remove_attrs_func(
                    fas, (AVS_LIST(void) *) &(*instance_ptr)->default_attrs,
                    ssid_ref);
            AVS_LIST(fas_resource_entry_t) *res_ptr;
            AVS_LIST(fas_resource_entry_t) res_helper;
            AVS_LIST_DELETABLE_FOREACH_PTR(res_ptr, res_helper,
                                           &(*instance_ptr)->resources) {
                remove_attrs_func(fas, (AVS_LIST(void) *) &(*res_ptr)->attrs,
                                  ssid_ref);
                remove_resource_if_empty(res_ptr);
            }
            remove_instance_if_empty(instance_ptr);
        }
        remove_object_if_empty(object_ptr);
    }
}

int _anjay_attr_storage_compare_u16ids(const void *a,
                                       const void *b,
                                       size_t element_size) {
    assert(element_size == sizeof(uint16_t));
    (void) element_size;
    return *(const uint16_t *) a - *(const uint16_t *) b;
}

static int remove_servers_after_iteration(anjay_t *anjay,
                                          anjay_attr_storage_t *fas) {
    AVS_LIST(anjay_ssid_t) ssids = NULL;
    AVS_LIST(anjay_iid_t) iid;
    AVS_LIST_FOREACH(iid, fas->iteration.iids) {
        anjay_ssid_t ssid = query_ssid(anjay, fas->iteration.oid, *iid);
        if (!ssid) {
            continue;
        }
        AVS_LIST(anjay_ssid_t) ssid_entry = AVS_LIST_NEW_ELEMENT(anjay_ssid_t);
        if (!ssid_entry) {
            return ANJAY_ERR_INTERNAL;
        }
        *ssid_entry = ssid;
        AVS_LIST_INSERT(&ssids, ssid_entry);
    }

    AVS_LIST_SORT(&ssids, _anjay_attr_storage_compare_u16ids);
    remove_servers(fas, remove_attrs_for_servers_not_on_list, &ssids);
    AVS_LIST_CLEAR(&ssids);
    return 0;
}

void _anjay_attr_storage_remove_instances_not_on_sorted_list(
        anjay_attr_storage_t *fas,
        fas_object_entry_t *object,
        AVS_LIST(anjay_iid_t) iids) {
    AVS_LIST(anjay_iid_t) iid = iids;
    AVS_LIST(fas_instance_entry_t) *instance_ptr = &object->instances;
    while (*instance_ptr) {
        if (!iid || (*instance_ptr)->iid < *iid) {
            remove_instance_entry(fas, instance_ptr);
        } else {
            while (iid && (*instance_ptr)->iid > *iid) {
                AVS_LIST_ADVANCE(&iid);
            }
            if (iid && (*instance_ptr)->iid == *iid) {
                AVS_LIST_ADVANCE(&iid);
                AVS_LIST_ADVANCE_PTR(&instance_ptr);
            }
        }
    }
}

static int remove_instances_after_iteration(anjay_t *anjay,
                                            anjay_attr_storage_t *fas) {
    int result = 0;
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_object(fas, fas->iteration.oid);
    if (object_ptr) {
        AVS_LIST_SORT(&fas->iteration.iids, _anjay_attr_storage_compare_u16ids);
        _anjay_attr_storage_remove_instances_not_on_sorted_list(
                fas, *object_ptr, fas->iteration.iids);
        remove_object_if_empty(object_ptr);
    }
    if (is_ssid_reference_object(fas->iteration.oid)) {
        result = remove_servers_after_iteration(anjay, fas);
    }
    reset_it_state(&fas->iteration);
    return result;
}

static void read_default_attrs(AVS_LIST(fas_default_attrs_t) attrs,
                               anjay_ssid_t ssid,
                               anjay_dm_internal_attrs_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_INTERNAL_ATTRS_EMPTY;
}

static void read_resource_attrs(AVS_LIST(fas_resource_attrs_t) attrs,
                                anjay_ssid_t ssid,
                                anjay_dm_internal_res_attrs_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
}

static int write_attrs_impl(anjay_attr_storage_t *fas,
                            AVS_LIST(void) *out_attrs,
                            size_t element_size,
                            size_t attrs_field_offset,
                            size_t attrs_field_size,
                            is_empty_func_t *is_empty_func,
                            anjay_ssid_t ssid,
                            const void *attrs) {
    AVS_LIST_ITERATE_PTR(out_attrs) {
        if (*get_ssid_ptr(*out_attrs) >= ssid) {
            break;
        }
    }
    bool found = (*out_attrs && *get_ssid_ptr(*out_attrs) == ssid);
    bool filled = !is_empty_func(attrs);
    if (filled) {
        // writing non-empty set of attributes
        if (!found) {
            // entry does not exist, creating
            AVS_LIST(void) new_attrs = AVS_LIST_NEW_BUFFER(element_size);
            if (!new_attrs) {
                fas_log(ERROR, "Out of memory");
                return ANJAY_ERR_INTERNAL;
            }
            *get_ssid_ptr(new_attrs) = ssid;
            AVS_LIST_INSERT(out_attrs, new_attrs);
        }
        memcpy(get_attrs_ptr(*out_attrs, attrs_field_offset), attrs,
               attrs_field_size);
        _anjay_attr_storage_mark_modified(fas);
    } else if (found) {
        // entry exists, but writing EMPTY set of attributes
        // hence - removing
        remove_attrs_entry(fas, (AVS_LIST(void) *) out_attrs);
    }
    return 0;
}

#define WRITE_ATTRS(Fas, OutAttrs, IsEmptyFunc, Ssid, Attrs)                  \
    write_attrs_impl(                                                         \
            (Fas), (AVS_LIST(void) *) (OutAttrs), sizeof(**(OutAttrs)),       \
            (size_t) ((char *) &(*(OutAttrs))->attrs - (char *) *(OutAttrs)), \
            sizeof((*(OutAttrs))->attrs), (IsEmptyFunc), (Ssid), (Attrs))

static int write_object_attrs(anjay_t *anjay,
                              anjay_ssid_t ssid,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              const anjay_dm_internal_attrs_t *attrs) {
    anjay_attr_storage_t *fas = get_fas(anjay);
    if (!fas) {
        fas_log(ERROR, "Attribute Storage module is not installed");
        return -1;
    }
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_or_create_object(fas, (*obj_ptr)->oid);
    if (!object_ptr) {
        return -1;
    }
    int result = WRITE_ATTRS(fas, &(*object_ptr)->default_attrs,
                             default_attrs_empty, ssid, attrs);
    remove_object_if_empty(object_ptr);
    return result;
}

static int write_instance_attrs(anjay_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                const anjay_dm_internal_attrs_t *attrs) {
    anjay_attr_storage_t *fas = get_fas(anjay);
    if (!fas) {
        fas_log(ERROR, "Attribute Storage module is not installed");
        return -1;
    }
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_or_create_object(fas, (*obj_ptr)->oid);
    if (!object_ptr) {
        return -1;
    }
    int result = 0;
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            find_or_create_instance(*object_ptr, iid);
    if (!instance_ptr) {
        result = -1;
    }
    if (!result) {
        result = WRITE_ATTRS(fas, &(*instance_ptr)->default_attrs,
                             default_attrs_empty, ssid, attrs);
    }
    if (instance_ptr) {
        remove_instance_if_empty(instance_ptr);
    }
    remove_object_if_empty(object_ptr);
    return result;
}

static int write_resource_attrs(anjay_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                const anjay_dm_internal_res_attrs_t *attrs) {
    anjay_attr_storage_t *fas = get_fas(anjay);
    if (!fas) {
        fas_log(ERROR, "Attribute Storage module is not installed");
        return -1;
    }
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_or_create_object(fas, (*obj_ptr)->oid);
    if (!object_ptr) {
        return -1;
    }
    int result = 0;
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            find_or_create_instance(*object_ptr, iid);
    if (!instance_ptr) {
        result = -1;
    }
    AVS_LIST(fas_resource_entry_t) *resource_ptr =
            result ? NULL : find_or_create_resource(*instance_ptr, rid);
    if (!resource_ptr) {
        result = -1;
    }
    if (!result) {
        result = WRITE_ATTRS(fas, &(*resource_ptr)->attrs, resource_attrs_empty,
                             ssid, attrs);
    }
    if (resource_ptr) {
        remove_resource_if_empty(resource_ptr);
    }
    if (instance_ptr) {
        remove_instance_if_empty(instance_ptr);
    }
    remove_object_if_empty(object_ptr);
    return result;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

static int
object_read_default_attrs(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_ssid_t ssid,
                          anjay_dm_attributes_t *out_) {
    anjay_dm_internal_attrs_t *out = _anjay_dm_get_internal_attrs(out_);
    if (implements_any_object_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_object_read_default_attrs(anjay, obj_ptr, ssid, out,
                                                   &_anjay_attr_storage_MODULE);
    }
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_object(get_fas(anjay), (*obj_ptr)->oid);
    read_default_attrs(object_ptr ? (*object_ptr)->default_attrs : NULL, ssid,
                       out);
    return 0;
}

static int
object_write_default_attrs(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_ssid_t ssid,
                           const anjay_dm_attributes_t *attrs_) {
    const anjay_dm_internal_attrs_t *attrs =
            _anjay_dm_get_internal_attrs_const(attrs_);
    if (implements_any_object_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_object_write_default_attrs(
                anjay, obj_ptr, ssid, attrs, &_anjay_attr_storage_MODULE);
    }
    return write_object_attrs(anjay, ssid, obj_ptr, attrs) ? ANJAY_ERR_INTERNAL
                                                           : 0;
}

static int
instance_read_default_attrs(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_ssid_t ssid,
                            anjay_dm_attributes_t *out_) {
    anjay_dm_internal_attrs_t *out = _anjay_dm_get_internal_attrs(out_);
    if (implements_any_instance_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_instance_read_default_attrs(
                anjay, obj_ptr, iid, ssid, out, &_anjay_attr_storage_MODULE);
    }
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_object(get_fas(anjay), (*obj_ptr)->oid);
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            object_ptr ? find_instance(*object_ptr, iid) : NULL;
    read_default_attrs(instance_ptr ? (*instance_ptr)->default_attrs : NULL,
                       ssid, out);
    return 0;
}

static int
instance_write_default_attrs(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             const anjay_dm_attributes_t *attrs_) {
    const anjay_dm_internal_attrs_t *attrs =
            _anjay_dm_get_internal_attrs_const(attrs_);
    if (implements_any_instance_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_instance_write_default_attrs(
                anjay, obj_ptr, iid, ssid, attrs, &_anjay_attr_storage_MODULE);
    }
    return write_instance_attrs(anjay, ssid, obj_ptr, iid, attrs)
                   ? ANJAY_ERR_INTERNAL
                   : 0;
}

static int resource_read_attrs(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_ssid_t ssid,
                               anjay_dm_resource_attributes_t *out_) {
    anjay_dm_internal_res_attrs_t *out = _anjay_dm_get_internal_res_attrs(out_);
    if (implements_any_resource_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_resource_read_attrs(anjay, obj_ptr, iid, rid, ssid,
                                             out, &_anjay_attr_storage_MODULE);
    }
    AVS_LIST(fas_object_entry_t) *object_ptr =
            find_object(get_fas(anjay), (*obj_ptr)->oid);
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            object_ptr ? find_instance(*object_ptr, iid) : NULL;
    AVS_LIST(fas_resource_entry_t) *res_ptr =
            instance_ptr ? find_resource(*instance_ptr, rid) : NULL;
    read_resource_attrs(res_ptr ? (*res_ptr)->attrs : NULL, ssid, out);
    return 0;
}

static int resource_write_attrs(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                const anjay_dm_resource_attributes_t *attrs_) {
    const anjay_dm_internal_res_attrs_t *attrs =
            _anjay_dm_get_internal_res_attrs_const(attrs_);
    if (implements_any_resource_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_resource_write_attrs(anjay, obj_ptr, iid, rid, ssid,
                                              attrs,
                                              &_anjay_attr_storage_MODULE);
    }
    return write_resource_attrs(anjay, ssid, obj_ptr, iid, rid, attrs)
                   ? ANJAY_ERR_INTERNAL
                   : 0;
}

//// ACTIVE PROXY HANDLERS /////////////////////////////////////////////////////

static int instance_it(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj_ptr,
                       anjay_iid_t *out,
                       void **cookie) {
    // we have three cases here:
    // * we're called with *cookie == NULL
    //   - it means a start of iteration, so we reset our state
    // * we're called with consecutive cookie values
    //   (*cookie unchanged since after last call to backend->instance_it)
    //   - it means we're continuing last iteration; we do our processing,
    //     which ends with calling remove_instances_after_iteration()
    // * we're called with some unrelated cookie
    //   - it means parallel or nested iterations; we don't support it, so we
    //     reset our state and ignore it
    anjay_attr_storage_t *fas = get_fas(anjay);
    void *orig_cookie = *cookie;
    if (!orig_cookie) {
        reset_it_state(&fas->iteration);
        fas->iteration.oid = (*obj_ptr)->oid;
    }
    int result = _anjay_dm_instance_it(anjay, obj_ptr, out, cookie,
                                       &_anjay_attr_storage_MODULE);
    if (result || fas->iteration.oid != (*obj_ptr)->oid
            || fas->iteration.last_cookie != orig_cookie) {
        reset_it_state(&fas->iteration);
    } else {
        fas->iteration.last_cookie = *cookie;
        if (*out == ANJAY_IID_INVALID) {
            result = remove_instances_after_iteration(anjay, fas);
        } else {
            anjay_iid_t *new_iid = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
            if (!new_iid) {
                return ANJAY_ERR_INTERNAL;
            }
            *new_iid = *out;
            AVS_LIST_INSERT(&fas->iteration.iids, new_iid);
        }
    }
    return result;
}

static int instance_present(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid) {
    int result = _anjay_dm_instance_present(anjay, obj_ptr, iid,
                                            &_anjay_attr_storage_MODULE);
    if (result == 0) {
        anjay_attr_storage_t *fas = get_fas(anjay);
        AVS_LIST(fas_object_entry_t) *object_ptr =
                find_object(fas, (*obj_ptr)->oid);
        if (object_ptr) {
            remove_instance(fas, object_ptr, iid);
        }
    }
    return result;
}

static int instance_remove(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid) {
    anjay_ssid_t ssid = query_ssid(anjay, (*obj_ptr)->oid, iid);
    int result = _anjay_dm_instance_remove(anjay, obj_ptr, iid,
                                           &_anjay_attr_storage_MODULE);
    if (result == 0) {
        anjay_attr_storage_t *fas = get_fas(anjay);
        AVS_LIST(fas_object_entry_t) *object_ptr =
                find_object(fas, (*obj_ptr)->oid);
        if (object_ptr) {
            remove_instance(fas, object_ptr, iid);
        }
        if (ssid) {
            remove_servers(fas, remove_attrs_for_server, &ssid);
        }
    }
    return result;
}

static int resource_present(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid) {
    int result = _anjay_dm_resource_present(anjay, obj_ptr, iid, rid,
                                            &_anjay_attr_storage_MODULE);
    if (result == 0) {
        anjay_attr_storage_t *fas = get_fas(anjay);
        AVS_LIST(fas_object_entry_t) *object_ptr =
                find_object(fas, (*obj_ptr)->oid);
        AVS_LIST(fas_instance_entry_t) *instance_ptr =
                object_ptr ? find_instance(*object_ptr, iid) : NULL;
        if (instance_ptr) {
            remove_resource(fas, object_ptr, instance_ptr, rid);
        }
    }
    return result;
}

static void saved_state_reset(anjay_attr_storage_t *fas) {
    avs_stream_reset(fas->saved_state.persist_data);
    avs_stream_membuf_fit(fas->saved_state.persist_data);
}

static int saved_state_save(anjay_attr_storage_t *fas) {
    fas->saved_state.modified_since_persist = fas->modified_since_persist;
    return _anjay_attr_storage_persist_inner(fas,
                                             fas->saved_state.persist_data);
}

static int saved_state_restore(anjay_t *anjay, anjay_attr_storage_t *fas) {
    int result =
            _anjay_attr_storage_restore_inner(anjay, fas,
                                              fas->saved_state.persist_data);
    fas->modified_since_persist =
            (result ? true : fas->saved_state.modified_since_persist);
    return result;
}

static int transaction_begin(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_attr_storage_t *fas = get_fas(anjay);
    if (fas->saved_state.depth++ == 0) {
        if (saved_state_save(fas)) {
            --fas->saved_state.depth;
            return ANJAY_ERR_INTERNAL;
        }
    }
    int result =
            _anjay_dm_delegate_transaction_begin(anjay, obj_ptr,
                                                 &_anjay_attr_storage_MODULE);
    if (result) {
        saved_state_reset(fas);
    }
    return result;
}

static int transaction_commit(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_attr_storage_t *fas = get_fas(anjay);
    int result =
            _anjay_dm_delegate_transaction_commit(anjay, obj_ptr,
                                                  &_anjay_attr_storage_MODULE);
    if (--fas->saved_state.depth == 0) {
        if (result && saved_state_restore(anjay, fas)) {
            result = ANJAY_ERR_INTERNAL;
        }
        saved_state_reset(fas);
    }
    return result;
}

static int transaction_rollback(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_attr_storage_t *fas = get_fas(anjay);
    int result = _anjay_dm_delegate_transaction_rollback(
            anjay, obj_ptr, &_anjay_attr_storage_MODULE);
    if (--fas->saved_state.depth == 0) {
        if (saved_state_restore(anjay, fas)) {
            result = ANJAY_ERR_INTERNAL;
        }
        saved_state_reset(fas);
    }
    return result;
}

static const anjay_dm_object_def_t *const *
maybe_get_object_before_setting_attrs(anjay_t *anjay,
                                      anjay_ssid_t ssid,
                                      anjay_oid_t oid,
                                      const void *attrs) {
    if (!attrs) {
        fas_log(ERROR, "attributes cannot be NULL");
        return NULL;
    }
    if (ssid == ANJAY_SSID_BOOTSTRAP || !_anjay_dm_ssid_exists(anjay, ssid)) {
        fas_log(ERROR, "SSID %" PRIu16 " does not exist", ssid);
        return NULL;
    }
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    if (!obj) {
        fas_log(ERROR, "/%" PRIu16 " does not exist", oid);
    }
    return obj;
}

#define ERR_HANDLERS_IMPLEMENTED_BY_BACKEND                                \
    "cannot set %s level attribs: %s or %s is implemented by the backend " \
    "object"

#define ERR_INSTANCE_PRESENCE_CHECK  \
    "instance /%" PRIu16 "/%" PRIu16 \
    " does not exist or an error occurred during querying its presence"

int anjay_attr_storage_set_object_attrs(anjay_t *anjay,
                                        anjay_ssid_t ssid,
                                        anjay_oid_t oid,
                                        const anjay_dm_attributes_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (!obj) {
        return -1;
    }
    if (implements_any_object_default_attrs_handlers(anjay, obj)) {
        fas_log(ERROR, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "object",
                "object_read_default_attrs", "object_write_default_attrs");
        return -1;
    }
    const anjay_dm_internal_attrs_t internal_attrs = {
#ifdef WITH_CUSTOM_ATTRIBUTES
        _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
#endif // WITH_CUSTOM_ATTRIBUTES
                .standard = *attrs
    };

    int result;
    if (!(result = write_object_attrs(anjay, ssid, obj, &internal_attrs))) {
        (void) anjay_notify_instances_changed(anjay, oid);
    }
    return result;
}

int anjay_attr_storage_set_instance_attrs(anjay_t *anjay,
                                          anjay_ssid_t ssid,
                                          anjay_oid_t oid,
                                          anjay_iid_t iid,
                                          const anjay_dm_attributes_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (!obj) {
        return -1;
    }
    if (implements_any_instance_default_attrs_handlers(anjay, obj)) {
        fas_log(ERROR, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "instance",
                "instance_read_default_attrs", "instance_write_default_attrs");
        return -1;
    }
    if (iid == ANJAY_IID_INVALID) {
        fas_log(ERROR, "invalid instance id");
        return -1;
    }
    if (_anjay_dm_instance_present(anjay, obj, iid, NULL) <= 0) {
        fas_log(ERROR, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        return -1;
    }

    const anjay_dm_internal_attrs_t internal_attrs = {
#ifdef WITH_CUSTOM_ATTRIBUTES
        _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
#endif // WITH_CUSTOM_ATTRIBUTES
                .standard = *attrs
    };

    int result;
    if (!(result = write_instance_attrs(anjay, ssid, obj, iid,
                                        &internal_attrs))) {
        (void) anjay_notify_instances_changed(anjay, oid);
    }
    return result;
}

int anjay_attr_storage_set_resource_attrs(
        anjay_t *anjay,
        anjay_ssid_t ssid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        const anjay_dm_resource_attributes_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (!obj) {
        return -1;
    }
    if (implements_any_resource_attrs_handlers(anjay, obj)) {
        fas_log(ERROR, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "resource",
                "resource_read_attrs", "resource_write_attrs");
        return -1;
    }
    if (iid == ANJAY_IID_INVALID) {
        fas_log(ERROR, "invalid instance id");
        return -1;
    }
    if (_anjay_dm_instance_present(anjay, obj, iid, NULL) <= 0) {
        fas_log(ERROR, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        return -1;
    }
    if (_anjay_dm_resource_supported_and_present(anjay, obj, iid, rid, NULL)
            <= 0) {
        fas_log(ERROR,
                "resource /%" PRIu16 "/%" PRIu16 "/%" PRIu16
                "does not exist or an error occurred during querying "
                "its presence",
                oid, iid, rid);
        return -1;
    }
    const anjay_dm_internal_res_attrs_t internal_attrs = {
#ifdef WITH_CUSTOM_ATTRIBUTES
        _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
#endif // WITH_CUSTOM_ATTRIBUTES
                .standard = *attrs
    };
    int result;
    if (!(result = write_resource_attrs(anjay, ssid, obj, iid, rid,
                                        &internal_attrs))) {
        (void) anjay_notify_instances_changed(anjay, oid);
    }
    return result;
}

#ifdef ANJAY_TEST
#    include "test/attr_storage.c"
#endif // ANJAY_TEST
