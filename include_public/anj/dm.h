/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_DM_H
#define ANJAY_DM_DM_H

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/dm_io.h>

typedef struct dm_object_def_struct dm_object_def_t;

/**
 * Installed object struct. It is used only to allocate memory
 * before @ref dm_initialize call.
 *
 * WARNING: Arrays allocated with this type must remain valid
 * throughout the entire usage of the data model. This type is
 * not intended for direct user use.
 *
 * NOTE: refer to description of @ref dm_initialize for more
 * information and usage example.
 */
typedef struct {
    /**
     * Object definition pointer.
     */
    const dm_object_def_t *const *def;
} dm_installed_object_t;

/** data model object which stores registered LwM2M objects. */
typedef struct dm {
    dm_installed_object_t *objects;
    size_t objects_count;
    size_t objects_count_max;
} dm_t;

/**
 * A handler that enumerates all Object Instances for the Object.
 *
 * @param dm      data model to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref dm_register_object .
 * @param ctx     Context through which the Instance IDs shall be returned, see
 *                @ref dm_emit .
 *
 * Instance listing handlers MUST always return Instance IDs in a strictly
 * ascending, sorted order. Failure to do so will result in an error being sent
 * to the LwM2M server or passed down to internal routines that called this
 * handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - an negative value or one of FLUF_COAP_CODE_ constants in case of error.
 */
typedef int dm_list_instances_t(dm_t *dm,
                                const dm_object_def_t *const *obj_ptr,
                                dm_list_ctx_t *ctx);

/**
 * Convenience function to use as the list_instances handler in Single Instance
 * objects.
 *
 * Implements a valid iteration that returns a single Instance ID: 0.
 */
int dm_list_instances_SINGLE(dm_t *dm,
                             const dm_object_def_t *const *obj_ptr,
                             dm_list_ctx_t *ctx);

/**
 * A handler that enumerates SUPPORTED Resources for an Object Instance, called
 * only if the Object Instance is PRESENT (has recently been returned via
 * @ref dm_list_instances_t).
 *
 * CAUTION: The library MAY call other data model handlers for the same Object
 * from within the @ref dm_emit_res call. Please make sure that your code
 * is able to handle this - e.g. avoid calling @ref dm_emit_res with
 * a non-recursive object-scope mutex locked.
 *
 * @param dm      data model to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref dm_register_object .
 * @param iid     Object Instance ID.
 * @param ctx     Context through which the Resource IDs shall be returned, see
 *                @ref dm_emit_res .
 *
 * Resource listing handlers MUST always return Resource IDs in a strictly
 * ascending, sorted order. Failure to do so will result in an error being sent
 * to the LwM2M server or passed down to internal routines that called this
 * handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - an negative value or one of FLUF_COAP_CODE_ constants in case of error.
 */
typedef int dm_list_resources_t(dm_t *dm,
                                const dm_object_def_t *const *obj_ptr,
                                fluf_iid_t iid,
                                dm_resource_list_ctx_t *ctx);

/**
 * A handler that reads the Resource or Resource Instance value, called only if
 * the Resource is PRESENT and is one of the @ref DM_RES_R,
 * @ref DM_RES_RW, @ref DM_RES_RM, @ref DM_RES_RWM or @ref DM_RES_BS_RW kinds
 * (as returned by @ref dm_list_resources_t).
 *
 * @param dm      data model to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref dm_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param riid    Resource Instance ID, or @ref FLUF_ID_INVALID in case of a
 *                Single Resource.
 * @param ctx     Output context to write the resource value to using the
 *                <c>dm_ret_*</c> function family.
 *
 * NOTE: One of the <c>dm_ret_*</c> functions <strong>MUST</strong> be called
 * in this handler before returning successfully. Failure to do so will result
 * in 5.00 Internal Server Error being sent to the server.
 *
 * NOTE: This handler will only be called with @p riid set to a valid value if
 * the Resource Instance is PRESENT (has recently been returned via
 * @ref dm_list_resource_instances_t).
 *
 * @returns This handler should return:
 * - 0 on success,
 * - an negative value or one of FLUF_COAP_CODE_ constants in case of error.
 */
typedef int dm_resource_read_t(dm_t *dm,
                               const dm_object_def_t *const *obj_ptr,
                               fluf_iid_t iid,
                               fluf_rid_t rid,
                               fluf_riid_t riid,
                               dm_output_ctx_t *ctx);

