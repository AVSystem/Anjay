/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_SW_MGMT_H
#define ANJAY_INCLUDE_ANJAY_SW_MGMT_H

#include <anjay/anjay_config.h>
#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Numeric values of the Update Result resource. See LwM2M
 * specification related to object 9 for details.
 */
typedef enum {
    ANJAY_SW_MGMT_UPDATE_RESULT_INITIAL = 0,
    ANJAY_SW_MGMT_UPDATE_RESULT_DOWNLOADING = 1,
    ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLED = 2,
    ANJAY_SW_MGMT_UPDATE_RESULT_DOWNLOADED_VERIFIED = 3,
    ANJAY_SW_MGMT_UPDATE_RESULT_NOT_ENOUGH_SPACE = 50,
    ANJAY_SW_MGMT_UPDATE_RESULT_OUT_OF_MEMORY = 51,
    ANJAY_SW_MGMT_UPDATE_RESULT_CONNECTION_LOST = 52,
    ANJAY_SW_MGMT_UPDATE_RESULT_INTEGRITY_FAILURE = 53,
    ANJAY_SW_MGMT_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE = 54,
    ANJAY_SW_MGMT_UPDATE_RESULT_INVALID_URI = 56,
    ANJAY_SW_MGMT_UPDATE_RESULT_UPDATE_ERROR = 57,
    ANJAY_SW_MGMT_UPDATE_RESULT_INSTALLATION_FAILURE = 58,
    ANJAY_SW_MGMT_UPDATE_RESULT_UNINSTALLATION_FAILURE = 59
} anjay_sw_mgmt_update_result_t;

/** @name Software update result codes
 * @{
 * The following result codes may be returned from
 * @ref anjay_sw_mgmt_stream_write_t or @ref anjay_sw_mgmt_stream_finish_t to
 * control the value of Update Result resource in case of an error.
 *
 * Their values correspond to negated numeric values of that resource. However,
 * attempting to use other negated value will be checked and cause a fall-back
 * to a value default for a given handler.
 */
#define ANJAY_SW_MGMT_ERR_NOT_ENOUGH_SPACE \
    (-(int) ANJAY_SW_MGMT_UPDATE_RESULT_NOT_ENOUGH_SPACE)
#define ANJAY_SW_MGMT_ERR_OUT_OF_MEMORY \
    (-(int) ANJAY_SW_MGMT_UPDATE_RESULT_OUT_OF_MEMORY)
#define ANJAY_SW_MGMT_ERR_INTEGRITY_FAILURE \
    (-(int) ANJAY_SW_MGMT_UPDATE_RESULT_INTEGRITY_FAILURE)
#define ANJAY_SW_MGMT_ERR_UNSUPPORTED_PACKAGE_TYPE \
    (-(int) ANJAY_SW_MGMT_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE)
/** @} */

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Possible values that control Update State, Update Result and Activation State
 * resources at the time of initialization of the Software Management object.
 */
typedef enum {
    /**
     * Corresponds to the "Initial" Update State and "Initial" Update Result.
     * Shall be used for software instances which are not yet downloaded.
     *
     * Note: "Initial" Update State and "Initial" Update Result can be also
     * caused by preparing installed software for update process (by executing
     * Uninstall resource with "1" argument), although, in case of a reboot, it
     * is recommended to revert back to "Installed" Update State by initializing
     * the object instance with @ref
     * ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED or @ref
     * ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_ACTIVATED . Software Management
     * Object in its current state is not able to differentiate these two
     * situations.
     */
    ANJAY_SW_MGMT_INITIAL_STATE_IDLE,

    /**
     * Corresponds to the "Downloaded" Update State and "Initial" Update Result.
     * Shall be used when the device unexpectedly rebooted when the software
     * package has already been downloaded into some non-volatile memory and
     * integrity check wasn't performed yet.
     */
    ANJAY_SW_MGMT_INITIAL_STATE_DOWNLOADED,

    /**
     * Corresponds to the "Delivered" Update State and "Initial" Update Result.
     * Shall be used when the device unexpectedly rebooted when the software
     * package has already been downloaded into some non-volatile memory and
     * integrity check was performed.
     */
    ANJAY_SW_MGMT_INITIAL_STATE_DELIVERED,

    /**
     * Corresponds to the "Delivered" Update State and "Initial" Update Result.
     * Shall be used when the device has rebooted as a part of installation
     * process, which hasn't completed yet. The application should call @ref
     * anjay_sw_mgmt_finish_pkg_install to set the result to success or failure
     * after the installation process is complete.
     */
    ANJAY_SW_MGMT_INITIAL_STATE_INSTALLING,

    /**
     * Corresponds to the "Installed" Update State, "Installed"
     * Update Result and Activation State set to false. Shall be used when given
     * software instance is installed, but deactivated.
     */
    ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_DEACTIVATED,

    /**
     * Corresponds to the "Installed" Update State, "Installed"
     * Update Result and Activation State set to true. Shall be used when given
     * software instance is installed and activated.
     */
    ANJAY_SW_MGMT_INITIAL_STATE_INSTALLED_ACTIVATED
} anjay_sw_mgmt_initial_state_t;

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Information about the state to initialize the Software Management object
 * instance.
 */
