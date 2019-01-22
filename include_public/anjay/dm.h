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

#ifndef ANJAY_INCLUDE_ANJAY_DM_H
#define ANJAY_INCLUDE_ANJAY_DM_H

#include <math.h>
#include <stdint.h>

#include <anjay/io.h>

#ifdef __cplusplus
#    if __cplusplus >= 201103L
#        include <tuple> // for ANJAY_DM_SUPPORTED_RIDS
#    endif

extern "C" {
#endif

typedef struct anjay_dm_object_def_struct anjay_dm_object_def_t;

/** Object/Object Instance Attributes */
typedef struct {
    int32_t min_period; //< Minimum Period as defined by LwM2M spec
    int32_t max_period; //< Maximum Period as defined by LwM2M spec
} anjay_dm_attributes_t;

/** Resource attributes. */
typedef struct {
    /** Attributes shared with Objects/Object Instances */
    anjay_dm_attributes_t common;
    /** Greater Than attribute as defined by LwM2M spec */
    double greater_than;
    /** Less Than attribute as defined by LwM2M spec */
    double less_than;
    /** Step attribute as defined by LwM2M spec */
    double step;
} anjay_dm_resource_attributes_t;

/** A value indicating that the Min/Max Period attribute is not set */
#define ANJAY_ATTRIB_PERIOD_NONE (-1)

/** A value indicating that the Less Than/Greater Than/Step attribute
 * is not set */
#define ANJAY_ATTRIB_VALUE_NONE (NAN)

/** Convenience Object/Object Instance attributes constant, filled with
 * "attribute not set" values */
extern const anjay_dm_attributes_t ANJAY_DM_ATTRIBS_EMPTY;

/** Convenience Resource attributes constant, filled with
 * "attribute not set" values */
extern const anjay_dm_resource_attributes_t ANJAY_RES_ATTRIBS_EMPTY;

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
        anjay_dm_attributes_t *out);

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
        const anjay_dm_attributes_t *attrs);

/**
 * A handler that enumerates all Object Instances for the Object.
 *
 * Example usage:
 * @code
 * static int instance_it_handler(anjay_t *anjay,
 *                                const anjay_dm_object_def_t *const *obj_ptr,
 *                                anjay_iid_t *out,
 *                                void **cookie) {
 *     // assuming there are NUM_INSTANCES Object Instances with consecutive
 *     // Instance IDs, starting with 0
 *
 *     int curr_iid = (int)(intptr_t)*cookie;
 *     if (curr_iid >= NUM_INSTANCES) {
 *         *out = ANJAY_IID_INVALID;
 *     } else {
 *         *out = (anjay_iid_t)curr_iid;
 *         *cookie = (void*)(intptr_t)(curr_iid + 1);
 *     }
 *     return 0;
 * }
 * @endcode
 *
 * @param        anjay   Anjay object to operate on.
 * @param        obj_ptr Object definition pointer, as passed to
 *                       @ref anjay_register_object .
 * @param[out]   out     Instance ID of the next valid Object Instance
 *                       or ANJAY_IID_INVALID when there are no more instances.
 * @param[inout] cookie  Opaque pointer that may be used to store iteration
 *                       state. At first call to the handler it is set to NULL.
 *                       Since the iteration may be stopped at any point, the
 *                       value stored in @p cookie must not require cleanup.
 *                       The implementation is free to invalidate any such
 *                       cookies during a call to
 *                       @ref anjay_dm_instance_remove_t or
 *                       @ref anjay_dm_instance_create_t - so it is legal to use
 *                       the cookie to store e.g. an array index cast to a
 *                       pointer type.
 *
 * During the same call to Anjay library from top-level user code (e.g.
 * @ref anjay_sched_run or @ref anjay_serve), subsequent or concurrent
 * iterations on the same Object MUST return the Instance IDs in the same order,
 * provided that the set of existing Instances did not change in between.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_it_t(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t *out,
                                   void **cookie);

/**
 * Convenience function to use as the instance_it handler in Single Instance
 * objects.
 *
 * Implements a valid iteration that returns a single Instance ID: 0.
 */
