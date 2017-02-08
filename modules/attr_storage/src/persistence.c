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

#include <stdio.h>
#include <string.h>

#include <anjay_modules/dm.h>
#include <anjay_modules/io.h>
#include <anjay_modules/utils.h>
#include <anjay/persistence.h>

#include "attr_storage.h"

VISIBILITY_SOURCE_BEGIN

#define HANDLE_LIST(Type, Ctx, ListPtr) \
        anjay_persistence_list((Ctx), (AVS_LIST(void) *) (ListPtr), \
                               sizeof(**(ListPtr)), handle_##Type)

//// DATA STRUCTURE HANDLERS ///////////////////////////////////////////////////

static int handle_dm_attributes(anjay_persistence_context_t *ctx,
                                anjay_dm_attributes_t *attrs) {
    int retval;
    (void) ((retval = anjay_persistence_time(ctx, &attrs->min_period))
            || (retval = anjay_persistence_time(ctx, &attrs->max_period))
            || (retval = anjay_persistence_double(ctx, &attrs->greater_than))
            || (retval = anjay_persistence_double(ctx, &attrs->less_than))
            || (retval = anjay_persistence_double(ctx, &attrs->step)));
    return retval;
}

static int handle_default_attrs(anjay_persistence_context_t *ctx, void *attrs_) {
    fas_attrs_t *attrs = (fas_attrs_t *) attrs_;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &attrs->ssid))
            || (retval = handle_dm_attributes(ctx, &attrs->attrs)));
    return retval;
}

static int handle_resource_attrs(anjay_persistence_context_t *ctx, void *attrs_) {
    fas_attrs_t *attrs = (fas_attrs_t *) attrs_;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &attrs->ssid))
            || (retval = handle_dm_attributes(ctx, &attrs->attrs)));
    return retval;
}

static int handle_resource_entry(anjay_persistence_context_t *ctx, void *resource_) {
    fas_resource_entry_t *resource = (fas_resource_entry_t *) resource_;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &resource->rid))
            || (retval = HANDLE_LIST(resource_attrs, ctx, &resource->attrs)));
    return retval;
}

static int handle_instance_entry(anjay_persistence_context_t *ctx, void *instance_) {
    fas_instance_entry_t *instance = (fas_instance_entry_t *) instance_;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &instance->iid))
            || (retval = HANDLE_LIST(default_attrs, ctx,
                                     &instance->default_attrs))
            || (retval = HANDLE_LIST(resource_entry, ctx,
                                     &instance->resources)));
    return retval;
}

static int handle_object(anjay_persistence_context_t *ctx, void *object_) {
    fas_object_t *object = (fas_object_t *) object_;
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &object->def.oid))
            || (retval = HANDLE_LIST(default_attrs, ctx,
                                     &object->default_attrs))
            || (retval = HANDLE_LIST(instance_entry, ctx, &object->instances)));
    return retval;
}

// HELPERS /////////////////////////////////////////////////////////////////////

static void clear_storage(anjay_attr_storage_t *attr_storage) {
    AVS_LIST(fas_object_t) object;
    AVS_LIST_FOREACH(object, attr_storage->objects) {
        _anjay_attr_storage_clear_object(object);
    }
}

static int stream_at_end(avs_stream_abstract_t *in) {
    if (avs_stream_peek(in, 0) != EOF) {
        return 0; // data ahead
    }

    size_t bytes_read;
    char message_finished;
    char value;
    int result = avs_stream_read(in, &bytes_read, &message_finished,
                                 &value, sizeof(value));
    if (!result && !bytes_read && message_finished) {
        return 1;
    }
    return result < 0 ? result : -1;
}

