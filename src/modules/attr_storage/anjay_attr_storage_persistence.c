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

#    include <stdio.h>
#    include <string.h>

#    include <avsystem/commons/avs_persistence.h>

#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_io_utils.h>
#    include <anjay_modules/anjay_raw_buffer.h>

#    include "anjay_mod_attr_storage.h"

VISIBILITY_SOURCE_BEGIN

//// VERSIONS //////////////////////////////////////////////////////////////////

/**
 * NOTE: Anjay Attr Storage is called FAS in the magic header for historical
 * reasons stemming from Anjay's initial codename which started with an F.
 *
 * NOTE: Magic header is followed by one byte which is supposed to be a version
 * number.
 *
 * Known versions are:
 * - 0: used in development versions and up to Anjay 1.3.1
 * - 1: briefly used and released as part of Anjay 1.0.0, when the attributes
 *   were temporarily unified (i.e., Objects could have lt/gt/st attributes)
 * - 2: Anjay 2.0.5, doesn't support Resource Instance attributes
 * - 3: Anjay 2.1.0, supports Resource Instance attributes
 */
static const char *MAGIC = "FAS";

// clang-format off
#define SUPPORTED_VERSIONS              \
    AS_PERSISTENCE_VERSION_ANJAY_1_3_1, \
    AS_PERSISTENCE_VERSION_ANJAY_1_0_0, \
    AS_PERSISTENCE_VERSION_ANJAY_2_0_5, \
    AS_PERSISTENCE_VERSION_ANJAY_2_1_0, \
    AS_PERSISTENCE_VERSION_ANJAY_2_2_0
// clang-format on

typedef enum {
    SUPPORTED_VERSIONS,
    AS_PERSISTENCE_VERSION_NEXT,
    AS_PERSISTENCE_VERSION_CURRENT = AS_PERSISTENCE_VERSION_NEXT - 1
} as_persistence_version_t;

static const uint8_t SUPPORTED_VERSIONS_ARRAY[] = { SUPPORTED_VERSIONS };

#    undef SUPPORTED_VERSIONS

//// DATA STRUCTURE HANDLERS ///////////////////////////////////////////////////

#    define HANDLE_LIST(Type, Ctx, ListPtr, UserPtr)                      \
        avs_persistence_list((Ctx), (AVS_LIST(void) *) (ListPtr),         \
                             sizeof(**(ListPtr)), handle_##Type, UserPtr, \
                             NULL)

static avs_error_t handle_dm_oi_attributes(avs_persistence_context_t *ctx,
                                           anjay_dm_oi_attributes_t *attrs,
                                           as_persistence_version_t version) {
    avs_error_t err;
    if (avs_is_err((err = avs_persistence_u32(ctx,
                                              (uint32_t *) &attrs->min_period)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx, (uint32_t *) &attrs->max_period)))) {
        return err;
    }
    if (version >= AS_PERSISTENCE_VERSION_ANJAY_2_2_0) {
        (void) (avs_is_err((err = avs_persistence_u32(
                                    ctx, (uint32_t *) &attrs->min_eval_period)))
                || avs_is_err((err = avs_persistence_u32(
                                       ctx,
                                       (uint32_t *) &attrs->max_eval_period))));
    } else if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        attrs->min_eval_period = ANJAY_ATTRIB_PERIOD_NONE;
        attrs->max_eval_period = ANJAY_ATTRIB_PERIOD_NONE;
    }
    return err;
}

static avs_error_t handle_dm_r_attributes(avs_persistence_context_t *ctx,
                                          anjay_dm_r_attributes_t *attrs,
                                          as_persistence_version_t version) {
    avs_error_t err;
    (void) (avs_is_err((err = handle_dm_oi_attributes(ctx, &attrs->common,
                                                      version)))
            || avs_is_err((
                       err = avs_persistence_double(ctx, &attrs->greater_than)))
            || avs_is_err(
                       (err = avs_persistence_double(ctx, &attrs->less_than)))
            || avs_is_err((err = avs_persistence_double(ctx, &attrs->step))));
    return err;
}

