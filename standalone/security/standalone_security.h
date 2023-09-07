#ifndef ANJAY_STANDALONE_ANJAY_SECURITY_H
#define ANJAY_STANDALONE_ANJAY_SECURITY_H

#include <anjay/dm.h>

#include <avsystem/commons/avs_stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Resource: Short Server ID */
    anjay_ssid_t ssid;
    /** Resource: LwM2M Server URI */
    const char *server_uri;
    /** Resource: Bootstrap Server */
    bool bootstrap_server;
    /** Resource: Security Mode */
    anjay_security_mode_t security_mode;
    /** Resource: Client Hold Off Time */
    int32_t client_holdoff_s;
    /** Resource: Bootstrap Server Account Timeout */
    int32_t bootstrap_timeout_s;
    /** Resource: Public Key Or Identity */
    const uint8_t *public_cert_or_psk_identity;
    size_t public_cert_or_psk_identity_size;
    /** Resource: Secret Key */
    const uint8_t *private_cert_or_psk_key;
    size_t private_cert_or_psk_key_size;
    /** Resource: Server Public Key */
    const uint8_t *server_public_key;
    size_t server_public_key_size;
#ifdef ANJAY_WITH_SMS
    /** Resource: SMS Security Mode */
    anjay_sms_security_mode_t sms_security_mode;
    /** Resource: SMS Binding Key Parameters */
    const uint8_t *sms_key_parameters;
    size_t sms_key_parameters_size;
    /** Resource: SMS Binding Secret Key(s) */
    const uint8_t *sms_secret_key;
    size_t sms_secret_key_size;
    /** Resource: LwM2M Server SMS Number */
    const char *server_sms_number;
#endif // ANJAY_WITH_SMS
#ifdef ANJAY_WITH_LWM2M11
    /** Resource: Matching Type (NULL for not present) */
    const uint8_t *matching_type;
    /** Resource: SNI */
    const char *server_name_indication;
    /** Resource: Certificate Usage (NULL for not present) */
    const uint8_t *certificate_usage;
    /** Resource: DTLS/TLS Ciphersuite;
     * Note: Passing a value with <c>num_ids == 0</c> (default) will cause the
     * resource to be absent, resulting in a fallback to defaults. */
    avs_net_socket_tls_ciphersuites_t ciphersuites;
#    ifdef ANJAY_WITH_COAP_OSCORE
    /** Resource: OSCORE Security Mode (NULL for not present) */
    const anjay_iid_t *oscore_iid;
#    endif // ANJAY_WITH_COAP_OSCORE
#endif     // ANJAY_WITH_LWM2M11
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
    /** Resource: Public Key Or Identity;
     * This is an alternative to the @p public_cert_or_psk_identity and
     * @p psk_identity fields that may be used only if @p security_mode is
     * either @ref ANJAY_SECURITY_CERTIFICATE or @ref ANJAY_SECURITY_EST; it is
     * also an error to specify non-empty values for more than one of these
     * fields at the same time. */
    avs_crypto_certificate_chain_info_t public_cert;
    /** Resource: Secret Key;
     * This is an alternative to the @p private_cert_or_psk_key and @ref psk_key
     * fields that may be used only if @p security_mode is either
     * @ref ANJAY_SECURITY_CERTIFICATE or @ref ANJAY_SECURITY_EST; it is also an
     * error to specify non-empty values for more than one of these fields at
     * the same time. */
    avs_crypto_private_key_info_t private_key;
    /** Resource: Public Key Or Identity;
     * This is an alternative to the @p public_cert_or_psk_identity and
     * @ref public_cert fields that may be used only if @p security_mode is
     * @ref ANJAY_SECURITY_PSK; it is also an error to specify non-empty values
     * for more than one of these fields at the same time. */
    avs_crypto_psk_identity_info_t psk_identity;
    /** Resource: Secret Key;
     * This is an alternative to the @p private_cert_or_psk_key and
     * @ref private_key fields that may be used only if @p security_mode is
     * @ref ANJAY_SECURITY_PSK; it is also an error to specify non-empty values
     * for more than one of these fields at the same time. */
    avs_crypto_psk_key_info_t psk_key;