typedef struct {
    /**
     * Controls initialization of Update State, Update Result and Activation
     * State resources.
     */
    anjay_sw_mgmt_initial_state_t initial_state;

    /**
     * Software Management object instance ID. As the server may expect
     * the instance ID's to be unchanged, they must be set explicitly
     * by the user.
     */
    anjay_iid_t iid;

    /**
     * Opaque pointer to instance-specific user data.
     */
    void *inst_ctx;
} anjay_sw_mgmt_instance_initializer_t;

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Opens the stream that will be used to write the software package to.
 *
 * The intended way of implementing this handler is to open a temporary file
 * using <c>fopen()</c> or allocate some memory buffer that may then be used to
 * store the downloaded data in. The library will not attempt to call
 * @ref anjay_sw_mgmt_stream_write_t without having previously called
 * @ref anjay_sw_mgmt_stream_open_t . Please see
 * @ref anjay_sw_mgmt_handlers_t for more information about state transitions.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed
 *                 to @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int
anjay_sw_mgmt_stream_open_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Writes data to the download stream.
 *
 * May be called multiple times after @ref anjay_sw_mgmt_stream_open_t, once
 * for each consecutive chunk of downloaded data.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @param data     Pointer to a chunk of the software package being downloaded.
 *                 Guaranteed to be non-<c>NULL</c>.
 *
 * @param length   Number of bytes in the chunk pointed to by <c>data</c>.
 *                 Guaranteed to be greater than zero.
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_SW_MGMT_ERR_*</c> value is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource.
 */
typedef int anjay_sw_mgmt_stream_write_t(void *obj_ctx,
                                         anjay_iid_t iid,
                                         void *inst_ctx,
                                         const void *data,
                                         size_t length);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Closes the download stream.
 *
 * Will be called after a series of @ref anjay_sw_mgmt_stream_write_t calls
 * after the whole package is downloaded.
 *
 * The intended way of implementing this handler is to e.g. call <c>fclose()</c>
 * on the downloaded file. After that, package might also be uncompressed,
 * decrypted and checked for integrity in @ref anjay_sw_mgmt_check_integrity_t .
 * The exact split of responsibility between these two methods is not clearly
 * defined and up to implementor.
 *
 * Note that regardless of the return value, the stream is considered to be
 * closed. That is, upon successful return, the Update State resource is
 * considered to be either in the <em>Downloaded</em> state, and upon
 * returning an error - in the <em>Initial</em> state, with appropriate
 * Update Result set.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_SW_MGMT_ERR_*</c> value is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource.
 */
typedef int
anjay_sw_mgmt_stream_finish_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Conducts integrity check of downloaded package.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), integrity check will be entirely skipped, and the Update
 * State resource upon finished download will change the state directly from
 * <em>Download started</em> to <em>Delivered</em>.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_SW_MGMT_ERR_*</c> value is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource.
 */
