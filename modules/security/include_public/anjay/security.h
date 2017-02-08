/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <anjay/anjay.h>

#include <avsystem/commons/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Resource: Short Server ID */
    anjay_ssid_t ssid;
    /** Resource: LWM2M Server URI */
    const char *server_uri;
    /** Resource: Bootstrap Server */
    bool bootstrap_server;
    /** Resource: Security Mode */
    anjay_udp_security_mode_t security_mode;
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
} anjay_security_instance_t;

/**
 * Creates Security Object ready to get registered in Anjay.
 *
 * @returns pointer to Security Object or NULL in case of error.
 */
const anjay_dm_object_def_t **anjay_security_object_create(void);

/**
 * Adds new Instance of Security Object and returns newly created Instance id
 * via @p inout_iid .
 *
 * Note: if @p *inout_iid is set to @ref ANJAY_IID_INVALID then the Instance id
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
 * @param obj       Security Object to operate on.
 * @param instance  Security Instance to insert.
 * @param inout_iid Security Instance id to use or @ref ANJAY_IID_INVALID .
 *
 * @return 0 on success, negative value in case of an error or if the instance
 * of specified id already exists.
 */
int anjay_security_object_add_instance(
        const anjay_dm_object_def_t *const *obj,
        const anjay_security_instance_t *instance,
        anjay_iid_t *inout_iid);


/**
 * Purges instances of Security Object leaving it in an empty state.
 *
 * @param obj   Security Object to purge.
 */
void anjay_security_object_purge(const anjay_dm_object_def_t *const *obj);

/**
 * Deletes Security Object.
 *
 * @param security  Security Object to remove.
 */
void anjay_security_object_delete(const anjay_dm_object_def_t **security);

/**
 * Dumps Security Object Instance to the @p out_stream.
 * Warning: @p obj MUST NOT be wrapped.
 *
 * @param obj           Security Object.
 * @param out_stream    Stream to write to.
 * @return 0 in case of success, negative value in case of an error.
 */
int anjay_security_object_persist(const anjay_dm_object_def_t *const *obj,
                                  avs_stream_abstract_t *out_stream);

/**
 * Attempts to restore Security Object Instances from specified @p in_stream.
 * Warning: @p obj MUST NOT be wrapped.
 *
 * Note: if restore fails, then Security Object will be left untouched, on
 * success though all Instances stored within the Object will be purged.
 *
 * @param obj       Security Object.
 * @param in_stream Stream to read from.
 * @return 0 in case of success, negative value in case of an error.
 */
int anjay_security_object_restore(const anjay_dm_object_def_t *const *obj,
                                  avs_stream_abstract_t *in_stream);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_SECURITY_H */
