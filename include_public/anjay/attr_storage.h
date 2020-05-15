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

#ifndef ANJAY_INCLUDE_ANJAY_ATTR_STORAGE_H
#define ANJAY_INCLUDE_ANJAY_ATTR_STORAGE_H

#include <avsystem/commons/avs_stream.h>

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Installs the Attribute Storage handlers in an Anjay object, making it
 * possible to automatically manage attributes for LwM2M Objects, their
 * instances and resources.
 *
 * In accordance to the LwM2M specification, there are three levels on which
 * attributes may be stored:
 *
 * - Resource level (@ref anjay_dm_resource_read_attrs_t,
 *   @ref anjay_dm_resource_write_attrs_t)
 * - Instance level (@ref anjay_dm_instance_read_default_attrs_t,
 *   @ref anjay_dm_instance_write_default_attrs_t)
 * - Object level (@ref anjay_dm_object_read_default_attrs_t,
 *   @ref anjay_dm_object_write_default_attrs_t)
 *
 * If at least one of either read or write handlers is provided in a given
 * object for a given level, attribute handling on that level will not be
 * altered, but instead any calls will be directly forwarded to the original
 * handlers.
 *
 * If both read and write handlers are left as NULL in a given object for a
 * given level, attribute storage will be handled by the Attribute Storage
 * module instead, implementing both handlers.
 *
 * The Attribute Storage module does not require explicit cleanup; all resources
 * will be automatically freed up during the call to @ref anjay_delete.
 *
 * @param anjay ANJAY object for which the Attribute Storage is installed.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_attr_storage_install(anjay_t *anjay);

/**
 * Checks whether the attribute storage has been modified since last successful
 * call to @ref anjay_attr_storage_persist or @ref anjay_attr_storage_restore.
 */
bool anjay_attr_storage_is_modified(anjay_t *anjay);

/**
 * Removes all attributes from all entities, leaving the Attribute Storage in an
 * empty state.
 *
 * @param anjay Anjay instance with the Attribute Storage installed to purge.
 */
void anjay_attr_storage_purge(anjay_t *anjay);

/**
 * Dumps all set attributes to the @p out_stream.
 *
 * @param anjay         Anjay instance with the Attribute Storage installed.
 * @param out_stream    Stream to write to.
 * @return 0 in case of success, negative value in case of an error.
 */
avs_error_t anjay_attr_storage_persist(anjay_t *anjay,
                                       avs_stream_t *out_stream);

/**
 * Attempts to restore attribute storage from specified @p in_stream.
 *
 * Note: before attempting restoration, the Attribute Storage is cleared, so no
 * previously set attributes will be retained. In particular, if restore fails,
 * then the Attribute Storage will be completely cleared and
 * @ref anjay_attr_storage_is_modified will return <c>true</c>.
 *
 * @param anjay     Anjay instance with Security Object installed.
 * @param in_stream Stream to read from.
 * @return 0 in case of success, negative value in case of an error.
 *
 * <strong>NOTE:</strong> For historical reasons, this function behaves
 * differently than all other <c>*_restore()</c> functions in Anjay in two ways:
 *
 * - On failed restoration, the storage is cleared, rather than left untouched
 * - Zero-length stream is treated as valid, and causes the storage to be
 *   cleared with success returned, instead of causing an error
 *
 * Relying on these behaviours is <strong>DEPRECATED</strong>. Future versions
 * of Anjay may change the semantics of this function so that it retains the
 * contents of Attribute Storage on failure, and does not treat empty streams as
 * valid. It is <strong>RECOMMENDED</strong> that any new code involving this
 * function is written to work properly with both semantics.
 */
avs_error_t anjay_attr_storage_restore(anjay_t *anjay, avs_stream_t *in_stream);

/**
 * Sets Object level attributes for the specified @p ssid.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  SSID for which given Attributes shall be set (must be a valid
 *              SSID corresponding to one of the non-Bootstrap LwM2M Servers).
 * @param oid   Object ID for which given Attributes shall be set.
 * @param attrs Attributes to be set (MUST NOT be NULL).
 *
 * NOTE: This function will fail if the object has object_read_default_attrs or
 * object_write_default_attrs handler implemented.
 *
 * @returns 0 on success, negative value in case of an error.
 */
int anjay_attr_storage_set_object_attrs(anjay_t *anjay,
                                        anjay_ssid_t ssid,
                                        anjay_oid_t oid,
                                        const anjay_dm_oi_attributes_t *attrs);
/**
 * Sets Instance level attributes for the specified @p ssid.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  SSID for which given Attributes shall be set (must be a valid
 *              SSID corresponding to one of the non-Bootstrap LwM2M Servers).
 * @param oid   Object ID for which given Attributes shall be set.
 * @param iid   Instance ID for which given Attributes shall be set.
 * @param attrs Attributes to be set (MUST NOT be NULL).
 *
 * NOTE: This function will fail if the object has instance_read_default_attrs
 * or instance_write_default_attrs handler implemented.
 *
 * @returns 0 on success, negative value in case of an error.
 */
int anjay_attr_storage_set_instance_attrs(
        anjay_t *anjay,
        anjay_ssid_t ssid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_dm_oi_attributes_t *attrs);

/**
 * Sets Resource level attributes for the specified @p ssid.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  SSID for which given Attributes shall be set (must be a valid
 *              SSID corresponding to one of the non-Bootstrap LwM2M Servers).
 * @param oid   Object ID owning the specified Instance.
 * @param iid   Instance ID owning the specified Resource.
 * @param rid   Resource ID for which given Attributes shall be set.
 * @param attrs Attributes to be set (MUST NOT be NULL).
 *
 * NOTE: This function will fail if the object has resource_read_attrs
 * or resource_write_attrs handler implemented.
 *
 * @returns 0 on success, negative value in case of an error.
 */
int anjay_attr_storage_set_resource_attrs(anjay_t *anjay,
                                          anjay_ssid_t ssid,
                                          anjay_oid_t oid,
                                          anjay_iid_t iid,
                                          anjay_rid_t rid,
                                          const anjay_dm_r_attributes_t *attrs);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_ATTR_STORAGE_H */
