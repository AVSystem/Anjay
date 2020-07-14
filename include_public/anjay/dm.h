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

#ifndef ANJAY_INCLUDE_ANJAY_DM_H
#define ANJAY_INCLUDE_ANJAY_DM_H

#include <math.h>
#include <stdint.h>

#include <anjay/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct anjay_dm_object_def_struct anjay_dm_object_def_t;

/** Object/Object Instance Attributes */
typedef struct {
    /** Minimum Period as defined by LwM2M spec */
    int32_t min_period;
    /** Maximum Period as defined by LwM2M spec */
    int32_t max_period;
    /** Minimum Evaluation Period as defined by LwM2M spec */
    int32_t min_eval_period;
    /** Maximum Evaluation Period as defined by LwM2M spec */
    int32_t max_eval_period;
} anjay_dm_oi_attributes_t;

/** Resource attributes. */
typedef struct {
    /** Attributes shared with Objects/Object Instances */
    anjay_dm_oi_attributes_t common;
    /** Greater Than attribute as defined by LwM2M spec */
    double greater_than;
    /** Less Than attribute as defined by LwM2M spec */
    double less_than;
    /** Step attribute as defined by LwM2M spec */
    double step;
} anjay_dm_r_attributes_t;

/** A value indicating that the Min/Max Period attribute is not set */
#define ANJAY_ATTRIB_PERIOD_NONE (-1)

/** A value indicating that the Less Than/Greater Than/Step attribute
 * is not set */
#define ANJAY_ATTRIB_VALUE_NONE (NAN)

/** Convenience Object/Object Instance attributes constant, filled with
 * "attribute not set" values */
extern const anjay_dm_oi_attributes_t ANJAY_DM_OI_ATTRIBUTES_EMPTY;

/** Convenience Resource attributes constant, filled with
 * "attribute not set" values */
extern const anjay_dm_r_attributes_t ANJAY_DM_R_ATTRIBUTES_EMPTY;

/**
 * A handler that returns default attribute values set for the Object.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      ssid    Short Server ID of the server requesting the RPC.
 * @param[out] out     Attributes struct to be filled by the handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_object_read_default_attrs_t(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out);

/**
 * A handler that sets default attribute values for the Object.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param ssid    Short Server ID of the server requesting the RPC.
 * @param attrs   Attributes struct to be set for the Object.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_object_write_default_attrs_t(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs);

/**
 * A handler that enumerates all Object Instances for the Object.
 *
 * The library will not attempt to call @ref anjay_dm_instance_remove_t or
 * @ref anjay_dm_instance_create_t handlers inside the @ref anjay_dm_emit calls
 * performed from this handler, so the implementation is free to use iteration
 * state that would be invalidated by such calls.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param ctx     Context through which the Instance IDs shall be returned, see
 *                @ref anjay_dm_emit .
 *
 * Instance listing handlers MUST always return Instance IDs in a strictly
 * ascending, sorted order. Failure to do so will result in an error being sent
 * to the LwM2M server or passed down to internal routines that called this
 * handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_list_instances_t(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_dm_list_ctx_t *ctx);

/**
 * Convenience function to use as the list_instances handler in Single Instance
 * objects.
 *
 * Implements a valid iteration that returns a single Instance ID: 0.
 */
int anjay_dm_list_instances_SINGLE(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_dm_list_ctx_t *ctx);