#    ifdef ANJAY_WITH_SMS
    /** Resource: SMS Binding Key Parameters;
     * This is an alternative to the @p sms_key_parameters field that may be
     * used only if @p sms_security_mode is @ref ANJAY_SMS_SECURITY_DTLS_PSK; it
     * is also an error to specify non-empty values for both fields at the same
     * time. */
    avs_crypto_psk_identity_info_t sms_psk_identity;
    /** Resource: SMS Binding Secret Key(s);
     * This is an alternative to the @p sms_secret_key field that may be used
     * only if @p sms_security_mode is @ref ANJAY_SMS_SECURITY_DTLS_PSK; it is
     * also an error to specify non-empty values for both fields at the same
     * time. */
    avs_crypto_psk_key_info_t sms_psk_key;
#    endif // ANJAY_WITH_SMS
#endif     // ANJAY_WITH_SECURITY_STRUCTURED
} standalone_security_instance_t;

/**
 * Adds new Instance of Security Object and returns newly created Instance id
 * via @p inout_iid .
 *
 * Note: if @p *inout_iid is set to @ref ANJAY_ID_INVALID then the Instance id
 * is generated automatically, otherwise value of @p *inout_iid is used as a
 * new Security Instance Id.
 *
 * Note: @p instance may be safely freed by the user code after this function
 * finishes (internally a deep copy of @ref standalone_security_instance_t is
 * performed).
 *
 * Warning: calling this function during active communication with Bootstrap
 * Server may yield undefined behavior and unexpected failures may occur.
 *
 * @param obj_ptr   Installed Security Object to operate on.
 * @param instance  Security Instance to insert.
 * @param inout_iid Security Instance id to use or @ref ANJAY_ID_INVALID .
 *
 * @return 0 on success, negative value in case of an error or if the instance
 * of specified id already exists.
 */
int standalone_security_object_add_instance(
        const anjay_dm_object_def_t *const *obj_ptr,
        const standalone_security_instance_t *instance,
        anjay_iid_t *inout_iid);

/**
 * Purges instances of Security Object leaving it in an empty state.
 *
 * @param obj_ptr Installed Security Object to purge.
 */
