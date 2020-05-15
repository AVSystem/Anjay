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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H
#define ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H

#include <avsystem/commons/avs_stream.h>

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Installs the Access Control Object in an Anjay object.
 *
 * The Access Control module does not require explicit cleanup; all resources
 * will be automatically freed up during the call to @ref anjay_delete.
 *
 * WARNING: After any modification of Security, Server or Access Control Object
 * by means other than LwM2M one has to execute
 * @ref anjay_notify_instances_changed in order to trigger necessary
 * revalidation routines of Access Control Object instances.
 *
 * @param anjay ANJAY object for which the Access Control Object is installed.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_access_control_install(anjay_t *anjay);

/**
 * Removes all instances of Access Control Object, leaving it in an empty state.
 *
 * @param anjay ANJAY object with the Access Control module installed
 */
void anjay_access_control_purge(anjay_t *anjay);

/**
 * Dumps Access Control Object Instances to the @p out_stream.
 *
 * @param anjay         ANJAY object with the Access Control module installed
 * @param out_stream    stream to write to
 * @return 0 in case of success, negative value in case of an error
 */
avs_error_t anjay_access_control_persist(anjay_t *anjay,
                                         avs_stream_t *out_stream);

/**
 * Tries to restore Access Control Object Instances from given @p in_stream.
 *
 * @param anjay         ANJAY object with the Access Control module installed
 * @param in_stream     stream used for reading Access Control Object Instances
 * @return 0 in case of success, negative value in case of an error
 */
avs_error_t anjay_access_control_restore(anjay_t *anjay,
                                         avs_stream_t *in_stream);

/**
 * Checks whether the Access Control Object from Anjay instance has been
 * modified since last successful call to @ref anjay_access_control_persist or
 * @ref anjay_access_control_restore.
 */
bool anjay_access_control_is_modified(anjay_t *anjay);

/**
 * Assign permissions for Instance /OID/IID to a particular server.
 *
 * @param anjay       ANJAY object with the Access Control module installed
 * @param oid         Object ID of the target Instance.
 * @param iid         Target Object Instance ID, or <c>ANJAY_ID_INVALID</c>
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
int anjay_access_control_set_acl(anjay_t *anjay,
                                 anjay_oid_t oid,
                                 anjay_iid_t iid,
                                 anjay_ssid_t ssid,
                                 anjay_access_mask_t access_mask);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H */