/**
 * A handler that shall reset Object Instance to its default (after creational)
 * state.
 *
 * Note: if this handler is not implemented, then non-partial write on the
 * Object Instance (@p iid) will not succeed.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @param iid       Instance ID to reset.
 *
 * @return This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_instance_reset_t(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid);

/**
 * A handler that removes an Object Instance with given Instance ID.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Object Instance ID.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_instance_remove_t(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid);

/**
 * A handler that creates an Object Instance.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Instance ID to create, chosen either by the server or the
 *                library. An ID that has been previously checked (using
 *                @ref anjay_dm_list_instances_t) to not be PRESENT is
 *                guaranteed to be passed.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_instance_create_t(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid);

/**
 * A handler that returns default attributes set for the Object Instance.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      iid     Checked Object Instance ID.
 * @param      ssid    Short Server ID of the server requesting the RPC.
 * @param[out] out     Returned attributes.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_read_default_attrs_t(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        anjay_dm_oi_attributes_t *out);

/**
 * A handler that sets default attributes for the Object Instance.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Object Instance ID.
 * @param ssid    Short Server ID of the server requesting the RPC.
 * @param attrs   Attributes to set for the Object Instance.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_write_default_attrs_t(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_ssid_t ssid,
        const anjay_dm_oi_attributes_t *attrs);

/**
 * A handler that enumerates SUPPORTED Resources for an Object Instance, called
 * only if the Object Instance is PRESENT (has recently been returned via
 * @ref anjay_dm_list_instances_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param ctx     Context through which the Resource IDs shall be returned, see
 *                @ref anjay_dm_emit_res .
 *
 * Resource listing handlers MUST always return Resource IDs in a strictly
 * ascending, sorted order. Failure to do so will result in an error being sent
 * to the LwM2M server or passed down to internal routines that called this
 * handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_list_resources_t(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_dm_resource_list_ctx_t *ctx);

/**
 * A handler that reads the Resource or Resource Instance value, called only if
 * the Resource is PRESENT and is one of the @ref ANJAY_DM_RES_R,
 * @ref ANJAY_DM_RES_RW, @ref ANJAY_DM_RES_RM or @ref ANJAY_DM_RES_RWM kinds (as
 * returned by @ref anjay_dm_list_resources_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param riid    Resource Instance ID, or @ref ANJAY_ID_INVALID in case of a
 *                Single Resource.
 * @param ctx     Output context to write the resource value to using the
 *                <c>anjay_ret_*</c> function family.
 *
 * NOTE: One of the <c>anjay_ret_*</c> functions <strong>MUST</strong> be called
 * in this handler before returning successfully. Failure to do so will result
 * in 5.00 Internal Server Error being sent to the server.
 *
 * NOTE: This handler will only be called with @p riid set to a valid value if
 * the Resource Instance is PRESENT (has recently been returned via
 * @ref anjay_dm_list_resource_instances_t).
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, it will be used as a hint for the CoAP response code to use. The
 *   library may decide to override the returned value in case of a more
 *   specific internal error (e.g. 4.06 Not Acceptable in response to an invalid
 *   Accept option).
 *
 *   Note that the CoAP response sent by the library will always be valid.
 *   If the value returned is a negative number that is not any of the
 *   ANJAY_ERR_ constant, the normal fallback response is
 *   5.00 Internal Server Error.
 */
typedef int
anjay_dm_resource_read_t(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid,
                         anjay_rid_t rid,
                         anjay_riid_t riid,
                         anjay_output_ctx_t *ctx);