int anjay_dm_instance_it_SINGLE(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t *out,
                                void **cookie);

/**
 * A handler that checks if an Object Instance with given Instance ID exists.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Object Instance ID.
 *
 * @returns This handler should return:
 * - 1 if the Object Instance exists,
 * - 0 if the Object Instance does not exist,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_instance_present_t(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid);

/**
 * Convenience function to use as the instance_present handler in Single
 * Instance objects.
 *
 * @returns 1 (true) if <c>iid == 0</c>, 0 (false) otherwise.
 */
int anjay_dm_instance_present_SINGLE(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid);

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
 * The library will not attempt to interleave calls to
 * @ref anjay_dm_instance_it_t with deleting instances, so the implementation
 * is free to invalidate any iteration cookies during instance removal.
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
 * The library will not attempt to interleave calls to
 * @ref anjay_dm_instance_it_t with creating instances, so the implementation
 * is free to invalidate any iteration cookies during instance creation.
 *
 * @param        anjay     Anjay object to operate on.
 * @param        obj_ptr   Object definition pointer, as passed to
 *                         @ref anjay_register_object .
 * @param[inout] inout_iid Instance ID to create if it was specified by the
 *                         server or @ref ANJAY_IID_INVALID otherwise.
 *                         The handler should set this value to an ID of the
 *                         created Object Instance.
 * @param        ssid      Short Server ID of the Object Instance creator.
 *                         @ref ANJAY_SSID_BOOTSTRAP may be used when an
 *                         instance is created by the Bootstrap Write request.
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
                           anjay_iid_t *inout_iid,
                           anjay_ssid_t ssid);

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
        anjay_dm_attributes_t *out);

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
        const anjay_dm_attributes_t *attrs);

/**
 * A handler that checks if a Resource has been instantiated in Object Instance,
 * called only if Resource is SUPPORTED (see @ref anjay_dm_supported_rids_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Instance ID.
 * @param rid     Checked Resource ID.
 *
 * @returns This handler should return:
 * - 1 if the Resource is present,
 * - 0 if the Resource is not present,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int
anjay_dm_resource_present_t(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid);

/**
 * Convenience function to use as the resource_present handler in objects that
 * implement all possible Resources.
 *
 * @returns Always 1.
 */
int anjay_dm_resource_present_TRUE(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid);

typedef enum {
    ANJAY_DM_RESOURCE_OP_BIT_R = (1 << 0),
    ANJAY_DM_RESOURCE_OP_BIT_W = (1 << 1),
    ANJAY_DM_RESOURCE_OP_BIT_E = (1 << 2)
} anjay_dm_resource_op_bit_t;

typedef uint16_t anjay_dm_resource_op_mask_t;

#define ANJAY_DM_RESOURCE_OP_NONE ((anjay_dm_resource_op_mask_t) 0)

/**
 * A handler that returns supported non-Bootstrap operations by the client's
 * implementation for a specified Resource.
 *
 * Note: If not implemented, all operations on resource are supported.
 *
 * @param anjay     Anjay object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @param rid       Resource being queried.
 * @param out       Combination of @ref anjay_dm_resource_op_bit_t or
 *                  @ref ANJAY_DM_RESOURCE_OP_NONE if no operation is supported.
 * @return This handler should return:
 * - 0 if it can be queried
 * - a negative value if for some reason resource cannot be queried. If it
 *   returns one of ANJAY_ERR_* constants, the response message will have an
 *   appropriate CoAP response code. Otherwise, the device will respond with
 *   an unspecified (but valid) error code.
 */
typedef int
anjay_dm_resource_operations_t(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_rid_t rid,
                               anjay_dm_resource_op_mask_t *out);

