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

#ifdef ANJAY_WITH_MODULE_ATTR_STORAGE

#    include <assert.h>
#    include <inttypes.h>
#    include <math.h>
#    include <string.h>

#    include <avsystem/commons/avs_stream_membuf.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_raw_buffer.h>

#    include "anjay_mod_attr_storage.h"

VISIBILITY_SOURCE_BEGIN

//// LIFETIME AND OBJECT HANDLING //////////////////////////////////////////////

static anjay_dm_object_read_default_attrs_t object_read_default_attrs;
static anjay_dm_object_write_default_attrs_t object_write_default_attrs;
static anjay_dm_instance_read_default_attrs_t instance_read_default_attrs;
static anjay_dm_instance_write_default_attrs_t instance_write_default_attrs;
static anjay_dm_resource_read_attrs_t resource_read_attrs;
static anjay_dm_resource_write_attrs_t resource_write_attrs;
static anjay_dm_transaction_begin_t transaction_begin;
static anjay_dm_transaction_commit_t transaction_commit;
static anjay_dm_transaction_rollback_t transaction_rollback;

static anjay_notify_callback_t as_notify_callback;

static void as_delete(void *as_) {
    anjay_attr_storage_t *as = (anjay_attr_storage_t *) as_;
    assert(as);
    _anjay_attr_storage_clear(as);
    avs_stream_cleanup(&as->saved_state.persist_data);
    avs_free(as);
}

const anjay_dm_module_t _anjay_attr_storage_MODULE = {
    .overlay_handlers = {
        .object_read_default_attrs = object_read_default_attrs,
        .object_write_default_attrs = object_write_default_attrs,
        .instance_read_default_attrs = instance_read_default_attrs,
        .instance_write_default_attrs = instance_write_default_attrs,
        .resource_read_attrs = resource_read_attrs,
        .resource_write_attrs = resource_write_attrs,
        .transaction_begin = transaction_begin,
        .transaction_commit = transaction_commit,
        .transaction_rollback = transaction_rollback
    },
    .notify_callback = as_notify_callback,
    .deleter = as_delete
};

int anjay_attr_storage_install(anjay_t *anjay) {
    if (!anjay) {
        as_log(ERROR, _("ANJAY object must not be NULL"));
        return -1;
    }
    anjay_attr_storage_t *as =
            (anjay_attr_storage_t *) avs_calloc(1,
                                                sizeof(anjay_attr_storage_t));
    if (!as) {
        as_log(ERROR, _("out of memory"));
        return -1;
    }
    if (!(as->saved_state.persist_data = avs_stream_membuf_create())
            || _anjay_dm_module_install(anjay, &_anjay_attr_storage_MODULE,
                                        as)) {
        avs_stream_cleanup(&as->saved_state.persist_data);
        avs_free(as);
        return -1;
    }
    return 0;
}

bool anjay_attr_storage_is_modified(anjay_t *anjay) {
    anjay_attr_storage_t *as = _anjay_attr_storage_get(anjay);
    if (!as) {
        as_log(ERROR, _("Attribute Storage is not installed"));
        return false;
    }
    return as->modified_since_persist;
}

void _anjay_attr_storage_clear(anjay_attr_storage_t *as) {
    while (as->objects) {
        remove_object_entry(as, &as->objects);
    }
}