void standalone_security_object_purge(
        const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Dumps Security Object Instances to the @p out_stream.
 *
 * @param obj_ptr       Installed Security Object to operate on.
 * @param out_stream    Stream to write to.
 * @returns AVS_OK in case of success, or an error code.
 */
avs_error_t
standalone_security_object_persist(const anjay_dm_object_def_t *const *obj_ptr,
                                   avs_stream_t *out_stream);

/**
 * Attempts to restore Security Object Instances from specified @p in_stream.
 *
 * Note: if restore fails, then Security Object will be left untouched, on
 * success though all Instances stored within the Object will be purged.
 *
 * @param obj_ptr   Installed Security Object to operate on.
 * @param in_stream Stream to read from.
 * @returns AVS_OK in case of success, or an error code.
 */
avs_error_t
standalone_security_object_restore(const anjay_dm_object_def_t *const *obj_ptr,
                                   avs_stream_t *in_stream);

/**
 * Checks whether the Security Object has been modified since
 * last successful call to @ref standalone_security_object_persist or @ref
 * standalone_security_object_restore.
 */
bool standalone_security_object_is_modified(
        const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Creates the Security Object and installs it in an Anjay instance using
 * @ref anjay_register_object.
 *
 * Do NOT attempt to call @ref anjay_register_object with this object manually,
 * and do NOT try to use the same instance of the Security object with another
 * Anjay instance.
 *
 * @param anjay Anjay instance for which the Security Object is installed.
 *
 * @returns Handle to the created object that can be passed to other functions
 *          declared in this file, or <c>NULL</c> in case of error.
 */
const anjay_dm_object_def_t **
standalone_security_object_install(anjay_t *anjay);

/**
 * Frees all system resources allocated by the Security Object.
 *
 * <strong>NOTE:</strong> Attempting to call this function before deregistering
 * the object using @ref anjay_unregister_object, @ref anjay_delete or
 * @ref anjay_delete_with_core_persistence is undefined behavior.
 *
 * @param obj_ptr Server Object to operate on.
 */
void standalone_security_object_cleanup(
        const anjay_dm_object_def_t *const *obj_ptr);

#ifdef ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT
/**
 * Type for a callback function that will be called by the Security object
 * implementation whenever a query string for storing a new security credential
 * (provisioned by means other than EST) in external security engine is
 * necessary. See also the fields of the @ref
 * standalone_security_hsm_configuration_t structure.
 *
 * @param iid       ID of the Security object Instance for which the credential
 *                  is to be stored.
 *
 * @param ssid      Short Server ID of the server account for which the
 *                  credential is to be stored (@ref ANJAY_SSID_BOOTSTRAP in
 *                  case of the Bootstrap Server).
 *
 * @param data      Pointer to a buffer containing the credential that will be
 *                  stored.
 *
 * @param data_size Size in bytes of the data located at @p data.
 *
 * @param arg       Opaque argument configured through a corresponding
 *                  <c>*_arg</c> field of
 *                  @ref standalone_security_hsm_configuration_t.
 *
 * @returns String that will be used as a query string for the provisioned
 *          security credential. The string will be copied shortly after this
 *          function returns, so it is safe to deallocate or overwrite it when
 *          control is returned to user code, or during the next call to this
 *          function, whichever happens first. Security Object code will never
 *          attempt to modify or deallocate the returned value.
 *
 * @attention The @p data and @p data_size are provided <strong>only as a
 *            hint</strong> for users who want the query strings to depend on
 *            the credential contents in any way. This callback <strong>shall
 *            not attempt to store the certificate</strong> itself. This will be
 *            performed by Anjay afterwards.
 */
typedef const char *standalone_security_hsm_query_cb_t(anjay_iid_t iid,
                                                       anjay_ssid_t ssid,
                                                       const void *data,
                                                       size_t data_size,
                                                       void *arg);

/**
 * Configuration of the callbacks for generating the query string addresses
 * under which different kinds of security credentials will be stored on the
 * hardware security engine.
 */
typedef struct {
    /**
     * Callback function that will be called whenever a public client
     * certificate needs to be stored in an external security engine.
     *
     * If NULL, public client certificates will be stored in main system memory
     * unless explicitly requested via either EST or the <c>public_cert</c>
     * field in @ref standalone_security_instance_t.
     */
    standalone_security_hsm_query_cb_t *public_cert_cb;

    /**
     * Opaque argument that will be passed to the function configured in the
     * <c>public_cert_cb</c> field.
     *
     * If <c>public_cert_cb</c> is NULL, this field is ignored.
     */
    void *public_cert_cb_arg;

    /**
     * Callback function that will be called whenever a client private key needs
     * to be stored in an external security engine.
     *
     * If NULL, client private keys will be stored in main system memory unless
     * explicitly requested via either EST or the <c>private_key</c> field in
     * @ref standalone_security_instance_t.
     */
    standalone_security_hsm_query_cb_t *private_key_cb;

    /**
     * Opaque argument that will be passed to the function configured in the
     * <c>private_key_cb</c> field.
     *
     * If <c>private_key_cb</c> is NULL, this field is ignored.
     */
    void *private_key_cb_arg;

    /**
     * Callback function that will be called whenever a PSK identity for use
     * with the main connection needs to be stored in an external security
     * engine.
     *
     * If NULL, PSK identities for use with the main connection will be stored
     * in main system memory unless explicitly requested via the
     * <c>psk_identity</c> field in @ref standalone_security_instance_t.
     */
    standalone_security_hsm_query_cb_t *psk_identity_cb;

    /**
     * Opaque argument that will be passed to the function configured in the
     * <c>psk_identity_cb</c> field.
     *
     * If <c>psk_identity_cb</c> is NULL, this field is ignored.
     */
    void *psk_identity_cb_arg;

    /**
     * Callback function that will be called whenever a PSK key for use with the
     * main connection needs to be stored in an external security engine.
     *
     * If NULL, PSK keys for use with the main connection will be stored in main
     * system memory unless explicitly requested via the <c>psk_key</c> field in
     * @ref standalone_security_instance_t.
     */
    standalone_security_hsm_query_cb_t *psk_key_cb;

    /**
     * Opaque argument that will be passed to the function configured in the
     * <c>psk_key_cb</c> field.
     *
     * If <c>psk_key_cb</c> is NULL, this field is ignored.
     */
    void *psk_key_cb_arg;
#    ifdef ANJAY_WITH_SMS
    /**
     * Callback function that will be called whenever a PSK identity for use
     * with SMS binding needs to be stored in an external security engine.
     *
     * If NULL, PSK identities for use with SMS binding will be stored in main
     * system memory unless explicitly requested via the <c>sms_psk_identity</c>
     * field in @ref standalone_security_instance_t.
     */
    standalone_security_hsm_query_cb_t *sms_psk_identity_cb;

    /**
     * Opaque argument that will be passed to the function configured in the
     * <c>sms_psk_identity_cb</c> field.
     *
     * If <c>sms_psk_identity_cb</c> is NULL, this field is ignored.
     */
    void *sms_psk_identity_cb_arg;

    /**
     * Callback function that will be called whenever a PSK key for use with SMS
     * binding needs to be stored in an external security engine.
     *
     * If NULL, PSK keys for use with SMS binding will be stored in main system
     * memory unless explicitly requested via the <c>sms_psk_key</c> field in
     * @ref standalone_security_instance_t.
     */
    standalone_security_hsm_query_cb_t *sms_psk_key_cb;

    /**
     * Opaque argument that will be passed to the function configured in the
     * <c>sms_psk_key_cb</c> field.
     *
     * If <c>sms_psk_key_cb</c> is NULL, this field is ignored.
     */
    void *sms_psk_key_cb_arg;
#    endif // ANJAY_WITH_SMS
} standalone_security_hsm_configuration_t;

/**
 * Creates the Security Object in an Anjay instance, with support for moving
 * security credentials to a hardware security module, and installs it in an
 * Anjay instance using @ref anjay_register_object.
 *
 * Do NOT attempt to call @ref anjay_register_object with this object manually,
 * and do NOT try to use the same instance of the Security object with another
 * Anjay instance.
 *
 * For each of the security credential type for which the query string
 * generation callback is provided, any credentials provisioned either using
 * @ref standalone_security_object_add_instance or by the Bootstrap Server, will
 * be stored in the hardware security module and wiped from the main system
 * memory. These credentials will be managed by Anjay and automatically deleted
 * when removed from the data model (either by the Bootstrap Server or
 * @ref standalone_security_object_purge) or when the object is cleaned up
 * without having been properly persisted (see the next paragraph for details).
 *
 * A call to @ref standalone_security_object_cleanup will also cause the removal
 * of all the keys moved into the hardware security module, unless unchanged
 * since the last call to @ref standalone_security_object_persist or @ref
 * standalone_security_object_restore, or marked permanent using @ref
 * standalone_security_mark_hsm_permanent.
 *
 * @param anjay      Anjay instance for which the Security Object is installed.
 *
 * @param hsm_config Configuration of the mechanism that moves security
 *                   credentials to the hardware security module. When the
 *                   pointer is <c>NULL</c> or all of the callback fields are
 *                   <c>NULL</c>, this functions is equivalent to
 *                   @ref standalone_security_object_install.
 *
 * @param prng_ctx   Custom PRNG context to use. If @c NULL , a default one is
 *                   used, with entropy source specific to selected cryptograpic
 *                   backend.
 *
 * NOTE: @p prng_ctx is used when moving security credentials into the HSM,
 * which may happen in one of three scenarios:
 *
 * - During the <c>transaction_validate</c> operation performed by Anjay,
 *   typically while processing a Bootstrap Finish message
 * - As part of @ref standalone_security_object_add_instance
 * - As part of @ref standalone_security_object_restore
 *
 * @returns Handle to the created object that can be passed to other functions
 *          declared in this file, or <c>NULL</c> in case of error.
 */
const anjay_dm_object_def_t **standalone_security_object_install_with_hsm(
        anjay_t *anjay,
        const standalone_security_hsm_configuration_t *hsm_config,
        avs_crypto_prng_ctx_t *prng_ctx);

/**
 * Marks security credential for a given Server Account as "permanent",
 * preventing them from being removed from the hardware security module.
 *
 * The credentials that are moved into hardware security module according to the
 * logic described for @ref standalone_security_object_install_with_hsm are, by
 * default, automatically removed whenever they are deleted from the data model
 * (e.g. by the Bootstrap Server or using @ref
 * standalone_security_object_purge).
 *
 * This function causes such credentials to be marked as "permanent", equivalent
 * to credentials provisioned using the <c>avs_crypto_*_from_engine</c> APIs
 * passed into the fields of the @ref standalone_security_instance_t structure.
 * This will prevent them from being automatically erased from the hardware even
 * if they are removed from the data model, or if the object is cleaned up
 * without up-to-date persistence status.
 *
 * @param obj_ptr Installed Security Object to operate on.
 *
 * @param ssid  Short Server ID of the Server Account whose credentials to mark
 *              as permanent. @ref ANJAY_SSID_BOOTSTRAP may be used to refer to
 *              the Bootstrap Server (if present), and @ref ANJAY_SSID_ANY may
 *              be used to mark <strong>all</strong> the Server Account
 *              credentials as permanent.
 */
void standalone_security_mark_hsm_permanent(
        const anjay_dm_object_def_t *const *obj_ptr, anjay_ssid_t ssid);
#endif // ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_STANDALONE_ANJAY_SECURITY_H */
