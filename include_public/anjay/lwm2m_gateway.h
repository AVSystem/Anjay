/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_LWM2M_GATEWAY_H
#define ANJAY_INCLUDE_LWM2M_GATEWAY_H

#include <anjay/anjay.h>
#include <anjay/anjay_config.h>
#include <anjay/lwm2m_send.h>

#ifdef ANJAY_WITH_LWM2M_GATEWAY

#    ifdef __cplusplus
extern "C" {
#    endif

/**
 * Registers LwM2M Gateway Object and initializes the Gateway Module.
 *
 * @param anjay   Anjay object to operate on
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_lwm2m_gateway_install(anjay_t *anjay);

/**
 * Register an End Device in LwM2M Gateway and assign the necessary Resources.
 * /0 Device ID Resource is set as @p device_id parameter.
 * /1 Prefix Resource is assigned automatically as "dev<x>" where <x> is the
 * Device ID returned with @p inout_iid parameter.
 * /3 IoT Device Object Resource is generated as Corelnk format upon Read
 * Request according to the Data Model set with
 * @ref anjay_lwm2m_gateway_register_object calls
 *
 * Note: if @p inout_iid is set to @ref ANJAY_ID_INVALID then the Instance id
 * is generated automatically, otherwise value of @p inout_iid is used as a
 * new Gateway Instance ID.
 *
 * @param anjay      Anjay object to operate on
 * @param device_id  Globally Unique Device ID (/0 Resource) as a
 *                   NULL-terminated string. It's value is not copied, so
 *                   the pointer must remain valid
 * @param inout_iid  Gateway Instance ID to use or @ref ANJAY_ID_INVALID .
 *                   Treated also as End IoT Device ID which shall be used with
 *                   further API calls to specify the End Device entity in
 *                   Gateway
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_lwm2m_gateway_register_device(anjay_t *anjay,
                                        const char *device_id,
                                        anjay_iid_t *inout_iid);

/**
 * Deregister an End Device in LwM2M Gateway.
 *
 * @param anjay  Anjay object to operate on
 * @param iid    End Device Instance ID to be deregistered
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_lwm2m_gateway_deregister_device(anjay_t *anjay, anjay_iid_t iid);

/**
 * Register an Object in LwM2M Gateway End Device Data Model.
 *
 * @param anjay   Anjay object to operate on
 * @param iid     End Device Instance ID
 * @param def_ptr Pointer to the Object definition struct. The exact value
 *                passed to this function will be forwarded to all data model
 *                handler calls.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_lwm2m_gateway_register_object(
        anjay_t *anjay,
        anjay_iid_t iid,
        const anjay_dm_object_def_t *const *def_ptr);

/**
 * Unregister an Object in LwM2M Gateway End Device Data Model.
 *
 * @param anjay   Anjay object to operate on
 * @param iid     End Device Instance ID
 * @param def_ptr Pointer to the Object definition struct.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_lwm2m_gateway_unregister_object(
        anjay_t *anjay,
        anjay_iid_t iid,
        const anjay_dm_object_def_t *const *def_ptr);

#    ifdef ANJAY_WITH_SEND

/**
 * Adds a value to batch builder. This function is intended to be used with
 * LwM2M Gateway End Device objects.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder     Pointer to batch builder
 * @param gateway_iid End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param oid         Object ID, MUST NOT be @c UINT16_MAX
 * @param iid         Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid         Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid        Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp   Time related to value being sent (e.g. when the
 *                    measurement corresponding to the passed value was made)
 * @param value       Value to add to the batch.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_add_int(anjay_send_batch_builder_t *builder,
                                           anjay_iid_t gateway_iid,
                                           anjay_oid_t oid,
                                           anjay_iid_t iid,
                                           anjay_rid_t rid,
                                           anjay_riid_t riid,
                                           avs_time_real_t timestamp,
                                           int64_t value);

/**
 * @copydoc anjay_lwm2m_gateway_send_batch_add_int()
 */
int anjay_lwm2m_gateway_send_batch_add_uint(anjay_send_batch_builder_t *builder,
                                            anjay_iid_t gateway_iid,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            anjay_riid_t riid,
                                            avs_time_real_t timestamp,
                                            uint64_t value);

/**
 * @copydoc anjay_lwm2m_gateway_send_batch_add_int()
 */
int anjay_lwm2m_gateway_send_batch_add_double(
        anjay_send_batch_builder_t *builder,
        anjay_iid_t gateway_iid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        avs_time_real_t timestamp,
        double value);

/**
 * @copydoc anjay_lwm2m_gateway_send_batch_add_int()
 */
int anjay_lwm2m_gateway_send_batch_add_bool(anjay_send_batch_builder_t *builder,
                                            anjay_iid_t gateway_iid,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            anjay_riid_t riid,
                                            avs_time_real_t timestamp,
                                            bool value);

/**
 * Adds a string to batch builder. This function is intended to be used with
 * LwM2M Gateway End Device objects.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder     Pointer to batch builder
 * @param gateway_iid End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param oid         Object ID, MUST NOT be @c UINT16_MAX
 * @param iid         Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid         Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid        Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp   Time related to string being send (e.g. when the
 *                    measurement corresponding to the passed string was made)
 * @param str         Pointer to a NULL-terminated string. Must not be NULL.
 *                    No longer required by batch builder after call to this
 *                    function, because internal copy is made.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_add_string(
        anjay_send_batch_builder_t *builder,
        anjay_iid_t gateway_iid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        avs_time_real_t timestamp,
        const char *str);

/**
 * Adds bytes to batch builder. This function is intended to be used with LwM2M
 * Gateway End Device objects.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder     Pointer to batch builder
 * @param gateway_iid End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param oid         Object ID, MUST NOT be @c UINT16_MAX
 * @param iid         Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid         Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid        Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp   Time related to bytes being send (e.g. when the
 *                    measurement corresponding to the passed bytes was made)
 * @param data        Pointer to data. No longer required by batch builder after
 *                    call to this function, because internal copy is made. Can
 *                    be NULL only if @p length is 0.
 * @param length      Length of data in bytes.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_add_bytes(
        anjay_send_batch_builder_t *builder,
        anjay_iid_t gateway_iid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        avs_time_real_t timestamp,
        const void *data,
        size_t length);

/**
 * Adds an Object Link to batch builder. This function is intended to be used
 * with LwM2M Gateway End Device objects.
 *
 * IMPORTANT NOTE:
 * If @p timestamp is earlier than 1978-07-04 21:24:16 UTC (2**28 seconds since
 * Unix epoch), then it's assumed to be relative to some arbitrary point in
 * time, and will be encoded as relative to "now". Otherwise, the time is
 * assumed to be an Unix timestamp, and encoded as time since Unix epoch. See
 * also: RFC 8428, "Requirements and Design Goals"
 *
 * @param builder     Pointer to batch builder
 * @param gateway_iid End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param oid         Object ID, MUST NOT be @c UINT16_MAX
 * @param iid         Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid         Resource ID, MUST NOT be @c UINT16_MAX
 * @param riid        Resource Instance ID, @c UINT16_MAX for no RIID
 * @param timestamp   Time related to Object Link being send (e.g. when the
 *                    measurement corresponding to the passed Object Link was
 *                    made)
 * @param objlnk_oid  OID of Object Link
 * @param objlnk_iid  IID of Object Link
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_add_objlnk(
        anjay_send_batch_builder_t *builder,
        anjay_iid_t gateway_iid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        avs_time_real_t timestamp,
        anjay_oid_t objlnk_oid,
        anjay_iid_t objlnk_iid);

/**
 * Reads value from data model of the End Device (without checking access
 * privileges) and adds it to the builder with timestamp set to @c
 * avs_time_real_now().
 *
 * May possibly add multiple entries if /prefix/oid/iid/rid is a Multiple
 * Resource.
 *
 * @param builder     Pointer to batch builder, MUST NOT be @c NULL
 * @param anjay       Pointer to Anjay object, MUST NOT be @c NULL
 * @param gateway_iid End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param oid         Object ID, MUST NOT be @c UINT16_MAX , @c 0 (Security
 *                    object ID) or @c 21 (OSCORE object ID).
 * @param iid         Instance ID, MUST NOT be @c UINT16_MAX
 * @param rid         Resource ID, MUST NOT be @c UINT16_MAX
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_data_add_current(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay,
        anjay_iid_t gateway_iid,
        anjay_oid_t oid,
        anjay_iid_t iid,
        anjay_rid_t rid);

/**
 * Reads value from data model of the End Device (without checking access
 * privileges) and adds them to the builder with the same timestamp for every
 * value. Timestamp is set to @c avs_time_real_now().
 *
 * IMPORTANT: All @p paths must point to the objects of the same End Device.
 *
 * @param builder      Pointer to batch builder, MUST NOT be @c NULL
 * @param anjay        Pointer to Anjay object, MUST NOT be @c NULL
 * @param gateway_iid  End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param paths        Pointer to array of @ref anjay_send_resource_path_t .
 * @param paths_length Length of @p paths array.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_data_add_current_multiple(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay,
        anjay_iid_t gateway_iid,
        const anjay_send_resource_path_t *paths,
        size_t paths_length);

/**
 * Reads value from data model of the End Device (without checking access
 * privileges) and adds them to the builder with the same timestamp for every
 * value. Timestamp is set to @c avs_time_real_now().
 *
 * IMPORTANT: All @p paths must point to the objects of the same End Device.
 *
 * If a resource is not found, it's ignored, the error isn't returned and the
 * function adds next resources from the @p paths. Hoverer, if the End Device is
 * not present, the error is returned.
 *
 * @param builder      Pointer to batch builder, MUST NOT be @c NULL
 * @param anjay        Pointer to Anjay object, MUST NOT be @c NULL
 * @param gateway_iid  End Device Instance ID, MUST NOT be @c UINT16_MAX
 * @param paths        Pointer to array of @ref anjay_send_resource_path_t .
 * @param paths_length Length of @p paths array.
 *
 * @returns 0 on success, negative value otherwise. In case of failure, the
 *          @p builder is left unchanged.
 */
int anjay_lwm2m_gateway_send_batch_data_add_current_multiple_ignore_not_found(
        anjay_send_batch_builder_t *builder,
        anjay_t *anjay,
        anjay_iid_t gateway_iid,
        const anjay_send_resource_path_t *paths,
        size_t paths_length);

#    endif // ANJAY_WITH_SEND

/**
 * Notifies the library that the value of given Resource changed. It may trigger
 * a LwM2M Notify message, update server connections and perform other tasks,
 * as required for the specified Resource.
 *
 * Needs to be called for any Resource after its value is changed by means other
 * than LwM2M.
 *
 * Note that it should not be called after a Write performed by the LwM2M
 * server.
 *
 * @param anjay   Anjay object to operate on.
 * @param end_dev End Device Instance ID.
 * @param oid     Object ID of the changed Resource.
 * @param iid     Object Instance ID of the changed Resource.
 * @param rid     Resource ID of the changed Resource.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_lwm2m_gateway_notify_changed(anjay_t *anjay,
                                       anjay_iid_t end_dev,
                                       anjay_oid_t oid,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid);

/**
 * Notifies the library that the set of Instances existing in a given Object
 * changed. It may trigger a LwM2M Notify message, update server connections
 * and perform other tasks, as required for the specified Object ID.
 *
 * Needs to be called for each Object, after an Instance is created or removed
 * by means other than LwM2M.
 *
 * Note that it should not be called after a Create or Delete performed by the
 * LwM2M server.
 *
 * @param anjay   Anjay object to operate on.
 * @param end_dev End Device Instance ID.
 * @param oid     Object ID of the changed Object.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_lwm2m_gateway_notify_instances_changed(anjay_t *anjay,
                                                 anjay_iid_t end_dev,
                                                 anjay_oid_t oid);
#    ifdef ANJAY_WITH_OBSERVATION_STATUS
/**
 * Gets information whether and how a given Resource is observed. See
 * @ref anjay_resource_observation_status_t for details.
 *
 * NOTE: This API is a companion to @ref anjay_notify_changed. There is no
 * analogous API that would be a companion to
 * @ref anjay_notify_instances_changed. Any changes to set of instances of any
 * LwM2M Object MUST be considered observed at all times and notified as soon as
 * possible.
 *
 * @param anjay Anjay object to operate on.
 * @param end_dev End Device Instance ID.
 * @param oid   Object ID of the Resource to check.
 * @param iid   Object Instance ID of the Resource to check.
 * @param rid   Resource ID of the Resource to check.
 *
 * @returns Observation status of a given Resource. If the arguments do not
 *          specify a valid Resource path, data equivalent to a non-observed
 *          Resource will be returned.
 *
 * NOTE: This function may be used to implement notifications for Resources that
 * require active polling by the client application. A naive implementation
 * could look more or less like this (pseudocode):
 *
 * <code>
 * status = anjay_resource_observation_status(anjay, oid, iid, rid);
 * if (status.is_observed
 *         && current_time >= last_check_time + status.min_period) {
 *     new_value = read_resource_value();
 *     if (new_value != old_value) {
 *         anjay_notify_changed(anjay, oid, iid, rid);
 *     }
 *     last_check_time = current_time;
 * }
 * </code>
 *
 * However, please note that such implementation may not be strictly conformant
 * to the LwM2M specification. For example, in the following case:
 *
 * [time] --|--------|-*------|-->     | - intervals between resource reads
 *          |<------>|                 * - point in time when underlying state
 *          min_period                     actually changes
 *
 * the specification would require the notification to be sent exactly at the
 * time of the (*) event, but with this naive implementation, will be delayed
 * until the next (|).
 */
anjay_resource_observation_status_t
anjay_lwm2m_gateway_resource_observation_status(anjay_t *anjay,
                                                anjay_iid_t end_dev,
                                                anjay_oid_t oid,
                                                anjay_iid_t iid,
                                                anjay_rid_t rid);
#    endif // ANJAY_WITH_OBSERVATION_STATUS
#    ifdef __cplusplus
}
#    endif

#endif // ANJAY_WITH_LWM2M_GATEWAY
#endif // ANJAY_INCLUDE_LWM2M_GATEWAY_H