typedef int
anjay_sw_mgmt_check_integrity_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Resets the software installation state and performs any applicable cleanup of
 * temporary storage if necessary.
 *
 * Will be called at request of the server (upon execution of Uninstall resource
 * in <em>Delivered</em> state in purpose of removing downloaded, but not yet
 * installed software package) or after a failed download. Note that it may be
 * called without previously calling @ref anjay_sw_mgmt_stream_finish_t, so it
 * shall also close the currently open download stream, if any.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 */
typedef void
anjay_sw_mgmt_reset_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Returns the name of downloaded software package.
 *
 * The name will be exposed in the data model as the PkgName Resource. If this
 * callback returns <c>NULL</c> or is not implemented at all (with the
 * corresponding field set to <c>NULL</c>), that Resource will have its value
 * set to empty string.
 *
 * It only makes sense for this handler to return non-<c>NULL</c> values if
 * there is a valid package already downloaded. The library will call this
 * handler in <em>Delivered</em> and <em>Installed</em> states.
 *
 * The library will not attempt to deallocate the returned pointer. User code
 * must assure that the pointer will remain valid at least until return from
 * @ref anjay_serve or @ref anjay_sched_run .
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package name, or <c>NULL</c> if it is not currently
 *          available.
 */
typedef const char *
anjay_sw_mgmt_get_name_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Returns the version of downloaded software package.
 *
 * The version will be exposed in the data model as the PkgVersion Resource. If
 * this callback returns <c>NULL</c> or is not implemented at all (with the
 * corresponding field set to <c>NULL</c>), that Resource will have its value
 * set to empty string.
 *
 * It only makes sense for this handler to return non-<c>NULL</c> values if
 * there is a valid package already downloaded. The library will call this
 * handler in <em>Delivered</em> and <em>Installed</em> states.
 *
 * The library will not attempt to deallocate the returned pointer. User code
 * must assure that the pointer will remain valid at least until return from
 * @ref anjay_serve or @ref anjay_sched_run .
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package name, or <c>NULL</c> if it is not currently
 *          available.
 */
typedef const char *
anjay_sw_mgmt_get_version_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Performs the actual installation of previously downloaded software package.
 *
 * Will be called at the request of the server, after a package has been
 * downloaded and its integrity has been checked.
 *
 * Some users will want to implement software installation in a way that
 * involves a reboot. In such case, it is expected that this callback will do
 * either one of the following:
 *
 * - software package installation, terminate outermost event loop and return,
 *   call reboot after @ref anjay_event_loop_run()
 * - perform the software package installation internally and then reboot, it
 *   means that the return will never happen (although the library won't be able
 *   to send the acknowledgement of execution of Install resource)
 *
 * After rebooting, the result of the installation process may be passed to the
 * library during initialization via the <c>initial_state</c> field of
 * @ref anjay_sw_mgmt_instance_initializer_t .
 *
 * Alternatively, if the installation can be performed without reinitializing
 * Anjay, you can use @ref anjay_sw_mgmt_finish_pkg_install (either from within
 * the handler or some time after returning from it) to pass the installation
 * result.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return a negative value if it can be determined
 *          without a reboot that the package installation cannot be
 *          successfully performed. Error codes are <strong>NOT</strong> handled
 *          here, so attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 *
 */
typedef int
anjay_sw_mgmt_pkg_install_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Uninstalls software package.
 *
 * This callback will be called only in <em>Installed</em> state, if the
 * Uninstall resource was executed with no argument or argument "0".
 *
 * If this callback is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), uninstalling software will not be possible.
 *
 * Note: in case the server requests to remove the software package
 * which has been delivered, but not yet installed (<em>Delivered</em> state),
 * anjay_sw_mgmt_reset_t callback will be used.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int