/**
 * A handler that writes the Resource value, called only if the Resource is
 * SUPPORTED and not of the @ref DM_RES_E kind (as returned by
 * @ref dm_list_resources_t).
 *
 * @param dm      data model object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref dm_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param riid    Resource Instance ID, or @ref FLUF_ID_INVALID in case of a
 *                Single Resource.
 * @param ctx     Input context to read the resource value from using the
 *                dm_get_* function family.
 *
 * NOTE: This handler will only be called with @p riid set to a valid value if
 * the Resource has been verified to be a Multiple Resource (as returned by
 * @ref dm_list_resources_t).
 *
 * @returns This handler should return:
 * - 0 on success,
 * - an negative value or one of FLUF_COAP_CODE_ constants in case of error.
 */
typedef int dm_resource_write_t(dm_t *dm,
                                const dm_object_def_t *const *obj_ptr,
                                fluf_iid_t iid,
                                fluf_rid_t rid,
                                fluf_riid_t riid,
                                dm_input_ctx_t *ctx);

/**
 * A handler that performs the Execute action on given Resource, called only if
 * the Resource is PRESENT and of the @ref DM_RES_E kind (as returned by
 * @ref dm_list_resources_t).
 *
 * @param dm      data model object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref dm_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Execute context to read the execution arguments from, using
 * the dm_execute_get_* function family.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - an negative value or one of FLUF_COAP_CODE_ constants in case of error.
 */
typedef int dm_resource_execute_t(dm_t *dm,
                                  const dm_object_def_t *const *obj_ptr,
                                  fluf_iid_t iid,
                                  fluf_rid_t rid,
                                  dm_execute_ctx_t *ctx);

/**
 * A handler that enumerates all Resource Instances of a Multiple Resource,
 * called only if the Resource is PRESENT and is of either @ref DM_RES_RM,
 * @ref DM_RES_WM or @ref DM_RES_RWM kind (as returned by
 * @ref dm_list_resources_t).
 *
 * The library will not attempt to call @ref dm_resource_write_t inside the
 * @ref dm_emit calls performed from this handler, so the implementation
 * is free to use iteration state that would be invalidated by such calls.
 *
 * CAUTION: Aside from the note above, the library MAY call other data model
 * handlers for the same Object from within the @ref dm_emit call. Please
 * make sure that your code is able to handle this - e.g. avoid calling
 * @ref dm_emit with a non-recursive object-scope mutex locked.
 *
 * @param dm      data model object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Context through which the Resource Instance IDs shall be
 *                returned, see @ref dm_emit .
 *
 * Resource instance listing handlers MUST always return Resource Instance IDs
 * in a strictly ascending, sorted order. Failure to do so will result in an
 * error being sent to the LwM2M server or passed down to internal routines that
 * called this handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - an negative value or one of FLUF_COAP_CODE_ constants in case of error.
 */
typedef int dm_list_resource_instances_t(dm_t *dm,
                                         const dm_object_def_t *const *obj_ptr,
                                         fluf_iid_t iid,
                                         fluf_rid_t rid,
                                         dm_list_ctx_t *ctx);

/** A struct containing pointers to Object handlers. */
typedef struct {
    /**
     * Enumerate available Object Instances, @ref dm_list_instances_t
     *
     * Required for every LwM2M operation.
     *
     * **Must not be NULL.** @ref dm_list_instances_SINGLE can be used
     * here.
     */
    dm_list_instances_t *list_instances;

    /**
     * Enumerate PRESENT Resources in a given Object Instance,
     * @ref dm_list_resources_t
     *
     * Required for every LwM2M operation.
     *
     * **Must not be NULL.**
     */
    dm_list_resources_t *list_resources;

    /**
     * Get Resource value, @ref dm_resource_read_t
     *
     * Required for *LwM2M Read* operation.
     *
     * Can be NULL if the object does not contain readable resources.
     */
    dm_resource_read_t *resource_read;

    /**
     * Set Resource value, @ref dm_resource_write_t
     *
     * Required for *LwM2M Write* operation.
     *
     * Can be NULL if the object does not contain writable resources.
     */
    dm_resource_write_t *resource_write;

    /**
     * Perform Execute action on a Resource, @ref dm_resource_execute_t
     *
     * Required for *LwM2M Execute* operation.
     *
     * Can be NULL if the object does not contain executable resources.
     */
    dm_resource_execute_t *resource_execute;

    /**
     * Enumerate available Resource Instances,
     * @ref dm_list_resource_instances_t
     *
     * Required for *LwM2M Read*, *LwM2M Write* and *LwM2M Discover* operations
     * performed on multiple-instance resources..
     *
     * Can be NULL if the object does not contain multiple resources.
     */
    dm_list_resource_instances_t *list_resource_instances;
} dm_handlers_t;

