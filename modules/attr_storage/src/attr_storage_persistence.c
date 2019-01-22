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

#include <stdio.h>
#include <string.h>

#include <avsystem/commons/persistence.h>

#include <anjay_modules/dm_utils.h>
#include <anjay_modules/io_utils.h>
#include <anjay_modules/raw_buffer.h>

#include "mod_attr_storage.h"

VISIBILITY_SOURCE_BEGIN

#define HANDLE_LIST(Type, Ctx, ListPtr, UserPtr)              \
    avs_persistence_list((Ctx), (AVS_LIST(void) *) (ListPtr), \
                         sizeof(**(ListPtr)), handle_##Type, UserPtr, NULL)

//// DATA STRUCTURE HANDLERS ///////////////////////////////////////////////////

static int handle_dm_attributes(avs_persistence_context_t *ctx,
                                anjay_dm_attributes_t *attrs) {
    int retval;
    (void) ((retval = avs_persistence_u32(ctx, (uint32_t *) &attrs->min_period))
            || (retval = avs_persistence_u32(ctx,
                                             (uint32_t *) &attrs->max_period)));
    return retval;
}

static int handle_resource_attributes(avs_persistence_context_t *ctx,
                                      anjay_dm_resource_attributes_t *attrs) {
    int retval;
    (void) ((retval = handle_dm_attributes(ctx, &attrs->common))
            || (retval = avs_persistence_double(ctx, &attrs->greater_than))
            || (retval = avs_persistence_double(ctx, &attrs->less_than))
            || (retval = avs_persistence_double(ctx, &attrs->step)));
    return retval;
}

static int handle_custom_attributes(avs_persistence_context_t *ctx,
                                    anjay_dm_internal_attrs_t *attrs,
                                    int version) {
    int retval = 0;
    int8_t con = ANJAY_DM_CON_ATTR_DEFAULT;
    if (version >= 2) {
        (void) attrs;
#ifdef WITH_CON_ATTR
        con = (int8_t) attrs->custom.data.con;
#endif // WITH_CON_ATTR
        retval = avs_persistence_bytes(ctx, (uint8_t *) &con, 1);
    }
#ifdef WITH_CON_ATTR
    if (!retval) {
        switch (con) {
        case ANJAY_DM_CON_ATTR_DEFAULT:
        case ANJAY_DM_CON_ATTR_NON:
        case ANJAY_DM_CON_ATTR_CON:
            attrs->custom.data.con = (anjay_dm_con_attr_t) con;
            break;
        default:
            retval = -1;
        }
    }
#endif // WITH_CON_ATTR
    return retval;
}

static int handle_internal_attrs(avs_persistence_context_t *ctx,
                                 anjay_dm_internal_attrs_t *attrs,
                                 int version) {
    int retval;
    (void) ((retval = handle_dm_attributes(ctx, &attrs->standard))
            || (retval = handle_custom_attributes(ctx, attrs, version)));
    return retval;
}

static int handle_internal_res_attrs(avs_persistence_context_t *ctx,
                                     anjay_dm_internal_res_attrs_t *attrs,
                                     int version) {
    int retval;
    (void) ((retval = handle_resource_attributes(ctx, &attrs->standard))
            || (retval = handle_custom_attributes(
                        ctx,
                        _anjay_dm_get_internal_attrs(&attrs->standard.common),
                        version)));
    return retval;
}

static int handle_default_attrs(avs_persistence_context_t *ctx,
                                void *attrs_,
                                void *version_as_ptr) {
    fas_default_attrs_t *attrs = (fas_default_attrs_t *) attrs_;
    int retval;
    (void) ((retval = avs_persistence_u16(ctx, &attrs->ssid))
            || (retval = handle_internal_attrs(
                        ctx, &attrs->attrs, (int) (intptr_t) version_as_ptr)));
    return retval;
}

static int handle_resource_attrs(avs_persistence_context_t *ctx,
                                 void *attrs_,
                                 void *version_as_ptr) {
    fas_resource_attrs_t *attrs = (fas_resource_attrs_t *) attrs_;
    int retval;
    (void) ((retval = avs_persistence_u16(ctx, &attrs->ssid))
            || (retval = handle_internal_res_attrs(
                        ctx, &attrs->attrs, (int) (intptr_t) version_as_ptr)));
    return retval;
}

static int handle_resource_entry(avs_persistence_context_t *ctx,
                                 void *resource_,
                                 void *version_as_ptr) {
    fas_resource_entry_t *resource = (fas_resource_entry_t *) resource_;
    int retval;
    (void) ((retval = avs_persistence_u16(ctx, &resource->rid))
            || (retval = HANDLE_LIST(resource_attrs, ctx, &resource->attrs,
                                     version_as_ptr)));
    return retval;
}

static int handle_instance_entry(avs_persistence_context_t *ctx,
                                 void *instance_,
                                 void *version_as_ptr) {
    fas_instance_entry_t *instance = (fas_instance_entry_t *) instance_;
    int retval;
    (void) ((retval = avs_persistence_u16(ctx, &instance->iid))
            || (retval = HANDLE_LIST(default_attrs, ctx,
                                     &instance->default_attrs, version_as_ptr))
            || (retval = HANDLE_LIST(resource_entry, ctx, &instance->resources,
                                     version_as_ptr)));
    return retval;
}

static int handle_object(avs_persistence_context_t *ctx,
                         void *object_,
                         void *version_as_ptr) {
    fas_object_entry_t *object = (fas_object_entry_t *) object_;
    int retval;
    (void) ((retval = avs_persistence_u16(ctx, &object->oid))
            || (retval = HANDLE_LIST(default_attrs, ctx, &object->default_attrs,
                                     version_as_ptr))
            || (retval = HANDLE_LIST(instance_entry, ctx, &object->instances,
                                     version_as_ptr)));
    return retval;
}

// HELPERS /////////////////////////////////////////////////////////////////////

/**
 * NOTE: The last byte is supposed to be a version number.
 *
 * Known versions are:
 * - 0: used in development versions and up to Anjay 1.3.1
 * - 1: briefly used and released as part of Anjay 1.0.0, when the attributes
 *   were temporarily unified (i.e., Objects could have lt/gt/st attributes)
 * - 2: current version
 */
typedef char fas_magic_t[4];

static const fas_magic_t MAGIC_V0 = { 'F', 'A', 'S', '\0' };
static const fas_magic_t MAGIC_V2 = { 'F', 'A', 'S', '\2' };

typedef enum { RM_SUCCESS, RM_ERROR, RM_EOF } read_magic_result_t;

static int read_magic_or_eof(avs_stream_abstract_t *stream, fas_magic_t *out) {
    size_t bytes_read = 0;
    char message_finished = 0;
    while (bytes_read < sizeof(*out) && !message_finished) {
        size_t current_read = 0;
        // try to support broken implementations of read()
        // that may not set message_finished to 0 when they should
        message_finished = 0;
        if (avs_stream_read(stream, &current_read, &message_finished,
                            *out + bytes_read, sizeof(*out) - bytes_read)) {
            return RM_ERROR;
        }
        bytes_read += current_read;
    }
    if (bytes_read == sizeof(*out)) {
        return RM_SUCCESS;
    } else if (bytes_read == 0 && message_finished) {
        return RM_EOF;
    } else {
        return RM_ERROR;
    }
}

static bool is_attrs_list_sane(AVS_LIST(void) attrs_list,
                               size_t attrs_field_offset,
                               is_empty_func_t *is_empty_func) {
    int32_t last_ssid = -1;
    AVS_LIST(void) attrs;
    AVS_LIST_FOREACH(attrs, attrs_list) {
        if (*get_ssid_ptr(attrs) <= last_ssid
                || is_empty_func(get_attrs_ptr(attrs, attrs_field_offset))) {
            return false;
        }
        last_ssid = *get_ssid_ptr(attrs);
    }
    return true;
}

static bool is_resources_list_sane(AVS_LIST(fas_resource_entry_t) resources) {
    int32_t last_rid = -1;
    AVS_LIST(fas_resource_entry_t) resource;
    AVS_LIST_FOREACH(resource, resources) {
        if (resource->rid <= last_rid) {
            return false;
        }
        last_rid = resource->rid;
        if (!is_attrs_list_sane(resource->attrs,
                                offsetof(fas_resource_attrs_t, attrs),
                                resource_attrs_empty)) {
            return false;
        }
    }
    return true;
}

static bool is_instances_list_sane(AVS_LIST(fas_instance_entry_t) instances) {
    int32_t last_iid = -1;
    AVS_LIST(fas_instance_entry_t) instance;
    AVS_LIST_FOREACH(instance, instances) {
        if (instance->iid <= last_iid) {
            return false;
        }
        last_iid = instance->iid;
        if (!is_attrs_list_sane(instance->default_attrs,
                                offsetof(fas_default_attrs_t, attrs),
                                default_attrs_empty)
                || !is_resources_list_sane(instance->resources)) {
            return false;
        }
    }
    return true;
}

static bool is_object_sane(fas_object_entry_t *object) {
    return is_attrs_list_sane(object->default_attrs,
                              offsetof(fas_default_attrs_t, attrs),
                              default_attrs_empty)
           && is_instances_list_sane(object->instances);
}

static bool is_attr_storage_sane(anjay_attr_storage_t *fas) {
    int32_t last_oid = -1;
    AVS_LIST(fas_object_entry_t) *object_ptr;
    AVS_LIST(fas_object_entry_t) object_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(object_ptr, object_helper, &fas->objects) {
        if ((*object_ptr)->oid <= last_oid) {
            return false;
        }
        last_oid = (*object_ptr)->oid;
        if (!is_object_sane(*object_ptr)) {
            return false;
        }
    }
    return true;
}

static int collect_existing_iids(anjay_t *anjay,
                                 AVS_LIST(anjay_iid_t) *out,
                                 const anjay_dm_object_def_t *const *def_ptr) {
    assert(!*out);
    int result = 0;
    void *cookie = NULL;
    while (true) {
        anjay_iid_t iid;
        result = _anjay_dm_instance_it(anjay, def_ptr, &iid, &cookie,
                                       &_anjay_attr_storage_MODULE);
        if (result || iid == ANJAY_IID_INVALID) {
            break;
        }
        anjay_iid_t *new_iid = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
        if (!new_iid) {
            fas_log(ERROR, "Out of memory");
            result = ANJAY_ERR_INTERNAL;
            break;
        }
        *new_iid = iid;
        AVS_LIST_INSERT(out, new_iid);
    }
    if (!result) {
        AVS_LIST_SORT(out, _anjay_attr_storage_compare_u16ids);
    }
    return result;
}

static int clear_nonexistent_iids(anjay_t *anjay,
                                  anjay_attr_storage_t *fas,
                                  AVS_LIST(fas_object_entry_t) *object_ptr,
                                  const anjay_dm_object_def_t *const *def_ptr) {
    AVS_LIST(anjay_iid_t) iids = NULL;
    int result = collect_existing_iids(anjay, &iids, def_ptr);
    if (!result) {
        _anjay_attr_storage_remove_instances_not_on_sorted_list(
                fas, *object_ptr, iids);
    }
    AVS_LIST_CLEAR(&iids);
    return result;
}

static int clear_nonexistent_rids(anjay_t *anjay,
                                  anjay_attr_storage_t *fas,
                                  AVS_LIST(fas_object_entry_t) *object_ptr,
                                  const anjay_dm_object_def_t *const *def_ptr) {
    AVS_LIST(fas_instance_entry_t) *instance_ptr;
    AVS_LIST(fas_instance_entry_t) instance_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper,
                                   &(*object_ptr)->instances) {
        AVS_LIST(fas_resource_entry_t) *resource_ptr;
        AVS_LIST(fas_resource_entry_t) resource_helper;
        AVS_LIST_DELETABLE_FOREACH_PTR(resource_ptr, resource_helper,
                                       &(*instance_ptr)->resources) {
            int rid_present = _anjay_dm_resource_supported_and_present(
                    anjay, def_ptr, (*instance_ptr)->iid, (*resource_ptr)->rid,
                    &_anjay_attr_storage_MODULE);
            if (rid_present < 0) {
                return -1;
            } else if (!rid_present) {
                remove_resource_entry(fas, resource_ptr);
            }
        }
        remove_instance_if_empty(instance_ptr);
    }
    return 0;
}