static bool is_attribute_list_sane(AVS_LIST(fas_attrs_t) attrs_list) {
    int32_t last_ssid = -1;
    AVS_LIST(fas_attrs_t) attrs;
    AVS_LIST_FOREACH(attrs, attrs_list) {
        if (attrs->ssid <= last_ssid
                || _anjay_dm_attributes_empty(&attrs->attrs)) {
            return false;
        }
        last_ssid = attrs->ssid;
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
        if (!is_attribute_list_sane(resource->attrs)) {
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
        if (!is_attribute_list_sane(instance->default_attrs)
                || !is_resources_list_sane(instance->resources)) {
            return false;
        }
    }
    return true;
}

static bool is_object_sane(fas_object_t *object) {
    return is_attribute_list_sane(object->default_attrs)
            && is_instances_list_sane(object->instances);
}

static int clear_nonexistent_iids(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *def_ptr) {
    // def_ptr refers to the wrapper object, so it effectively calls the
    // FAS handlers that will eventually clear what is meant to be cleared
    void *cookie = NULL;
    anjay_iid_t iid = 0;
    while (iid != ANJAY_IID_INVALID) {
        int retval = _anjay_dm_instance_it(anjay, def_ptr, &iid, &cookie);
        if (retval) {
            return retval;
        }
    }
    return 0;
}

static int clear_nonexistent_rids(anjay_t *anjay, fas_object_t *object) {
    AVS_LIST(fas_instance_entry_t) *instance_ptr;
    AVS_LIST(fas_instance_entry_t) instance_helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(instance_ptr, instance_helper,
                                   &object->instances) {
        AVS_LIST(fas_resource_entry_t) *resource_ptr;
        AVS_LIST(fas_resource_entry_t) resource_helper;
        AVS_LIST_DELETABLE_FOREACH_PTR(resource_ptr, resource_helper,
                                       &(*instance_ptr)->resources) {
            int rid_present
                = _anjay_dm_resource_supported_and_present(anjay,
                                                           object->backend,
                                                           (*instance_ptr)->iid,
                                                           (*resource_ptr)->rid);
            if (rid_present < 0) {
                return -1;
            } else if (!rid_present) {
                remove_resource_entry(object->fas, resource_ptr);
            }
        }
        remove_instance_if_empty(instance_ptr);
    }
    return 0;
}

static int clear_nonexistent(anjay_t *anjay, fas_object_t *object) {
    int retval;
    (void) ((retval = clear_nonexistent_iids(anjay, &object->def_ptr))
            || (retval = clear_nonexistent_rids(anjay, object)));
    return retval;
}

static int restore_object_data(anjay_persistence_context_t *ctx,
                               fas_object_t *object) {
    int retval;
    (void) ((retval = HANDLE_LIST(default_attrs, ctx, &object->default_attrs))
            || (retval = HANDLE_LIST(instance_entry, ctx, &object->instances)));
    return retval;
}

static int restore_objects_inner(anjay_attr_storage_t *attr_storage,
                                 anjay_persistence_context_t *restore_ctx,
                                 anjay_persistence_context_t *ignore_ctx) {
    int retval;
    uint32_t count;
    if ((retval = anjay_persistence_u32(restore_ctx, &count))) {
        return retval;
    }
    int32_t last_oid = -1;
    while (count--) {
        anjay_oid_t oid;
        if ((retval = anjay_persistence_u16(restore_ctx, &oid))) {
            return retval;
        }
        if (oid <= last_oid) {
            return -1;
        }
        last_oid = oid;
        fas_object_t *object =
                _anjay_attr_storage_find_object(attr_storage, oid);
        if ((retval = restore_object_data(object ? restore_ctx : ignore_ctx,
                                          object))) {
            return retval;
        }
        if (object) {
            if (!is_object_sane(object)) {
                fas_log(ERROR, "Invalid persistence data format");
                return -1;
            } else if ((retval = clear_nonexistent(attr_storage->anjay,
                                                   object))) {
                fas_log(ERROR, "Could not properly clear data for "
                               "nonexistent instances");
                return retval;
            }
        }
    }
    return 0;
}

static int restore_objects(anjay_attr_storage_t *attr_storage,
                           avs_stream_abstract_t *in) {
    anjay_persistence_context_t *restore_ctx =
            anjay_persistence_restore_context_new(in);
    anjay_persistence_context_t *ignore_ctx =
            anjay_persistence_ignore_context_new(in);
    int retval = -1;
    if (restore_ctx && ignore_ctx) {
        retval = restore_objects_inner(attr_storage, restore_ctx, ignore_ctx);
    } else {
        fas_log(ERROR, "Out of memory");
    }
    anjay_persistence_context_delete(restore_ctx);
    anjay_persistence_context_delete(ignore_ctx);
    return retval;
}

//// PUBLIC FUNCTIONS //////////////////////////////////////////////////////////

static const char MAGIC[] = { 'F', 'A', 'S', '\1' };

int _anjay_attr_storage_persist_inner(anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *out) {
    int retval = avs_stream_write(out, MAGIC, sizeof(MAGIC));
    if (retval) {
        return retval;
    }
    anjay_persistence_context_t *ctx = anjay_persistence_store_context_new(out);
    if (!ctx) {
        fas_log(ERROR, "Out of memory");
        return -1;
    }
    retval = HANDLE_LIST(object, ctx, &attr_storage->objects);
    anjay_persistence_context_delete(ctx);
    return retval;
}

int _anjay_attr_storage_restore_inner(anjay_attr_storage_t *attr_storage,
                                      avs_stream_abstract_t *in) {
    clear_storage(attr_storage);
    int retval = stream_at_end(in);
    if (retval) {
        return (retval < 0) ? retval : 0;
    }

    char magic_buffer[sizeof(MAGIC)];
    retval = avs_stream_read_reliably(in, magic_buffer, sizeof(magic_buffer));
    if (retval) {
        return retval;
    } else if (memcmp(MAGIC, magic_buffer, sizeof(MAGIC))) {
        fas_log(ERROR, "Magic value mismatch");
        return -1;
    } else if ((retval = restore_objects(attr_storage, in))) {
        clear_storage(attr_storage);
    }
    return retval;
}

int anjay_attr_storage_persist(anjay_attr_storage_t *attr_storage,
                               avs_stream_abstract_t *out) {
    int retval = _anjay_attr_storage_persist_inner(attr_storage, out);
    if (!retval) {
        attr_storage->modified_since_persist = false;
    }
    return retval;
}

int anjay_attr_storage_restore(anjay_attr_storage_t *attr_storage,
                               avs_stream_abstract_t *in) {
    attr_storage->modified_since_persist = false;
    int retval = _anjay_attr_storage_restore_inner(attr_storage, in);
    if (retval) {
        attr_storage->modified_since_persist = true;
    }
    return retval;
}

#ifdef ANJAY_TEST
#include "test/persistence.c"
#endif // ANJAY_TEST
