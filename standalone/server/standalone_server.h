#ifndef ANJAY_STANDALONE_ANJAY_SERVER_H
#define ANJAY_STANDALONE_ANJAY_SERVER_H

#include <anjay/anjay_config.h>
#include <anjay/dm.h>

#include <avsystem/commons/avs_stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Resource: Short Server ID */
    anjay_ssid_t ssid;
    /** Resource: Lifetime */
    int32_t lifetime;
    /** Resource: Default Minimum Period - or a negative value to disable
     * presence */
    int32_t default_min_period;
    /** Resource: Default Maximum Period - or a negative value to disable
     * presence */
    int32_t default_max_period;
    /** Resource: Disable Timeout - or a negative value to disable presence */
    int32_t disable_timeout;
    /** Resource: Binding */
    const char *binding;
    /** Resource: Notification Storing When Disabled or Offline */
    bool notification_storing;
#ifdef ANJAY_WITH_LWM2M11
    /** Resource: Bootstrap on Registration Failure. True if not set. */
    const bool *bootstrap_on_registration_failure;
    /** Resource: Preferred Transport */
    char preferred_transport;
    /** Resource: Mute Send */
    bool mute_send;
    /** Resource: Communication Retry Count. NULL if not set. */
    const uint32_t *communication_retry_count;
    /** Resource: Communication Retry Timer. NULL if not set. */
    const uint32_t *communication_retry_timer;
    /** Resource: Communication Sequence Retry Count. NULL if not set. */
    const uint32_t *communication_sequence_retry_count;
    /** Resource: Communication Sequence Delay Timer (in seconds). NULL if not
     * set. */
    const uint32_t *communication_sequence_delay_timer;
#    ifdef ANJAY_WITH_SMS
    /** Resource: Trigger */
    const bool *trigger;
#    endif // ANJAY_WITH_SMS
#endif     // ANJAY_WITH_LWM2M11
} standalone_server_instance_t;

/**
 * Adds new Instance of Server Object and returns newly created Instance id
 * via @p inout_iid .
 *
 * Note: if @p *inout_iid is set to @ref ANJAY_ID_INVALID then the Instance id
 * is generated automatically, otherwise value of @p *inout_iid is used as a
 * new Server Instance Id.
 *
 * Note: @p instance may be safely freed by the user code after this function
 * finishes (internally a deep copy of @ref standalone_server_instance_t is
 * performed).
 *
 * @param obj_ptr   Installed Server Object to operate on.
 * @param instance  Server Instance to insert.
 * @param inout_iid Server Instance id to use or @ref ANJAY_ID_INVALID .
 *
 * @return 0 on success, negative value in case of an error or if the instance
 * of specified id already exists.
 */
int standalone_server_object_add_instance(
        const anjay_dm_object_def_t *const *obj_ptr,
        const standalone_server_instance_t *instance,
        anjay_iid_t *inout_iid);

/**
 * Removes all instances of Server Object leaving it in an empty state.
 *
 * @param obj_ptr Installed Server Object to operate on.
 */
void standalone_server_object_purge(
        const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Retrieves a list of SSIDs currently present in the Server object. The SSIDs
 * are NOT guaranteed to be returned in any particular order. Returned list may
 * not be freed nor modified.
 *
 * The returned list pointer shall be considered invalidated by any call to @ref
 * anjay_sched_run, @ref anjay_serve, @ref
 * standalone_server_object_add_instance,
 * @ref standalone_server_object_purge, @ref standalone_server_object_restore,
 * or, if called from within some callback handler, on return from that handler.
 *
 * If a transaction on the Server object is currently ongoing (e.g., during
 * Bootstrap), last known state from before the transaction will be returned.
 *
 * @param obj_ptr Installed Server Object to operate on.
 *
 * @returns A list of known SSIDs on success, NULL when the object is empty.
 */
AVS_LIST(const anjay_ssid_t)
standalone_server_get_ssids(const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Dumps Server Object Instances into the @p out_stream .
 *
 * @param obj_ptr       Installed Server Object to operate on.
 * @param out_stream    Stream to write to.
 * @return AVS_OK in case of success, or an error code.
 */
avs_error_t
standalone_server_object_persist(const anjay_dm_object_def_t *const *obj_ptr,
                                 avs_stream_t *out_stream);

/**
 * Attempts to restore Server Object Instances from specified @p in_stream .
 *
 * Note: if restore fails, then Server Object will be left untouched, on
 * success though all Instances stored within the Object will be purged.
 *
 * @param obj_ptr   Installed Server Object to operate on.
 * @param in_stream Stream to read from.
 * @return AVS_OK in case of success, or an error code.
 */
avs_error_t
standalone_server_object_restore(const anjay_dm_object_def_t *const *obj_ptr,
                                 avs_stream_t *in_stream);

/**
 * Checks whether the Server Object has been modified since
 * last successful call to @ref standalone_server_object_persist or @ref
 * standalone_server_object_restore.
 */
bool standalone_server_object_is_modified(
        const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Creates a Server Object and installs it in an Anjay instance using
 * @ref anjay_register_object.
 *
 * Do NOT attempt to call @ref anjay_register_object with this object manually,
 * and do NOT try to use the same instance of the Server object with another
 * Anjay instance.
 *
 * @param anjay Anjay instance for which the Server Object is installed.
 *
 * @returns Handle to the created object that can be passed to other functions
 *          declared in this file, or <c>NULL</c> in case of error.
 */
const anjay_dm_object_def_t **standalone_server_object_install(anjay_t *anjay);

/**
 * Frees all system resources allocated by the Server Object.
 *
 * <strong>NOTE:</strong> Attempting to call this function before deregistering
 * the object using @ref anjay_unregister_object, @ref anjay_delete or
 * @ref anjay_delete_with_core_persistence is undefined behavior.
 *
 * @param obj_ptr Server Object to operate on.
 */
void standalone_server_object_cleanup(
        const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Sets the Lifetime value for the specified Server Instance ID.
 *
 * NOTE: Calling this function MAY trigger sending LwM2M Update message to
 * an associated LwM2M Server.
 *
 * @param obj_ptr   Installed Server Object to operate on.
 * @param iid       Server Object Instance for which the Lifetime shall be
 *                  altered.
 * @param lifetime  New value of the Lifetime Resource. MUST BE strictly
 *                  positive.
 *
 * @returns 0 on success, negative value in case of an error. If an error
 * is returned, the Lifetime value remains unchanged.
 */
int standalone_server_object_set_lifetime(
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        int32_t lifetime);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_STANDALONE_ANJAY_SERVER_H */
