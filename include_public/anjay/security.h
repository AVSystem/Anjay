/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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
 * @return 0 in case of success, negative value in case of an error.
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
 * @return 0 in case of success, negative value in case of an error.
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