anjay_sw_mgmt_pkg_uninstall_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Prepares software package for update.
 *
 * This callback will be called only in <em>Installed</em> state, if the
 * Uninstall resource was executed with argument "1".
 *
 * If this callback is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), updating software will not be possible.
 *
 * Most users will want to implement this callback as a no-op.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int anjay_sw_mgmt_prepare_for_update_t(void *obj_ctx,
                                               anjay_iid_t iid,
                                               void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Activates software package.
 *
 * This callback will be called only in <em>Installed</em> state. The activation
 * state does not affect the execution of this callback. If the user wants to
 * block the execution when the package is already active, this must be done on
 * user side. The @ref anjay_sw_mgmt_get_activation_state function may be
 * useful.
 *
 * Some of the users will want to opt-out from ability to handle the activation
 * state - if this callback is not implemented at all (with the corresponding
 * field set to <c>NULL</c>), executing Activate resource will always succeed.
 * If this callback is not implemented, @ref anjay_sw_mgmt_deactivate_t MUST
 * NOT be implemented too.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error or when user do not want to execute this callback.
 *          Error codes are <strong>NOT</strong> handled here, so attempting to
 *          return <c>ANJAY_SW_MGMT_ERR_*</c> values will <strong>NOT</strong>
 *          cause any effect different than any other negative value.
 */
typedef int
anjay_sw_mgmt_activate_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Deactivates software package.
 *
 * This callback will be called only in <em>Installed</em> state. The activation
 * state does not affect the execution of this callback. If the user wants to
 * block the execution when the package is already deactivate, this must be done
 * on user side. The @ref anjay_sw_mgmt_get_activation_state function may be
 * useful.
 *
 * Some of the users will want to opt-out from ability to handle the activation
 * state - if this callback is not implemented at all (with the corresponding
 * field set to <c>NULL</c>), executing Deactivate resource will always succeed.
 * If this callback is not implemented, @ref anjay_sw_mgmt_activate_t MUST
 * NOT be implemented too.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error or when user do not want to execute this callback.
 *          Error codes are <strong>NOT</strong> handled here, so attempting to
 *          return <c>ANJAY_SW_MGMT_ERR_*</c> values will <strong>NOT</strong>
 *          cause any effect different than any other negative value.
 */
typedef int
anjay_sw_mgmt_deactivate_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

#ifdef ANJAY_WITH_DOWNLOADER

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Queries security information that shall be used for an encrypted connection
 * with a PULL-mode download server.
 *
 * May be called before @ref anjay_sw_mgmt_stream_open_t if the download is to
 * be performed in PULL mode and the connection needs to use TLS or DTLS
 * encryption.
 *
 * Note that the @ref anjay_security_config_t contains references to file paths,
 * binary security keys, and/or ciphersuite lists. It is the user's
 * responsibility to appropriately allocate them and ensure proper lifetime of
 * the returned pointers. The returned security information may only be
 * invalidated in a call to @ref anjay_sw_mgmt_reset_t or after a call to
 * @ref anjay_delete .
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), @ref anjay_security_config_from_dm will be used as a default
 * way to get security information.
 *
 * <strong>WARNING:</strong> If the aforementioned @ref
 * anjay_security_config_from_dm function won't find any server
 * connection that matches the <c>download_uri</c> by protocol,
 * hostname and port triple, it'll attempt to match a configuration just by the
 * hostname. This may cause Anjay to use wrong security configuration, e.g. in
 * case when both CoAPS LwM2M server and HTTPS software package server have the
 * same hostname, but require different security configs.
 *
 * If no user-defined handler is provided and the call to
 * @ref anjay_security_config_from_dm fails (including case when no matching
 * LwM2M Security Object instance is found, even just by the hostname),
 * @ref anjay_security_config_pkix will be used as an additional fallback
 * if <c>ANJAY_WITH_LWM2M11</c> is enabled and a valid trust store is available
 * (either specified through <c>use_system_trust_store</c>,
 * <c>trust_store_certs</c> or <c>trust_store_crls</c> fields in
 * <c>anjay_configuration_t</c>, or obtained via <c>/est/crts</c> request if
 * <c>est_cacerts_policy</c> is set to
 * <c>ANJAY_EST_CACERTS_IF_EST_CONFIGURED</c> or
 * <c>ANJAY_EST_CACERTS_ALWAYS</c>).
 *
 * You may also use these functions yourself, for example as a fallback
 * mechanism.
 *
 * @param obj_ctx             Opaque pointer to object-wide user data, as
 *                            passed to @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid                 ID of Software Management object instance.
 *
 * @param inst_ctx            Opaque pointer to instance-specific user data,
 *                            as passed to
 *                            @ref anjay_sw_mgmt_instance_initializer_t or
 *                            <c>out_inst_ctx</c> parameter of
 *                            @ref anjay_sw_mgmt_add_handler_t .
 *
 * @param download_uri        URI of the package from which a Pull-mode download
 *                            is performed.
 *
 * @param out_security_info   Pointer in which the handler shall fill in
 *                            security configuration to use for download. Note
 *                            that leaving this value as empty without filling
 *                            it in will result in a configuration that is
 *                            <strong>valid, but very insecure</strong>: it will
 *                            cause any server certificate to be accepted
 *                            without validation. Any pointers used within the
 *                            supplied structure shall remain valid until either
 *                            a call to @ref anjay_sw_mgmt_reset_t, or exit to
 *                            the event loop (from either @ref anjay_serve,
 *                            @ref anjay_sched_run or
 *                            @ref anjay_sw_mgmt_add_instance), whichever
 *                            happens first. Anjay will <strong>not</strong>
 *                            attempt to deallocate anything automatically.
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int
anjay_sw_mgmt_get_security_config_t(void *obj_ctx,
                                    anjay_iid_t iid,
                                    void *inst_ctx,
                                    const char *download_uri,
                                    anjay_security_config_t *out_security_info);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Returns tx_params used to override default ones.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), <c>udp_tx_params</c> from <c>anjay_t</c> object are used.
 *
 * @param obj_ctx      Opaque pointer to object-wide user data, as passed to
 *                     @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid          ID of Software Management object instance.
 *
 * @param inst_ctx     Opaque pointer to instance-specific user data, as passed
 *                     to @ref anjay_sw_mgmt_instance_initializer_t or
 *                     <c>out_inst_ctx</c> parameter of
 *                     @ref anjay_sw_mgmt_add_handler_t .
 *
 * @param download_uri Target software URI.
 *
 * @returns Object with CoAP transmission parameters.
 */
