/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_SECURITY_H
#define ANJAY_INCLUDE_ANJAY_SECURITY_H

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
#endif // ANJAY_WITH_LWM2M11
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
#endif // ANJAY_WITH_SECURITY_STRUCTURED
} anjay_security_instance_t;

/**
 * Adds new Instance of Security Object and returns newly created Instance id
 * via @p inout_iid .
 *
 * Note: if @p *inout_iid is set to @ref ANJAY_ID_INVALID then the Instance id
 * is generated automatically, otherwise value of @p *inout_iid is used as a
 * new Security Instance Id.
 *
 * Note: @p instance may be safely freed by the user code after this function
 * finishes (internally a deep copy of @ref anjay_security_instance_t is
 * performed).
 *
 * Warning: calling this function during active communication with Bootstrap
 * Server may yield undefined behavior and unexpected failures may occur.
 *
 * @param anjay     Anjay instance with Security Object installed to operate on.
 * @param instance  Security Instance to insert.
 * @param inout_iid Security Instance id to use or @ref ANJAY_ID_INVALID .
 *
 * @return 0 on success, negative value in case of an error or if the instance
 * of specified id already exists.
 */
int anjay_security_object_add_instance(
        anjay_t *anjay,
        const anjay_security_instance_t *instance,
        anjay_iid_t *inout_iid);

/**
 * Purges instances of Security Object leaving it in an empty state.
 *
 * @param anjay Anjay instance with Security Object installed to purge.
 */
void anjay_security_object_purge(anjay_t *anjay);

/**
 * Dumps Security Object Instances to the @p out_stream.
 *
 * @param anjay         Anjay instance with Security Object installed.
 * @param out_stream    Stream to write to.
 * @returns AVS_OK in case of success, or an error code.
 */
avs_error_t anjay_security_object_persist(anjay_t *anjay,
                                          avs_stream_t *out_stream);

/**
 * Attempts to restore Security Object Instances from specified @p in_stream.
 *
 * Note: if restore fails, then Security Object will be left untouched, on
 * success though all Instances stored within the Object will be purged.
 *
 * @param anjay     Anjay instance with Security Object installed.
 * @param in_stream Stream to read from.
 * @returns AVS_OK in case of success, or an error code.
 */
avs_error_t anjay_security_object_restore(anjay_t *anjay,
                                          avs_stream_t *in_stream);

/**
 * Checks whether the Security Object in Anjay instance has been modified since
 * last successful call to @ref anjay_security_object_persist or @ref
 * anjay_security_object_restore.
 */
bool anjay_security_object_is_modified(anjay_t *anjay);

/**
 * Installs the Security Object in an Anjay instance.
 *
 * The Security module does not require explicit cleanup; all resources will be
 * automatically freed up during the call to @ref anjay_delete.
 *
 * @param anjay Anjay instance for which the Security Object is installed.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_security_object_install(anjay_t *anjay);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_SECURITY_H */