static int clear_nonexistent_entries(anjay_t *anjay,
                                     anjay_attr_storage_t *fas) {
    AVS_LIST(fas_object_entry_t) *object_ptr;
    AVS_LIST(fas_object_entry_t) object_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(object_ptr, object_helper, &fas->objects) {
        const anjay_dm_object_def_t *const *def_ptr =
                _anjay_dm_find_object_by_oid(anjay, (*object_ptr)->oid);
        if (!def_ptr) {
            remove_object_entry(fas, object_ptr);
        } else {
            int retval;
            if ((retval = clear_nonexistent_iids(anjay, fas, object_ptr,
                                                 def_ptr))
                    || (retval = clear_nonexistent_rids(anjay, fas, object_ptr,
                                                        def_ptr))) {
                return retval;
            }
            remove_object_if_empty(object_ptr);
        }
    }
    return 0;
}

//// PUBLIC FUNCTIONS //////////////////////////////////////////////////////////

int _anjay_attr_storage_persist_inner(anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *out) {
    int retval = avs_stream_write(out, MAGIC_V2, sizeof(MAGIC_V2));
    if (retval) {
        return retval;
    }
    avs_persistence_context_t *ctx = avs_persistence_store_context_new(out);
    if (!ctx) {
        fas_log(ERROR, "Out of memory");
        return -1;
    }
    retval = HANDLE_LIST(object, ctx, &attr_storage->objects, (void *) 2);
    avs_persistence_context_delete(ctx);
    return retval;
}

