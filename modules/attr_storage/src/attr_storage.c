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

#include <assert.h>
#include <inttypes.h>
#include <math.h>

#include <avsystem/commons/stream/stream_membuf.h>

#include <anjay_modules/dm.h>
#include <anjay_modules/utils.h>

#include "attr_storage.h"

VISIBILITY_SOURCE_BEGIN

//// LIFETIME //////////////////////////////////////////////////////////////////

anjay_attr_storage_t *anjay_attr_storage_new(anjay_t *anjay) {
    if (!anjay) {
        fas_log(ERROR, "ANJAY object must not be NULL");
        return NULL;
    }
    anjay_attr_storage_t *fas =
            (anjay_attr_storage_t *) calloc(1, sizeof(anjay_attr_storage_t));
    fas->anjay = anjay;
    if (!(fas->saved_state.persist_data = avs_stream_membuf_create())) {
        free(fas);
        return NULL;
    }
    return fas;
}

static void remove_instance_entry(anjay_attr_storage_t *fas,
                                  AVS_LIST(fas_instance_entry_t) *entry_ptr) {
    AVS_LIST_CLEAR(&(*entry_ptr)->default_attrs);
    while ((*entry_ptr)->resources) {
        remove_resource_entry(fas, &(*entry_ptr)->resources);
    }
    AVS_LIST_DELETE(entry_ptr);
    mark_modified(fas);
}

static void reset_instance_it_state(fas_object_t *obj) {
    AVS_LIST_CLEAR(&obj->instance_it_iids);
    obj->instance_it_last_cookie = NULL;
}

fas_object_t *_anjay_attr_storage_find_object(anjay_attr_storage_t *fas,
                                              anjay_oid_t oid) {
    AVS_LIST(fas_object_t) object;
    AVS_LIST_FOREACH(object, fas->objects) {
        if (object->def.oid == oid) {
            return object;
        } else if (object->def.oid > oid) {
            break;
        }
    }
    return NULL;
}

void _anjay_attr_storage_clear_object(fas_object_t *obj) {
    anjay_attr_storage_t *fas = obj->fas;
    reset_instance_it_state(obj);
    AVS_LIST_CLEAR(&obj->default_attrs);
    while (obj->instances) {
        remove_instance_entry(fas, &obj->instances);
    }
}

void anjay_attr_storage_delete(anjay_attr_storage_t *attr_storage) {
    AVS_LIST_CLEAR(&attr_storage->objects) {
        _anjay_attr_storage_clear_object(attr_storage->objects);
    }
    avs_stream_cleanup(&attr_storage->saved_state.persist_data);
    free(attr_storage);
}

//// HELPERS ///////////////////////////////////////////////////////////////////

static bool implements_any_object_default_attrs_handlers(
        const anjay_dm_object_def_t *def) {
    return def->object_read_default_attrs
            || def->object_write_default_attrs;
}

static bool implements_any_instance_default_attrs_handlers(
        const anjay_dm_object_def_t *def) {
    return def->instance_read_default_attrs
            || def->instance_write_default_attrs;
}

static bool implements_any_resource_attrs_handlers(
        const anjay_dm_object_def_t *def) {
    return def->resource_read_attrs
            || def->resource_write_attrs;
}

