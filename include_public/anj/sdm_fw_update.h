/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_FW_UPDATE_H
#define SDM_FW_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include <anj/anj_config.h>
#include <anj/sdm_io.h>

#include <anj/anj_config.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANJ_WITH_FOTA_OBJECT

#    define SDM_FW_UPDATE_OID 5
#    define SDM_FW_UPDATE_URI_MAX_LEN 255
#    define SDM_FW_UPDATE_RESOURCES_COUNT 9

/*
 * FW Update Object Resources IDs
 */
enum sdm_fw_update_resources {
    SDM_FW_UPDATE_RID_PACKAGE = 0,
    SDM_FW_UPDATE_RID_PACKAGE_URI = 1,
    SDM_FW_UPDATE_RID_UPDATE = 2,
    SDM_FW_UPDATE_RID_STATE = 3,
    SDM_FW_UPDATE_RID_UPDATE_RESULT = 5,
    SDM_FW_UPDATE_RID_PKG_NAME = 6,
    SDM_FW_UPDATE_RID_PKG_VERSION = 7,
    SDM_FW_UPDATE_RID_UPDATE_PROTOCOL_SUPPORT = 8,
    SDM_FW_UPDATE_RID_UPDATE_DELIVERY_METHOD = 9
};

/*
 * Numeric values of the Firmware Update Protocol Support resource. See LwM2M
 * specification for details.
 */
typedef enum {
    SDM_FW_UPDATE_STATE_IDLE = 0,
    SDM_FW_UPDATE_STATE_DOWNLOADING,
    SDM_FW_UPDATE_STATE_DOWNLOADED,
    SDM_FW_UPDATE_STATE_UPDATING
} sdm_fw_update_state_t;

/**
 * Numeric values of the Firmware Update Result resource. See LwM2M
 * specification for details.
 *
 * IMPORTANT NOTE: The values of this enum are incorporated error codes defined
 * in the LwM2M documentation, but actual code logic introduces a variation in
 * the interpretation of the success code SDM_FW_UPDATE_RESULT_SUCCESS. While
 * the LwM2M documentation specifies it as an overall success code, in this
 * implementation, it is used in other contexts, mainly to signalize success at
 * every stage of the process. User should be mindful of this distinction and
 * adhere to the return code descriptions provided for each function in this
 * file.
 *
 */
typedef enum {
    SDM_FW_UPDATE_RESULT_INITIAL = 0,
    SDM_FW_UPDATE_RESULT_SUCCESS = 1,
    SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE = 2,
    SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY = 3,
    SDM_FW_UPDATE_RESULT_CONNECTION_LOST = 4,
    SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE = 5,
    SDM_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE = 6,
    SDM_FW_UPDATE_RESULT_INVALID_URI = 7,
    SDM_FW_UPDATE_RESULT_FAILED = 8,
    SDM_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL = 9,
} sdm_fw_update_result_t;

/*
 * Numeric values of the Firmware Update Protocol Support resource. See LwM2M
 * specification for details.
 */
enum sdm_fw_update_protocols {
    SDM_FW_UPDATE_PROTOCOL_COAP = 0,
    SDM_FW_UPDATE_PROTOCOL_COAPS = 1,
    SDM_FW_UPDATE_PROTOCOL_HTTP = 2,
    SDM_FW_UPDATE_PROTOCOL_HTTPS = 3,
    SDM_FW_UPDATE_PROTOCOL_COAP_TCP = 4,
    SDM_FW_UPDATE_PROTOCOL_COAP_TLS = 5
};

/**
 * Initates the Push-mode download of FW package. The library calls this
 * function when an LwM2M Server performs a Write on Package resource and is
 * immediately followed with series of
 * @ref sdm_fw_update_package_write_t callback calls that pass the actual binary
 * data of the downloaded image if it returns @ref SDM_FW_UPDATE_RESULT_SUCCESS.
 *
 * @note This handler has to be implemented if ANJ_FOTA_PUSH_METHOD_SUPPORTED is
 * enabled
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 *
 * @returns The callback shall return @ref SDM_FW_UPDATE_RESULT_SUCCESS if
 *          successful or an appropriate reason for the write failure:
 *          @ref SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE
 *          @ref SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY
 *          @ref SDM_FW_UPDATE_RESULT_CONNECTION_LOST
 */
typedef sdm_fw_update_result_t
sdm_fw_update_package_write_start_t(void *user_ptr);

/**
 * Passes the binary data written by an LwM2M Server to the Package resource
 * in chunks as they come in a block transfer. If it returns a value other than
 * @ref SDM_FW_UPDATE_RESULT_SUCCESS , it is set as Result resource and
 * subsequent chunks coming from the server are rejected.
 *
 * @note This handler has to be implemented if ANJ_FOTA_PUSH_METHOD_SUPPORTED is
 * enabled
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 *
 * @returns The callback shall return @ref SDM_FW_UPDATE_RESULT_SUCCESS if
 *          successful or an appropriate reason for the write failure:
 *          @ref SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE
 *          @ref SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY
 *          @ref SDM_FW_UPDATE_RESULT_CONNECTION_LOST
 */
