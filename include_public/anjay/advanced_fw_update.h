/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_ADVANCED_FW_UPDATE_H
#define ANJAY_INCLUDE_ANJAY_ADVANCED_FW_UPDATE_H

#include <anjay/core.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANJAY_ADVANCED_FW_UPDATE_OID 33629

/**
 * Numeric values of the Advanced Firmware Update State resource.
 * See AVSystem specification of Advanced Firmware Update for details.
 *
 * Note: they SHOULD only be used with
 * @ref anjay_advanced_fw_update_set_state_and_result .
 */
typedef enum {
    ANJAY_ADVANCED_FW_UPDATE_STATE_IDLE = 0,
    ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING,
    ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED,
    ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING
} anjay_advanced_fw_update_state_t;

/**
 * Numeric values of the Advanced Firmware Update Result resource.
 * See AVSystem specification of Advanced Firmware Update for details.
 *
 * Note: they SHOULD only be used with
 * @ref anjay_advanced_fw_update_set_state_and_result .
 */
typedef enum {
    ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL = 0,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS = 1,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE = 2,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY = 3,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_CONNECTION_LOST = 4,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_INTEGRITY_FAILURE = 5,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE = 6,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_INVALID_URI = 7,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_FAILED = 8,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL = 9,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_UPDATE_CANCELLED = 10,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED = 11,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_CONFLICTING_STATE = 12,
    ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR = 13,
} anjay_advanced_fw_update_result_t;

/** @name Advanced Firmware Update result codes
 * @{
 * The following result codes may be returned from
 * @ref anjay_advanced_fw_update_stream_write_t,
 * @ref anjay_advanced_fw_update_stream_finish_t or
 * @ref anjay_advanced_fw_update_perform_upgrade_t to control the value of the
 * Update Result Resource after the failure.
 *
 * Their values correspond to negated numeric values of that resource. However,
 * attempting to use other negated value will be checked and cause a fall-back
 * to a value default for a given handler.
 */
#define ANJAY_ADVANCED_FW_UPDATE_ERR_NOT_ENOUGH_SPACE \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_NOT_ENOUGH_SPACE)
#define ANJAY_ADVANCED_FW_UPDATE_ERR_OUT_OF_MEMORY \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_OUT_OF_MEMORY)
#define ANJAY_ADVANCED_FW_UPDATE_ERR_INTEGRITY_FAILURE \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_INTEGRITY_FAILURE)
#define ANJAY_ADVANCED_FW_UPDATE_ERR_UNSUPPORTED_PACKAGE_TYPE \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_UNSUPPORTED_PACKAGE_TYPE)
#define ANJAY_ADVANCED_FW_UPDATE_ERR_DEFERRED \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_DEFERRED)
#define ANJAY_ADVANCED_FW_UPDATE_ERR_CONFLICTING_STATE \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_CONFLICTING_STATE)
#define ANJAY_ADVANCED_FW_UPDATE_ERR_DEPENDENCY_ERROR \
    (-ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR)
/** @} */

/**
 * Numeric values of the Advanced Firmware Update Severity resource.
 * See AVSystem specification of Advanced Firmware Update for details.
 */
typedef enum {
    ANJAY_ADVANCED_FW_UPDATE_SEVERITY_CRITICAL = 0,
    ANJAY_ADVANCED_FW_UPDATE_SEVERITY_MANDATORY,
    ANJAY_ADVANCED_FW_UPDATE_SEVERITY_OPTIONAL
} anjay_advanced_fw_update_severity_t;

/**
 * Bool values of the Advanced Firmware Update object configuration.
 * This Advanced Firmware Update object configuration affects all instances.
 */
typedef struct {
    /**
     * Informs the module to try reusing sockets of existing LwM2M Servers to
     * download the firmware image if the download URI matches any of the LwM2M
     * Servers.
     */
    bool prefer_same_socket_downloads;
#ifdef ANJAY_WITH_SEND
    /**
     * Enables using LwM2M Send to report State, Update Result and Firmware
     * Version to the LwM2M Server (if LwM2M Send is enabled) during firmware
     * update.
     */
    bool use_lwm2m_send;
#endif // ANJAY_WITH_SEND
} anjay_advanced_fw_update_global_config_t;