void anjay_attr_storage_purge(anjay_t *anjay) {
    anjay_attr_storage_t *as = _anjay_attr_storage_get(anjay);
    if (!as) {
        as_log(ERROR, _("Attribute Storage is not installed"));
        return;
    }
    _anjay_attr_storage_clear(as);
    _anjay_attr_storage_mark_modified(as);
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

anjay_attr_storage_t *_anjay_attr_storage_get(anjay_t *anjay) {
    return (anjay_attr_storage_t *) _anjay_dm_module_get_arg(
            anjay, &_anjay_attr_storage_MODULE);
}

static anjay_attr_storage_t *get_as(anjay_t *anjay) {
    assert(anjay);
    anjay_attr_storage_t *as = _anjay_attr_storage_get(anjay);
    assert(as);
    return (anjay_attr_storage_t *) as;
}

AVS_STATIC_ASSERT(offsetof(as_object_entry_t, oid) == 0, object_id_offset);
AVS_STATIC_ASSERT(offsetof(as_instance_entry_t, iid) == 0, instance_id_offset);
AVS_STATIC_ASSERT(offsetof(as_resource_entry_t, rid) == 0, resource_id_offset);
AVS_STATIC_ASSERT(offsetof(as_resource_instance_entry_t, riid) == 0,
                  resource_instance_id_offset);

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
                as_log(ERROR, _("out of memory"));
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

static inline AVS_LIST(as_object_entry_t) *
find_object(anjay_attr_storage_t *parent, anjay_oid_t id) {
    return (AVS_LIST(as_object_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->objects, sizeof(as_object_entry_t), id,
            false);
}

static inline AVS_LIST(as_object_entry_t) *
find_or_create_object(anjay_attr_storage_t *parent, anjay_oid_t id) {
    return (AVS_LIST(as_object_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->objects, sizeof(as_object_entry_t), id,
            true);
}

static inline AVS_LIST(as_instance_entry_t) *
find_instance(as_object_entry_t *parent, anjay_iid_t id) {
    return (AVS_LIST(as_instance_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->instances, sizeof(as_instance_entry_t),
            id, false);
}

static inline AVS_LIST(as_instance_entry_t) *
find_or_create_instance(as_object_entry_t *parent, anjay_iid_t id) {
    return (AVS_LIST(as_instance_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->instances, sizeof(as_instance_entry_t),
            id, true);
}

static inline AVS_LIST(as_resource_entry_t) *
find_resource(as_instance_entry_t *parent, anjay_rid_t id) {
    return (AVS_LIST(as_resource_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->resources, sizeof(as_resource_entry_t),
            id, false);
}

static inline AVS_LIST(as_resource_entry_t) *
find_or_create_resource(as_instance_entry_t *parent, anjay_rid_t id) {
    return (AVS_LIST(as_resource_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->resources, sizeof(as_resource_entry_t),
            id, true);
}

static void remove_instance_if_empty(AVS_LIST(as_instance_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->default_attrs && !(*entry_ptr)->resources) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

static void remove_resource_if_empty(AVS_LIST(as_resource_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->attrs) {
        AVS_LIST_DELETE(entry_ptr);
    }
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
    as_log(ERROR, _("Could not get valid RID"));
    return (anjay_rid_t) -1;
}

static anjay_ssid_t
query_ssid(anjay_t *anjay, anjay_oid_t oid, anjay_iid_t iid) {
    if (!is_ssid_reference_object(oid)) {
        return 0;
    }
    int64_t ssid;
    const anjay_uri_path_t uri = MAKE_RESOURCE_PATH(oid, iid, ssid_rid(oid));
    int result = _anjay_dm_read_resource_i64(anjay, &uri, &ssid);
    if (result || ssid <= 0 || ssid >= UINT16_MAX) {
        /* Most likely a Bootstrap instance, ignore. */
        return 0;
    }
    return (anjay_ssid_t) ssid;
}

static void remove_attrs_entry(anjay_attr_storage_t *as,
                               AVS_LIST(void) *attrs_ptr) {
    AVS_LIST_DELETE(attrs_ptr);
    _anjay_attr_storage_mark_modified(as);
}

static void
remove_attrs_for_servers_not_on_list(anjay_attr_storage_t *as,
                                     AVS_LIST(void) *attrs_ptr,
                                     AVS_LIST(anjay_ssid_t) ssid_list) {
    AVS_LIST(anjay_ssid_t) ssid_ptr = ssid_list;
    while (*attrs_ptr) {
        if (!ssid_ptr || *get_ssid_ptr(*attrs_ptr) < *ssid_ptr) {
            remove_attrs_entry(as, attrs_ptr);
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

static void remove_servers_not_on_ssid_list(anjay_attr_storage_t *as,
                                            AVS_LIST(anjay_ssid_t) ssid_list) {
    AVS_LIST(as_object_entry_t) *object_ptr;
    AVS_LIST(as_object_entry_t) object_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(object_ptr, object_helper, &as->objects) {
        remove_attrs_for_servers_not_on_list(
                as, (AVS_LIST(void) *) &(*object_ptr)->default_attrs,
                ssid_list);
        AVS_LIST(as_instance_entry_t) *instance_ptr;
        AVS_LIST(as_instance_entry_t) instance_helper;
        AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper,
                                       &(*object_ptr)->instances) {
            remove_attrs_for_servers_not_on_list(
                    as, (AVS_LIST(void) *) &(*instance_ptr)->default_attrs,
                    ssid_list);
            AVS_LIST(as_resource_entry_t) *res_ptr;
            AVS_LIST(as_resource_entry_t) res_helper;
            AVS_LIST_DELETABLE_FOREACH_PTR(res_ptr, res_helper,
                                           &(*instance_ptr)->resources) {
                remove_attrs_for_servers_not_on_list(
                        as, (AVS_LIST(void) *) &(*res_ptr)->attrs, ssid_list);
                remove_resource_if_empty(res_ptr);
            }
            remove_instance_if_empty(instance_ptr);
        }
        remove_object_if_empty(object_ptr);
    }
}

int _anjay_attr_storage_remove_absent_instances_clb(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *def_ptr,
        anjay_iid_t iid,
        void *instance_ptr_ptr_) {
    (void) def_ptr;
    AVS_LIST(as_instance_entry_t) **instance_ptr_ptr =
            (AVS_LIST(as_instance_entry_t) **) instance_ptr_ptr_;
    if (**instance_ptr_ptr && (**instance_ptr_ptr)->iid < iid) {
        anjay_attr_storage_t *as = get_as(anjay);
        while (**instance_ptr_ptr && (**instance_ptr_ptr)->iid < iid) {
            remove_instance_entry(as, *instance_ptr_ptr);
        }
    }
    if (**instance_ptr_ptr && (**instance_ptr_ptr)->iid == iid) {
        AVS_LIST_ADVANCE_PTR(instance_ptr_ptr);
    }
    return 0;
}

typedef struct {
    anjay_attr_storage_t *as;
    AVS_LIST(as_resource_entry_t) *resource_ptr;
} remove_absent_resources_clb_args_t;

static int
remove_absent_resources_clb(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *def_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_dm_resource_kind_t kind,
                            anjay_dm_resource_presence_t presence,
                            void *args_) {
    (void) anjay;
    (void) def_ptr;
    (void) iid;
    (void) kind;
    remove_absent_resources_clb_args_t *args =
            (remove_absent_resources_clb_args_t *) args_;
    while (*args->resource_ptr && (*args->resource_ptr)->rid < rid) {
        remove_resource_entry(args->as, args->resource_ptr);
    }
    if (*args->resource_ptr && (*args->resource_ptr)->rid == rid) {
        if (presence == ANJAY_DM_RES_ABSENT) {
            remove_resource_entry(args->as, args->resource_ptr);
        } else {
            AVS_LIST_ADVANCE_PTR(&args->resource_ptr);
        }
    }
    return 0;
}

int _anjay_attr_storage_remove_absent_resources(
        anjay_t *anjay,
        anjay_attr_storage_t *as,
        AVS_LIST(as_instance_entry_t) *instance_ptr,
        const anjay_dm_object_def_t *const *def_ptr) {
    remove_absent_resources_clb_args_t args = {
        .as = as,
        .resource_ptr = &(*instance_ptr)->resources
    };
    int result = 0;
    if (def_ptr) {
        result =
                _anjay_dm_foreach_resource(anjay, def_ptr, (*instance_ptr)->iid,
                                           remove_absent_resources_clb, &args);
    }
    while (!result && *args.resource_ptr) {
        remove_resource_entry(as, args.resource_ptr);
    }
    remove_instance_if_empty(instance_ptr);
    return result;
}

static void read_default_attrs(AVS_LIST(as_default_attrs_t) attrs,
                               anjay_ssid_t ssid,
                               anjay_dm_internal_oi_attrs_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
}

static void read_resource_attrs(AVS_LIST(as_resource_attrs_t) attrs,
                                anjay_ssid_t ssid,
                                anjay_dm_internal_r_attrs_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;
}

static int write_attrs_impl(anjay_attr_storage_t *as,
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
                as_log(ERROR, _("out of memory"));
                return ANJAY_ERR_INTERNAL;
            }
            *get_ssid_ptr(new_attrs) = ssid;
            AVS_LIST_INSERT(out_attrs, new_attrs);
        }
        memcpy(get_attrs_ptr(*out_attrs, attrs_field_offset), attrs,
               attrs_field_size);
        _anjay_attr_storage_mark_modified(as);
    } else if (found) {
        // entry exists, but writing EMPTY set of attributes
        // hence - removing
        remove_attrs_entry(as, (AVS_LIST(void) *) out_attrs);
    }
    return 0;
}

#    define WRITE_ATTRS(As, OutAttrs, IsEmptyFunc, Ssid, Attrs)               \
        write_attrs_impl((As), (AVS_LIST(void) *) (OutAttrs),                 \
                         sizeof(**(OutAttrs)),                                \
                         (size_t) ((char *) &(*(OutAttrs))->attrs             \
                                   - (char *) *(OutAttrs)),                   \
                         sizeof((*(OutAttrs))->attrs), (IsEmptyFunc), (Ssid), \
                         (Attrs))

static int write_object_attrs(anjay_t *anjay,
                              anjay_ssid_t ssid,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              const anjay_dm_internal_oi_attrs_t *attrs) {
    anjay_attr_storage_t *as = get_as(anjay);
    if (!as) {
        as_log(ERROR, _("Attribute Storage module is not installed"));
        return -1;
    }
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_or_create_object(as, (*obj_ptr)->oid);
    if (!object_ptr) {
        return -1;
    }
    int result = WRITE_ATTRS(as, &(*object_ptr)->default_attrs,
                             default_attrs_empty, ssid, attrs);
    remove_object_if_empty(object_ptr);
    return result;
}

static int write_instance_attrs(anjay_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                const anjay_dm_internal_oi_attrs_t *attrs) {
    assert(iid != ANJAY_ID_INVALID);
    anjay_attr_storage_t *as = get_as(anjay);
    if (!as) {
        as_log(ERROR, _("Attribute Storage module is not installed"));
        return -1;
    }

    int result = -1;
    AVS_LIST(as_object_entry_t) *object_ptr = NULL;
    AVS_LIST(as_instance_entry_t) *instance_ptr = NULL;
    if ((object_ptr = find_or_create_object(as, (*obj_ptr)->oid))
            && (instance_ptr = find_or_create_instance(*object_ptr, iid))) {
        result = WRITE_ATTRS(as, &(*instance_ptr)->default_attrs,
                             default_attrs_empty, ssid, attrs);
    }

    if (instance_ptr) {
        remove_instance_if_empty(instance_ptr);
    }
    if (object_ptr) {
        remove_object_if_empty(object_ptr);
    }
    return result;
}

static int write_resource_attrs(anjay_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                const anjay_dm_internal_r_attrs_t *attrs) {
    assert(iid != ANJAY_ID_INVALID && rid != ANJAY_ID_INVALID);
    anjay_attr_storage_t *as = get_as(anjay);
    if (!as) {
        as_log(ERROR, _("Attribute Storage module is not installed"));
        return -1;
    }

    int result = -1;
    AVS_LIST(as_object_entry_t) *object_ptr = NULL;
    AVS_LIST(as_instance_entry_t) *instance_ptr = NULL;
    AVS_LIST(as_resource_entry_t) *resource_ptr = NULL;
    if ((object_ptr = find_or_create_object(as, (*obj_ptr)->oid))
            && (instance_ptr = find_or_create_instance(*object_ptr, iid))
            && (resource_ptr = find_or_create_resource(*instance_ptr, rid))) {
        result = WRITE_ATTRS(as, &(*resource_ptr)->attrs, resource_attrs_empty,
                             ssid, attrs);
    }

    if (resource_ptr) {
        remove_resource_if_empty(resource_ptr);
    }
    if (instance_ptr) {
        remove_instance_if_empty(instance_ptr);
    }
    if (object_ptr) {
        remove_object_if_empty(object_ptr);
    }
    return result;
}

//// NOTIFICATION HANDLING /////////////////////////////////////////////////////

typedef struct {
    AVS_LIST(as_instance_entry_t) *instance_ptr;
    AVS_LIST(anjay_ssid_t) *ssid_ptr;
} remove_absent_instances_and_enumerate_ssids_args_t;

static int remove_absent_instances_and_enumerate_ssids_clb(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *def_ptr,
        anjay_iid_t iid,
        void *args_) {
    remove_absent_instances_and_enumerate_ssids_args_t *args =
            (remove_absent_instances_and_enumerate_ssids_args_t *) args_;
    int result = 0;
    if (args->instance_ptr) {
        result = _anjay_attr_storage_remove_absent_instances_clb(
                anjay, def_ptr, iid, &args->instance_ptr);
    }
    if (!result && args->ssid_ptr) {
        anjay_ssid_t ssid = query_ssid(anjay, (*def_ptr)->oid, iid);
        if (ssid) {
            assert(!*args->ssid_ptr);
            if (!(*args->ssid_ptr = AVS_LIST_NEW_ELEMENT(anjay_ssid_t))) {
                return ANJAY_ERR_INTERNAL;
            }
            // scan-build-7 is unable to deduce this is not NULL despite it
            // being checked in the if() above
            assert(*args->ssid_ptr);
            **args->ssid_ptr = ssid;
            AVS_LIST_ADVANCE_PTR(&args->ssid_ptr);
        }
    }
    return result;
}

static int compare_u16ids(const void *a, const void *b, size_t element_size) {
    assert(element_size == sizeof(uint16_t));
    (void) element_size;
    return *(const uint16_t *) a - *(const uint16_t *) b;
}

static int remove_absent_instances(anjay_t *anjay,
                                   anjay_attr_storage_t *as,
                                   anjay_oid_t oid) {
    AVS_LIST(as_object_entry_t) *obj_entry_ptr = find_object(as, oid);
    if (!obj_entry_ptr && !is_ssid_reference_object(oid)) {
        return 0;
    }
    assert(!obj_entry_ptr || *obj_entry_ptr);
    const anjay_dm_object_def_t *const *def_ptr =
            _anjay_dm_find_object_by_oid(anjay, oid);
    if (!def_ptr && obj_entry_ptr) {
        remove_object_entry(as, obj_entry_ptr);
        return 0;
    }
    AVS_LIST(anjay_ssid_t) ssids = NULL;
    remove_absent_instances_and_enumerate_ssids_args_t args = {
        .instance_ptr = obj_entry_ptr ? &(*obj_entry_ptr)->instances : NULL,
        .ssid_ptr = is_ssid_reference_object(oid) ? &ssids : NULL
    };
    int result = _anjay_dm_foreach_instance(
            anjay, def_ptr, remove_absent_instances_and_enumerate_ssids_clb,
            &args);
    if (!result) {
        while (args.instance_ptr && *args.instance_ptr) {
            remove_instance_entry(as, args.instance_ptr);
        }
    }
    if (obj_entry_ptr) {
        remove_object_if_empty(obj_entry_ptr);
    }
    if (!result && args.ssid_ptr) {
        AVS_LIST_SORT(&ssids, compare_u16ids);
        remove_servers_not_on_ssid_list(as, ssids);
    }
    AVS_LIST_CLEAR(&ssids);
    return result;
}

static int remove_absent_resources(anjay_t *anjay,
                                   anjay_attr_storage_t *as,
                                   AVS_LIST(as_object_entry_t) *as_object_ptr,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid) {
    assert(as_object_ptr);
    AVS_LIST(as_instance_entry_t) *instance_ptr =
            find_instance(*as_object_ptr, iid);
    if (!instance_ptr) {
        return 0;
    }
    int result = 0;
    if (obj_ptr) {
        result = _anjay_attr_storage_remove_absent_resources(
                anjay, as, instance_ptr, obj_ptr);
    }
    return result;
}

static int
as_notify_callback(anjay_t *anjay, anjay_notify_queue_t queue, void *data) {
    anjay_attr_storage_t *as = (anjay_attr_storage_t *) data;
    int result = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) object_entry;
    AVS_LIST_FOREACH(object_entry, queue) {
        int partial_result =
                remove_absent_instances(anjay, as, object_entry->oid);
        _anjay_update_ret(&result, partial_result);
        if (partial_result) {
            continue;
        }

        AVS_LIST(as_object_entry_t) *object_ptr =
                find_object(as, object_entry->oid);
        if (object_ptr) {
            const anjay_dm_object_def_t *const *obj_ptr =
                    _anjay_dm_find_object_by_oid(anjay, object_entry->oid);
            anjay_iid_t last_iid = ANJAY_ID_INVALID;
            AVS_LIST(anjay_notify_queue_resource_entry_t) resource_entry;
            AVS_LIST_FOREACH(resource_entry, object_entry->resources_changed) {
                if (resource_entry->iid != last_iid) {
                    // note that remove_absent_resources() does NOT call
                    // remove_object_if_empty().
                    _anjay_update_ret(&result,
                                      remove_absent_resources(
                                              anjay, as, object_ptr, obj_ptr,
                                              resource_entry->iid));
                }
                last_iid = resource_entry->iid;
            }
            remove_object_if_empty(object_ptr);
        }
    }
    return result;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

static int
object_read_default_attrs(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_ssid_t ssid,
                          anjay_dm_oi_attributes_t *out_) {
    anjay_dm_internal_oi_attrs_t *out = _anjay_dm_get_internal_oi_attrs(out_);
    if (implements_any_object_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_call_object_read_default_attrs(
                anjay, obj_ptr, ssid, out, &_anjay_attr_storage_MODULE);
    }
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(get_as(anjay), (*obj_ptr)->oid);
    read_default_attrs(object_ptr ? (*object_ptr)->default_attrs : NULL, ssid,
                       out);
    return 0;
}

static int
object_write_default_attrs(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_ssid_t ssid,
                           const anjay_dm_oi_attributes_t *attrs_) {
    const anjay_dm_internal_oi_attrs_t *attrs =
            _anjay_dm_get_internal_oi_attrs_const(attrs_);
    if (implements_any_object_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_call_object_write_default_attrs(
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
                            anjay_dm_oi_attributes_t *out_) {
    anjay_dm_internal_oi_attrs_t *out = _anjay_dm_get_internal_oi_attrs(out_);
    if (implements_any_instance_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_call_instance_read_default_attrs(
                anjay, obj_ptr, iid, ssid, out, &_anjay_attr_storage_MODULE);
    }
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(get_as(anjay), (*obj_ptr)->oid);
    AVS_LIST(as_instance_entry_t) *instance_ptr =
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
                             const anjay_dm_oi_attributes_t *attrs_) {
    const anjay_dm_internal_oi_attrs_t *attrs =
            _anjay_dm_get_internal_oi_attrs_const(attrs_);
    if (implements_any_instance_default_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_call_instance_write_default_attrs(
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
                               anjay_dm_r_attributes_t *out_) {
    anjay_dm_internal_r_attrs_t *out = _anjay_dm_get_internal_r_attrs(out_);
    if (implements_any_resource_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_call_resource_read_attrs(anjay, obj_ptr, iid, rid,
                                                  ssid, out,
                                                  &_anjay_attr_storage_MODULE);
    }
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(get_as(anjay), (*obj_ptr)->oid);
    AVS_LIST(as_instance_entry_t) *instance_ptr =
            object_ptr ? find_instance(*object_ptr, iid) : NULL;
    AVS_LIST(as_resource_entry_t) *res_ptr =
            instance_ptr ? find_resource(*instance_ptr, rid) : NULL;
    read_resource_attrs(res_ptr ? (*res_ptr)->attrs : NULL, ssid, out);
    return 0;
}

static int resource_write_attrs(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                const anjay_dm_r_attributes_t *attrs_) {
    const anjay_dm_internal_r_attrs_t *attrs =
            _anjay_dm_get_internal_r_attrs_const(attrs_);
    if (implements_any_resource_attrs_handlers(anjay, obj_ptr)) {
        return _anjay_dm_call_resource_write_attrs(anjay, obj_ptr, iid, rid,
                                                   ssid, attrs,
                                                   &_anjay_attr_storage_MODULE);
    }
    return write_resource_attrs(anjay, ssid, obj_ptr, iid, rid, attrs)
                   ? ANJAY_ERR_INTERNAL
                   : 0;
}

//// ACTIVE PROXY HANDLERS /////////////////////////////////////////////////////

static void saved_state_reset(anjay_attr_storage_t *as) {
    avs_stream_reset(as->saved_state.persist_data);
    avs_stream_membuf_fit(as->saved_state.persist_data);
}

static avs_error_t saved_state_save(anjay_attr_storage_t *as) {
    as->saved_state.modified_since_persist = as->modified_since_persist;
    return _anjay_attr_storage_persist_inner(as, as->saved_state.persist_data);
}

static avs_error_t saved_state_restore(anjay_t *anjay,
                                       anjay_attr_storage_t *as) {
    avs_error_t err =
            _anjay_attr_storage_restore_inner(anjay, as,
                                              as->saved_state.persist_data);
    as->modified_since_persist =
            (avs_is_err(err) ? true : as->saved_state.modified_since_persist);
    return err;
}

static int transaction_begin(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_attr_storage_t *as = get_as(anjay);
    if (as->saved_state.depth++ == 0) {
        if (avs_is_err(saved_state_save(as))) {
            --as->saved_state.depth;
            return ANJAY_ERR_INTERNAL;
        }
    }
    int result = _anjay_dm_call_transaction_begin(anjay, obj_ptr,
                                                  &_anjay_attr_storage_MODULE);
    if (result) {
        saved_state_reset(as);
    }
    return result;
}

static int transaction_commit(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_attr_storage_t *as = get_as(anjay);
    int result = _anjay_dm_call_transaction_commit(anjay, obj_ptr,
                                                   &_anjay_attr_storage_MODULE);
    if (--as->saved_state.depth == 0) {
        if (result && avs_is_err(saved_state_restore(anjay, as))) {
            result = ANJAY_ERR_INTERNAL;
        }
        saved_state_reset(as);
    }
    return result;
}

static int transaction_rollback(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr) {
    anjay_attr_storage_t *as = get_as(anjay);
    int result =
            _anjay_dm_call_transaction_rollback(anjay, obj_ptr,
                                                &_anjay_attr_storage_MODULE);
    if (--as->saved_state.depth == 0) {
        if (avs_is_err(saved_state_restore(anjay, as))) {
            result = ANJAY_ERR_INTERNAL;
        }
        saved_state_reset(as);
    }
    return result;
}

static const anjay_dm_object_def_t *const *
maybe_get_object_before_setting_attrs(anjay_t *anjay,
                                      anjay_ssid_t ssid,
                                      anjay_oid_t oid,
                                      const void *attrs) {
    if (!attrs) {
        as_log(ERROR, _("attributes cannot be NULL"));
        return NULL;
    }
    if (ssid == ANJAY_SSID_BOOTSTRAP || !_anjay_dm_ssid_exists(anjay, ssid)) {
        as_log(ERROR, _("SSID ") "%" PRIu16 _(" does not exist"), ssid);
        return NULL;
    }
    const anjay_dm_object_def_t *const *obj =
            _anjay_dm_find_object_by_oid(anjay, oid);
    if (!obj) {
        as_log(ERROR, "/%" PRIu16 _(" does not exist"), oid);
    }
    return obj;
}

#    define ERR_HANDLERS_IMPLEMENTED_BY_BACKEND           \
        _("cannot set ")                                  \
        "%s" _(" level attribs: ") "%s" _(" or ") "%s" _( \
                " is implemented by the backend object")

#    define ERR_INSTANCE_PRESENCE_CHECK                              \
        _("instance ")                                               \
        "/%" PRIu16                                                  \
        "/%" PRIu16 _(" does not exist or an error occurred during " \
                      "querying its presence")

#    define ERR_RESOURCE_PRESENCE_CHECK                                    \
        _("resource ")                                                     \
        "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 _(                             \
                "does not exist or an error occurred during querying its " \
                "presence")

#    define ERR_RESOURCE_INSTANCE_PRESENCE_CHECK                           \
        _("resource instance ")                                            \
        "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 _(                 \
                "does not exist or an error occurred during querying its " \
                "presence")

int anjay_attr_storage_set_object_attrs(anjay_t *anjay,
                                        anjay_ssid_t ssid,
                                        anjay_oid_t oid,
                                        const anjay_dm_oi_attributes_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (!obj) {
        return -1;
    }
    if (implements_any_object_default_attrs_handlers(anjay, obj)) {
        as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "object",
               "object_read_default_attrs", "object_write_default_attrs");
        return -1;
    }
    const anjay_dm_internal_oi_attrs_t internal_attrs = {
        .standard = *attrs, _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
    };

    int result;
    if (!(result = write_object_attrs(anjay, ssid, obj, &internal_attrs))) {
        (void) anjay_notify_instances_changed(anjay, oid);
    }
    return result;
}

int anjay_attr_storage_set_instance_attrs(
        anjay_t *anjay,
        anjay_ssid_t ssid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_dm_oi_attributes_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (!obj) {
        return -1;
    }
    if (implements_any_instance_default_attrs_handlers(anjay, obj)) {
        as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "instance",
               "instance_read_default_attrs", "instance_write_default_attrs");
        return -1;
    }
    if (_anjay_dm_verify_instance_present(anjay, obj, iid)) {
        as_log(DEBUG, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        return -1;
    }

    const anjay_dm_internal_oi_attrs_t internal_attrs = {
        .standard = *attrs, _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
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
        const anjay_dm_r_attributes_t *attrs) {
    const anjay_dm_object_def_t *const *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (!obj) {
        return -1;
    }
    if (implements_any_resource_attrs_handlers(anjay, obj)) {
        as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "resource",
               "resource_read_attrs", "resource_write_attrs");
        return -1;
    }
    if (_anjay_dm_verify_instance_present(anjay, obj, iid)) {
        as_log(DEBUG, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        return -1;
    }
    if (_anjay_dm_verify_resource_present(anjay, obj, iid, rid, NULL)) {
        as_log(DEBUG, ERR_RESOURCE_PRESENCE_CHECK, oid, iid, rid);
        return -1;
    }

    const anjay_dm_internal_r_attrs_t internal_attrs = {
        .standard = *attrs, _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
    };
    int result;
    if (!(result = write_resource_attrs(anjay, ssid, obj, iid, rid,
                                        &internal_attrs))) {
        (void) anjay_notify_instances_changed(anjay, oid);
    }
    return result;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/attr_storage/attr_storage.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_MODULE_ATTR_STORAGE