typedef sdm_fw_update_result_t
sdm_fw_update_package_write_t(void *user_ptr, void *data, size_t data_size);

/**
 * Finitializes the operation of writing FW package chunks.
 * The library informs the application that the last call of @ref
 * sdm_fw_update_package_write_t was the final one. If this function returns 0,
 * FOTA State Machine goes to Downloaded state and waits for LwM2M Server to
 * execute Update resource.
 *
 * @note This handler has to be implemented if ANJ_FOTA_PUSH_METHOD_SUPPORTED is
 * enabled
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 *
 * @returns The callback shall return @ref SDM_FW_UPDATE_RESULT_SUCCESS if
 *          successful or an appropriate reason for the write failure:
 *          @ref SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE
 *          @ref SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY
 *          @ref SDM_FW_UPDATE_RESULT_CONNECTION_LOST
 *          @ref SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE
 */
typedef sdm_fw_update_result_t
sdm_fw_update_package_write_finish_t(void *user_ptr);

/**
 * Informs the application that an LwM2M Server initiated FOTA in Pull mode by
 * writing Package URI Resource. If this function return 0, library goes into
 * Downloading state and waits for
 * @ref sdm_fw_update_object_set_download_result call.
 *
 * Download abort with a '\0' write to Package URI is handled internally and
 * other callback,
 * @ref sdm_fw_update_cancel_download_t is called then.
 *
 * @note This handler has to be implemented if ANJ_FOTA_PULL_METHOD_SUPPORTED is
 * enabled
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 * @param uri Null-terminated string containing the URI of FW package
 *
 * @returns The callback shall return @ref SDM_FW_UPDATE_RESULT_SUCCESS if
 *          successful or an appropriate reason for the write failure:
 *          @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE
 *          @ref SDM_FW_UPDATE_RESULT_INVALID_URI
 *          @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL
 */
typedef sdm_fw_update_result_t sdm_fw_update_uri_write_t(void *user_ptr,
                                                         const char *uri);

/**
 * Schedules performing the actual upgrade with previously downloaded package.
 *
 * Will be called at request of the server, after a package has been downloaded.
 * Since performing an upgrade can be a relatively long operation and this
 * handler is called directly from the LwM2M Request processing context, it is
 * recommended that it schedules the update and returns so that the library can
 * send an appropriate response to this request on time.
 *
 * Most users will want to implement firmware update in a way that involves a
 * reboot. In such case, it is expected that this callback will do either one of
 * the following:
 *
 * - perform firmware upgrade, terminate outermost event loop and return,
 *   call reboot
 * - perform the firmware upgrade internally and then reboot, it means that
 *   the return will never happen (although the library won't be able to send
 *   the acknowledgement to execution of Update resource)
 *
 * Regardless of the method, the Update result should be set with a call to
 * @ref sdm_fw_update_object_set_update_result
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 *
 * @returns 0 for success, non-zero for an internal failure (Result resource
 * will be set to SDM_FW_UPDATE_RESULT_FAILED).
 */
typedef int sdm_fw_update_update_start_t(void *user_ptr);

/**
 * Returns the name of downloaded firmware package.
 *
 * The name will be exposed in the data model as the PkgName Resource. If this
 * callback returns <c>NULL</c> or is not implemented at all (with the
 * corresponding field set to <c>NULL</c>), PkgName Resource will contain an
 * empty string
 *
 * It only makes sense for this handler to return non-<c>NULL</c> values if
 * there is a valid package already downloaded. The library will not call this
 * handler in any state other than <em>Downloaded</em>.
 *
 * The library will not attempt to deallocate the returned pointer. User code
 * must assure that the pointer will remain valid.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package name, or <c>NULL</c> if it is not currently
 *          available.
 */
typedef const char *sdm_fw_update_get_name_t(void *user_ptr);

/**
 * Returns the version of downloaded firmware package.
 *
 * The version will be exposed in the data model as the PkgVersion Resource. If
 * this callback returns <c>NULL</c> or is not implemented at all (with the
 * corresponding field set to <c>NULL</c>), PkgVersion Resource will contain an
 * empty string
 *
 * It only makes sense for this handler to return non-<c>NULL</c> values if
 * there is a valid package already downloaded. The library will not call this
 * handler in any state other than <em>Downloaded</em>.
 *
 * The library will not attempt to deallocate the returned pointer. User code
 * must assure that the pointer will remain valid.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package version, or <c>NULL</c> if it is not
 *          currently available.
 */
typedef const char *sdm_fw_update_get_version_t(void *user_ptr);

/**
 * Resets the firmware update state and performs any applicable cleanup of
 * temporary storage, including aborting any ongoing FW package download.
 *
 * Will be called at request of the server, or after a failed download. Note
 * that it may be called without previously calling
 * @ref anjay_fw_update_stream_finish_t, so it shall also close the currently
 * open download stream, if any.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install in
 *                 <c>entity_ctx->repr.user_ptr</c>
 */
typedef void sdm_fw_update_reset_t(void *user_ptr);