/**
 * Information about the state to initialize the instances of Advanced
 * Firmware Update objects in.
 */
typedef struct {
    /**
     * Information about the state of update of particular instance of
     * Advance Firmware Update object, at the moment of initialization.
     */
    anjay_advanced_fw_update_state_t state;

    /**
     * Information about the result of update of particular instance of
     * Advance Firmware Update object, at the moment of initialization.
     */
    anjay_advanced_fw_update_result_t result;

    /**
     * Value to initialize the Severity resource with.
     */
    anjay_advanced_fw_update_severity_t persisted_severity;

    /**
     * Value to initialize the Last State Change Time resource with.
     */
    avs_time_real_t persisted_last_state_change_time;

    /**
     * Update deadline based on Maximum Defer Period resource value and time of
     * executing Update resource.
     */
    avs_time_real_t persisted_update_deadline;
} anjay_advanced_fw_update_initial_state_t;

/**
 * Opens the stream that will be used to write the firmware package to.
 *
 * The intended way of implementing this handler is to open a temporary file
 * using <c>fopen()</c> or allocate some memory buffer that may then be used to
 * store the downloaded data in. The library will not attempt to call
 * @ref anjay_advanced_fw_update_stream_write_t without having previously called
 * @ref anjay_advanced_fw_update_stream_open_t . Please see
 * @ref anjay_advanced_fw_update_handlers_t for more information about state
 * transitions.
 *
 * Note that this handler will NOT be called after initializing the object with
 * the <c>ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING</c> option, so any
 * necessary resources shall be already open before calling
 * @ref anjay_advanced_fw_update_instance_add .
 *
 * @param iid          Instance ID of an Advanced Firmware Object which tries to
 *                     open a stream.
 *
 * @param user_ptr     Opaque pointer to user data, as passed to
 *                     @ref anjay_advanced_fw_update_instance_add
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. Error codes are <strong>NOT</strong> handled here, so
 *          attempting to return <c>ANJAY_ADVANCED_FW_UPDATE_ERR_*</c> values
 *          will <strong>NOT</strong> cause any effect different than any other
 *          negative value.
 */
typedef int anjay_advanced_fw_update_stream_open_t(anjay_iid_t iid,
                                                   void *user_ptr);
/**
 * Writes data to the download stream.
 *
 * May be called multipled times after
 * @ref anjay_advanced_fw_update_stream_open_t, once for each consecutive chunk
 * of downloaded data.
 *
 * @param iid      Instance ID of an Advanced Firmware Object which tries to
 *                 write to a stream.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_advanced_fw_update_instance_add
 *
 * @param data     Pointer to a chunk of the firmware package being downloaded.
 *                 Guaranteed to be non-<c>NULL</c>.
 *
 * @param length   Number of bytes in the chunk pointed to by <c>data</c>.
 *                 Guaranteed to be greater than zero.
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_ADVANCED_FW_UPDATE_ERR_*</c>
 *          value is returned, an equivalent value will be set in the Update
 *          Result Resource.
 */
typedef int anjay_advanced_fw_update_stream_write_t(anjay_iid_t iid,
                                                    void *user_ptr,
                                                    const void *data,
                                                    size_t length);

/**
 * Closes the download stream and prepares the firmware package to be flashed.
 *
 * Will be called after a series of @ref anjay_advanced_fw_update_stream_write_t
 * calls, after the whole package is downloaded.
 *
 * The intended way of implementing this handler is to e.g. call <c>fclose()</c>
 * and perform integrity check on the downloaded file. It might also be
 * uncompressed or decrypted as necessary, so that it is ready to be flashed.
 * The exact split of responsibility between
 * @ref anjay_advanced_fw_update_stream_finish_t and
 * @ref anjay_advanced_fw_update_perform_upgrade_t is not clearly defined and up
 * to the implementor.
 *
 * Note that regardless of the return value, the stream is considered to be
 * closed. That is, upon successful return, the Advanced Firmware Update object
 * is considered to be in the <em>Downloaded</em> state, and upon returning an
 * error - in the <em>Idle</em> state.
 *
 * @param iid      Instance ID of an Advanced Firmware Object which tries to
 *                 finish a stream.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_advanced_fw_update_instance_add
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_ADVANCED_FW_UPDATE_ERR_*</c>
 *          value is returned, an equivalent value will be set in the Update
 *          Result Resource.
 */
