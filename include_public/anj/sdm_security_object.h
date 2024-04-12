/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_SECURITY_OBJECT_H
#define SDM_SECURITY_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <avsystem/commons/avs_defs.h>

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>
#include <fluf/fluf_utils.h>

#include <anj/sdm.h>
#include <anj/sdm_security_object.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANJ_WITH_DEFAULT_SECURITY_OBJ

/*
 * Security Object Resources IDs
 */
enum sdm_security_resources {
    SDM_SECURITY_RID_SERVER_URI = 0,
    SDM_SECURITY_RID_BOOTSTRAP_SERVER = 1,
    SDM_SECURITY_RID_SECURITY_MODE = 2,
    SDM_SECURITY_RID_PUBLIC_KEY_OR_IDENTITY = 3,
    SDM_SECURITY_RID_SERVER_PUBLIC_KEY = 4,
    SDM_SECURITY_RID_SECRET_KEY = 5,
    SDM_SECURITY_RID_SSID = 10,
};

/* Set maximum value established by the LwM2M
 * specification */
#    define ANJ_SERVER_URI_MAX_SIZE 255

/*
 * Security Object Instance context, used to store Instance specific data, don't
 * modify directly.
 */
typedef struct {
    char server_uri[ANJ_SERVER_URI_MAX_SIZE];
    bool bootstrap_server;
    int64_t security_mode;
    char public_key_or_identity[ANJ_PUBLIC_KEY_OR_IDENTITY_MAX_SIZE];
    size_t public_key_or_identity_size;
    char server_public_key[ANJ_SERVER_PUBLIC_KEY_MAX_SIZE];
    size_t server_public_key_size;
    char secret_key[ANJ_SECRET_KEY_MAX_SIZE];
    size_t secret_key_size;
    uint16_t ssid;
} sdm_security_instance_t;

/**
 * Possible values of the Security Mode Resource, as described in the Security
 * Object definition.
 */
typedef enum {
    SDM_SECURITY_PSK = 0,         // Pre-Shared Key mode
    SDM_SECURITY_RPK = 1,         // Raw Public Key mode
    SDM_SECURITY_CERTIFICATE = 2, // Certificate mode
    SDM_SECURITY_NOSEC = 3,       // NoSec mode
    SDM_SECURITY_EST = 4,         // Certificate mode with EST
} sdm_security_mode_t;

/**
 * Initial structure of a single Instance of the Security Object.
 */
typedef struct {
    /** Resource 0: LwM2M Server URI
     * This resource has to be provided for initialization */
    const char *server_uri;
    /** Resource 1: Bootstrap-Server */
    bool bootstrap_server;
    /** Resource 2: Security Mode */
    sdm_security_mode_t security_mode;
    /** Resource 3: Public Key or Identity */
    const char *public_key_or_identity;
    size_t public_key_or_identity_size;
    /** Resource 4: Server Public Key */
    const char *server_public_key;
    size_t server_public_key_size;
    /** Resource 5: Secret Key */
    const char *secret_key;
    size_t secret_key_size;
    /** Resource 10: Short Server ID, for Bootstrap-Server instance ignored.*/
    uint16_t ssid;
    /** Instance ID. If not set, first non-negative, free integer value is used.
     */
    const fluf_iid_t *iid;
} sdm_security_instance_init_t;

/*
 * Complex structure of a whole Security Object entity context that holds the
 * Object and its Instances that are linked to Static Data Model.
 *
 * User is expected to instantiate a structure of this type and not modify it
 * directly throughout the LwM2M Client life.
 */
typedef struct {
    sdm_obj_t obj;
    sdm_obj_inst_t inst[ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER];
    sdm_obj_inst_t *inst_ptr[ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER];

    sdm_security_instance_t
            security_instances[ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER];
    sdm_security_instance_t
            cache_security_instances[ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER];
    fluf_op_t op;
    fluf_iid_t new_instance_iid;
    bool installed;
} sdm_security_obj_t;

/**
 * Initialize Security Object context. Call this function only once before
 * adding any Instances.
 *
 * @param security_obj_ctx Security Object context to be initialized.
 */
void sdm_security_obj_init(sdm_security_obj_t *security_obj_ctx);

/**
 * Adds new Instance of Security Object.
 *
 * @param security_obj_ctx  Context of the Security Object.
 * @param instance         Security Instance to insert.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int sdm_security_obj_add_instance(sdm_security_obj_t *security_obj_ctx,
                                  sdm_security_instance_init_t *instance);

/**
 * Installs the Security Object into the Static Data Model. Call this function
 * after adding all Instances using @ref sdm_security_obj_add_instance.
 *
 * After calling this function, new Instances can be added only by LwM2M
 * Bootstrap Server.
 *
 * @param dm              Context of the Static Data Model.
 * @param security_obj_ctx  Context of the Security Object.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int sdm_security_obj_install(sdm_data_model_t *dm,
                             sdm_security_obj_t *security_obj_ctx);

#endif // ANJ_WITH_DEFAULT_SECURITY_OBJ

#ifdef __cplusplus
}
#endif

#endif // SDM_SECURITY_OBJECT_H