/** A struct defining a LwM2M Object. */
struct dm_object_def_struct {
    /** Object ID; MUST not be <c>FLUF_ID_INVALID</c> (65535) */
    fluf_oid_t oid;

    /**
     * Object version: a string with static lifetime, containing two digits
     * separated by a dot (for example: "1.1").
     * If left NULL, client will not include the "ver=" attribute in Register
     * and Discover messages. This implies:
     * 1. Version 1.0 for Non-Core Objects.
     * 2. The version corresponding to the version in the LwM2M Enabler for Core
     * Objects.
     */
    const char *version;

    /** Handler callbacks for this object. */
    dm_handlers_t handlers;
};

/**
 * A callback that is called for each resource that is read within a call to
 * @ref dm_read . The purpose of this callback is to retrieve values that are
 * read from the data model, provided as values of type @ref fluf_io_out_entry_t
 * which are supposed to be e.g. copied by the user, and later serialized and
 * sent over the network.
 *
 * <c>arg</c> argument is the context argument that was provided with the call
 * to @ref dm_read .
 *
 * @param arg        Opaque pointer to user data.
 * @param out_entry  Pointer to retrieved data model entry.
 *
 * @returns 0 on success, a negative value in case of error.
 */
typedef int dm_output_ctx_cb_t(void *arg, fluf_io_out_entry_t *out_entry);

/** A struct defining a context for data model read operation. */
struct dm_output_ctx_struct {
    /**
     * Pointer to callback which provides user with data model entry,
     * @ref dm_output_ctx_cb_t.
     *
     * Required for every @ref dm_read call.
     *
     * **Must not be NULL.**
     */
    dm_output_ctx_cb_t *callback;

    /**
     * Pointer to user data which will be passed as first
     * argument to callback function.
     *
     * It is optional, it can be NULL.
     */
    void *arg;
};

/**
 * A callback that is called for each resource that is written within a call to
 * @ref dm_write . The purpose of this callback is to provide values that are
 * written to the data model, provided as values of type @ref
 * fluf_io_out_entry_t which are going to be copied and stored by data model.
 *
 * This callback is called directly by <c>dm_get_*</c> functions (while handling
 * <c>dm_write</c>) to get the @param in_entry value.
 *
 * WARNING: If @ref fluf_data_type_t in @ref fluf_io_out_entry_t is
 * one of @ref FLUF_DATA_TYPE_BYTES, @ref FLUF_DATA_TYPE_STRING,
 * @ref FLUF_DATA_TYPE_EXTERNAL_BYTES or @ref FLUF_DATA_TYPE_EXTERNAL_STRING
 * it's user responsibility to ensure that any context required by it (data
 * behind the pointer or <c>get_external_data</> with <c>user_args</c>) is
 * valid, until the last reference to it.
 *
 * @param arg        Opaque pointer to user data.
 * @param out_entry  Pointer to place where data model entry should be written.
 *
 * @returns 0 on success, other value in case of error.
 */
typedef int dm_input_ctx_cb_t(void *arg,
                              fluf_data_type_t expected_type,
                              fluf_io_out_entry_t *in_entry);

/** A struct defining a context for data model write operation. */
struct dm_input_ctx_struct {
    /**
     * Pointer to callback which provides user with pointer to place where,
     * data model entry should be written, @ref dm_input_ctx_cb_t.
     *
     * Required for every @ref dm_write call.
     *
     * **Must not be NULL.**
     */
    dm_input_ctx_cb_t *callback;

    /**
     * Pointer to user data which will be passed as first
     * argument to callback function.
     *
     * It is optional, it can be NULL.
     */
    void *arg;
};

/**
 * A callback that is called within a call to @ref dm_register_prepare . It is
 * called for each present object and object instance. The purpose of this
 * callback is to retrieve data sent in LwM2M Register message.
 *
 * @param arg Opaque pointer to user data.
 * @param uri Pointer to URI path of an object or an object instance.
 *
 * @returns 0 on success, a negative value in case of error.
 */