typedef int anjay_advanced_fw_update_stream_finish_t(anjay_iid_t iid,
                                                     void *user_ptr);

/**
 * Resets the firmware update state and performs any applicable cleanup of
 * temporary storage if necessary.
 *
 * Will be called at request of the server, or after a failed download. Note
 * that it may be called without previously calling
 * @ref anjay_advanced_fw_update_stream_finish_t, so it shall also close the
 * currently open download stream, if any.
 *
 * @note If reset of particular instance is done while it is in
 *       <c>ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED</c> state, it is likely
 *       possible that it is listed as linked instance of another instance.
 *       If that is the case, it should be marked as Conflicting instance
 *       in every instance that it is linked with.
 *
 * @param iid      Instance ID of an Advanced Firmware Object which performs
 *                 reset.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_advanced_fw_update_instance_add
 */
typedef void anjay_advanced_fw_update_reset_t(anjay_iid_t iid, void *user_ptr);

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
 * @param iid      Instance ID of an Advanced Firmware Object which tries to get
 *                 related package name.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_advanced_fw_update_instance_add
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package name, or <c>NULL</c> if it is not currently
 *          available.
 */
typedef const char *anjay_advanced_fw_update_get_pkg_name_t(anjay_iid_t iid,
                                                            void *user_ptr);

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
 * @param iid      Instance ID of an Advanced Firmware Object which tries to get
 *                 related package version.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_advanced_fw_update_instance_add
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package version, or <c>NULL</c> if it is not
 *          currently available.
 */
typedef const char *anjay_advanced_fw_update_get_pkg_version_t(anjay_iid_t iid,
                                                               void *user_ptr);

/**
 * Returns the current version of firmware represented by Advanced Firmware
 * Update object instance.
 *
 * The version will be exposed in the data model as the Current Version
 * Resource. If this callback returns <c>NULL</c> or is not implemented at all
 * (with the corresponding field set to <c>NULL</c>), that Resource will not be
 * present in the data model.
 *
 * @param iid      Instance ID of an Advanced Firmware Object which tries to get
 *                 related current version.
 *
 * @param user_ptr Opaque pointer to user data, as passed to
 *                 @ref anjay_advanced_fw_update_instance_add
 *
 * @returns The callback shall return a pointer to a null-terminated string
 *          containing the package version, or <c>NULL</c> if it is not
 *          currently available.
 */
typedef const char *
anjay_advanced_fw_update_get_current_version_t(anjay_iid_t iid, void *user_ptr);

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
 *   call reboot after @ref anjay_event_loop_run
 * - perform the firmware upgrade internally and then reboot, it means that
 *   the return will never happen
 *
 * After rebooting, the result of the upgrade process may be passed to the
 * library during initialization via the <c>initial_result</c> argument to
 * @ref anjay_advanced_fw_update_instance_add .
 *
 * Alternatively, if the update can be performed without reinitializing Anjay,
 * you can use @ref anjay_advanced_fw_update_set_state_and_result (either from
 * within the handler or some time after returning from it) to pass the update
 * result.
 *
 * @param iid                               Instance ID of an Advanced Firmware
 *                                          Object which tries to perform
 *                                          upgrade.
 *
 * @param user_ptr                          Opaque pointer to user data, as
 *                                          passed to
 *                                    @ref anjay_advanced_fw_update_instance_add
 *
 * @param requested_supplemental_iids       Pointer to list of Advanced Firmware
 *                                          Object instances that server request
 *                                          to upgrade along with instance that
 *                                          this callback belongs to.
 *
 * @param requested_supplemental_iids_count Count of requested supplemental iids
 *
 * @returns The callback shall return a negative value if it can be determined
 *          without a reboot, that the firmware upgrade cannot be successfully
 *          performed.
 *
 *          If one of the <c>ANJAY_ADVANCED_FW_UPDATE_ERR_*</c> values is
 *          returned, an equivalent value will be set in the Update Result
 *          Resource. Otherwise, if a non-zero value is returned, the Update
 *          Result Resource is set to generic "Firmware update failed" code.
 *
 */