static avs_error_t handle_custom_attributes(avs_persistence_context_t *ctx,
                                            anjay_dm_internal_oi_attrs_t *attrs,
                                            as_persistence_version_t version) {
    avs_error_t err = AVS_OK;
    int8_t con = ANJAY_DM_CON_ATTR_DEFAULT;
    if (version >= AS_PERSISTENCE_VERSION_ANJAY_2_0_5) {
        (void) attrs;
#    ifdef ANJAY_WITH_CON_ATTR
        con = (int8_t) attrs->custom.data.con;
#    endif // ANJAY_WITH_CON_ATTR
        err = avs_persistence_bytes(ctx, (uint8_t *) &con, 1);
    }
#    ifdef ANJAY_WITH_CON_ATTR
    if (avs_is_ok(err)) {
        switch (con) {
        case ANJAY_DM_CON_ATTR_DEFAULT:
        case ANJAY_DM_CON_ATTR_NON:
        case ANJAY_DM_CON_ATTR_CON:
            attrs->custom.data.con = (anjay_dm_con_attr_t) con;
            break;
        default:
            err = avs_errno(AVS_EBADMSG);
        }
    }
#    endif // ANJAY_WITH_CON_ATTR
    return err;
}

static avs_error_t
handle_dm_internal_oi_attrs(avs_persistence_context_t *ctx,
                            anjay_dm_internal_oi_attrs_t *attrs,
                            as_persistence_version_t version) {
    avs_error_t err;
    (void) (avs_is_err((err = handle_dm_oi_attributes(ctx, &attrs->standard,
                                                      version)))
            || avs_is_err(
                       (err = handle_custom_attributes(ctx, attrs, version))));
    return err;
}

static avs_error_t
handle_dm_internal_r_attrs(avs_persistence_context_t *ctx,
                           anjay_dm_internal_r_attrs_t *attrs,
                           as_persistence_version_t version) {
    avs_error_t err;
    (void) (avs_is_err((err = handle_dm_r_attributes(ctx, &attrs->standard,
                                                     version)))
            || avs_is_err((err = handle_custom_attributes(
                                   ctx,
                                   _anjay_dm_get_internal_oi_attrs(
                                           &attrs->standard.common),
                                   version))));
    return err;
}

static avs_error_t handle_default_attrs(avs_persistence_context_t *ctx,
                                        void *attrs_,
                                        void *version_as_ptr) {
    as_default_attrs_t *attrs = (as_default_attrs_t *) attrs_;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &attrs->ssid)))
            || avs_is_err((err = handle_dm_internal_oi_attrs(
                                   ctx, &attrs->attrs,
                                   (as_persistence_version_t) (intptr_t)
                                           version_as_ptr))));
    return err;
}

static avs_error_t handle_resource_attrs(avs_persistence_context_t *ctx,
                                         void *attrs_,
                                         void *version_as_ptr) {
    as_resource_attrs_t *attrs = (as_resource_attrs_t *) attrs_;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &attrs->ssid)))
            || avs_is_err((err = handle_dm_internal_r_attrs(
                                   ctx, &attrs->attrs,
                                   (as_persistence_version_t) (intptr_t)
                                           version_as_ptr))));
    return err;
}

static avs_error_t
handle_resource_instance_entry(avs_persistence_context_t *ctx,
                               void *resource_instance_,
                               void *version_as_ptr) {
    as_resource_instance_entry_t *resource_instance =
            (as_resource_instance_entry_t *) resource_instance_;
    avs_error_t err;
    (void) (avs_is_err(
                    (err = avs_persistence_u16(ctx, &resource_instance->riid)))
            || avs_is_err((err = HANDLE_LIST(resource_attrs, ctx,
                                             &resource_instance->attrs,
                                             version_as_ptr))));
    return err;
}

static avs_error_t handle_resource_entry(avs_persistence_context_t *ctx,
                                         void *resource_,
                                         void *version_as_ptr) {
    const as_persistence_version_t version =
            (as_persistence_version_t) (intptr_t) version_as_ptr;
    as_resource_entry_t *resource = (as_resource_entry_t *) resource_;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &resource->rid)))
            || avs_is_err(
                       (err = HANDLE_LIST(resource_attrs, ctx, &resource->attrs,
                                          version_as_ptr))));
    if (avs_is_ok(err) && version >= AS_PERSISTENCE_VERSION_ANJAY_2_1_0) {
        AVS_LIST(as_resource_instance_entry_t) *resource_instances_ptr =
                &(AVS_LIST(as_resource_instance_entry_t)) { NULL };
        err = HANDLE_LIST(resource_instance_entry, ctx, resource_instances_ptr,
                          version_as_ptr);
        AVS_LIST_CLEAR(resource_instances_ptr) {
            AVS_LIST_CLEAR(&(*resource_instances_ptr)->attrs);
        }
    }
    return err;
}

