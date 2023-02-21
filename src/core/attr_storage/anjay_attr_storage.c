/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#ifdef ANJAY_WITH_ATTR_STORAGE

#    include <assert.h>
#    include <inttypes.h>
#    include <math.h>
#    include <string.h>

#    include <avsystem/commons/avs_stream_membuf.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_raw_buffer.h>

#    include "../anjay_core.h"

#    define ANJAY_ATTR_STORAGE_INTERNALS

#    include "anjay_attr_storage_private.h"

VISIBILITY_SOURCE_BEGIN

//// LIFETIME AND OBJECT HANDLING //////////////////////////////////////////////

int _anjay_attr_storage_init(anjay_unlocked_t *anjay) {
    assert(anjay);
    if (!(anjay->attr_storage.saved_state.persist_data =
                  avs_stream_membuf_create())) {
        return -1;
    }
    return 0;
}

void _anjay_attr_storage_cleanup(anjay_attr_storage_t *as) {
    assert(as);
    _anjay_attr_storage_clear(as);
    avs_stream_cleanup(&as->saved_state.persist_data);
}

bool anjay_attr_storage_is_modified(anjay_t *anjay_locked) {
    bool result = false;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    result = anjay->attr_storage.modified_since_persist;
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

void _anjay_attr_storage_clear(anjay_attr_storage_t *as) {
    while (as->objects) {
        remove_object_entry(as, &as->objects);
    }
}

void anjay_attr_storage_purge(anjay_t *anjay_locked) {
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    _anjay_attr_storage_clear(&anjay->attr_storage);
    _anjay_attr_storage_mark_modified(&anjay->attr_storage);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

//// HELPERS ///////////////////////////////////////////////////////////////////

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

#    ifdef ANJAY_WITH_LWM2M11
static inline AVS_LIST(as_resource_instance_entry_t) *
find_resource_instance(as_resource_entry_t *parent, anjay_riid_t id) {
    return (AVS_LIST(as_resource_instance_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->resource_instances,
            sizeof(as_resource_instance_entry_t), id, false);
}

static inline AVS_LIST(as_resource_instance_entry_t) *
find_or_create_resource_instance(as_resource_entry_t *parent, anjay_riid_t id) {
    return (AVS_LIST(as_resource_instance_entry_t) *) find_or_create_entry_impl(
            (AVS_LIST(void) *) &parent->resource_instances,
            sizeof(as_resource_instance_entry_t), id, true);
}
#    endif // ANJAY_WITH_LWM2M11

static void remove_instance_if_empty(AVS_LIST(as_instance_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->default_attrs && !(*entry_ptr)->resources) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

static void remove_resource_if_empty(AVS_LIST(as_resource_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->attrs
#    ifdef ANJAY_WITH_LWM2M11
            && !(*entry_ptr)->resource_instances
#    endif // ANJAY_WITH_LWM2M11
    ) {
        AVS_LIST_DELETE(entry_ptr);
    }
}

#    ifdef ANJAY_WITH_LWM2M11
static void remove_resource_instance_if_empty(
        AVS_LIST(as_resource_instance_entry_t) *entry_ptr) {
    if (!(*entry_ptr)->attrs) {
        AVS_LIST_DELETE(entry_ptr);
    }
}
#    endif // ANJAY_WITH_LWM2M11

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
query_ssid(anjay_unlocked_t *anjay, anjay_oid_t oid, anjay_iid_t iid) {
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
#    ifdef ANJAY_WITH_LWM2M11
                AVS_LIST(as_resource_instance_entry_t) *res_instance_ptr;
                AVS_LIST(as_resource_instance_entry_t) res_instance_helper;
                AVS_LIST_DELETABLE_FOREACH_PTR(
                        res_instance_ptr, res_instance_helper,
                        &(*res_ptr)->resource_instances) {
                    remove_attrs_for_servers_not_on_list(
                            as, (AVS_LIST(void) *) &(*res_instance_ptr)->attrs,
                            ssid_list);

                    remove_resource_instance_if_empty(res_instance_ptr);
                }
#    endif // ANJAY_WITH_LWM2M11
                remove_resource_if_empty(res_ptr);
            }
            remove_instance_if_empty(instance_ptr);
        }
        remove_object_if_empty(object_ptr);
    }
}

int _anjay_attr_storage_remove_absent_instances_clb(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *def_ptr,
        anjay_iid_t iid,
        void *instance_ptr_ptr_) {
    (void) def_ptr;
    AVS_LIST(as_instance_entry_t) **instance_ptr_ptr =
            (AVS_LIST(as_instance_entry_t) **) instance_ptr_ptr_;
    if (**instance_ptr_ptr && (**instance_ptr_ptr)->iid < iid) {
        while (**instance_ptr_ptr && (**instance_ptr_ptr)->iid < iid) {
            remove_instance_entry(&anjay->attr_storage, *instance_ptr_ptr);
        }
    }
    if (**instance_ptr_ptr && (**instance_ptr_ptr)->iid == iid) {
        AVS_LIST_ADVANCE_PTR(instance_ptr_ptr);
    }
    return 0;
}

static int
remove_absent_resources_clb(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t *def_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_dm_resource_kind_t kind,
                            anjay_dm_resource_presence_t presence,
                            void *resource_ptr_ptr_) {
    (void) anjay;
    (void) def_ptr;
    (void) iid;
    (void) kind;
    AVS_LIST(as_resource_entry_t) **resource_ptr_ptr =
            (AVS_LIST(as_resource_entry_t) **) resource_ptr_ptr_;
    while (**resource_ptr_ptr && (**resource_ptr_ptr)->rid < rid) {
        remove_resource_entry(&anjay->attr_storage, *resource_ptr_ptr);
    }
    if (**resource_ptr_ptr && (**resource_ptr_ptr)->rid == rid) {
        if (presence == ANJAY_DM_RES_ABSENT) {
            remove_resource_entry(&anjay->attr_storage, *resource_ptr_ptr);
        } else {
            AVS_LIST_ADVANCE_PTR(resource_ptr_ptr);
        }
    }
    return 0;
}

int _anjay_attr_storage_remove_absent_resources(
        anjay_unlocked_t *anjay,
        AVS_LIST(as_instance_entry_t) *instance_ptr,
        const anjay_dm_installed_object_t *def_ptr) {
    AVS_LIST(as_resource_entry_t) *resource_ptr = &(*instance_ptr)->resources;
    int result = 0;
    if (def_ptr) {
        result =
                _anjay_dm_foreach_resource(anjay, def_ptr, (*instance_ptr)->iid,
                                           remove_absent_resources_clb,
                                           &resource_ptr);
    }
    if (!result) {
        while (*resource_ptr) {
            remove_resource_entry(&anjay->attr_storage, resource_ptr);
        }
    }
    remove_instance_if_empty(instance_ptr);
    return result;
}

#    ifdef ANJAY_WITH_LWM2M11
static int
remove_absent_resource_instances_clb(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t *def_ptr,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     anjay_riid_t riid,
                                     void *resource_instance_ptr_ptr_) {
    (void) anjay;
    (void) def_ptr;
    (void) iid;
    (void) rid;
    AVS_LIST(as_resource_instance_entry_t) **resource_instance_ptr_ptr =
            (AVS_LIST(as_resource_instance_entry_t) **)
                    resource_instance_ptr_ptr_;
    while (**resource_instance_ptr_ptr
           && (**resource_instance_ptr_ptr)->riid < riid) {
        remove_resource_instance_entry(&anjay->attr_storage,
                                       *resource_instance_ptr_ptr);
    }
    if (**resource_instance_ptr_ptr
            && (**resource_instance_ptr_ptr)->riid == riid) {
        AVS_LIST_ADVANCE_PTR(resource_instance_ptr_ptr);
    }
    return 0;
}

int _anjay_attr_storage_remove_absent_resource_instances(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *def_ptr,
        anjay_iid_t iid,
        AVS_LIST(as_resource_entry_t) *resource_ptr) {
    AVS_LIST(as_resource_instance_entry_t) *resource_instance_ptr =
            &(*resource_ptr)->resource_instances;
    int result = 0;
    if (def_ptr) {
        anjay_dm_resource_kind_t resource_kind;
        (void) ((result = _anjay_dm_resource_kind_and_presence(
                         anjay, def_ptr, iid, (*resource_ptr)->rid,
                         &resource_kind, NULL))
                || !_anjay_dm_res_kind_multiple(resource_kind)
                || (result = _anjay_dm_foreach_resource_instance(
                            anjay, def_ptr, iid, (*resource_ptr)->rid,
                            remove_absent_resource_instances_clb,
                            &resource_instance_ptr)));
    }
    if (!result) {
        while (*resource_instance_ptr) {
            remove_resource_instance_entry(&anjay->attr_storage,
                                           resource_instance_ptr);
        }
    }
    remove_resource_if_empty(resource_ptr);
    return result;
}
#    endif // ANJAY_WITH_LWM2M11

static void read_default_attrs(AVS_LIST(as_default_attrs_t) attrs,
                               anjay_ssid_t ssid,
                               anjay_dm_oi_attributes_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_OI_ATTRIBUTES_EMPTY;
}

static void read_resource_attrs(AVS_LIST(as_resource_attrs_t) attrs,
                                anjay_ssid_t ssid,
                                anjay_dm_r_attributes_t *out) {
    AVS_LIST_ITERATE(attrs) {
        if (attrs->ssid == ssid) {
            *out = attrs->attrs;
            return;
        } else if (attrs->ssid > ssid) {
            break;
        }
    }
    *out = ANJAY_DM_R_ATTRIBUTES_EMPTY;
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

static int write_object_attrs(anjay_unlocked_t *anjay,
                              anjay_ssid_t ssid,
                              const anjay_dm_installed_object_t *obj_ptr,
                              const anjay_dm_oi_attributes_t *attrs) {
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_or_create_object(&anjay->attr_storage,
                                  _anjay_dm_installed_object_oid(obj_ptr));
    if (!object_ptr) {
        return -1;
    }
    int result =
            WRITE_ATTRS(&anjay->attr_storage, &(*object_ptr)->default_attrs,
                        default_attrs_empty, ssid, attrs);
    remove_object_if_empty(object_ptr);
    return result;
}

static int write_instance_attrs(anjay_unlocked_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_dm_installed_object_t *obj_ptr,
                                anjay_iid_t iid,
                                const anjay_dm_oi_attributes_t *attrs) {
    assert(iid != ANJAY_ID_INVALID);
    int result = -1;
    AVS_LIST(as_object_entry_t) *object_ptr = NULL;
    AVS_LIST(as_instance_entry_t) *instance_ptr = NULL;
    if ((object_ptr = find_or_create_object(
                 &anjay->attr_storage, _anjay_dm_installed_object_oid(obj_ptr)))
            && (instance_ptr = find_or_create_instance(*object_ptr, iid))) {
        result = WRITE_ATTRS(&anjay->attr_storage,
                             &(*instance_ptr)->default_attrs,
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

static int write_resource_attrs(anjay_unlocked_t *anjay,
                                anjay_ssid_t ssid,
                                const anjay_dm_installed_object_t *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                const anjay_dm_r_attributes_t *attrs) {
    assert(iid != ANJAY_ID_INVALID && rid != ANJAY_ID_INVALID);
    int result = -1;
    AVS_LIST(as_object_entry_t) *object_ptr = NULL;
    AVS_LIST(as_instance_entry_t) *instance_ptr = NULL;
    AVS_LIST(as_resource_entry_t) *resource_ptr = NULL;
    if ((object_ptr = find_or_create_object(
                 &anjay->attr_storage, _anjay_dm_installed_object_oid(obj_ptr)))
            && (instance_ptr = find_or_create_instance(*object_ptr, iid))
            && (resource_ptr = find_or_create_resource(*instance_ptr, rid))) {
        result = WRITE_ATTRS(&anjay->attr_storage, &(*resource_ptr)->attrs,
                             resource_attrs_empty, ssid, attrs);
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

#    ifdef ANJAY_WITH_LWM2M11
static int
write_resource_instance_attrs(anjay_unlocked_t *anjay,
                              anjay_ssid_t ssid,
                              const anjay_dm_installed_object_t *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              const anjay_dm_r_attributes_t *attrs) {
    assert(iid != ANJAY_ID_INVALID && rid != ANJAY_ID_INVALID
           && riid != ANJAY_ID_INVALID);
    int result = -1;
    AVS_LIST(as_object_entry_t) *object_ptr = NULL;
    AVS_LIST(as_instance_entry_t) *instance_ptr = NULL;
    AVS_LIST(as_resource_entry_t) *resource_ptr = NULL;
    AVS_LIST(as_resource_instance_entry_t) *resource_instance_ptr = NULL;
    if ((object_ptr = find_or_create_object(
                 &anjay->attr_storage, _anjay_dm_installed_object_oid(obj_ptr)))
            && (instance_ptr = find_or_create_instance(*object_ptr, iid))
            && (resource_ptr = find_or_create_resource(*instance_ptr, rid))
            && (resource_instance_ptr = find_or_create_resource_instance(
                        *resource_ptr, riid))) {
        result = WRITE_ATTRS(&anjay->attr_storage,
                             &(*resource_instance_ptr)->attrs,
                             resource_attrs_empty, ssid, attrs);
    }

    if (resource_instance_ptr) {
        remove_resource_instance_if_empty(resource_instance_ptr);
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
#    endif // ANJAY_WITH_LWM2M11

//// NOTIFICATION HANDLING /////////////////////////////////////////////////////

typedef struct {
    AVS_LIST(as_instance_entry_t) *instance_ptr;
    AVS_LIST(anjay_ssid_t) *ssid_ptr;
} remove_absent_instances_and_enumerate_ssids_args_t;

static int remove_absent_instances_and_enumerate_ssids_clb(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *def_ptr,
        anjay_iid_t iid,
        void *args_) {
    remove_absent_instances_and_enumerate_ssids_args_t *args =
            (remove_absent_instances_and_enumerate_ssids_args_t *) args_;
    int result = 0;
    if (args->instance_ptr) {
        result = _anjay_attr_storage_remove_absent_instances_clb(
                anjay, def_ptr, iid, &args->instance_ptr);
        if (result) {
            return result;
        }
    }
    anjay_ssid_t ssid =
            query_ssid(anjay, _anjay_dm_installed_object_oid(def_ptr), iid);
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
    return 0;
}

static int remove_absent_instances_and_enumerate_ssids(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *def_ptr,
        AVS_LIST(as_object_entry_t) *object_ptr,
        AVS_LIST(anjay_ssid_t) *out_ssids) {
    assert(out_ssids && !*out_ssids);
    remove_absent_instances_and_enumerate_ssids_args_t args = {
        .instance_ptr = object_ptr ? &(*object_ptr)->instances : NULL,
        .ssid_ptr = out_ssids
    };
    int result = _anjay_dm_foreach_instance(
            anjay, def_ptr, remove_absent_instances_and_enumerate_ssids_clb,
            &args);
    if (result) {
        return result;
    }
    while (args.instance_ptr && *args.instance_ptr) {
        remove_instance_entry(&anjay->attr_storage, args.instance_ptr);
    }
    return 0;
}

static int compare_u16ids(const void *a, const void *b, size_t element_size) {
    assert(element_size == sizeof(uint16_t));
    (void) element_size;
    return *(const uint16_t *) a - *(const uint16_t *) b;
}

static int remove_absent_resources(anjay_unlocked_t *anjay,
                                   AVS_LIST(as_object_entry_t) *as_object_ptr,
                                   const anjay_dm_installed_object_t *obj_ptr,
                                   anjay_iid_t iid) {
    assert(as_object_ptr);
    AVS_LIST(as_instance_entry_t) *instance_ptr =
            find_instance(*as_object_ptr, iid);
    if (!instance_ptr) {
        return 0;
    }
    int result = 0;
    if (obj_ptr) {
        result =
                _anjay_attr_storage_remove_absent_resources(anjay, instance_ptr,
                                                            obj_ptr);
    }
    return result;
}

static int remove_absent_resources_in_all_instances(
        anjay_unlocked_t *anjay,
        const anjay_dm_installed_object_t *def_ptr,
        AVS_LIST(anjay_notify_queue_resource_entry_t) resources_changed) {
    int result = 0;
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(&anjay->attr_storage,
                        _anjay_dm_installed_object_oid(def_ptr));
    if (object_ptr) {
        anjay_iid_t last_iid = ANJAY_ID_INVALID;
        AVS_LIST(anjay_notify_queue_resource_entry_t) resource_entry;
        AVS_LIST_FOREACH(resource_entry, resources_changed) {
            if (resource_entry->iid != last_iid) {
                // note that remove_absent_resources() does NOT call
                // remove_object_if_empty().
                _anjay_update_ret(&result, remove_absent_resources(
                                                   anjay, object_ptr, def_ptr,
                                                   resource_entry->iid));
            }
            last_iid = resource_entry->iid;
        }
        remove_object_if_empty(object_ptr);
    }
    return result;
}

int _anjay_attr_storage_notify(anjay_unlocked_t *anjay,
                               anjay_notify_queue_t queue) {
    int result = 0;
    AVS_LIST(anjay_notify_queue_object_entry_t) object_entry;
    AVS_LIST_FOREACH(object_entry, queue) {
        AVS_LIST(as_object_entry_t) *object_ptr =
                find_object(&anjay->attr_storage, object_entry->oid);
        assert(!object_ptr || *object_ptr);
        if (!object_ptr && !is_ssid_reference_object(object_entry->oid)) {
            continue;
        }
        const anjay_dm_installed_object_t *def_ptr =
                _anjay_dm_find_object_by_oid(anjay, object_entry->oid);
        if (!def_ptr && object_ptr) {
            remove_object_entry(&anjay->attr_storage, object_ptr);
            continue;
        }
        AVS_LIST(anjay_ssid_t) ssids = NULL;
        int partial_result =
                remove_absent_instances_and_enumerate_ssids(anjay, def_ptr,
                                                            object_ptr, &ssids);
        if (object_ptr) {
            remove_object_if_empty(object_ptr);
        }
        if (!partial_result && is_ssid_reference_object(object_entry->oid)) {
            AVS_LIST_SORT(&ssids, compare_u16ids);
            remove_servers_not_on_ssid_list(&anjay->attr_storage, ssids);
        }
        AVS_LIST_CLEAR(&ssids);
        if (!partial_result) {
            // NOTE: This looks up object_ptr the second time, which is
            // necessary because the above code might have removed
            // as_object_entry_t entries, thus potentially invalidating
            // object_ptr
            assert(def_ptr);
            partial_result = remove_absent_resources_in_all_instances(
                    anjay, def_ptr, object_entry->resources_changed);
        }
        _anjay_update_ret(&result, partial_result);
    }
    return result;
}

//// ATTRIBUTE HANDLERS ////////////////////////////////////////////////////////

static int object_read_default_attrs(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t obj_ptr,
                                     anjay_ssid_t ssid,
                                     anjay_dm_oi_attributes_t *out) {
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(&anjay->attr_storage,
                        _anjay_dm_installed_object_oid(&obj_ptr));
    read_default_attrs(object_ptr ? (*object_ptr)->default_attrs : NULL, ssid,
                       out);
    return 0;
}

static int object_write_default_attrs(anjay_unlocked_t *anjay,
                                      const anjay_dm_installed_object_t obj_ptr,
                                      anjay_ssid_t ssid,
                                      const anjay_dm_oi_attributes_t *attrs) {
    return write_object_attrs(anjay, ssid, &obj_ptr, attrs) ? ANJAY_ERR_INTERNAL
                                                            : 0;
}

static int
instance_read_default_attrs(anjay_unlocked_t *anjay,
                            const anjay_dm_installed_object_t obj_ptr,
                            anjay_iid_t iid,
                            anjay_ssid_t ssid,
                            anjay_dm_oi_attributes_t *out) {
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(&anjay->attr_storage,
                        _anjay_dm_installed_object_oid(&obj_ptr));
    AVS_LIST(as_instance_entry_t) *instance_ptr =
            object_ptr ? find_instance(*object_ptr, iid) : NULL;
    read_default_attrs(instance_ptr ? (*instance_ptr)->default_attrs : NULL,
                       ssid, out);
    return 0;
}

static int
instance_write_default_attrs(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_ptr,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             const anjay_dm_oi_attributes_t *attrs) {
    return write_instance_attrs(anjay, ssid, &obj_ptr, iid, attrs)
                   ? ANJAY_ERR_INTERNAL
                   : 0;
}

static int resource_read_attrs(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_ssid_t ssid,
                               anjay_dm_r_attributes_t *out) {
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(&anjay->attr_storage,
                        _anjay_dm_installed_object_oid(&obj_ptr));
    AVS_LIST(as_instance_entry_t) *instance_ptr =
            object_ptr ? find_instance(*object_ptr, iid) : NULL;
    AVS_LIST(as_resource_entry_t) *res_ptr =
            instance_ptr ? find_resource(*instance_ptr, rid) : NULL;
    read_resource_attrs(res_ptr ? (*res_ptr)->attrs : NULL, ssid, out);
    return 0;
}

static int resource_write_attrs(anjay_unlocked_t *anjay,
                                const anjay_dm_installed_object_t obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                const anjay_dm_r_attributes_t *attrs) {
    return write_resource_attrs(anjay, ssid, &obj_ptr, iid, rid, attrs)
                   ? ANJAY_ERR_INTERNAL
                   : 0;
}

#    ifdef ANJAY_WITH_LWM2M11
static int
resource_instance_read_attrs(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             anjay_ssid_t ssid,
                             anjay_dm_r_attributes_t *out) {
    AVS_LIST(as_object_entry_t) *object_ptr =
            find_object(&anjay->attr_storage,
                        _anjay_dm_installed_object_oid(&obj_ptr));
    AVS_LIST(as_instance_entry_t) *instance_ptr =
            object_ptr ? find_instance(*object_ptr, iid) : NULL;
    AVS_LIST(as_resource_entry_t) *res_ptr =
            instance_ptr ? find_resource(*instance_ptr, rid) : NULL;
    AVS_LIST(as_resource_instance_entry_t) *res_instance_ptr =
            res_ptr ? find_resource_instance(*res_ptr, riid) : NULL;
    read_resource_attrs(res_instance_ptr ? (*res_instance_ptr)->attrs : NULL,
                        ssid, out);
    return 0;
}

static int
resource_instance_write_attrs(anjay_unlocked_t *anjay,
                              const anjay_dm_installed_object_t obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_ssid_t ssid,
                              const anjay_dm_r_attributes_t *attrs) {
    return write_resource_instance_attrs(anjay, ssid, &obj_ptr, iid, rid, riid,
                                         attrs)
                   ? ANJAY_ERR_INTERNAL
                   : 0;
}
#    endif // ANJAY_WITH_LWM2M11

const anjay_unlocked_dm_handlers_t _ANJAY_ATTR_STORAGE_HANDLERS = {
    .object_read_default_attrs = object_read_default_attrs,
    .object_write_default_attrs = object_write_default_attrs,
    .instance_read_default_attrs = instance_read_default_attrs,
    .instance_write_default_attrs = instance_write_default_attrs,
    .resource_read_attrs = resource_read_attrs,
    .resource_write_attrs = resource_write_attrs,
#    ifdef ANJAY_WITH_LWM2M11
    .resource_instance_read_attrs = resource_instance_read_attrs,
    .resource_instance_write_attrs = resource_instance_write_attrs,
#    endif // ANJAY_WITH_LWM2M11
};

//// ACTIVE PROXY HANDLERS /////////////////////////////////////////////////////

static void saved_state_reset(anjay_attr_storage_t *as) {
    avs_stream_reset(as->saved_state.persist_data);
    avs_stream_membuf_fit(as->saved_state.persist_data);
}

avs_error_t _anjay_attr_storage_transaction_begin(anjay_unlocked_t *anjay) {
    anjay->attr_storage.saved_state.modified_since_persist =
            anjay->attr_storage.modified_since_persist;
    return _anjay_attr_storage_persist_inner(
            &anjay->attr_storage, anjay->attr_storage.saved_state.persist_data);
}

void _anjay_attr_storage_transaction_commit(anjay_unlocked_t *anjay) {
    saved_state_reset(&anjay->attr_storage);
}

avs_error_t _anjay_attr_storage_transaction_rollback(anjay_unlocked_t *anjay) {
    avs_error_t err;
    if (avs_is_err((err = _anjay_attr_storage_restore_inner(
                            anjay,
                            anjay->attr_storage.saved_state.persist_data)))) {
        anjay->attr_storage.modified_since_persist = true;
    } else {
        anjay->attr_storage.modified_since_persist =
                anjay->attr_storage.saved_state.modified_since_persist;
    }
    saved_state_reset(&anjay->attr_storage);
    return err;
}

static const anjay_dm_installed_object_t *
maybe_get_object_before_setting_attrs(anjay_unlocked_t *anjay,
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
    const anjay_dm_installed_object_t *obj =
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

int anjay_attr_storage_set_object_attrs(anjay_t *anjay_locked,
                                        anjay_ssid_t ssid,
                                        anjay_oid_t oid,
                                        const anjay_dm_oi_attributes_t *attrs) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (obj) {
        if (_anjay_dm_implements_any_object_default_attrs_handlers(obj)) {
            as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "object",
                   "object_read_default_attrs", "object_write_default_attrs");
        } else if (!(result = write_object_attrs(anjay, ssid, obj, attrs))) {
            (void) _anjay_notify_instances_changed_unlocked(anjay, oid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_attr_storage_set_instance_attrs(
        anjay_t *anjay_locked,
        anjay_ssid_t ssid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_dm_oi_attributes_t *attrs) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (obj) {
        if (_anjay_dm_implements_any_instance_default_attrs_handlers(obj)) {
            as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "instance",
                   "instance_read_default_attrs",
                   "instance_write_default_attrs");
        } else if (_anjay_dm_verify_instance_present(anjay, obj, iid)) {
            as_log(DEBUG, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        } else if (!(result = write_instance_attrs(anjay, ssid, obj, iid,
                                                   attrs))) {
            (void) _anjay_notify_instances_changed_unlocked(anjay, oid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

int anjay_attr_storage_set_resource_attrs(
        anjay_t *anjay_locked,
        anjay_ssid_t ssid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        const anjay_dm_r_attributes_t *attrs) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    const anjay_dm_installed_object_t *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (obj) {
        if (_anjay_dm_implements_any_resource_attrs_handlers(obj)) {
            as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND, "resource",
                   "resource_read_attrs", "resource_write_attrs");
        } else if (_anjay_dm_verify_instance_present(anjay, obj, iid)) {
            as_log(DEBUG, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        } else if (_anjay_dm_verify_resource_present(anjay, obj, iid, rid,
                                                     NULL)) {
            as_log(DEBUG, ERR_RESOURCE_PRESENCE_CHECK, oid, iid, rid);
        } else if (!(result = write_resource_attrs(anjay, ssid, obj, iid, rid,
                                                   attrs))) {
            (void) _anjay_notify_instances_changed_unlocked(anjay, oid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}

#    ifdef ANJAY_WITH_LWM2M11
int anjay_attr_storage_set_resource_instance_attrs(
        anjay_t *anjay_locked,
        anjay_ssid_t ssid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        const anjay_dm_r_attributes_t *attrs) {
    int result = -1;
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    anjay_dm_resource_kind_t kind;
    const anjay_dm_installed_object_t *obj =
            maybe_get_object_before_setting_attrs(anjay, ssid, oid, attrs);
    if (obj) {
        if (_anjay_dm_implements_any_resource_instance_attrs_handlers(obj)) {
            as_log(DEBUG, ERR_HANDLERS_IMPLEMENTED_BY_BACKEND,
                   "resource instance", "resource_instance_read_attrs",
                   "resource_instance_write_attrs");
        } else if (_anjay_dm_verify_instance_present(anjay, obj, iid)) {
            as_log(DEBUG, ERR_INSTANCE_PRESENCE_CHECK, oid, iid);
        } else if (_anjay_dm_verify_resource_present(anjay, obj, iid, rid,
                                                     &kind)
                   || !_anjay_dm_res_kind_multiple(kind)) {
            as_log(DEBUG, ERR_RESOURCE_PRESENCE_CHECK, oid, iid, rid);
        } else if (_anjay_dm_verify_resource_instance_present(anjay, obj, iid,
                                                              rid, riid)) {
            as_log(DEBUG, ERR_RESOURCE_INSTANCE_PRESENCE_CHECK, oid, iid, rid,
                   riid);
        } else if (!(result = write_resource_instance_attrs(
                             anjay, ssid, obj, iid, rid, riid, attrs))) {
            (void) _anjay_notify_instances_changed_unlocked(anjay, oid);
        }
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return result;
}
#    endif // ANJAY_WITH_LWM2M11

#    ifdef ANJAY_TEST
#        include "tests/core/attr_storage/attr_storage.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_ATTR_STORAGE