/**
 * A handler that reads the Resource value, called only if the Resource is
 * PRESENT (see @ref anjay_dm_resource_present_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Output context to write the resource value to using the
 *                <c>anjay_ret_*</c> function family.
 *
 * NOTE: One of the <c>anjay_ret_*</c> functions <strong>MUST</strong> be called
 * in this handler before returning successfully. Failure to do so will result
 * in 5.00 Internal Server Error being sent to the server.
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
                         anjay_output_ctx_t *ctx);

/**
 * A handler that writes the Resource value.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Input context to read the resource value from using the
 *                anjay_get_* function family.
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
                          anjay_input_ctx_t *ctx);

/**
 * A handler that performs the Execute action on given Resource.
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

#define ANJAY_DM_DIM_INVALID ANJAY_ERR_NOT_IMPLEMENTED

/**
 * A handler that returns number of instances in a Multiple Resource.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      iid     Object Instance ID.
 * @param      rid     Resource ID.
 *
 * @returns This handler should return:
 *
 * - @ref ANJAY_DM_DIM_INVALID if the queried Resource is not a
 *   Multiple Resource, or querying its size is not supported,
 * - a non-negative value equal to the number of instances on success,
 * - a negative value in case of a fatal error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_dim_t(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid);

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
                               anjay_dm_resource_attributes_t *out);

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
                                const anjay_dm_resource_attributes_t *attrs);

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
     */
    anjay_dm_object_read_default_attrs_t *object_read_default_attrs;
    /**
     * Set default Object attributes,
     * @ref anjay_dm_object_write_default_attrs_t
     */
    anjay_dm_object_write_default_attrs_t *object_write_default_attrs;

    /** Enumerate available Object Instances, @ref anjay_dm_instance_it_t */
    anjay_dm_instance_it_t *instance_it;
    /** Check if an Object Instance exists, @ref anjay_dm_instance_present_t */
    anjay_dm_instance_present_t *instance_present;

    /** Resets an Object Instance, @ref anjay_dm_instance_reset_t */
    anjay_dm_instance_reset_t *instance_reset;
    /** Create an Object Instance, @ref anjay_dm_instance_create_t */
    anjay_dm_instance_create_t *instance_create;
    /** Delete an Object Instance, @ref anjay_dm_instance_remove_t */
    anjay_dm_instance_remove_t *instance_remove;

    /**
     * Get default Object Instance attributes,
     * @ref anjay_dm_instance_read_default_attrs_t
     */
    anjay_dm_instance_read_default_attrs_t *instance_read_default_attrs;
    /**
     * Set default Object Instance attributes,
     * @ref anjay_dm_instance_write_default_attrs_t
     */
    anjay_dm_instance_write_default_attrs_t *instance_write_default_attrs;

    /**
     * Check if a Resource is present in given Object Instance,
     * @ref anjay_dm_resource_present_t
     */
    anjay_dm_resource_present_t *resource_present;
    /**
     * Returns a mask of supported operations on a given Resource,
     * @ref anjay_dm_resource_operations_t
     */
    anjay_dm_resource_operations_t *resource_operations;

    /** Get Resource value, @ref anjay_dm_resource_read_t */
    anjay_dm_resource_read_t *resource_read;
    /** Set Resource value, @ref anjay_dm_resource_write_t */
    anjay_dm_resource_write_t *resource_write;
    /**
     * Perform Execute action on a Resource, @ref anjay_dm_resource_execute_t
     */
    anjay_dm_resource_execute_t *resource_execute;

    /**
     * Get number of Multiple Resource instances, @ref anjay_dm_resource_dim_t
     */
    anjay_dm_resource_dim_t *resource_dim;
    /** Get Resource attributes, @ref anjay_dm_resource_read_attrs_t */
    anjay_dm_resource_read_attrs_t *resource_read_attrs;
    /** Set Resource attributes, @ref anjay_dm_resource_write_attrs_t */
    anjay_dm_resource_write_attrs_t *resource_write_attrs;

    /** Begin a transaction on this Object, @ref anjay_dm_transaction_begin_t */
    anjay_dm_transaction_begin_t *transaction_begin;
    /**
     * Validate whether a transaction on this Object can be cleanly committed.
     * See @ref anjay_dm_transaction_validate_t
     */
    anjay_dm_transaction_validate_t *transaction_validate;
    /**
     * Commit changes made in a transaction, @ref anjay_dm_transaction_commit_t
     */
    anjay_dm_transaction_commit_t *transaction_commit;
    /**
     * Rollback changes made in a transaction,
     * @ref anjay_dm_transaction_rollback_t
     */
    anjay_dm_transaction_rollback_t *transaction_rollback;
} anjay_dm_handlers_t;

