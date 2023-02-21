/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
 * @returns AVS_OK in case of success, or an error code.
 */
avs_error_t anjay_access_control_persist(anjay_t *anjay,
                                         avs_stream_t *out_stream);

/**
 * Tries to restore Access Control Object Instances from given @p in_stream.
 *
 * @param anjay         ANJAY object with the Access Control module installed
 * @param in_stream     stream used for reading Access Control Object Instances
 * @returns AVS_OK in case of success, or an error code.
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

/**
 * Set the Access Control Owner for a given Object Instance.
 *
 * @param anjay         ANJAY object with the Access Control module installed
 *
 * @param target_oid    Object ID of the target Instance.
 *
 * @param target_iid    Target Object Instance ID, or <c>ANJAY_ID_INVALID</c>
 *                      (i.e., MAX_ID==65535) to set an ACL referring to new
 *                      instance creation.
 *
 * @param owner_ssid    SSID of the server which should become the Access
 *                      Control Owner for the given Object Instance.
 *                      <c>ANJAY_SSID_BOOTSTRAP</c> can be specified to signify
 *                      that the ACL shall not be editable by any regular LwM2M
 *                      Server.
 *
 * @param inout_acl_iid Setting related to the Instance ID of the Access Control
 *                      Object Instance that governs the given target.
 *                      @li If <c>NULL</c>, any existing instance governing the
 *                          given target will be used if present, or a new
 *                          instance with a first free Instance ID will be
 *                          created.
 *                      @li If non-<c>NULL</c> and <c>*inout_acl_iid ==
 *                          ANJAY_ID_INVALID</c>, any existing instance
 *                          governing the given target will be used if present,
 *                          or a new instance with a first free Instance ID will
 *                          be created, and <c>*inout_acl_iid</c> will be set to
 *                          the Instance ID of the affected Access Control
 *                          Object Instance upon a successful return from this
 *                          function.
 *                      @li If non-</c>NULL</c> and <c>*inout_acl_iid !=
 *                          ANJAY_ID_INVALID</c>, a new instance with that ID
 *                          will be created; an existing instance may also be
 *                          used, but only if the instance governing the given
 *                          target has the ID specified. If an instance
 *                          governing the given target already exists and has a
 *                          different Instance ID, or if an instance with the
 *                          given ID, but governs a different target,
 *                          <c>*inout_acl_iid</c> will be set to the ID of the
 *                          conflicting instance and this function will return
 *                          an error.
 *
 * @return 0 in case of success, negative value in case of an error (including
 *         the case where target Object Instance does not exist).
 */
int anjay_access_control_set_owner(anjay_t *anjay,
                                   anjay_oid_t target_oid,
                                   anjay_iid_t target_iid,
                                   anjay_ssid_t owner_ssid,
                                   anjay_iid_t *inout_acl_iid);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_ACCESS_CONTROL_H */
