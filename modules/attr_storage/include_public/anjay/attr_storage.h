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

#ifndef ANJAY_INCLUDE_ANJAY_ATTR_STORAGE_H
#define ANJAY_INCLUDE_ANJAY_ATTR_STORAGE_H

#include <avsystem/commons/stream.h>

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Anjay Attribute Storage object that may be used to handle attribute setting
 * for any LWM2M entities.
 */
typedef struct anjay_attr_storage_struct anjay_attr_storage_t;

/**
 * Creates a new Anjay Attribute Storage object.
 *
 * @param anjay ANJAY object for which the Attribute Storage is created.
 *
 * @returns Created Anjay Attribute Storage object on success,
 *          NULL in case of error.
 */
anjay_attr_storage_t *anjay_attr_storage_new(anjay_t *anjay);

/**
 * Cleans up all resources and releases the Anjay Attribute Storage object,
 * also discarding any stored attributes.
 *
 * NOTE: It shall not be called before releasing all references to LWM2M Objects
 * wrapped in this object - likely not before calling @ref anjay_delete.
 *
 * @param attr_storage Anjay Attribute Storage object to delete.
 */
void anjay_attr_storage_delete(anjay_attr_storage_t *attr_storage);

/**
 * Registers an LWM2M Object in the Attribute Storage, making it possible to
 * automatically manage attributes for it, its instances and resources.
 *
 * In accordance to the LWM2M specification, there are three levels on which
 * attributes may be stored:
 *
 * - Resource level (@ref anjay_dm_resource_read_attrs_t,
 *   @ref anjay_dm_resource_write_attrs_t)
 * - Instance level (@ref anjay_dm_instance_read_default_attrs_t,
 *   @ref anjay_dm_instance_write_default_attrs_t)
 * - Object level (@ref anjay_dm_object_read_default_attrs_t,
 *   @ref anjay_dm_object_write_default_attrs_t)
 *
 * If at least one of either read or write handlers is provided in the original
 * object for a given level, attribute handling on that level will not be
 * altered, but instead any calls will be directly forwarded to the original
 * handlers.
 *
 * If both read and write handlers are left as NULL in the original object for
 * a given level, attribute storage will be handled by the Attribute Storage
 * module instead, implementing both handlers.
 *
 * The enhanced object is returned as another, wrapped object, which may then be
 * passed to @ref anjay_register_object. The pointer will remain valid until
 * a call to @ref anjay_attr_storage_delete.
 *
 * @returns A wrapped LWM2M object definition, or NULL in case of error.
 */
const anjay_dm_object_def_t *const *
anjay_attr_storage_wrap_object(anjay_attr_storage_t *attr_storage,
                               const anjay_dm_object_def_t *const *def_ptr);

/**
 * Checks whether the attribute storage has been modified since last call to
 * @ref anjay_attr_storage_persist or @ref anjay_attr_storage_restore.
 */
bool anjay_attr_storage_is_modified(anjay_attr_storage_t *attr_storage);

int anjay_attr_storage_persist(anjay_attr_storage_t *attr_storage,
                               avs_stream_abstract_t *out_stream);

int anjay_attr_storage_restore(anjay_attr_storage_t *attr_storage,
                               avs_stream_abstract_t *in_stream);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_ATTR_STORAGE_H */