typedef struct {
    sdm_fw_update_package_write_start_t *package_write_start_handler;
    sdm_fw_update_package_write_t *package_write_handler;
    sdm_fw_update_package_write_finish_t *package_write_finish_handler;
    sdm_fw_update_uri_write_t *uri_write_handler;
    sdm_fw_update_update_start_t *update_start_handler;
    sdm_fw_update_get_name_t *get_name;
    sdm_fw_update_get_version_t *get_version;
    sdm_fw_update_reset_t *reset_handler;
} sdm_fw_update_handlers_t;

/*
 * Representation of a FW Update Object that holds values of its resources and
 * its internal state.
 */
typedef struct {
    /* /5/0/3 State Resouce value holder */
    int8_t state;
    /* 5/0/5 Result Resource value holder */
    int8_t result;
    /* Set of user provided callback handlers */
    sdm_fw_update_handlers_t *user_handlers;
    /* Opaque user pointer passed back with each call of @ref user_handlers
     * callbacks. Can be used to e.g. determine the the context or distiguish FW
     * Update Object entities in a multiple LwM2M Clients system */
    void *user_ptr;
#    if defined(ANJ_FOTA_PULL_METHOD_SUPPORTED)
    char uri[SDM_FW_UPDATE_URI_MAX_LEN + 1];
    size_t uri_bytes_written;
#    endif // defined (ANJ_FOTA_PULL_METHOD_SUPPORTED)
#    if defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED)
    bool write_start_called;
    size_t package_bytes_written;
#    endif // defined (ANJ_FOTA_PUSH_METHOD_SUPPORTED)
} sdm_fw_update_repr_t;

/*
 * Complex structure of a whole FW Update Object entity context that holds the
 * object and its instance that are linked to Static Data Model as well as the
 * object representation.
 *
 * User is expected to instantiate a structure of this type, initialize
 * only the @ref repr field and not modify it directly throughout the LwM2M
 * Client life.
 */
typedef struct {
    sdm_obj_t obj;
    sdm_obj_inst_t inst;
    sdm_obj_inst_t *inst_ptr;
    sdm_res_t res[SDM_FW_UPDATE_RESOURCES_COUNT];
    sdm_fw_update_repr_t repr;
} sdm_fw_update_entity_ctx_t;

/**
 * Installs the Firmware Update object in an SDM.
 *
 * @param dm         Data Model context where FW Update Object shall be
 *                   installed
 * @param entity_ctx Objects entity context that shall be alocated by
 *                   the user. Please refer to @ref sdm_fw_update_entity_ctx_t
 *                   documentation for more details.
 *
 * @param handlers   Pointer to a set of handler functions that handle
 *                   the platform-specific part of firmware update
 *                   process. Note: Contents of the structure are NOT
 *                   copied, so it needs to remain valid for the
 *                   lifetime of the object.
 *
 * @param user_ptr   Opaque user pointer that will be copied to
 *                   <c>entity_ctx->repr.user_ptr</c>
 *
 * @return 0 in case of success, negative value in case of error
 */
int sdm_fw_update_object_install(sdm_data_model_t *dm,
                                 sdm_fw_update_entity_ctx_t *entity_ctx,
                                 sdm_fw_update_handlers_t *handlers,
                                 void *user_ptr);

/**
 * Sets the result of FW update triggered with /5/0/2 execution.
 *
 * This function sets /5/0/5 Result to the value passed through argument and
 * /5/0/3 State to IDLE. If FW upgrade is performed with reboot, this function
 * should be called right after installing FW Update object.
 *
 * @param entity_ctx Objects entity context initilized previously with @ref
 *                   sdm_fw_update_object_install .
 *
 * @param result     Result of the FW update. To comply with LwM2M specification
 *                   should be one of the following:
 *                   @ref SDM_FW_UPDATE_RESULT_SUCCESS
 *                   @ref SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE
 *                   @ref SDM_FW_UPDATE_RESULT_FAILED
 */
void sdm_fw_update_object_set_update_result(
        sdm_fw_update_entity_ctx_t *entity_ctx, sdm_fw_update_result_t result);

/**
 * Sets the result of FW download in FOTA with PULL method.
 *
 * @param entity_ctx Objects entity context initilized previously with @ref
 *                   sdm_fw_update_object_install .
 *
 * @param result     Result of the downloading. To comply with LwM2M
 *                   specification should be one of the following:
 *                   @ref SDM_FW_UPDATE_RESULT_SUCCESS
 *                   @ref SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE
 *                   @ref SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY
 *                   @ref SDM_FW_UPDATE_RESULT_CONNECTION_LOST
 *                   @ref SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE
 *                   @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE
 *                   @ref SDM_FW_UPDATE_RESULT_INVALID_URI
 *                   @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL
 *
 * @return 0 in case of success, negative value in case of calling the function
 *         in other state than UPDATE_STATE_DOWNLOADING.
 */
int sdm_fw_update_object_set_download_result(
        sdm_fw_update_entity_ctx_t *entity_ctx, sdm_fw_update_result_t result);

#endif // ANJ_WITH_FOTA_OBJECT

#ifdef __cplusplus
}
#endif

#endif // SDM_FW_UPDATE_H