typedef avs_coap_udp_tx_params_t
anjay_sw_mgmt_get_coap_tx_params_t(void *obj_ctx,
                                   anjay_iid_t iid,
                                   void *inst_ctx,
                                   const char *download_uri);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Returns request timeout to be used during software download over CoAP+TCP or
 * HTTP.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), <c>coap_tcp_request_timeout</c> from <c>anjay_t</c> object
 * will be used for CoAP+TCP, and <c>AVS_NET_SOCKET_DEFAULT_RECV_TIMEOUT</c>
 * (i.e., 30 seconds) will be used for HTTP.
 *
 * @param obj_ctx      Opaque pointer to object-wide user data, as passed to
 *                     @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid          ID of Software Management object instance.
 *
 * @param inst_ctx     Opaque pointer to instance-specific user data, as passed
 *                     to @ref anjay_sw_mgmt_instance_initializer_t or
 *                     <c>out_inst_ctx</c> parameter of
 *                     @ref anjay_sw_mgmt_add_handler_t .
 *
 * @param download_uri  Target software URI.
 *
 * @returns The desired request timeout. If the value returned is non-positive
 *          (including zero and invalid value), the default will be used.
 */
typedef avs_time_duration_t
anjay_sw_mgmt_get_tcp_request_timeout_t(void *obj_ctx,
                                        anjay_iid_t iid,
                                        void *inst_ctx,
                                        const char *download_uri);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Suspends the operation of PULL-mode downloads in the Software Management
 * module.
 *
 * This will have the effect of suspending any ongoing downloads (see
 * @ref anjay_download_suspend for details), as well as preventing new downloads
 * from being started.
 *
 * When PULL-mode downloads are suspended, @ref anjay_sw_mgmt_stream_open_t
 * will <strong>NOT</strong> be called when a download request is issued.
 * However, @ref anjay_sw_mgmt_get_security_config_t,
 * @ref anjay_sw_mgmt_get_coap_tx_params_t and
 * @ref anjay_sw_mgmt_get_tcp_request_timeout_t will be called. You may call
 * @ref anjay_sw_mgmt_pull_reconnect from one of these functions if you decide
 * to accept the download immediately after all.
 *
 * @param anjay         Anjay object to operate on.
 */