static void
remove_resource_if_empty(AVS_LIST(fas_resource_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->attrs) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

static inline fas_object_t *
get_object(const anjay_dm_object_def_t *const *obj_ptr) {
    assert(obj_ptr);
    return AVS_CONTAINER_OF(obj_ptr, fas_object_t, def_ptr);
}

#define DEFINE_FIND_OR_CREATE(Entity, ParentType, Id) \
static AVS_LIST(fas_##Entity##_entry_t) * \
find_or_create_##Entity##_impl (ParentType *parent, \
                                anjay_##Id##_t Id, \
                                bool allow_create) { \
    if (!parent) { \
        return NULL; \
    } \
    AVS_LIST(fas_##Entity##_entry_t) *entry_ptr; \
    AVS_LIST_FOREACH_PTR(entry_ptr, &parent->Entity##s) { \
        if ((*entry_ptr)->Id >= Id) { \
            break; \
        } \
    } \
    if (!*entry_ptr || (*entry_ptr)->Id != Id) { \
        if (allow_create) { \
            AVS_LIST(fas_##Entity##_entry_t) new_entry = \
                    AVS_LIST_NEW_ELEMENT(fas_##Entity##_entry_t); \
            if (!new_entry) { \
                fas_log(ERROR, "Out of memory"); \
                return NULL; \
            } \
            new_entry->Id = Id; \
            AVS_LIST_INSERT(entry_ptr, new_entry); \
        } else { \
            return NULL; \
        } \
    } \
    return entry_ptr; \
} \
\
static inline AVS_LIST(fas_##Entity##_entry_t) * \
find_##Entity (ParentType *parent, anjay_##Id##_t Id) { \
    return find_or_create_##Entity##_impl(parent, Id, false); \
} \
\
static inline AVS_LIST(fas_##Entity##_entry_t) * \
find_or_create_##Entity (ParentType *parent, anjay_##Id##_t Id) { \
    return find_or_create_##Entity##_impl(parent, Id, true); \
}

// find_or_create_instance_impl
// find_or_create_instance
DEFINE_FIND_OR_CREATE(instance, fas_object_t, iid)
// find_or_create_resource_impl
// find_or_create_resource
DEFINE_FIND_OR_CREATE(resource, fas_instance_entry_t, rid)

static void remove_instance(fas_object_t *obj, anjay_iid_t iid) {
    AVS_LIST(fas_instance_entry_t) *instance_ptr = find_instance(obj, iid);
    if (instance_ptr && *instance_ptr) {
        remove_instance_entry(obj->fas, instance_ptr);
    }
}

static void remove_resource(fas_object_t *obj, anjay_rid_t rid) {
    AVS_LIST(fas_instance_entry_t) *instance_ptr;
    AVS_LIST(fas_instance_entry_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, helper, &obj->instances) {
        AVS_LIST(fas_resource_entry_t) *resource_ptr =
                find_resource(*instance_ptr, rid);
        if (resource_ptr && *resource_ptr) {
            remove_resource_entry(obj->fas, resource_ptr);
        }
        remove_instance_if_empty(instance_ptr);
    }
}

static inline bool is_ssid_reference_object(anjay_oid_t oid) {
    return oid == ANJAY_DM_OID_SECURITY
            || oid == ANJAY_DM_OID_SERVER;
}

static inline anjay_rid_t ssid_rid(anjay_oid_t oid) {
    switch (oid) {
    case ANJAY_DM_OID_SECURITY:
        return ANJAY_DM_RID_SECURITY_SSID;
    case ANJAY_DM_OID_SERVER:
        return ANJAY_DM_RID_SERVER_SSID;
    default:
        assert(0 && "Invalid object for Short Server ID query");
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
    int result = _anjay_dm_res_read_i64(anjay,
                                        &(const anjay_resource_path_t) {
                                            oid, iid, ssid_rid(oid)
                                        }, &ssid);
    if (result || ssid <= 0 || ssid >= UINT16_MAX) {
        fas_log(WARNING, "Could not get valid SSID via /%" PRIu16 "/%" PRIu16,
                oid, iid);
        return 0;
    }
    return (anjay_ssid_t) ssid;
}

static void remove_attrs_entry(anjay_attr_storage_t *fas,
                               AVS_LIST(fas_attrs_t) *attrs_ptr) {
    AVS_LIST_DELETE(attrs_ptr);
    mark_modified(fas);
}

static void remove_attrs_for_server(anjay_attr_storage_t *fas,
                                    AVS_LIST(fas_attrs_t) *attrs_ptr,
                                    anjay_ssid_t ssid) {
    AVS_LIST_ITERATE_PTR(attrs_ptr) {
        assert(!*AVS_LIST_NEXT_PTR(attrs_ptr)
               || (*attrs_ptr)->ssid < (*AVS_LIST_NEXT_PTR(attrs_ptr))->ssid);
        if ((*attrs_ptr)->ssid == ssid) {
            remove_attrs_entry(fas, attrs_ptr);
            assert(!*attrs_ptr || ssid < (*attrs_ptr)->ssid);
            return;
        } else if ((*attrs_ptr)->ssid > ssid) {
            break;
        }
    }
}

static void
remove_attrs_for_servers_not_on_list(anjay_attr_storage_t *fas,
                                     AVS_LIST(fas_attrs_t) *attrs_ptr,
                                     AVS_LIST(anjay_ssid_t) ssid) {
    while (*attrs_ptr) {
        if (!ssid || (*attrs_ptr)->ssid < *ssid) {
            remove_attrs_entry (fas, attrs_ptr);
        } else {
            while (ssid && (*attrs_ptr)->ssid > *ssid) {
                ssid = AVS_LIST_NEXT(ssid);
            }
            if (ssid && (*attrs_ptr)->ssid == *ssid) {
                ssid = AVS_LIST_NEXT(ssid);
                attrs_ptr = AVS_LIST_NEXT_PTR(attrs_ptr);
            }
        }
    }
}

#define DEFINE_REMOVE_SERVERS(Subject, SsidReferenceType) \
static void remove_##Subject (anjay_attr_storage_t *fas, \
                              SsidReferenceType ssid_ref) { \
    AVS_LIST(fas_object_t) object; \
    AVS_LIST_FOREACH(object, fas->objects) { \
        remove_attrs_for_##Subject (fas, &object->default_attrs, ssid_ref); \
        AVS_LIST(fas_instance_entry_t) *instance_ptr; \
        AVS_LIST(fas_instance_entry_t) instance_helper; \
        AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper, \
                                       &object->instances) { \
            remove_attrs_for_##Subject ( \
                    fas, &(*instance_ptr)->default_attrs, ssid_ref); \
            AVS_LIST(fas_resource_entry_t) *res_ptr; \
            AVS_LIST(fas_resource_entry_t) res_helper; \
            AVS_LIST_DELETABLE_FOREACH_PTR(res_ptr, res_helper, \
                                           &(*instance_ptr)->resources) { \
                remove_attrs_for_##Subject (fas, &(*res_ptr)->attrs, \
                                                     ssid_ref); \
                remove_resource_if_empty(res_ptr); \
            } \
            remove_instance_if_empty(instance_ptr); \
        } \
    } \
}

// remove_server
DEFINE_REMOVE_SERVERS(server, anjay_ssid_t)
// remove_servers_not_on_list
DEFINE_REMOVE_SERVERS(servers_not_on_list, AVS_LIST(anjay_ssid_t))

static int compare_u16ids(const void *a, const void *b, size_t element_size) {
    assert(element_size == sizeof(uint16_t));
    (void) element_size;
    return *(const uint16_t *) a - *(const uint16_t *) b;
}

static int remove_servers_after_iteration(anjay_t *anjay, fas_object_t *obj) {
    AVS_LIST(anjay_ssid_t) ssids = NULL;
    AVS_LIST(anjay_iid_t) iid;
    AVS_LIST_FOREACH(iid, obj->instance_it_iids) {
        anjay_ssid_t ssid = query_ssid(anjay, obj->def.oid, *iid);
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

    AVS_LIST_SORT(&ssids, compare_u16ids);
    remove_servers_not_on_list(obj->fas, ssids);
    AVS_LIST_CLEAR(&ssids);
    return 0;
}

static int remove_instances_after_iteration(anjay_t *anjay, fas_object_t *obj) {
    int result = 0;
    AVS_LIST_SORT(&obj->instance_it_iids, compare_u16ids);
    AVS_LIST(anjay_iid_t) iid = obj->instance_it_iids;
    AVS_LIST(fas_instance_entry_t) *instance_ptr = &obj->instances;
    while (*instance_ptr) {
        if (!iid || (*instance_ptr)->iid < *iid) {
            remove_instance_entry(obj->fas, instance_ptr);
        } else {
            while (iid && (*instance_ptr)->iid > *iid) {
                iid = AVS_LIST_NEXT(iid);
            }
            if (iid && (*instance_ptr)->iid == *iid) {
                iid = AVS_LIST_NEXT(iid);
                instance_ptr = AVS_LIST_NEXT_PTR(instance_ptr);
            }
        }
    }
    if (is_ssid_reference_object(obj->def.oid)) {
        result = remove_servers_after_iteration(anjay, obj);
    }
    reset_instance_it_state(obj);
    return result;
}

static void read_attrs(AVS_LIST(fas_attrs_t) attrs,
                       anjay_ssid_t ssid,
                       anjay_dm_attributes_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_ATTRIBS_EMPTY;
}

static int write_attrs(anjay_attr_storage_t *fas,
                       AVS_LIST(fas_attrs_t) * out_attrs,
                       anjay_ssid_t ssid,
                       const anjay_dm_attributes_t *attrs) {
    AVS_LIST_ITERATE_PTR(out_attrs) {
        if ((*out_attrs)->ssid >= ssid) {
            break;
        }
    }
    bool found = (*out_attrs && (*out_attrs)->ssid == ssid);
    bool filled = !_anjay_dm_attributes_empty(attrs);
    if (filled) {
        // writing non-empty set of attributes
        if (!found) {
            // entry does not exist, creating
            AVS_LIST(fas_attrs_t) new_attrs = AVS_LIST_NEW_ELEMENT(fas_attrs_t);
            if (!new_attrs) {
                fas_log(ERROR, "Out of memory");
                return ANJAY_ERR_INTERNAL;
            }
            new_attrs->ssid = ssid;
            AVS_LIST_INSERT(out_attrs, new_attrs);
        }
        (*out_attrs)->attrs = *attrs;
        mark_modified(fas);
    } else if (found) {
         // entry exists, but writing EMPTY set of attributes
         // hence - removing
        remove_attrs_entry(fas, out_attrs);
    }
    return 0;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

static int object_read_default_attrs(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_ssid_t ssid,
                                     anjay_dm_attributes_t *out) {
    fas_object_t *obj = get_object(obj_ptr);
    if (implements_any_object_default_attrs_handlers(*obj->backend)) {
        return (*obj->backend)->object_read_default_attrs(anjay, obj->backend,
                                                          ssid, out);
    }
    read_attrs(obj->default_attrs, ssid, out);
    return 0;
}

static int object_write_default_attrs(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_ssid_t ssid,
                                      const anjay_dm_attributes_t *attrs) {
    fas_object_t *obj = get_object(obj_ptr);
    if (implements_any_object_default_attrs_handlers(*obj->backend)) {
        return (*obj->backend)->object_write_default_attrs(anjay, obj->backend,
                                                           ssid, attrs);
    }
    return write_attrs(obj->fas, &obj->default_attrs, ssid, attrs);
}

static int instance_read_default_attrs(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid,
                                       anjay_ssid_t ssid,
                                       anjay_dm_attributes_t *out) {
    fas_object_t *obj = get_object(obj_ptr);
    if (implements_any_instance_default_attrs_handlers(*obj->backend)) {
        return (*obj->backend)->instance_read_default_attrs(
                anjay, obj->backend, iid, ssid, out);
    }
    AVS_LIST(fas_instance_entry_t) *instance_ptr = find_instance(obj, iid);
    read_attrs((instance_ptr && *instance_ptr) ? (*instance_ptr)->default_attrs
                                               : NULL,
               ssid, out);
    return 0;
}

static int instance_write_default_attrs(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid,
                                        anjay_ssid_t ssid,
                                        const anjay_dm_attributes_t *attrs) {
    fas_object_t *obj = get_object(obj_ptr);
    if (implements_any_instance_default_attrs_handlers(*obj->backend)) {
        return (*obj->backend)->instance_write_default_attrs(
                anjay, obj->backend, iid, ssid, attrs);
    }
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            find_or_create_instance(obj, iid);
    if (!(instance_ptr && *instance_ptr)) {
        return ANJAY_ERR_INTERNAL;
    }
    int result =
            write_attrs(obj->fas, &(*instance_ptr)->default_attrs, ssid, attrs);
    remove_instance_if_empty(instance_ptr);
    return result;
}

static int resource_read_attrs(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_ssid_t ssid,
                               anjay_dm_attributes_t *out) {
    fas_object_t *obj = get_object(obj_ptr);
    if (implements_any_resource_attrs_handlers(*obj->backend)) {
        return (*obj->backend)->resource_read_attrs(
                anjay, obj->backend, iid, rid, ssid, out);
    }
    AVS_LIST(fas_instance_entry_t) *instance_ptr = find_instance(obj, iid);
    AVS_LIST(fas_resource_entry_t) *res_ptr =
            find_resource(instance_ptr ? *instance_ptr : NULL, rid);
    read_attrs((res_ptr && *res_ptr) ? (*res_ptr)->attrs : NULL, ssid, out);
    return 0;
}

static int resource_write_attrs(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                const anjay_dm_attributes_t *attrs) {
    fas_object_t *obj = get_object(obj_ptr);
    if (implements_any_resource_attrs_handlers(*obj->backend)) {
        return (*obj->backend)->resource_write_attrs(
                anjay, obj->backend, iid, rid, ssid, attrs);
    }
    AVS_LIST(fas_instance_entry_t) *instance_ptr =
            find_or_create_instance(obj, iid);
    if (!(instance_ptr && *instance_ptr)) {
        return ANJAY_ERR_INTERNAL;
    }
    AVS_LIST(fas_resource_entry_t) *resource_ptr =
            find_or_create_resource(*instance_ptr, rid);
    if (!(resource_ptr && *resource_ptr)) {
        return ANJAY_ERR_INTERNAL;
    }
    int result = write_attrs(obj->fas, &(*resource_ptr)->attrs, ssid, attrs);
    remove_resource_if_empty(resource_ptr);
    remove_instance_if_empty(instance_ptr);
    return result;
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
    fas_object_t *obj = get_object(obj_ptr);
    void *orig_cookie = *cookie;
    if (!orig_cookie) {
        reset_instance_it_state(obj);
    }
    int result = (*obj->backend)->instance_it(anjay, obj->backend, out, cookie);
    if (result || obj->instance_it_last_cookie != orig_cookie) {
        reset_instance_it_state(obj);
    } else {
        obj->instance_it_last_cookie = *cookie;
        if (*out == ANJAY_IID_INVALID) {
            result = remove_instances_after_iteration(anjay, obj);
        } else {
            anjay_iid_t *new_iid = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
            if (!new_iid) {
                return ANJAY_ERR_INTERNAL;
            }
            *new_iid = *out;
            AVS_LIST_INSERT(&obj->instance_it_iids, new_iid);
        }
    }
    return result;
}

static int instance_present(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid) {
    fas_object_t *obj = get_object(obj_ptr);
    int result = (*obj->backend)->instance_present(anjay, obj->backend, iid);
    if (result == 0) {
        remove_instance(obj, iid);
    }
    return result;
}

static int instance_remove(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid) {
    fas_object_t *obj = get_object(obj_ptr);
    anjay_ssid_t ssid = query_ssid(anjay, obj->def.oid, iid);
    int result = (*obj->backend)->instance_remove(anjay, obj->backend, iid);
    if (result == 0) {
        remove_instance(obj, iid);
        if (ssid) {
            remove_server(obj->fas, ssid);
        }
    }
    return result;
}

static int resource_present(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid) {
    fas_object_t *obj = get_object(obj_ptr);
    int result = (*obj->backend)->resource_present(anjay, obj->backend, iid, rid);
    if (result == 0) {
        remove_resource(obj, rid);
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

static int saved_state_restore(anjay_attr_storage_t *fas) {
    int result =
            _anjay_attr_storage_restore_inner(fas,
                                              fas->saved_state.persist_data);
    fas->modified_since_persist = fas->saved_state.modified_since_persist;
    return result;
}

static int transaction_begin(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    fas_object_t *obj = get_object(obj_ptr);

    if (saved_state_save(obj->fas)) {
        return ANJAY_ERR_INTERNAL;
    }
    int result = (*obj->backend)->transaction_begin(anjay, obj->backend);
    if (result) {
        saved_state_reset(obj->fas);
    }
    return result;
}

static int transaction_commit(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    fas_object_t *obj = get_object(obj_ptr);
    int result = (*obj->backend)->transaction_commit(anjay, obj->backend);
    if (result && saved_state_restore(obj->fas)) {
        result = ANJAY_ERR_INTERNAL;
    }
    saved_state_reset(obj->fas);
    return result;
}

static int transaction_rollback(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    fas_object_t *obj = get_object(obj_ptr);
    int result = (*obj->backend)->transaction_rollback(anjay, obj->backend);
    if (saved_state_restore(obj->fas)) {
        result = ANJAY_ERR_INTERNAL;
    }
    saved_state_reset(obj->fas);
    return result;
}

//// PASSIVE PROXY HANDLERS ////////////////////////////////////////////////////

static int instance_reset(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->instance_reset(anjay, obj->backend, iid);
}

static int instance_create(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t *inout_iid,
                           anjay_ssid_t ssid) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->instance_create(anjay, obj->backend, inout_iid,
                                            ssid);
}

static int resource_read(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_output_ctx_t *ctx) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->resource_read(anjay, obj->backend, iid, rid, ctx);
}

static int resource_write(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_input_ctx_t *ctx) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->resource_write(anjay, obj->backend, iid, rid, ctx);
}

static int resource_execute(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_execute_ctx_t *ctx) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->resource_execute(anjay, obj->backend, iid, rid,
                                             ctx);
}

static int resource_dim(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr,
                        anjay_iid_t iid,
                        anjay_rid_t rid) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->resource_dim(anjay, obj->backend, iid, rid);
}