/** A simple array-plus-size container for a list of supported Resource IDs. */
typedef struct {
    /** Number of element in the array */
    size_t count;
    /**
     * Pointer to an array of Resource IDs supported by the object. A Resource
     * is considered SUPPORTED if it may ever be present within the Object. The
     * array MUST be exactly <c>count</c> elements long and sorted in strictly
     * ascending order.
     */
    const uint16_t *rids;
} anjay_dm_supported_rids_t;

#if defined(__cplusplus) && __cplusplus >= 201103L
#    define ANJAY_DM_SUPPORTED_RIDS(...)                                \
        {                                                               \
            ::std::tuple_size<decltype(                                 \
                    ::std::make_tuple(__VA_ARGS__))>::value,            \
                    []() -> const uint16_t * {                          \
                        static const uint16_t rids[] = { __VA_ARGS__ }; \
                        return rids;                                    \
                    }()                                                 \
        }
#else // __cplusplus
/**
 * Convenience macro for initializing @ref anjay_dm_supported_rids_t objects.
 *
 * The parameters shall compose a properly sorted list of supported Resource
 * IDs. The result of the macro is an initializer list suitable for initializing
 * an object of type <c>anjay_dm_supported_rids_t</c>, like for example the
 * <c>supported_rids</c> field of @ref anjay_dm_object_def_t. The <c>count</c>
 * field will be automatically calculated.
 */
#    define ANJAY_DM_SUPPORTED_RIDS(...)                                   \
        {                                                                  \
            sizeof((const uint16_t[]) { __VA_ARGS__ }) / sizeof(uint16_t), \
                    (const uint16_t[]) {                                   \
                __VA_ARGS__                                                \
            }                                                              \
        }
#endif // __cplusplus

/** A struct defining an LwM2M Object. */
struct anjay_dm_object_def_struct {
    /** Object ID */
    anjay_oid_t oid;

    /**
     * Object version: a string with static lifetime, containing two digits
     * separated by a dot (for example: "1.1").
     * If left NULL, client will not include the "ver=" attribute in Register
     * and Discover messages, which implies version 1.0.
     */
    const char *version;

    /**
     * List of Resource IDs supported by the object. The
     * @ref ANJAY_DM_SUPPORTED_RIDS macro is the preferred way of initializing
     * it.
     */
    anjay_dm_supported_rids_t supported_rids;

    /** Handler callbacks for this object. */
    anjay_dm_handlers_t handlers;
};

/**
 * Notifies the library that the value of given Resource changed. It may trigger
 * an LwM2M Notify message, update server connections and perform other tasks,
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
 * changed. It may trigger an LwM2M Notify message, update server connections
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
    ANJAY_UDP_SECURITY_PSK = 0,         //< Pre-Shared Key mode
    ANJAY_UDP_SECURITY_RPK = 1,         //< Raw Public Key mode
    ANJAY_UDP_SECURITY_CERTIFICATE = 2, //< Certificate mode
    ANJAY_UDP_SECURITY_NOSEC = 3        //< NoSec mode
} anjay_udp_security_mode_t;

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