typedef int dm_register_ctx_cb_t(void *arg, fluf_uri_path_t *uri);

/** A struct defining a context for data model register operation. */
struct dm_register_ctx_struct {
    /**
     * Pointer to callback which is called for every present object and object
     * instance, @ref dm_register_ctx_cb_t.
     *
     * Required for every @ref dm_register_prepare call.
     *
     * **Must not be NULL.**
     */
    dm_register_ctx_cb_t *callback;

    /**
     * Pointer to user data which will be passed as first
     * argument to callback function.
     *
     * It is optional, it can be NULL.
     */
    void *arg;
};

/**
 * A callback that is called within a call to @ref dm_discover_resp_prepare . It
 * is called for every data model element that should be discovered. The purpose
 * of this callback is to retrieve data sent in LwM2M Discover message.
 *
 * @param arg Opaque pointer to user data.
 * @param uri Pointer to URI path of an object or an object instance.
 *
 * @returns 0 on success, a negative value in case of error.
 */
typedef int dm_discover_ctx_cb_t(void *arg, fluf_uri_path_t *uri);

/** A struct defining a context for data model discover operation. */
struct dm_discover_ctx_struct {
    /**
     * Pointer to callback which is called for every data model element
     * that should be discovered, @ref dm_discover_ctx_cb_t.
     *
     * Required for every @ref dm_discover_resp_prepare call.
     *
     * **Must not be NULL.**
     */
    dm_discover_ctx_cb_t *callback;

    /**
     * Pointer to user data which will be passed as first
     * argument to callback function.
     *
     * It is optional, it can be NULL.
     */
    void *arg;
};

/**
 * Initializes the data model module. This function is used to initialize
 * @ref dm_t object before any other operation on data model.
 *
 * Upon successful initialization, there are no registered LwM2M Objects.
 * At this point, the data model is ready for object registration and other
 * operations.
 *
 * <example>
 * #define MAX_OBJECTS_COUNT 2
 * static dm_t dm;
 * static dm_installed_object_t installed_objects[MAX_OBJECTS_COUNT];
 * dm_initialize(&dm, installed_objects, MAX_OBJECTS_COUNT);
 * </example>
 *
 * @param dm        data model pointer to operate on.
 * @param objects   pointer to an array used as a storage for installed objects
 * @param max_count maximum number of objects that can be installed.
 * @return 0 on success, other value in case of error.
 */
int dm_initialize(dm_t *dm, dm_installed_object_t *objects, size_t max_count);

/**
 * Registers the Object in the data model.
 *
 * NOTE: <c>def_ptr</c> MUST stay valid up to and including the corresponding
 * @ref dm_delete or @ref dm_unregister_object call.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior.
 *
 * @param dm      data model pointer to operate on.
 * @param def_ptr Pointer to the Object definition struct. The exact value
 *                passed to this function will be forwarded to all data model
 *                handler calls.
 * @return 0 on success, other value in case of error.
 */
int dm_register_object(dm_t *dm, const dm_object_def_t *const *def_ptr);

/**
 * Unregisters an Object in the data model.
 *
 * <c>def_ptr</c> MUST be a pointer previously passed to
 * @ref dm_register_object for the same <c>dm</c> object.
 *
 * After a successful unregister, any resources used by the actual object may be
 * safely freed up.
 *
 * NOTE: This function MUST NOT be called from within any data model handler
 * callback function (i.e. any of the @ref dm_handlers_t members). Doing
 * so is undefined behavior.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior.
 *
 * @param dm      data model object to operate on.
 * @param def_ptr Pointer to the Object definition struct.
 *
 * @returns 0 on success, a negative value if <c>def_ptr</c> does not correspond
 *          to any known registered object.
 */
int dm_unregister_object(dm_t *dm, const dm_object_def_t *const *def_ptr);

/**
 * Read data from data model. During the call to this function, the callback
 * @ref dm_output_ctx_cb_t provided by @ref dm_output_ctx_t structure will
 * be called for each resource instance that should be retrieved.
 *
 * Provided URI path can be: root path, object path, object instance path,
 * resource path or resource instance path. E.g. when provided URI path is
 * a root path, the callback will be called for every resource
 * instance in every object instance in every object.
 *
 * In case when provided URI path is a path to unknown object, unknown resource
 * or resource is not present or resource is not readable, the callback will not
 * be called and dm_read function will return an error.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior. This function must be called with pointer to callback
 * function, placed in @ref dm_output_ctx_t struct. This callback can't be NULL.
 *
 * @param dm      data model to operate on.
 * @param uri     URI path to read.
 * @param out_ctx Output context with pointer to callback function and optional
 *                pointer to user data.
 *
 * @return
 * - 0 on success
 * - one of FLUF_COAP_CODE_ constants in case of error.
 */