int _anjay_attr_storage_restore_inner(anjay_t *anjay,
                                      anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *in) {
    _anjay_attr_storage_clear(attr_storage);

    fas_magic_t magic_buffer;
    AVS_STATIC_ASSERT(sizeof(MAGIC_V0) == sizeof(magic_buffer), magic_v0_size);
    AVS_STATIC_ASSERT(sizeof(MAGIC_V2) == sizeof(magic_buffer), magic_v2_size);
    switch (read_magic_or_eof(in, &magic_buffer)) {
    case RM_SUCCESS:
        break;
    case RM_ERROR:
        return -1;
    case RM_EOF:
        // empty stream, treat as success
        return 0;
    }

    intptr_t version;
    if (!memcmp(magic_buffer, MAGIC_V0, sizeof(MAGIC_V0))) {
        version = 0;
    } else if (!memcmp(magic_buffer, MAGIC_V2, sizeof(MAGIC_V2))) {
        version = 2;
    } else {
        fas_log(ERROR, "Magic value mismatch");
        return -1;
    }

    int retval = -1;
    avs_persistence_context_t *ctx = avs_persistence_restore_context_new(in);
    if (!ctx) {
        fas_log(ERROR, "Out of memory");
    } else {
        (void) ((retval = HANDLE_LIST(object, ctx, &attr_storage->objects,
                                      (void *) version))
                || (retval = (is_attr_storage_sane(attr_storage) ? 0 : -1))
                || (retval = clear_nonexistent_entries(anjay, attr_storage)));
        avs_persistence_context_delete(ctx);
    }
    if (retval) {
        _anjay_attr_storage_clear(attr_storage);
    }
    return retval;
}

int anjay_attr_storage_persist(anjay_t *anjay, avs_stream_abstract_t *out) {
    anjay_attr_storage_t *fas = _anjay_attr_storage_get(anjay);
    if (!fas) {
        fas_log(ERROR,
                "Attribute Storage is not installed on this Anjay object");
        return -1;
    }
    int retval = _anjay_attr_storage_persist_inner(fas, out);
    if (!retval) {
        fas->modified_since_persist = false;
        fas_log(INFO, "Attribute Storage state persisted");
    }
    return retval;
}

int anjay_attr_storage_restore(anjay_t *anjay, avs_stream_abstract_t *in) {
    anjay_attr_storage_t *fas = _anjay_attr_storage_get(anjay);
    if (!fas) {
        fas_log(ERROR,
                "Attribute Storage is not installed on this Anjay object");
        return -1;
    }
    int retval = _anjay_attr_storage_restore_inner(anjay, fas, in);
    if (!retval) {
        fas_log(INFO, "Attribute Storage state restored");
    }
    fas->modified_since_persist = (retval != 0);
    return retval;
}

#ifdef ANJAY_TEST
#    include "test/persistence.c"
#endif // ANJAY_TEST