typedef int anjay_advanced_fw_update_perform_upgrade_t(
        anjay_iid_t iid,
        void *user_ptr,
        const anjay_iid_t *requested_supplemental_iids,
        size_t requested_supplemental_iids_count);

/**
 * Queries security information that shall be used for an encrypted connection
 * with a PULL-mode download server.
 *
 * May be called before @ref anjay_advanced_fw_update_stream_open_t if the
 * download is to be performed in PULL mode and the connection needs to use TLS
 * or DTLS encryption.
 *
 * Note that the @ref anjay_security_config_t contains references to file paths,
 * binary security keys, and/or ciphersuite lists. It is the user's
 * responsibility to appropriately allocate them and ensure proper lifetime of
 * the returned pointers. The returned security information may only be
 * invalidated in a call to @ref anjay_advanced_fw_update_reset_t or after a
 * call to @ref anjay_delete .
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), @ref anjay_security_config_from_dm will be used as a default
 * way to get security information.
 *
 * In that (no user-defined handler) case, <c>anjay_security_config_pkix()</c>
 * will be used as an additional fallback if <c>ANJAY_WITH_LWM2M11</c> is
 * enabled and a valid trust store is available (either specified through
 * <c>use_system_trust_store</c>, <c>trust_store_certs</c> or
 * <c>trust_store_crls</c> fields in <c>anjay_configuration_t</c>, or obtained
 * via <c>/est/crts</c> request if <c>est_cacerts_policy</c> is set to
 * <c>ANJAY_EST_CACERTS_IF_EST_CONFIGURED</c> or
 * <c>ANJAY_EST_CACERTS_ALWAYS</c>).
 *
 * You may also use these functions yourself, for example as a fallback
 * mechanism.
 *
 * @param iid                 Instance ID of an Advanced Firmware Object which
 *                            tries to get security config.
 *
 * @param user_ptr            Opaque pointer to user data, as passed to
 *                            @ref anjay_advanced_fw_update_instance_add
 *
 * @param out_security_config Pointer in which the handler shall fill in
 *                            security configuration to use for download. Note
 *                            that leaving this value as empty without filling
 *                            it in will result in a configuration that is
 *                            <strong>valid, but very insecure</strong>: it will
 *                            cause any server certificate to be accepted
 *                            without validation. Any pointers used within the
 *                            supplied structure shall remain valid until either
 *                            a call to @ref anjay_advanced_fw_update_reset_t,
 *                            or exit to the event loop (from either
 *                            @ref anjay_serve, @ref anjay_sched_run or
 *                            @ref anjay_advanced_fw_update_instance_add),
 *                            whichever happens first. Anjay will
 *                            <strong>not</strong> attempt to deallocate
 *                            anything automatically.
 *
 * @param download_uri        Target firmware URI.
 *
 * @returns The callback shall return 0 if successful or a negative value in
 *          case of error. If one of the <c>ANJAY_ADVANCED_FW_UPDATE_ERR_*</c>
 *          value is returned, an equivalent value will be set in the Update
 *          Result Resource.
 */
typedef int anjay_advanced_fw_update_get_security_config_t(
        anjay_iid_t iid,
        void *user_ptr,
        anjay_security_config_t *out_security_info,
        const char *download_uri);

/**
 * Returns tx_params used to override default ones.
 *
 * If this handler is not implemented at all (with the corresponding field set
 * to <c>NULL</c>), <c>udp_tx_params</c> from <c>anjay_t</c> object are used.
 *
 * @param iid           Instance ID of an Advanced Firmware Object which query
 *                      tx_params.
 *
 * @param user_ptr      Opaque pointer to user data, as passed to
 *                      @ref anjay_advanced_fw_update_instance_add .
 *
 * @param download_uri  Target firmware URI.
 *
 * @returns Object with CoAP transmission parameters.
 */
typedef avs_coap_udp_tx_params_t anjay_advanced_fw_update_get_coap_tx_params_t(
        anjay_iid_t iid, void *user_ptr, const char *download_uri);