void anjay_sw_mgmt_pull_suspend(anjay_t *anjay);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Reconnects any ongoing PULL-mode downloads in the Software Management module.
 * Which could be disconnected due to connection loss or deliberate suspend.
 * In the latter case, when PULL-mode downloads are suspended (see
 * @ref anjay_sw_mgmt_pull_suspend), resumes normal operation.
 *
 * If an ongoing PULL-mode download exists, this will call
 * @ref anjay_download_reconnect internally, so you may want to reference the
 * documentation of that function for details.
 *
 * @param anjay         Anjay object to operate on.
 *
 * @returns 0 on success; -1 if @p anjay does not have the Software Management
 *          object installed or the latest non-zero error code returned by @ref
 *          anjay_download_reconnect .
 */
int anjay_sw_mgmt_pull_reconnect(anjay_t *anjay);

#endif // ANJAY_WITH_DOWNLOADER

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Handles server's request to create new instance of Software Management
 * object.
 *
 * This callback allows the user to set up user-specific data or to reject
 * server's attempt to create a new object instance.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), library won't allow creating new instances of the object.
 *
 * This callback won't be called if the application adds a new instance of
 * the object on its own.
 *
 * @param obj_ctx      Opaque pointer to object-wide user data, as passed to
 *                     @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid          ID of Software Management object instance.
 *
 * @param out_inst_ctx Pointer in which the handler shall fill in
 *                     opaque pointer to instance-specific user data.
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int anjay_sw_mgmt_add_handler_t(void *obj_ctx,
                                        anjay_iid_t iid,
                                        void **out_inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Handles server's request to remove an instance of Software Management object.
 *
 * This callback allows the user to clean up user-specific data or to reject
 * server's attempt to remove an object instance.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), library won't allow deleting instances of the object.
 *
 * @param obj_ctx  Opaque pointer to object-wide user data, as passed to
 *                 @ref anjay_sw_mgmt_settings_t .
 *
 * @param iid      ID of Software Management object instance.
 *
 * @param inst_ctx Opaque pointer to instance-specific user data, as passed to
 *                 @ref anjay_sw_mgmt_instance_initializer_t or
 *                 <c>out_inst_ctx</c> parameter of
 *                 @ref anjay_sw_mgmt_add_handler_t .
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_SW_MGMT_ERR_*</c> values will
 *          <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int
anjay_sw_mgmt_remove_handler_t(void *obj_ctx, anjay_iid_t iid, void *inst_ctx);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 */
typedef struct {
    /** Opens the stream that will be used to write the software package to;
     * @ref anjay_sw_mgmt_stream_open_t */
    anjay_sw_mgmt_stream_open_t *stream_open;

    /** Writes data to the download stream;
     * @ref anjay_sw_mgmt_stream_write_t */
    anjay_sw_mgmt_stream_write_t *stream_write;

    /** Closes the download stream; @ref anjay_sw_mgmt_stream_finish_t */
    anjay_sw_mgmt_stream_finish_t *stream_finish;

    /** Prepares the software package to be installed and checks
     * its integrity; @ref anjay_sw_mgmt_check_integrity_t */
    anjay_sw_mgmt_check_integrity_t *check_integrity;

    /** Resets the software installation state and performs any applicable
     * cleanup of temporary storage if necessary; @ref anjay_sw_mgmt_reset_t */
    anjay_sw_mgmt_reset_t *reset;

    /** Returns the name of downloaded software package;
     * @ref anjay_sw_mgmt_get_name_t */
    anjay_sw_mgmt_get_name_t *get_name;

    /** Returns the version of downloaded software package;
     * @ref anjay_sw_mgmt_get_version_t */
    anjay_sw_mgmt_get_version_t *get_version;

    /** Installs downloaded software package; @ref anjay_sw_mgmt_pkg_install_t
     */
    anjay_sw_mgmt_pkg_install_t *pkg_install;

    /** Uninstalls software package; @ref anjay_sw_mgmt_pkg_uninstall_t */
    anjay_sw_mgmt_pkg_uninstall_t *pkg_uninstall;

    /** Prepares software package for update process; @ref
     * anjay_sw_mgmt_prepare_for_update_t */
    anjay_sw_mgmt_prepare_for_update_t *prepare_for_update;

    /** Activates software package; @ref anjay_sw_mgmt_activate_t */
    anjay_sw_mgmt_activate_t *activate;

    /** Deactivates software package; @ref anjay_sw_mgmt_deactivate_t */
    anjay_sw_mgmt_deactivate_t *deactivate;

#ifdef ANJAY_WITH_DOWNLOADER
    /** Queries security configuration that shall be used for an encrypted
     * connection; @ref anjay_sw_mgmt_get_security_config_t */
    anjay_sw_mgmt_get_security_config_t *get_security_config;

    /** Queries request timeout to be used during software download over
     * CoAP+TCP or HTTP; @ref anjay_sw_mgmt_get_tcp_request_timeout_t */
    anjay_sw_mgmt_get_tcp_request_timeout_t *get_tcp_request_timeout;

#    ifdef ANJAY_WITH_COAP_DOWNLOAD
    /** Queries CoAP transmission parameters to be used during download
     * process; @ref anjay_sw_mgmt_get_coap_tx_params_t */
    anjay_sw_mgmt_get_coap_tx_params_t *get_coap_tx_params;
#    endif // ANJAY_WITH_COAP_DOWNLOAD
#endif     // ANJAY_WITH_DOWNLOADER

    /** Accepts or rejects server's request to create a new instance
     * of Software Management object; @ref anjay_sw_mgmt_add_handler_t */
    anjay_sw_mgmt_add_handler_t *add_handler;

    /** Accepts or rejects server's request to remove an instance
     * of Software Management object; @ref anjay_sw_mgmt_remove_handler_t */
    anjay_sw_mgmt_remove_handler_t *remove_handler;
} anjay_sw_mgmt_handlers_t;

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Settings of the Software Management module, global for all instances
 * installed.
 */
typedef struct {
    /**
     * Pointer to a set of handler functions that handle the
     * platform-specific part of software management.
     * Note: Contents of the structure are NOT copied, so it
     * needs to remain valid for the lifetime of the object.
     */
    const anjay_sw_mgmt_handlers_t *handlers;

    /**
     * Opaque pointer to object-wide user data that will be
     * passed as the first argument to handler functions.
     */
    void *obj_ctx;

#if defined(ANJAY_WITH_DOWNLOADER)
    /**
     * Informs the module to try reusing sockets of existing LwM2M Servers to
     * download the software package if the download URI matches any of the
     * LwM2M Servers.
     */
    bool prefer_same_socket_downloads;
#endif // defined(ANJAY_WITH_DOWNLOADER)
} anjay_sw_mgmt_settings_t;

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Installs the Software Management object in an Anjay object.
 *
 * The Software Management module does not require explicit cleanup; all
 * resources will be automatically freed up during the call to @ref
 * anjay_delete.
 *
 * Specific instances of Software Management object shall be created using
 * @ref anjay_sw_mgmt_add_instance . It is desirable to create all instances
 * expected by the server before the first call to @ref anjay_event_loop_run ,
 * @ref anjay_serve or @ref anjay_sched_run , to make sure that they are present
 * from the beginning of the device registration.
 *
 * @param anjay    Anjay object for which the Software Management Object is
 *                 installed.
 *
 * @param settings Configuration of Software Management module, see @ref
 *                 anjay_sw_mgmt_settings_t for details.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_sw_mgmt_install(anjay_t *anjay,
                          const anjay_sw_mgmt_settings_t *settings);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Checks if the instance state is <em>Installed</em> and return the activation
 * state via \p out_state argument. Can be used in @ref anjay_sw_mgmt_activate_t
 * or @ref anjay_sw_mgmt_deactivate_t to check if we want to proceed with
 * current activation state with the code responsible for
 * activation/deactivation the package.
 *
 * @param anjay          Anjay object for which the Software Management Object
 *                       is installed.
 *
 * @param iid            ID of Software Management object instance.
 *
 * @param out_state      Activation state of Software Management object
 *                       instance.
 *
 * @returns 0 on success, -1 if there is no such instance or instance state is
 *          different than <em>Installed</em>
 */
int anjay_sw_mgmt_get_activation_state(anjay_t *anjay,
                                       anjay_iid_t iid,
                                       bool *out_state);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Possible values that control package state after installation.
 */
typedef enum {
    /**
     * Corresponds to the "Installed" Update State, "Installed" Update Result
     * and Activation State set to false.
     */
    ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_INACTIVE,
    /**
     * Corresponds to the "Installed" Update State, "Installed" Update Result
     * and Activation State set to true.
     *
     * WARNING: Setting the Activation State to true via @ref
     * anjay_sw_mgmt_finish_pkg_install breaks the specifications. Activation
     * should be done on the server side. However, there are known cases in
     * which such behavior is required.
     */
    ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_ACTIVE,
    /**
     * Corresponds to the "Delivered" Update State, "Installation failure"
     * Update Result and Activation State set to false.
     */
    ANJAY_SW_MGMT_FINISH_PKG_INSTALL_FAILURE
} anjay_sw_mgmt_finish_pkg_install_result_t;

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Marks delivered software package as installed and optionally activated,
 * making transition to <em>Installed</em> state or reports installation error.
 *
 * WARNING: Calling this function is only valid in <em>Delivered</em> state,
 * directly in the @ref anjay_sw_mgmt_pkg_install_t handler, or in some later
 * point of time, possibly after a reboot, as explained in
 * @ref anjay_sw_mgmt_pkg_install_t .
 *
 * NOTE: Setting activation state with this function does <strong>NOT</strong>
 * mean that activation ( @ref anjay_sw_mgmt_activate_t ) or deactivation ( @ref
 * anjay_sw_mgmt_deactivate_t ) software package handler will be called. Setting
 * activation state to true after installation breaks the specifications, but
 * there are known cases when this behavior is required.
 *
 * NOTE: If this function is called inside @ref anjay_sw_mgmt_pkg_install_t
 * handler with \p pkg_install_result set to @ref
 * ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_INACTIVE or @ref
 * ANJAY_SW_MGMT_FINISH_PKG_INSTALL_SUCCESS_ACTIVE , the handler is expected to
 * return 0. Otherwise, returning nonzero value will cause the result set by
 * this function being overwritten.
 *
 * @param anjay                 Anjay object for which the Software Management
 *                              Object is installed.
 *
 * @param iid                   ID of Software Management object instance.
 *
 * @param pkg_install_result    Result of the installation process.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_sw_mgmt_finish_pkg_install(
        anjay_t *anjay,
        anjay_iid_t iid,
        anjay_sw_mgmt_finish_pkg_install_result_t pkg_install_result);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Adds an instance of Software Management object.
 *
 * This method will not cause @ref anjay_sw_mgmt_add_handler_t to be called,
 * as this method creates a new instance of the object on application's request.
 *
 * @param anjay                Anjay object for which the Software Management
 *                             Object is installed.
 *
 * @param instance_initializer Information about the state to initialize the
 *                             Software Management object instance in.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_sw_mgmt_add_instance(
        anjay_t *anjay,
        const anjay_sw_mgmt_instance_initializer_t *instance_initializer);

/**
 * @experimental This is experimental Software Management object API. This API
 * can change in future versions without any notice.
 *
 * Remove an instance of Software Management object.
 *
 * This method will not cause @ref anjay_sw_mgmt_remove_handler_t to be called,
 * as this method deletes a instance of the object on application's request.
 *
 * <strong>CAUTION:</strong> Calling this function inside any Software
 * Management module handler with the same \p iid as passed to the
 * handler, will result in an error code with value 1. This function shouldn't
 * be called from any module handler.
 * In multi-threaded scenarios, it should be expected that this function can
 * also return an error code with value 1, in case one thread calls this
 * function when another thread is executing one of the module's handler
 * associated with the instance with the same \p iid as the one passed to this
 * function. In this case, the user should wait a while and call this function
 * again.
 *
 * @param anjay Anjay object for which the Software Management Object is
 *              installed.
 *
 * @param iid   ID of Software Management object instance.
 *
 * @returns 0 on success;
 *          1 if a handler associated with an instance with the same \p iid as
 *          the one passed to this function is currently being executed;
 *          a negative value in case of error
 */
int anjay_sw_mgmt_remove_instance(anjay_t *anjay, anjay_iid_t iid);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_SW_MGMT_H */