/**
 * A handler that writes the Resource value, called only if the Resource is
 * SUPPORTED and not of the @ref ANJAY_DM_RES_E kind (as returned by
 * @ref anjay_dm_list_resources_t). Note that it may be called on nominally
 * read-only Resources if the write is performed by the Bootstrap Server.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param riid    Resource Instance ID, or @ref ANJAY_ID_INVALID in case of a
 *                Single Resource.
 * @param ctx     Input context to read the resource value from using the
 *                anjay_get_* function family.
 *
 * NOTE: This handler will only be called with @p riid set to a valid value if
 * the Resource has been verified to be a Multiple Resource (as returned by
 * @ref anjay_dm_list_resources_t).
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_resource_write_t(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          anjay_riid_t riid,
                          anjay_input_ctx_t *ctx);

/**
 * A handler that performs the Execute action on given Resource, called only if
 * the Resource is PRESENT and of the @ref ANJAY_DM_RES_E kind (as returned by
 * @ref anjay_dm_list_resources_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Execute context to read the execution arguments from, using
 * the anjay_execute_get_* function family.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_resource_execute_t(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_execute_ctx_t *ctx);

/**
 * A handler that shall reset a Resource to its default (after creational)
 * state. In particular, for any writeable optional resource, it shall remove
 * it; for any writeable mandatory Multiple Resource, it shall remove all its
 * instances.
 *
 * NOTE: If this handler is not implemented for a Multiple Resource, then
 * non-partial write on it will not succeed.
 *
 * NOTE: In the current version of Anjay, this handler is only ever called on
 * Multiple Resources. It is REQUIRED so that after calling this handler, any
 * Multiple Resource is either not PRESENT, or PRESENT, but contain zero
 * Resource Instances.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @param iid       Object Instance ID.
 * @param rid       ID of the Resource to reset.
 *
 * @return This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_resource_reset_t(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid,
                          anjay_rid_t rid);

/**
 * A handler that enumerates all Resource Instances of a Multiple Resource,
 * called only if the Resource is PRESENT and is of either @ref ANJAY_DM_RES_RM,
 * @ref ANJAY_DM_RES_WM or @ref ANJAY_DM_RES_RWM kind (as returned by
 * @ref anjay_dm_list_resources_t).
 *
 * The library will not attempt to call @ref anjay_dm_resource_write_t or
 * @ref anjay_dm_resource_reset_t handlers inside the @ref anjay_dm_emit calls
 * performed from this handler, so the implementation is free to use iteration
 * state that would be invalidated by such calls.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Context through which the Resource Instance IDs shall be
 *                returned, see @ref anjay_dm_emit .
 *
 * Resource instance listing handlers MUST always return Resource Instance IDs
 * in a strictly ascending, sorted order. Failure to do so will result in an
 * error being sent to the LwM2M server or passed down to internal routines that
 * called this handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_list_resource_instances_t(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_dm_list_ctx_t *ctx);

/**
 * A handler that returns Resource attributes.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      iid     Object Instance ID.
 * @param      rid     Resource ID.
 * @param      ssid    Short Server ID of the LwM2M Server issuing the request.
 * @param[out] out     Returned Resource attributes.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_resource_read_attrs_t(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_ssid_t ssid,
                               anjay_dm_r_attributes_t *out);

/**
 * A handler that sets attributes for given Resource.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ssid    Short Server ID of the LwM2M Server issuing the request.
 * @param attrs   Attributes to set for this Resource.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_resource_write_attrs_t(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_ssid_t ssid,
                                const anjay_dm_r_attributes_t *attrs);

/**
 * A handler that is called when there is a request that might modify an Object
 * and fail. Such situation often requires to rollback changes, and this handler
 * shall implement logic that prepares for possible failure in the future.
 *
 * Handlers listed below are NOT called without beginning transaction in the
 * first place (note that if an Object does not implement transaction handlers,
 * then it will not be possible to perform operations listed below):
 *  - @ref anjay_dm_instance_create_t
 *  - @ref anjay_dm_instance_remove_t
 *  - @ref anjay_dm_instance_reset_t
 *  - @ref anjay_dm_resource_write_t
 *  - @ref anjay_dm_resource_reset_t
 *  - @ref anjay_dm_transaction_commit_t
 *  - @ref anjay_dm_transaction_rollback_t
 *
 * Note: if an error occurs during a transaction (i.e. after successful call of
 * this function) then the rollback handler @ref anjay_dm_transaction_rollback_t
 * will be executed by the library.
 *
 * @param anjay     Anjay object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 *
 * @return
 * - 0 on success
 * - a negative value in case of error
 */