int dm_read(dm_t *dm, const fluf_uri_path_t *uri, dm_output_ctx_t *out_ctx);

/**
 * Write data to data model. During the call to this function, the callback
 * @ref dm_input_ctx_cb_t provided by @ref dm_input_ctx_t structure will
 * be called for each resource instance that should be written.
 *
 * Provided URI path can be: object instance path, resource path or
 * resource instance path. In case when provided URI path is a path to unknown
 * object, unknown resource or resource is not present or resource is not
 * writable, the callback will not be called and dm_write function will return
 * an error.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior. This function must be called with pointer to callback
 * function, placed in @ref dm_input_ctx_t struct. This callback can't be NULL.
 *
 * @param dm     data model to operate on.
 * @param uri    URI path to write.
 * @param in_ctx Input context with pointer to callback function and optional
 *               pointer to user data.
 *
 * @return
 * - 0 on success
 * - one of FLUF_COAP_CODE_ constants in case of error.
 */
int dm_write(dm_t *dm, const fluf_uri_path_t *uri, dm_input_ctx_t *in_ctx);

/**
 * Performs execute operation on an data model's resource pointed by provided
 * URI path.
 *
 * NOTE: Provided URI has to point to resource path. Otherwise this function
 * will return FLUF_COAP_CODE_METHOD_NOT_ALLOWED.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior.
 *
 * @param dm  data model to operate on.
 * @param uri URI path to execute.
 *
 * @return
 * - 0 on success
 * - one of FLUF_COAP_CODE_ constants in case of error.
 */
int dm_execute(dm_t *dm, const fluf_uri_path_t *uri);

/**
 * Returns number of resources in a data model provided URI path.
 * Counts only readable resources.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior.
 *
 * @param dm        data model to operate on.
 * @param uri       URI path.
 * @param out_count pointer writes counted out value.

 * @return
 * - 0 on success
 * - one of FLUF_COAP_CODE_ constants in case of error.
 */
int dm_get_readable_res_count(dm_t *dm,
                              fluf_uri_path_t *uri,
                              size_t *out_count);

/**
 * Prepares data for LwM2M Register message.
 *
 * During the call to this function, the @ref dm_register_ctx_cb_t callback
 * provided by @ref dm_register_ctx_t structure will be called for every
 * registered object and present object instance.
 *
 * Call to this function with any argument as NULL will result in undefined
 * behavior. This function must be called with a pointer to callback
 * function, placed in @ref dm_register_ctx_t struct. This callback can't be
 * NULL.
 *
 * @param dm  data model to operate on.
 * @param ctx context with a pointer to callback function and optional pointer
 * to user data.
 *
 * @return
 * - 0 on success
 * - one of FLUF_COAP_CODE_ constants in case of error.
 */
int dm_register_prepare(dm_t *dm, dm_register_ctx_t *ctx);

/**
 * Prepares data for response to LwM2M Discover message.
 *
 * During the call to this function, the @ref dm_discover_ctx_cb_t callback
 * provided by @ref dm_discover_ctx_t structure will be called for every data
 * model element that should be discovered.
 *
 * Provided URI path can be: object path, object instance path, resource path,
 * otherwise behavior is undefined.
 *
 * Pointer to depth parameter can be NULL, in this case default depth will be
 * used. Otherwise, possible values behind the pointer are 0..3.
 *
 * Call to this function with any argument (other than depth) as NULL will
 * result in undefined behavior. This function must be called with a
 * pointer to callback function, placed in @ref dm_register_ctx_t struct. This
 * callback can't be NULL.
 *
 * @param dm    data model to operate on.
 * @param uri   URI path to discover.
 * @param depth pointer to depth value.
 * @param ctx   context with a pointer to callback function and optional pointer
 * to user data.
 * @return
 */
int dm_discover_resp_prepare(dm_t *dm,
                             fluf_uri_path_t *uri,
                             const uint8_t *depth,
                             dm_discover_ctx_t *ctx);

#endif /*ANJAY_DM_DM_H*/
