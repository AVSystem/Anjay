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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H
#define ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H

#include <avsystem/commons/stream.h>

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates Access Control Object.
 *
 * After valid Access Control Object has been created one can enable Access
 * Control by registering this Object in Anjay by using for example
 * @p anjay_register_object() function.
 *
 * WARNING: After any modification of Security, Server or Access Control Object
 * by means other than LWM2M one has to execute
 * @ref anjay_notify_instances_changed in order to trigger necessary
 * revalidation routines of Access Control Object instances.
 *
 * @param anjay ANJAY object for which the Attribute Storage is created.
 *
 * @return pointer to the pointer of the newly created object on success,
 *         NULL otherwise
 */
const anjay_dm_object_def_t *const *
anjay_access_control_object_new(anjay_t *anjay);

/**
 * Destroys Access Control Object.
 *
 * NOTE: It shall not be called before releasing all references to the object -
 * likely not before calling @ref anjay_delete.
 *
 * @param obj
 */
void anjay_access_control_object_delete(const anjay_dm_object_def_t *const *obj);

/**
 * Dumps Access Control Object Instances to the @p out_stream.
 * Warning: @p ac_obj must not be wrapped
 *
 * @param ac_obj        Access Control Object definition
 * @param out_stream    stream to write to
 * @return 0 in case of success, negative value in case of an error
 */
int anjay_access_control_persist(const anjay_dm_object_def_t *const *ac_obj,
                                 avs_stream_abstract_t *out_stream);

/**
 * Tries to restore Access Control Object Instances from given @p in_stream.
 * Warning: @p ac_obj must not be wrapped
 *
 * @param ac_obj        Access Control Object defintion
 * @param in_stream     stream used for reading Access Control Object Instances
 * @return 0 in case of success, negative value in case of an error
 */
int anjay_access_control_restore(const anjay_dm_object_def_t *const *ac_obj,
                                 avs_stream_abstract_t *in_stream);

/**
 * Assign permissions for Instance /OID/IID to a particular server.
 *
 * @param ac_obj      Access Control Object definition.
 * @param oid         Object ID of the target Instance.
 * @param iid         Target Object Instance ID, or <c>ANJAY_IID_INVALID</c>
 *                    (i.e., MAX_ID==65535) to set an ACL referring to new
 *                    instance creation.
 * @param ssid        SSID of the server to grant permissions to.
 *                    @ref ANJAY_SSID_ANY may be used to set default permissions
 *                    for all servers with no explicit ACL entry.
 *                    Must not be equal to MAX_ID (65535).
 * @param access_mask ACL value to set for given Instance.
 *                    NOTE: Create permission makes no sense for an Instance,
 *                    and other permissions make no sense for new instance
 *                    creation.
 * @return 0 in case of success, negative value in case of an error (including
 *         the case where target Object Instance does not exist).
 */
int anjay_access_control_set_acl(const anjay_dm_object_def_t *const *ac_obj,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid,
                                 anjay_ssid_t ssid,
                                 anjay_access_mask_t access_mask);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H */