static avs_error_t handle_instance_entry(avs_persistence_context_t *ctx,
                                         void *instance_,
                                         void *version_as_ptr) {
    as_instance_entry_t *instance = (as_instance_entry_t *) instance_;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &instance->iid)))
            || avs_is_err((err = HANDLE_LIST(default_attrs, ctx,
                                             &instance->default_attrs,
                                             version_as_ptr)))
            || avs_is_err((err = HANDLE_LIST(resource_entry, ctx,
                                             &instance->resources,
                                             version_as_ptr))));
    return err;
}

static avs_error_t handle_object(avs_persistence_context_t *ctx,
                                 void *object_,
                                 void *version_as_ptr) {
    as_object_entry_t *object = (as_object_entry_t *) object_;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &object->oid)))
            || avs_is_err((err = HANDLE_LIST(default_attrs, ctx,
                                             &object->default_attrs,
                                             version_as_ptr)))
            || avs_is_err((err = HANDLE_LIST(instance_entry, ctx,
                                             &object->instances,
                                             version_as_ptr))));
    return err;
}

//// HELPERS ///////////////////////////////////////////////////////////////////

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

static bool is_resources_list_sane(AVS_LIST(as_resource_entry_t) resources) {
    int32_t last_rid = -1;
    AVS_LIST(as_resource_entry_t) resource;
    AVS_LIST_FOREACH(resource, resources) {
        if (resource->rid <= last_rid) {
            return false;
        }
        last_rid = resource->rid;
        if (!is_attrs_list_sane(resource->attrs,
                                offsetof(as_resource_attrs_t, attrs),
                                resource_attrs_empty)) {
            return false;
        }
    }
    return true;
}

static bool is_instances_list_sane(AVS_LIST(as_instance_entry_t) instances) {
    int32_t last_iid = -1;
    AVS_LIST(as_instance_entry_t) instance;
    AVS_LIST_FOREACH(instance, instances) {
        if (instance->iid <= last_iid) {
            return false;
        }
        last_iid = instance->iid;
        if (!is_attrs_list_sane(instance->default_attrs,
                                offsetof(as_default_attrs_t, attrs),
                                default_attrs_empty)
                || !is_resources_list_sane(instance->resources)) {
            return false;
        }
    }
    return true;
}

static bool is_object_sane(as_object_entry_t *object) {
    return is_attrs_list_sane(object->default_attrs,
                              offsetof(as_default_attrs_t, attrs),
                              default_attrs_empty)
           && is_instances_list_sane(object->instances);
}

