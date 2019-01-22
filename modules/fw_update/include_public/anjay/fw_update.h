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

#ifndef ANJAY_INCLUDE_ANJAY_FW_UPDATE_H
#define ANJAY_INCLUDE_ANJAY_FW_UPDATE_H

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Firmware update result codes
 * @{
 * The following result codes may be returned from
 * @ref anjay_fw_update_stream_write_t, @ref anjay_fw_update_stream_finish_t or
 * @ref anjay_fw_update_perform_upgrade_t to control the value of the Update
 * Result Resource after the failure.
 *
 * Their values correspond to negated numeric values of that resource. However,
 * attempting to use other negated value will be checked and cause a fall-back
 * to a value default for a given handler.
 */
#define ANJAY_FW_UPDATE_ERR_NOT_ENOUGH_SPACE (-2)
#define ANJAY_FW_UPDATE_ERR_OUT_OF_MEMORY (-3)
#define ANJAY_FW_UPDATE_ERR_INTEGRITY_FAILURE (-5)
#define ANJAY_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE (-6)
/** @} */

/**
 * Possible values that control the State and Update Result resources at the
 * time of initialization of the Firmware Update object.
 */
typedef enum {
    /**
     * Corresponds to the "Downloaded" State and "Initial" Result. Shall be used
     * when the device unexpectedly rebooted when the firmware image has already
     * been downloaded into some non-volatile memory.
     */
    ANJAY_FW_UPDATE_INITIAL_DOWNLOADED = -2,

    /**
     * Corresponds to the "Downloading" State and "Initial" Result. Shall be
     * used when the device can determine that it unexpectedly rebooted during
     * the download of the firmware image, and it has all the information
     * necessary to resume the download. Such information shall then be passed
     * via other fields in the @ref anjay_fw_update_initial_state_t structure.
     */
    ANJAY_FW_UPDATE_INITIAL_DOWNLOADING = -1,

    /**
     * Corresponds to the "Idle" State and "Initial" Result. Shall be used when
     * the library is initializing normally, not after a firmware update
     * attempt.
     */
    ANJAY_FW_UPDATE_INITIAL_NEUTRAL = 0,

    /**
     * Corresponds to the "Idle" State and "Firmware updated successfully"
     * Result. Shall be used when the device has just rebooted after
     * successfully updating the firmware.
     */
    ANJAY_FW_UPDATE_INITIAL_SUCCESS = 1,

    /**
     * Corresponds to the "Idle" State and "Integrity check failure" Result.
     * Shall be used when the device has just rebooted after an unsuccessful
     * firmware update attempt that failed due to failed integrity check of the
     * firmware package.
     */
    ANJAY_FW_UPDATE_INITIAL_INTEGRITY_FAILURE = 5,

    /**
     * Corresponds to the "Idle" State "Firmware update failed" Result. Shall be
     * used when the device has just rebooted after a firmware upgrade attempt
     * that was unsuccessful for reason any other than integrity check.
     */
    ANJAY_FW_UPDATE_INITIAL_FAILED = 8
} anjay_fw_update_initial_result_t;

/**
 * Information about the state to initialize the Firmware Update object in.
 */
typedef struct {
    /**
     * Controls initialization of the State and Update Result resources. It is
     * intended to be used after a reboot caused by a firmware update attempt,
     * to report the update result.
     */
    anjay_fw_update_initial_result_t result;

    /**
     * Value to initialize the Package URI resource with. The passed string is
     * copied, so the pointer is allowed to become invalid after return from
     * @ref anjay_fw_update_install .
     *
     * Required when <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c>; if it
     * is not provided (<c>NULL</c>) in such case, @ref anjay_fw_update_reset_t
     * handler will be called from @ref anjay_fw_update_install to reset the
     * Firmware Update object into the Idle state.
     *
     * Optional when <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADED</c>; in
     * this case it signals that the firmware was downloaded using the Pull
     * mechanism.
     *
     * In all other cases it is ignored.
     */
    const char *persisted_uri;

    /**
     * Number of bytes that has been already successfully downloaded and are
     * available at the time of calling @ref anjay_fw_update_install .
     *
     * It is ignored unless
     * <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c>, in which case the
     * following call to @ref anjay_fw_update_stream_write_t shall append the
     * passed chunk of data at the offset set here. If resumption from the set
     * offset is impossible, the library will call @ref anjay_fw_update_reset_t
     * and @ref anjay_fw_update_stream_open_t to restart the download process.
     */
    size_t resume_offset;

    /**
     * ETag of the download process to resume. The passed value is copied, so
     * the pointer is allowed to become invalid after return from
     * @ref anjay_fw_update_install .
     *
     * Required when <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c> and
     * <c>resume_offset > 0</c>; if it is not provided (<c>NULL</c>) in such
     * case, @ref anjay_fw_update_reset_t handler will be called from
     * @ref anjay_fw_update_install to reset the Firmware Update object into the
     * Idle state.
     */
    const struct anjay_etag *resume_etag;
} anjay_fw_update_initial_state_t;

/**
 * Opens the stream that will be used to write the firmware package to.
 *
 * The intended way of implementing this handler is to open a temporary file
 * using <c>fopen()</c> or allocate some memory buffer that may then be used to
 * store the downloaded data in. The library will not attempt to call
 * @ref anjay_fw_update_stream_write_t without having previously called
 * @ref anjay_fw_update_stream_open_t . Please see
 * @ref anjay_fw_update_handlers_t for more information about state transitions.
 *
 * Note that this handler will NOT be called after initializing the object with
 * the <c>ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c> option, so any necessary
 * resources shall be already open before calling @ref anjay_fw_update_install .
 *
 * @param user_ptr     Opaque pointer to user data, as passed to
 *                     @ref anjay_fw_update_install
 *
 * @param package_uri  URI of the package from which a Pull-mode download is
 *                     performed, or <c>NULL</c> if it is a Push-mode download.
 *                     This argument may either be ignored, or persisted in
 *                     non-volatile storage if the client supports download
 *                     resumption after an unexpected reboot (see
 *                     @ref anjay_fw_update_initial_state_t and its fields).
 *
 * @param package_etag ETag of the data being downloaded in Pull mode, or
 *                     <c>NULL</c> if it is a Push-mode download or ETags are
 *                     not supported by the remote server. This argument may
 *                     either be ignored, or persisted in non-volatile storage
 *                     if the client supports download resumption after an
 *                     unexpected reboot (see
 *                     @ref anjay_fw_update_initial_state_t and its fields).
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_FW_UPDATE_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int
anjay_fw_update_stream_open_t(void *user_ptr,
                              const char *package_uri,
                              const struct anjay_etag *package_etag);

/**
 * Writes data to the download stream.
 *
 * May be called multipled times after @ref anjay_fw_update_stream_open_t, once
 * for each consecutive chunk of downloaded data.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_fw_update_install
 *
 * @param data     Pointer to a chunk of the firmware package being downloaded.
 *                 Guaranteed to be non-<c>NULL</c>.
 *
 * @param length   Number of bytes in the chunk pointed to by <c>data</c>.
 *                 Guaranteed to be greater than zero.
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_FW_UPDATE_ERR_*</c> value is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource.
 */
typedef int
anjay_fw_update_stream_write_t(void *user_ptr, const void *data, size_t length);

/**
 * Closes the download stream and prepares the firmware package to be flashed.
 *
 * Will be called after a series of @ref anjay_fw_update_stream_write_t calls
 * after the whole package is downloaded.
 *
 * The intended way of implementing this handler is to e.g. call <c>fclose()</c>
 * and perform integrity check on the downloaded file. It might also be
 * uncompressed or decrypted as necessary, so that it is ready to be flashed.
 * The exact split of responsibility between
 * @ref anjay_fw_update_stream_finish_t and
 * @ref anjay_fw_update_perform_upgrade_t is not clearly defined and up to the
 * implementor.
 *
 * Note that regardless of the return value, the stream is considered to be
 * closed. That is, upon successful return, the Firmware Update object is
 * considered to be in the <em>Downloaded</em> state, and upon returning an
 * error - in the <em>Idle</em> state.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_fw_update_install
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_FW_UPDATE_ERR_*</c> value is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource.
 */
typedef int anjay_fw_update_stream_finish_t(void *user_ptr);

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
 *                 @ref anjay_fw_update_install
 */
typedef void anjay_fw_update_reset_t(void *user_ptr);

/**
 * Returns the name of downloaded firmware package.
 *
 * The name will be exposed in the data model as the PkgName Resource. If this
 * callback returns <c>NULL</c> or is not implemented at all (with the
 * corresponding field set to <c>NULL</c>), that Resource will not be present in
 * the data model.
 *
 * It only makes sense for this handler to return non-<c>NULL</c> values if
 * there is a valid package already downloaded. The library will not call this
 * handler in any state other than <em>Downloaded</em>.
 *
 * The library will not attempt to deallocate the returned pointer. User code
 * must assure that the pointer will remain valid at least until return from
 * @ref anjay_serve or @ref anjay_sched_run .
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_fw_update_install
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing tha package name, or <c>NULL</c> if it is not currently
 *          available.
 */
typedef const char *anjay_fw_update_get_name_t(void *user_ptr);

/**
 * Returns the version of downloaded firmware package.
 *
 * The version will be exposed in the data model as the PkgVersion Resource. If
 * this callback returns <c>NULL</c> or is not implemented at all (with the
 * corresponding field set to <c>NULL</c>), that Resource will not be present in
 * the data model.
 *
 * It only makes sense for this handler to return non-<c>NULL</c> values if
 * there is a valid package already downloaded. The library will not call this
 * handler in any state other than <em>Downloaded</em>.
 *
 * The library will not attempt to deallocate the returned pointer. User code
 * must assure that the pointer will remain valid at least until return from
 * @ref anjay_serve or @ref anjay_sched_run .
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_fw_update_install
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing tha package version, or <c>NULL</c> if it is not
 *          currently available.
 */
typedef const char *anjay_fw_update_get_version_t(void *user_ptr);

/**
 * Performs the actual upgrade with previously downloaded package.
 *
 * Will be called at request of the server, after a package has been downloaded.
 * It is expected that this callback will do either one of the following:
 *
 * - return, causing the outermost event loop to terminate, shutdown the library
 *   and then perform the firmware upgrade and then the device to reboot
 * - perform the firmware upgrade internally and never return, causing a reboot
 *   in the process
 *
 * In any case, it is expected that the device will then reboot. After
 * rebooting, the result of the upgrade process may be passed to the library
 * during initialization via the <c>initial_result</c> argument to
 * @ref anjay_fw_update_install .
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_fw_update_install
 *
 * @returns The callback shall return a negative value if it can be determined
 *          without a reboot that the firmware upgrade cannot be successfully
 *          performed.
 *
 *          If one of the <c>ANJAY_FW_UPDATE_ERR_*</c> values is returned, an
 *          equivalent value will be set in the Update Result Resource.
 *          Otherwise, if a non-zero value is returned, the Update Result
 *          Resource is set to generic "Firmware update failed" code.
 *
 *          If an update is to be attempted, it shall either return 0 or
 *          perform a reboot internally without returning. In either case, a
 *          reboot or at least a reinitialization of the library is then
 *          required to pass the update result.
 */
typedef int anjay_fw_update_perform_upgrade_t(void *user_ptr);

/**
 * Queries security information that shall be used for an encrypted connection
 * with a PULL-mode download server.
 *
 * May be called before @ref anjay_fw_update_stream_open_t if the download is to
 * be performed in PULL mode and the connection needs to use TLS or DTLS
 * encryption.
 *
 * Note that the <c>avs_net_security_info_t</c> contains references to file
 * paths and/or binary security keys. It is the user's responsibility to
 * appropriately allocate them and ensure proper lifetime of the returned
 * pointers. The returned security information may only be invalidated in a call
 * to @ref anjay_fw_update_reset_t or after a call to @ref anjay_delete .
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), @ref anjay_fw_update_load_security_from_dm will be used as
 * a default way to get security information. You may also use that function
 * yourself, for example as a fallback mechanism.
 *
 * @param user_ptr          Opaque pointer to user data, as passed to
 *                          @ref anjay_fw_update_install
 *
 * @param out_security_info Pointer in which the handler shall fill in security
 *                          configuration to use for download. Note that leaving
 *                          this value as empty without filling it in will
 *                          result in a configuration that is <strong>valid, but
 *                          very insecure</strong>: it will cause any server
 *                          certificate to be accepted without validation. Any
 *                          pointers used within the supplied structure shall
 *                          remain valid until a call to
 *                          @ref anjay_fw_update_reset_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_FW_UPDATE_ERR_*</c> value is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource.
 */
typedef int
anjay_fw_update_get_security_info_t(void *user_ptr,
                                    avs_net_security_info_t *out_security_info,
                                    const char *download_uri);

/**
 * Returns tx_params used to override default ones.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), <c>udp_tx_params</c> from <c>anjay_t</c> object are used.
 *
 * @param user_ptr      Opaque pointer to user data, as passed to
 *                      @ref anjay_fw_update_install .
 *
 * @param download_uri  Target firmware URI.
 *
 * @returns Object with CoAP transmission parameters.
 */
typedef avs_coap_tx_params_t
anjay_fw_update_get_coap_tx_params_t(void *user_ptr, const char *download_uri);

/**
 * Handler callbacks that shall implement the platform-specific part of firmware
 * update process.
 *
 * The Firmware Update object logic may be in one of the following states:
 *
 * - <strong>Idle</strong>. This is the state in which the object is just after
 *   creation (unless initialized with either
 *   <c>ANJAY_FW_UPDATE_INITIAL_DOWNLOADED</c> or
 *   <c>ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c>). The following handlers may be
 *   called in this state:
 *   - <c>stream_open</c> - shall open the download stream; moves the object
 *     into the <em>Downloading</em> state
 *   - <c>get_security_info</c> - shall fill in security info that shall be used
 *     for a given URL
 *   - <c>reset</c> - shall free data allocated by <c>get_security_info</c>, if
 *     it was called and there is any
 * - <strong>Downloading</strong>. The object might be initialized directly into
 *   this state by using <c>ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c>. In this
 *   state, the download stream is open and data may be transferred. The
 *   following handlers may be called in this state:
 *   - <c>stream_write</c> - shall write a chunk of data into the download
 *     stream; it normally does not change state - however, if it fails, it will
 *     be immediately followed by a call to <c>reset</c>
 *   - <c>stream_finish</c> - shall close the download stream and perform
 *     integrity check on the downloaded image; if successful, this moves the
 *     object into the <em>Downloaded</em> state. If failed - into the
 *     <em>Idle</em> state; note that <c>reset</c> will NOT be called in that
 *     case
 *   - <c>reset</c> - shall remove all downloaded data; moves the object into
 *     the <em>Idle</em> state
 * - <strong>Downloaded</strong>. The object might be initialized directly into
 *   this state by using <c>ANJAY_FW_UPDATE_INITIAL_DOWNLOADED</c>. In this
 *   state, the firmware package has been downloaded and checked and is ready to
 *   be flashed. The following handlers may be called in this state:
 *   - <c>reset</c> - shall reset all downloaded data; moves the object into the
 *     <em>Idle</em> state
 *   - <c>get_name</c> - shall return the package name, if available
 *   - <c>get_version</c> - shall return the package version, if available
 *   - <c>perform_upgrade</c> - shall perform the actual upgrade; if it fails,
 *     it does not cause a state change and may be called again; upon success,
 *     it may be treated as a transition to a "terminal" state, after which the
 *     device is expected to reboot
 */
typedef struct {
    /** Opens the stream that will be used to write the firmware package to;
     * @ref anjay_fw_update_stream_open_t */
    anjay_fw_update_stream_open_t *stream_open;
    /** Writes data to the download stream;
     * @ref anjay_fw_update_stream_write_t */
    anjay_fw_update_stream_write_t *stream_write;
    /** Closes the download stream and prepares the firmware package to be
     * flashed; @ref anjay_fw_update_stream_finish_t */
    anjay_fw_update_stream_finish_t *stream_finish;

    /** Resets the firmware update state and performs any applicable cleanup of
     * temporary storage if necessary; @ref anjay_fw_update_reset_t */
    anjay_fw_update_reset_t *reset;

    /** Returns the name of downloaded firmware package;
     * @ref anjay_fw_update_get_name_t */
    anjay_fw_update_get_name_t *get_name;
    /** Return the version of downloaded firmware package;
     * @ref anjay_fw_update_get_version_t */
    anjay_fw_update_get_version_t *get_version;

    /** Performs the actual upgrade with previously downloaded package;
     * @ref anjay_fw_update_perform_upgrade_t */
    anjay_fw_update_perform_upgrade_t *perform_upgrade;

    /** Queries security information that shall be used for an encrypted
     * connection; @ref anjay_fw_update_get_security_info_t */
    anjay_fw_update_get_security_info_t *get_security_info;

    /** Queries CoAP transmission parameters to be used during firmware
     * update. */
    anjay_fw_update_get_coap_tx_params_t *get_coap_tx_params;
} anjay_fw_update_handlers_t;

/**
 * Installs the Firmware Update object in an Anjay object.
 *
 * The Firmware Update module does not require explicit cleanup; all resources
 * will be automatically freed up during the call to @ref anjay_delete.
 *
 * @param anjay         Anjay object for which the Firmware Update Object is
 *                      installed.
 *
 * @param handlers      Pointer to a set of handler functions that handle the
 *                      platform-specific part of firmware update process.
 *                      Note: Contents of the structure are NOT copied, so it
 *                      needs to remain valid for the lifetime of the object.
 *
 * @param user_arg      Opaque user pointer that will be passed as the first
 *                      argument to handler functions.
 *
 * @param initial_state Information about the state to initialize the Firmware
 *                      Update object in. It is intended to be used after either
 *                      an orderly reboot caused by a firmware update attempt to
 *                      report the update result, or by an unexpected reboot in
 *                      the middle of the download process. If the object shall
 *                      be initialized in a neutral initial state, <c>NULL</c>
 *                      might be passed.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_fw_update_install(
        anjay_t *anjay,
        const anjay_fw_update_handlers_t *handlers,
        void *user_arg,
        const anjay_fw_update_initial_state_t *initial_state);

/**
 * Helper function that is used as a default implementation of security
 * information querying for PULL-mode downloads from (D)TLS-encrypted URIs.
 *
 * Given a URI, the Security object is scanned for instances with Server URI
 * resource matching it in the following way:
 * - if there is at least one instance with matching hostname, protocol and port
 *   number, and valid secure connection configuration, the first such instance
 *   (in the order as returned via @ref anjay_dm_instance_it_t) is used
 * - otherwise, if there is at least one instance with matching hostname and
 *   valid secure connection configuration, the first such instance (in the
 *   order as returned via @ref anjay_dm_instance_it_t) is used
 *
 * The returned security information is exactly the same configuration that is
 * used for LwM2M connection with the server chosen with the rules described
 * above.
 *
 * @param anjay Anjay object whose data model shall be queried.
 *
 * @param uri   URI for which to find security configuration.
 *
 * @returns Security information found, or <c>NULL</c> if no suitable LwM2M
 *          Security Object instance could be found. The returned structure is
 *          heap-allocated; to release all memory allocated for it, it is enough
 *          to call <c>avs_free()</c> on the returned pointer.
 */
avs_net_security_info_t *anjay_fw_update_load_security_from_dm(anjay_t *anjay,
                                                               const char *uri);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_FW_UPDATE_H */