/**
 * Handler callbacks that shall implement the platform-specific part of firmware
 * update process.
 *
 * The Firmware Update object logic may be in one of the following states:
 *
 * - <strong>Idle</strong>. This is the state in which the object is just after
 *   creation (unless initialized with either
 *   <c>ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED</c> or
 *   <c>ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING</c>). The following handlers
 * may be called in this state:
 *   - <c>stream_open</c> - shall open the download stream; moves the object
 *     into the <em>Downloading</em> state
 *   - <c>get_security_config</c> - shall fill in security info that shall be
 *     used for a given URL
 *   - <c>reset</c> - shall free data allocated by <c>get_security_config</c>,
 *     if it was called and there is any
 * - <strong>Downloading</strong>. The object might be initialized directly into
 *   this state by using <c>ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADING</c>. In
 * this state, the download stream is open and data may be transferred. The
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
 *   this state by using <c>ANJAY_ADVANCED_FW_UPDATE_STATE_DOWNLOADED</c>. In
 * this state, the firmware package has been downloaded and checked and is ready
 * to be flashed. The following handlers may be called in this state:
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
     * @ref anjay_advanced_fw_update_stream_open_t */
    anjay_advanced_fw_update_stream_open_t *stream_open;
    /** Writes data to the download stream;
     * @ref anjay_advanced_fw_update_stream_write_t */
    anjay_advanced_fw_update_stream_write_t *stream_write;
    /** Closes the download stream and prepares the firmware package to be
     * flashed; @ref anjay_advanced_fw_update_stream_finish_t */
    anjay_advanced_fw_update_stream_finish_t *stream_finish;

    /** Resets the firmware update state and performs any applicable cleanup of
     * temporary storage if necessary; @ref anjay_advanced_fw_update_reset_t */
    anjay_advanced_fw_update_reset_t *reset;

    /** Returns the name of downloaded firmware package;
     * @ref anjay_advanced_fw_update_get_pkg_name_t */
    anjay_advanced_fw_update_get_pkg_name_t *get_pkg_name;
    /** Return the version of downloaded firmware package;
     * @ref anjay_advanced_fw_update_get_pkg_version_t */
    anjay_advanced_fw_update_get_pkg_version_t *get_pkg_version;
    /** Return the version of current firmware package;
     * @ref anjay_advanced_fw_update_get_current_version_t */
    anjay_advanced_fw_update_get_current_version_t *get_current_version;

    /** Performs the actual upgrade with previously downloaded package;
     * @ref anjay_advanced_fw_update_perform_upgrade_t */
    anjay_advanced_fw_update_perform_upgrade_t *perform_upgrade;

    /** Queries security configuration that shall be used for an encrypted
     * connection; @ref anjay_advanced_fw_update_get_security_config_t */
    anjay_advanced_fw_update_get_security_config_t *get_security_config;

    /** Queries CoAP transmission parameters to be used during firmware
     * update; @ref anjay_advanced_fw_update_get_coap_tx_params_t */
    anjay_advanced_fw_update_get_coap_tx_params_t *get_coap_tx_params;
} anjay_advanced_fw_update_handlers_t;