static bool is_attr_storage_sane(anjay_attr_storage_t *as) {
    int32_t last_oid = -1;
    AVS_LIST(as_object_entry_t) *object_ptr;
    AVS_LIST(as_object_entry_t) object_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(object_ptr, object_helper, &as->objects) {
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

static int clear_nonexistent_rids(anjay_t *anjay,
                                  anjay_attr_storage_t *as,
                                  AVS_LIST(as_object_entry_t) *object_ptr,
                                  const anjay_dm_object_def_t *const *def_ptr) {
    AVS_LIST(as_instance_entry_t) *instance_ptr;
    AVS_LIST(as_instance_entry_t) instance_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper,
                                   &(*object_ptr)->instances) {
        if (_anjay_attr_storage_remove_absent_resources(anjay, as, instance_ptr,
                                                        def_ptr)) {
            return -1;
        }
    }
    return 0;
}

static avs_error_t clear_nonexistent_entries(anjay_t *anjay,
                                             anjay_attr_storage_t *as) {
    AVS_LIST(as_object_entry_t) *object_ptr;
    AVS_LIST(as_object_entry_t) object_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(object_ptr, object_helper, &as->objects) {
        const anjay_dm_object_def_t *const *def_ptr =
                _anjay_dm_find_object_by_oid(anjay, (*object_ptr)->oid);
        if (!def_ptr) {
            remove_object_entry(as, object_ptr);
        } else {
            AVS_LIST(as_instance_entry_t) *instance_ptr =
                    &(*object_ptr)->instances;
            int retval = _anjay_dm_foreach_instance(
                    anjay, def_ptr,
                    _anjay_attr_storage_remove_absent_instances_clb,
                    &instance_ptr);
            while (!retval && instance_ptr && *instance_ptr) {
                remove_instance_entry(as, instance_ptr);
            }
            if (retval
                    || clear_nonexistent_rids(anjay, as, object_ptr, def_ptr)) {
                return avs_errno(AVS_EPROTO);
            }
            remove_object_if_empty(object_ptr);
        }
    }
    return AVS_OK;
}

//// PUBLIC FUNCTIONS //////////////////////////////////////////////////////////

avs_error_t
_anjay_attr_storage_persist_inner(anjay_attr_storage_t *attr_storage,
                                  avs_stream_t *out) {
    avs_persistence_context_t ctx = avs_persistence_store_context_create(out);
    as_persistence_version_t version = AS_PERSISTENCE_VERSION_CURRENT;
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_magic_string(&ctx, MAGIC)))
            || avs_is_err((err = avs_persistence_version(
                                   &ctx, (uint8_t *) &version,
                                   SUPPORTED_VERSIONS_ARRAY,
                                   sizeof(SUPPORTED_VERSIONS_ARRAY))))
            || avs_is_err(
                       (err = HANDLE_LIST(object, &ctx, &attr_storage->objects,
                                          (void *) version))));
    return err;
}

avs_error_t _anjay_attr_storage_restore_inner(
        anjay_t *anjay, anjay_attr_storage_t *attr_storage, avs_stream_t *in) {
    _anjay_attr_storage_clear(attr_storage);

    if (avs_is_eof(avs_stream_peek(in, 0, &(char) { 0 }))) {
        // empty stream, treat as success
        return AVS_OK;
    }

    avs_persistence_context_t ctx = avs_persistence_restore_context_create(in);
    as_persistence_version_t version = (as_persistence_version_t) 0;
    avs_error_t err;
    if (avs_is_err((err = avs_persistence_magic_string(&ctx, MAGIC)))
            || avs_is_err((err = avs_persistence_version(
                                   &ctx, (uint8_t *) &version,
                                   SUPPORTED_VERSIONS_ARRAY,
                                   sizeof(SUPPORTED_VERSIONS_ARRAY))))
            || avs_is_err(
                       (err = HANDLE_LIST(object, &ctx, &attr_storage->objects,
                                          (void *) version)))
            || avs_is_err((err = (is_attr_storage_sane(attr_storage)
                                          ? AVS_OK
                                          : avs_errno(AVS_EBADMSG))))
            || avs_is_err((
                       err = clear_nonexistent_entries(anjay, attr_storage)))) {
        _anjay_attr_storage_clear(attr_storage);
    }
    return err;
}

avs_error_t anjay_attr_storage_persist(anjay_t *anjay, avs_stream_t *out) {
    anjay_attr_storage_t *as = _anjay_attr_storage_get(anjay);
    if (!as) {
        as_log(ERROR,
               _("Attribute Storage is not installed on this Anjay object"));
        return avs_errno(AVS_EINVAL);
    }
    avs_error_t err = _anjay_attr_storage_persist_inner(as, out);
    if (avs_is_ok(err)) {
        as->modified_since_persist = false;
        as_log(INFO, _("Attribute Storage state persisted"));
    }
    return err;
}

avs_error_t anjay_attr_storage_restore(anjay_t *anjay, avs_stream_t *in) {
    anjay_attr_storage_t *as = _anjay_attr_storage_get(anjay);
    if (!as) {
        as_log(ERROR,
               _("Attribute Storage is not installed on this Anjay object"));
        return avs_errno(AVS_EINVAL);
    }
    avs_error_t err = _anjay_attr_storage_restore_inner(anjay, as, in);
    if (avs_is_ok(err)) {
        as_log(INFO, _("Attribute Storage state restored"));
    }
    as->modified_since_persist = avs_is_err(err);
    return err;
}

#    ifdef ANJAY_TEST
#        include "tests/modules/attr_storage/persistence.c"
#    endif // ANJAY_TEST

#endif // ANJAY_WITH_MODULE_ATTR_STORAGE