static int resource_supported(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_rid_t rid) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->resource_supported(anjay, obj->backend, rid);
}

static int resource_operations(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_rid_t rid,
                               anjay_dm_resource_op_mask_t *out) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->resource_operations(anjay, obj->backend, rid, out);
}

static int transaction_validate(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->transaction_validate(anjay, obj->backend);
}

static int on_register(anjay_t *anjay,
                       const anjay_dm_object_def_t *const *obj_ptr) {
    fas_object_t *obj = get_object(obj_ptr);
    return (*obj->backend)->on_register(anjay, obj->backend);
}

//// OBJECT HANDLING ///////////////////////////////////////////////////////////

static void init_object(anjay_attr_storage_t *attr_storage,
                        fas_object_t *out,
                        const anjay_dm_object_def_t *const *def_ptr) {
    out->def_ptr = &out->def;
    out->backend = def_ptr;
    out->fas = attr_storage;
    out->def.oid = (*def_ptr)->oid;
    out->def.rid_bound = (*def_ptr)->rid_bound;
#define INIT_HANDLER(Handler) (out->def.Handler = Handler)
#define INIT_PROXY(Handler) if ((*def_ptr)->Handler) INIT_HANDLER(Handler)
    if (implements_any_object_default_attrs_handlers(*def_ptr)) {
        INIT_PROXY(object_read_default_attrs);
        INIT_PROXY(object_write_default_attrs);
    } else {
        INIT_HANDLER(object_read_default_attrs);
        INIT_HANDLER(object_write_default_attrs);
    }
    INIT_PROXY(instance_it);
    INIT_PROXY(instance_reset);
    INIT_PROXY(instance_present);
    INIT_PROXY(instance_create);
    INIT_PROXY(instance_remove);
    if (implements_any_instance_default_attrs_handlers(*def_ptr)) {
        INIT_PROXY(instance_read_default_attrs);
        INIT_PROXY(instance_write_default_attrs);
    } else {
        INIT_HANDLER(instance_read_default_attrs);
        INIT_HANDLER(instance_write_default_attrs);
    }
    INIT_PROXY(resource_present);
    INIT_PROXY(resource_read);
    INIT_PROXY(resource_write);
    INIT_PROXY(resource_execute);
    INIT_PROXY(resource_dim);
    if (implements_any_resource_attrs_handlers(*def_ptr)) {
        INIT_PROXY(resource_read_attrs);
        INIT_PROXY(resource_write_attrs);
    } else {
        INIT_HANDLER(resource_read_attrs);
        INIT_HANDLER(resource_write_attrs);
    }
    INIT_PROXY(resource_supported);
    INIT_PROXY(resource_operations);
    INIT_PROXY(on_register);
    INIT_PROXY(transaction_begin);
    INIT_PROXY(transaction_validate);
    INIT_PROXY(transaction_commit);
    INIT_PROXY(transaction_rollback);
#undef INIT_PROXY
#undef INIT_HANDLER
}

