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

#include <anj/sdm_io.h>

enum sdm_fw_update_protocols {
    SDM_FW_UPDATE_PROTOCOL_COAP = (1 << 0),
    SDM_FW_UPDATE_PROTOCOL_COAPS = (1 << 1),
    SDM_FW_UPDATE_PROTOCOL_HTTP = (1 << 2),
    SDM_FW_UPDATE_PROTOCOL_HTTPS = (1 << 3),
    SDM_FW_UPDATE_PROTOCOL_COAP_TCP = (1 << 4),
    SDM_FW_UPDATE_PROTOCOL_COAP_TLS = (1 << 5)
};

/**
 * Numeric values of the Firmware Update Result resource. See LwM2M
 * specification for details.
 *
 * Note: they SHOULD only be used with @ref
 * sdm_fw_update_object_set_update_result .
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

/**
 * Initates the Push-mode download of FW package. The library calls this
 * function when an LwM2M Server performs a Write on Package resource and is
 * immediately followed with series of
 * @ref sdm_fw_update_package_write_t callback calls that pass the actual binary
 * data of the downloaded image if it returns 0.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install
 *
 * @returns The callback shall return 0 or @ref SDM_FW_UPDATE_RESULT_SUCCESS if
 *          successful or an appropriate reason for the write failure:
 *          @ref SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE
 *          @ref SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY
 *          @ref SDM_FW_UPDATE_RESULT_CONNECTION_LOST
 */
typedef sdm_fw_update_result_t
sdm_fw_update_package_write_start_t(void *user_ptr);

/**
 * Passes the binary data written by an LwM2M Server to the Package resource
 * in chunks as they come in a block transfer. If it returns a non-zero value,
 * it is set as Result resource and subsequent chunks comming from the server
 * are rejected.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install
 *
 * @returns The callback shall return 0 or @ref SDM_FW_UPDATE_RESULT_SUCCESS if
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
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install
 *
 * @returns The callback shall return 0 or @ref SDM_FW_UPDATE_RESULT_SUCCESS if
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
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install
 * @param uri Null-terminated string containing the URI of FW package
 *
 * @returns The callback shall return 0 or @ref SDM_FW_UPDATE_RESULT_SUCCESS if
 *          successful or an appropriate reason for the write failure:
 *          @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE
 *          @ref SDM_FW_UPDATE_RESULT_INVALID_URI
 *          @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL
 */
typedef sdm_fw_update_result_t sdm_fw_update_uri_write_t(void *user_ptr,
                                                         const char *uri);

/**
 * Informs the application that an LwM2M Server aborted FOTA with an empty write
 * to Package resoource or er empty string write to Package URI resource.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install
 * @param uri Null-terminated string containing the URI of FW package
 */
typedef void sdm_fw_update_cancel_download_t(void *user_ptr, const char *uri);

/**
 * Performs the actual upgrade with previously downloaded package.
 *
 * Will be called at request of the server, after a package has been downloaded.
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
 *                 @ref sdm_fw_update_object_install
 *
 * @returns 0 for success, non-zero for an internal failure (Result resource
 * will be set to SDM_FW_UPDATE_RESULT_FAILED).
 */
typedef sdm_fw_update_result_t sdm_fw_update_update_start_t(void *user_ptr);

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
 *                 @ref sdm_fw_update_object_install
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
 *                 @ref sdm_fw_update_object_install
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package version, or <c>NULL</c> if it is not
 *          currently available.
 */
typedef const char *sdm_fw_update_get_version_t(void *user_ptr);

/**
 * Resets the firmware update state and performs any applicable cleanup of
 * temporary storage if necessary.
 *
 * Will be called at request of the server, or after a failed download. Note
 * that it may be called without previously calling
 * @ref anjay_fw_update_stream_finish_t, so it shall also close the currently
 * open download stream, if any.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref sdm_fw_update_object_install
 */
typedef void sdm_fw_update_reset_t(void *user_ptr);

typedef struct {
    sdm_fw_update_package_write_start_t *package_write_start_handler;
    sdm_fw_update_package_write_t *package_write_handler;
    sdm_fw_update_package_write_finish_t *package_write_finish_handler;
    sdm_fw_update_uri_write_t *uri_write_handler;
    sdm_fw_update_cancel_download_t *cancel_download_handler;
    sdm_fw_update_update_start_t *update_start_handler;
    sdm_fw_update_get_name_t *get_name;
    sdm_fw_update_get_version_t *get_version;
    sdm_fw_update_reset_t *reset_handler;
} sdm_fw_update_handlers_t;

/**
 * Installs the Firmware Update object in an SDM.
 *
 * @param dm                  Data Model context where FW Update Object shall be
 *                            installed
 *
 * @param handlers            Pointer to a set of handler functions that handle
 *                            the platform-specific part of firmware update
 *                            process. Note: Contents of the structure are NOT
 *                            copied, so it needs to remain valid for the
 *                            lifetime of the object.
 *
 * @param user_ptr            Opaque user pointer that will be passed as the
 *                            first argument to handler functions.
 *
 * @param supported_protocols bitmap set according to
 *                            @ref sdm_fw_update_protocols
 *
 * @return 0 in case of success, negative value in case of error
 */
int sdm_fw_update_object_install(sdm_data_model_t *dm,
                                 sdm_fw_update_handlers_t *handlers,
                                 void *user_ptr,
                                 uint8_t supported_protocols);

/**
 * Sets the result of FW update triggered with /5/0/2 execution.
 *
 * If FW upgrade is performed with reboot, this function should be called right
 * after installing FW Update object.
 *
 * @param result Result of the FW update. To comply with LwM2M specification
 *               should be one of the following:
 *               @ref SDM_FW_UPDATE_RESULT_SUCCESS
 *               @ref SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE
 *               @ref SDM_FW_UPDATE_RESULT_FAILED
 *
 * @return 0 in case of success, negative value in case of calling the function
 *         in other state than Updating.
 */
int sdm_fw_update_object_set_update_result(sdm_fw_update_result_t result);

/**
 * Sets the result of FW download in FOTA with PULL method.
 *
 * @param result Result of the downloading. To comply with LwM2M specification
 *               should be one of the following:
 *               @ref SDM_FW_UPDATE_RESULT_SUCCESS
 *               @ref SDM_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE
 *               @ref SDM_FW_UPDATE_RESULT_OUT_OF_MEMORY
 *               @ref SDM_FW_UPDATE_RESULT_CONNECTION_LOST
 *               @ref SDM_FW_UPDATE_RESULT_INTEGRITY_FAILURE
 *               @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE
 *               @ref SDM_FW_UPDATE_RESULT_INVALID_URI
 *               @ref SDM_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL
 *
 * @return 0 in case of success, negative value in case of calling the function
 *         in other state than UPDATE_STATE_DOWNLOADING.
 */
int sdm_fw_update_object_set_download_result(sdm_fw_update_result_t result);

#endif // SDM_FW_UPDATE_H