/**
 * Installs the Advanced Firmware Update object in an Anjay object.
 *
 * The Advanced Firmware Update module does not require explicit cleanup; all
 * resources  will be automatically freed up during the call to
 * @ref anjay_delete.
 *
 * @param anjay         Anjay object for which the Advanced Firmware Update
 *                      Object is installed.
 *
 * @param config        Provides configuration of preferred socked downloads and
 *                      lwm2m send usage;
 *                      @ref anjay_advanced_fw_update_global_config_t
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_install(
        anjay_t *anjay, const anjay_advanced_fw_update_global_config_t *config);

/**
 * Adds the Advanced Firmware Update object instance in an Advanced Firmware
 * Update object.
 *
 * The Advanced Firmware Update module does not require explicit cleanup; all
 * resources  will be automatically freed up during the call to
 * @ref anjay_delete.
 *
 * @param anjay          Anjay object for which the Advanced Firmware Update
 *                       Object is installed.
 *
 * @param iid            Instance ID of an Advanced Firmware Object.
 *
 * @param component_name Pointer to null-terminated component name string.
 *                       Note: String is NOT copied, so it needs to remain valid
 *                       for the lifetime of the object instance.
 *
 * @param handlers       Pointer to a set of handler functions that handle the
 *                       platform-specific part of firmware update process.
 *                       Note: Contents of the structure are NOT copied, so it
 *                       needs to remain valid for the lifetime of the object
 *                       instance.
 *
 * @param user_arg      Opaque user pointer that will be passed as the first
 *                      argument to handler functions.
 *
 * @param initial_state Information about the state to initialize the Advanced
 *                      Firmware Update object instance in. It is intended to be
 *                      used after either an orderly reboot caused by a firmware
 *                      update attempt to report the update result, or by an
 *                      unexpected reboot in the middle of the download process.
 *                      If the object shall be initialized in a neutral initial
 *                      state, <c>NULL</c> might be passed.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_instance_add(
        anjay_t *anjay,
        anjay_iid_t iid,
        const char *component_name,
        const anjay_advanced_fw_update_handlers_t *handlers,
        void *user_arg,
        const anjay_advanced_fw_update_initial_state_t *initial_state);

/**
 * Sets the Advanced Firmware Update object instance State to @p state and
 * Result to @p result , interrupting the update process.
 *
 * If the function fails, neither Update State nor Update Result are changed.
 *
 * Some state transitions are disallowed and cause this function to fail:
 *
 * - @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_INITIAL and
 *   @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_UPDATE_CANCELLED are never allowed and
 *   cause this function to fail.
 *
 * - @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_SUCCESS is only allowed if the
 *   firmware application process was started by the server (an Execute
 *   operation was already performed on the Update resource of the Firmware
 *   Update object or @ref ANJAY_ADVANCED_FW_UPDATE_STATE_UPDATING was used in a
 *   call to @ref anjay_advanced_fw_update_instance_add). Otherwise, the
 *   function fails.
 *
 * - Other values of @p result (various error codes) are only allowed if
 *   Advanced Firmware Update State is not Idle (0), i.e. firmware is being
 *   downloaded, was already downloaded or is being applied.
 *
 * WARNING: calling this in @ref anjay_advanced_fw_update_perform_upgrade_t
 * handler is supported, but the result of using it from within any other of
 * @ref anjay_advanced_fw_update_handlers_t handlers is undefined.
 *
 * @param anjay  Anjay object to operate on.
 *
 * @param iid    Instance ID of an Advanced Firmware Object.
 *
 * @param state  Value of the State resource to set.
 *
 * @param result Value of the Update Result resource to set.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_set_state_and_result(
        anjay_t *anjay,
        anjay_iid_t iid,
        anjay_advanced_fw_update_state_t state,
        anjay_advanced_fw_update_result_t result);

/**
 * Gets the Advanced Firmware Update object instance State.
 *
 * @param anjay     Anjay object to operate on.
 *
 * @param iid       Instance ID of an Advanced Firmware Object.
 *
 * @param out_state Pointer to where write output state.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_get_state(
        anjay_t *anjay,
        anjay_iid_t iid,
        anjay_advanced_fw_update_state_t *out_state);

/**
 * Gets the Advanced Firmware Update object instance Result.
 *
 * @param anjay     Anjay object to operate on.
 *
 * @param iid       Instance ID of an Advanced Firmware Object.
 *
 * @param out_result Pointer to where write output result.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_get_result(
        anjay_t *anjay_locked,
        anjay_iid_t iid,
        anjay_advanced_fw_update_result_t *out_result);

/**
 * Sets linked instances resource of Advance Firmware Update object instance.
 *
 * Linked instances mark instances that will be updated in a batch together when
 * performing upgrade of a @p iid instance. See AVSystem specification of
 * Advanced Firmware Update for details.
 *
 * @param anjay             Anjay object to operate on.
 *
 * @param iid               Instance ID of an Advanced Firmware Object.
 *
 * @param target_iids       Points to array iids of linked instances in relation
 *                          to Advanced Firmware Update object instance @p iid.
 *                          NOTE: Only already added instances only of Advanced
 *                          Firmware Update object are allowed.
 *
 * @param target_iids_count Count of target iids in an array.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_set_linked_instances(
        anjay_t *anjay,
        anjay_iid_t iid,
        const anjay_iid_t *target_iids,
        size_t target_iids_count);

/**
 * Gets linked instances resource of Advance Firmware Update object instance.
 *
 * Linked instances mark instances that will be updated in a batch together when
 * performing upgrade of a @p iid instance. See AVSystem specification of
 * Advanced Firmware Update for details.
 *
 * @param anjay                 Anjay object to operate on.
 *
 * @param iid                   Instance ID of an Advanced Firmware Object.
 *
 * @param out_target_iids       Points to memory where to write array of iids of
 *                              linked instances.
 *
 * @param out_target_iids_count Point to memory where to write count of target
 *                              iids in an array.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_get_linked_instances(
        anjay_t *anjay,
        anjay_iid_t iid,
        const anjay_iid_t **out_target_iids,
        size_t *out_target_iids_count);

/**
 * Sets conflicting instances resource of Advance Firmware Update object
 * instance.
 *
 * When the download or update fails and the Update Result resource is set to
 * @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_CONFLICTING_STATE or
 * @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR
 * this resource MUST be present and contain references to the Advanced
 * Firmware Update object instances that caused the conflict. See LwM2M
 * specification for details.
 *
 * @param anjay             Anjay object to operate on.
 *
 * @param iid               Instance ID of an Advanced Firmware Object.
 *
 * @param target_iids       Points to array iids of conflicting instances in
 *                          relation to Advanced Firmware Update object instance
 *                          @p iid.
 *                          NOTE: Only already added instances only of Advanced
 *                          Firmware Update object are allowed.
 *
 * @param target_iids_count Count of target iids in an array.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_set_conflicting_instances(
        anjay_t *anjay,
        anjay_iid_t iid,
        const anjay_iid_t *target_iids,
        size_t target_iids_count);

/**
 * Gets conflicting instances resource of Advance Firmware Update object
 * instance.
 *
 * When the download or update fails and the Update Result resource is set to
 * @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_CONFLICTING_STATE or
 * @ref ANJAY_ADVANCED_FW_UPDATE_RESULT_DEPENDENCY_ERROR
 * this resource MUST be present and contain references to the Advanced
 * Firmware Update object instances that caused the conflict. See LwM2M
 * specification for details.
 *
 * @param anjay                 Anjay object to operate on.
 *
 * @param iid                   Instance ID of an Advanced Firmware Object.
 *
 * @param out_target_iids       Points to memory where to write array of iids of
 *                              conflicting instances.
 *
 * @param out_target_iids_count Point to memory where to write count of target
 *                              iids in an array.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_advanced_fw_update_get_conflicting_instances(
        anjay_t *anjay,
        anjay_iid_t iid,
        const anjay_iid_t **out_target_iids,
        size_t *out_target_iids_count);

/**
 * Gets the update deadline based on Maximum Defer Period resource value and
 * time of downloading full firmware.
 *
 * @param anjay Anjay object to operate on.
 *
 * @param iid   Instance ID of an Advanced Firmware Object.
 *
 * @returns Real time of the update deadline. In case of not deferring
 * update returns @c AVS_TIME_REAL_INVALID.
 */
avs_time_real_t anjay_advanced_fw_update_get_deadline(anjay_t *anjay,
                                                      anjay_iid_t iid);

/**
 * Gets the update severity.
 *
 * @param anjay Anjay object to operate on.
 *
 * @param iid   Instance ID of an Advanced Firmware Object.
 *
 * @returns Severity resource value present in Firmware Update object on
 * success, or @ref ANJAY_ADVANCED_FW_UPDATE_SEVERITY_MANDATORY on error.
 */
anjay_advanced_fw_update_severity_t
anjay_advanced_fw_update_get_severity(anjay_t *anjay, anjay_iid_t iid);

/**
 * Gets the value of Last State Change Time resource.
 *
 * @param anjay Anjay object to operate on.
 *
 * @param iid   Instance ID of an Advanced Firmware Object.
 *
 * @returns Real time of last State resource change, or @ref
 * AVS_TIME_REAL_INVALID on error.
 */
avs_time_real_t
anjay_advanced_fw_update_get_last_state_change_time(anjay_t *anjay,
                                                    anjay_iid_t iid);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_ADVANCED_FW_UPDATE_H */