const anjay_dm_object_def_t *const *
anjay_attr_storage_wrap_object(anjay_attr_storage_t *attr_storage,
                               const anjay_dm_object_def_t *const *def_ptr) {
    if (!attr_storage) {
        fas_log(ERROR, "Invalid anjay_attr_storage_t pointer");
        return NULL;
    }
    if (!def_ptr || !*def_ptr) {
        fas_log(ERROR, "invalid object pointer");
        return NULL;
    }
    AVS_LIST(fas_object_t) *obj_ptr = &attr_storage->objects;
    while (*obj_ptr && (*obj_ptr)->def.oid < (*def_ptr)->oid) {
        obj_ptr = AVS_LIST_NEXT_PTR(obj_ptr);
    }
    if (*obj_ptr && (*obj_ptr)->def.oid == (*def_ptr)->oid) {
        fas_log(ERROR, "Object %" PRIu16 " is already registered",
                       (*def_ptr)->oid);
        return NULL;
    }

    AVS_LIST(fas_object_t) obj = AVS_LIST_NEW_ELEMENT(fas_object_t);
    AVS_LIST_INSERT(obj_ptr, obj);
    init_object(attr_storage, obj, def_ptr);
    return &obj->def_ptr;
}

bool anjay_attr_storage_is_modified(anjay_attr_storage_t *attr_storage) {
    return attr_storage->modified_since_persist;
}

#ifdef ANJAY_TEST
#include "test/attr_storage.c"
#endif // ANJAY_TEST