typedef int
anjay_dm_transaction_begin_t(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called after transaction is finished, but before
 * @ref anjay_dm_transaction_commit_t is called. It is used to check whether the
 * commit operation may be successfully performed.
 *
 * Any validation of the object's state shall be performed in this function,
 * rather than in the commit handler. If there is a need to commit changes to
 * multiple objects at once, this handler is called on all modified objects
 * first, to avoid potential inconsistencies that may arise from a failing
 * commit operation.
 *
 * Returning success from this handler means that the corresponding commit
 * function shall subsequently execute successfully. The commit handler may
 * nevertheless fail, but if and only if a fatal, unpredictable and
 * irrecoverable error (e.g. physical write error) occurs.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return
 * - 0 on success
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_transaction_validate_t(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called after transaction is finished. If it fails then
 * @ref anjay_dm_transaction_rollback_t handler must be called by the user
 * code if it is necessary.
 *
 * NOTE: If this function fails, the data model will be left in an inconsistent
 * state. For this reason, it may return an error value if and only if a fatal,
 * unpredictable and irrecoverable error (e.g. physical write error) occurs.
 * All other errors (such as invalid object state) shall be reported via
 * @ref anjay_dm_transaction_validate_t .
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return
 * - 0 on success
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_transaction_commit_t(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Stub handler that can be substituted for any transaction operation. Does
 * nothing. It is <strong>NOT</strong> recommended for production usage.
 *
 * @return always 0
 */
int anjay_dm_transaction_NOOP(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called whenever there is a need to restore previous
 * Object state during a transaction or during committing a transaction.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return
 * - 0 on success
 * - a negative value in case of error.
 */
typedef int
anjay_dm_transaction_rollback_t(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr);

/** A struct containing pointers to Object handlers. */
typedef struct {
    /**
     * Get default Object attributes, @ref anjay_dm_object_read_default_attrs_t
     *
     * Required for handling *LwM2M Discover* and *LwM2M Observe* operations.
     *
     * Can be NULL when *Attribute Storage* module is installed. Non-NULL
     * handler overrides *Attribute Storage* logic.
     */
    anjay_dm_object_read_default_attrs_t *object_read_default_attrs;

    /**
     * Set default Object attributes,
     * @ref anjay_dm_object_write_default_attrs_t
     *
     * Required for handling *LwM2M Write-Attributes* operation.
     *
     * Can be NULL when *Attribute Storage* module is installed. Non-NULL
     * handler overrides *Attribute Storage* logic.
     */
    anjay_dm_object_write_default_attrs_t *object_write_default_attrs;

    /**
     * Enumerate available Object Instances, @ref anjay_dm_list_instances_t
     *
     * Required for every LwM2M operation.
     *
     * **Must not be NULL.** @ref anjay_dm_list_instances_SINGLE can be used
     * here.
     */
    anjay_dm_list_instances_t *list_instances;

    /**
     * Resets an Object Instance, @ref anjay_dm_instance_reset_t
     *
     * Required for handling *LwM2M Write* operation in *replace mode*.
     *
     * Can be NULL if the object does not contain writable resources.
     */
    anjay_dm_instance_reset_t *instance_reset;

    /**
     * Create an Object Instance, @ref anjay_dm_instance_create_t
     *
     * Required for handling *LwM2M Create* operation.
     *
     * Can be NULL for single instance objects.
     */
    anjay_dm_instance_create_t *instance_create;

    /**
     * Delete an Object Instance, @ref anjay_dm_instance_remove_t
     *
     * Required for handling *LwM2M Delete* operation.
     *
     * Can be NULL for single instance objects.
     */
    anjay_dm_instance_remove_t *instance_remove;

    /**
     * Get default Object Instance attributes,
     * @ref anjay_dm_instance_read_default_attrs_t
     *
     * Required for handling *LwM2M Discover* and *LwM2M Observe* operations.
     *
     * Can be NULL when *Attribute Storage* module is installed. Non-NULL
     * handler overrides *Attribute Storage* logic.
     */
    anjay_dm_instance_read_default_attrs_t *instance_read_default_attrs;

    /**
     * Set default Object Instance attributes,
     * @ref anjay_dm_instance_write_default_attrs_t
     *
     * Required for handling *LwM2M Write-Attributes* operation.
     *
     * Can be NULL when *Attribute Storage* module is installed. Non-NULL
     * handler overrides *Attribute Storage* logic.
     */
    anjay_dm_instance_write_default_attrs_t *instance_write_default_attrs;

    /**
     * Enumerate PRESENT Resources in a given Object Instance,
     * @ref anjay_dm_list_resources_t
     *
     * Required for every LwM2M operation.
     *
     * **Must not be NULL.**
     */
    anjay_dm_list_resources_t *list_resources;

    /**
     * Get Resource value, @ref anjay_dm_resource_read_t
     *
     * Required for *LwM2M Read* operation.
     *
     * Can be NULL if the object does not contain readable resources.
     */
    anjay_dm_resource_read_t *resource_read;

    /**
     * Set Resource value, @ref anjay_dm_resource_write_t
     *
     * Required for *LwM2M Write* operation.
     *
     * Can be NULL if the object does not contain writable resources.
     */
    anjay_dm_resource_write_t *resource_write;

    /**
     * Perform Execute action on a Resource, @ref anjay_dm_resource_execute_t
     *
     * Required for *LwM2M Execute* operation.
     *
     * Can be NULL if the object does not contain executable resources.
     */
    anjay_dm_resource_execute_t *resource_execute;

    /**
     * Remove all Resource Instances from a Multiple Resource,
     * @ref anjay_dm_resource_reset_t
     *
     * Required for *LwM2M Write* operation performed on multiple-instance
     * resources.
     *
     * Can be NULL if the object does not contain multiple writable resources.
     */
    anjay_dm_resource_reset_t *resource_reset;

    /**
     * Enumerate available Resource Instances,
     * @ref anjay_dm_list_resource_instances_t
     *
     * Required for *LwM2M Read*, *LwM2M Write* and *LwM2M Discover* operations
     * performed on multiple-instance resources..
     *
     * Can be NULL if the object does not contain multiple resources.
     */
    anjay_dm_list_resource_instances_t *list_resource_instances;

    /**
     * Get Resource attributes, @ref anjay_dm_resource_read_attrs_t
     *
     * Required for handling *LwM2M Discover* and *LwM2M Observe* operations.
     *
     * Can be NULL when *Attribute Storage* module is installed. Non-NULL
     * handler overrides *Attribute Storage* logic.
     */
    anjay_dm_resource_read_attrs_t *resource_read_attrs;

    /**
     * Set Resource attributes, @ref anjay_dm_resource_write_attrs_t
     *
     * Required for handling *LwM2M Write-Attributes* operation.
     *
     * Can be NULL when *Attribute Storage* module is installed. Non-NULL
     * handler overrides *Attribute Storage* logic.
     */
    anjay_dm_resource_write_attrs_t *resource_write_attrs;

    /**
     * Begin a transaction on this Object, @ref anjay_dm_transaction_begin_t
     *
     * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
     * or *LwM2M Delete*.
     *
     * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
     * used here.
     */
    anjay_dm_transaction_begin_t *transaction_begin;

    /**
     * Validate whether a transaction on this Object can be cleanly committed.
     * See @ref anjay_dm_transaction_validate_t
     *
     * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
     * or *LwM2M Delete*.
     *
     * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
     * used here.
     */
    anjay_dm_transaction_validate_t *transaction_validate;

    /**
     * Commit changes made in a transaction, @ref anjay_dm_transaction_commit_t
     *
     * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
     * or *LwM2M Delete*.
     *
     * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
     * used here.
     */
    anjay_dm_transaction_commit_t *transaction_commit;

    /**
     * Rollback changes made in a transaction,
     * @ref anjay_dm_transaction_rollback_t
     *
     * Required for handling modifying operation: *LwM2M Write*, *LwM2M Create*
     * or *LwM2M Delete*.
     *
     * Can be NULL for read-only objects. @ref anjay_dm_transaction_NOOP can be
     * used here.
     */
    anjay_dm_transaction_rollback_t *transaction_rollback;

} anjay_dm_handlers_t;

/** A struct defining a LwM2M Object. */
struct anjay_dm_object_def_struct {
    /** Object ID; MUST not be <c>ANJAY_ID_INVALID</c> (65535) */
    anjay_oid_t oid;

    /**
     * Object version: a string with static lifetime, containing two digits
     * separated by a dot (for example: "1.1").
     * If left NULL, client will not include the "ver=" attribute in Register
     * and Discover messages, which implies version 1.0.
     */
    const char *version;

    /** Handler callbacks for this object. */
    anjay_dm_handlers_t handlers;
};

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
 * @param anjay Anjay object to operate on.
 * @param oid   Object ID of the changed Resource.
 * @param iid   Object Instance ID of the changed Resource.
 * @param rid   Resource ID of the changed Resource.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_notify_changed(anjay_t *anjay,
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
 * @param anjay Anjay object to operate on.
 * @param oid   Object ID of the changed Object.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_notify_instances_changed(anjay_t *anjay, anjay_oid_t oid);

/**
 * Structure representing an observation state of a Resource.
 */
typedef struct {
    /**
     * Informs whether a given Resource is observed (by any server) or not.
     */
    bool is_observed;
    /**
     * The minimum effective value (in seconds) of the <c>pmin</c> attribute for
     * a given Resource. The value of this field equals 0 if <c>pmin</c> wasn't
     * set for any server or <c>is_observed</c> is false.
     */
    int32_t min_period;
    /**
     * The minimum effective value (in seconds) of the <c>epmax</c> attribute
     * for a given Resource. The value of this field equals @ref
     * ANJAY_ATTRIB_PERIOD_NONE if <c>epmax</c> wasn't set for any server or
     * <c>is_observed</c> is false.
     */
    int32_t max_eval_period;
} anjay_resource_observation_status_t;

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
anjay_resource_observation_status_t anjay_resource_observation_status(
        anjay_t *anjay, anjay_oid_t oid, anjay_iid_t iid, anjay_rid_t rid);

/**
 * Registers the Object in the data model, making it available for RPC calls.
 *
 * NOTE: <c>def_ptr</c> MUST stay valid up to and including the corresponding
 * @ref anjay_delete or @ref anjay_unregister_object call.
 *
 * @param anjay   Anjay object to operate on.
 * @param def_ptr Pointer to the Object definition struct. The exact value
 *                passed to this function will be forwarded to all data model
 *                handler calls.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_register_object(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *def_ptr);

/**
 * Unregisters an Object in the data model, so that it is no longer available
 * for RPC calls.
 *
 * <c>def_ptr</c> MUST be a pointer previously passed to
 * @ref anjay_register_object for the same <c>anjay</c> object.
 *
 * After a successful unregister, any resources used by the actual object may be
 * safely freed up.
 *
 * NOTE: This function MUST NOT be called from within any data model handler
 * callback function (i.e. any of the @ref anjay_dm_handlers_t members). Doing
 * so is undefined behavior.
 *
 * @param anjay   Anjay object to operate on.
 * @param def_ptr Pointer to the Object definition struct.
 *
 * @returns 0 on success, a negative value if <c>def_ptr</c> does not correspond
 *          to any known registered object.
 */
int anjay_unregister_object(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *def_ptr);

/**
 * Checks whether the passed string is a valid LwM2M Binding Mode.
 *
 * @return true for <c>"U"</c>, <c>"S"</c>, <c>"US"</c>, <c>"UQ"</c>,
 *         <c>"SQ"</c>, <c>"UQS"</c>, false in any other case.
 */
bool anjay_binding_mode_valid(const char *binding_mode);

/**
 * Possible values of the Security Mode Resource, as described in the Security
 * Object definition.
 */
typedef enum {
    ANJAY_SECURITY_PSK = 0,         //< Pre-Shared Key mode
    ANJAY_SECURITY_RPK = 1,         //< Raw Public Key mode
    ANJAY_SECURITY_CERTIFICATE = 2, //< Certificate mode
    ANJAY_SECURITY_NOSEC = 3,       //< NoSec mode
    ANJAY_SECURITY_EST = 4          //< Certificate mode with EST
} anjay_security_mode_t;

/**
 * Possible values of the SMS Security Mode Resource, as described in the
 * Security Object definition.
 */
typedef enum {
    ANJAY_SMS_SECURITY_DTLS_PSK = 1,      //< DTLS in PSK mode
    ANJAY_SMS_SECURITY_SECURE_PACKET = 2, //< Secure Packet Structure
    ANJAY_SMS_SECURITY_NOSEC = 3          //< NoSec mode
} anjay_sms_security_mode_t;

#define ANJAY_ACCESS_MASK_READ (1U << 0)
#define ANJAY_ACCESS_MASK_WRITE (1U << 1)
#define ANJAY_ACCESS_MASK_EXECUTE (1U << 2)
#define ANJAY_ACCESS_MASK_DELETE (1U << 3)
#define ANJAY_ACCESS_MASK_CREATE (1U << 4)
// clang-format off
#define ANJAY_ACCESS_MASK_FULL   \
    (ANJAY_ACCESS_MASK_READ      \
     | ANJAY_ACCESS_MASK_WRITE   \
     | ANJAY_ACCESS_MASK_DELETE  \
     | ANJAY_ACCESS_MASK_EXECUTE \
     | ANJAY_ACCESS_MASK_CREATE)
// clang-format on
#define ANJAY_ACCESS_MASK_NONE 0
#define ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP UINT16_MAX

typedef uint16_t anjay_access_mask_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_DM_H*/
