/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_SERVER_OBJ_H
#define SDM_SERVER_OBJ_H

#include <stddef.h>
#include <stdint.h>

#include <fluf/fluf_defs.h>

#include <anj/anj_config.h>

#include <anj/sdm_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDM_SERVER_OID FLUF_OBJ_ID_SERVER

#ifdef ANJ_WITH_DEFAULT_SERVER_OBJ

/*
 * Server Object Resources IDs
 */
enum sdm_server_resources {
    SDM_SERVER_RID_SSID = 0,
    SDM_SERVER_RID_LIFETIME = 1,
    SDM_SERVER_RID_DEFAULT_MIN_PERIOD = 2,
    SDM_SERVER_RID_DEFAULT_MAX_PERIOD = 3,
    SDM_SERVER_RID_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE = 6,
    SDM_SERVER_RID_BINDING = 7,
    SDM_SERVER_RID_REGISTRATION_UPDATE_TRIGGER = 8,
    SDM_SERVER_RID_BOOTSTRAP_REQUEST_TRIGGER = 9,
    SDM_SERVER_RID_BOOTSTRAP_ON_REGISTRATION_FAILURE = 16,
    SDM_SERVER_RID_MUTE_SEND = 23,
};

/*
 * Server Object Instance context, used to store Instance specific data, don't
 * modify directly.
 */
typedef struct {
    uint16_t ssid;
    int64_t lifetime;
    int64_t default_min_period;
    int64_t default_max_period;
    char binding[sizeof("UMHTSN")];
    bool bootstrap_on_registration_failure : 1;
    bool mute_send : 1;
    bool notification_storing : 1;
} server_instance_t;

/**
 * A handler called when the Registration Update Trigger Resource is executed.
 *
 * @param  ssid     Short Server ID of the triggered Object Instance.
 * @param  arg_ptr  Additional data passed when the function is called.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error.
 */
typedef int sdm_server_obj_registration_update_trigger_t(uint16_t ssid,
                                                         void *arg_ptr);

/**
 * A handler called when the Bootstrap Request Trigger Resource is executed.
 *
 * @param  ssid     Short Server ID of the triggered Object Instance.
 * @param  arg_ptr  Additional data passed when the function is called.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error.
 */
typedef int sdm_server_obj_bootstrap_request_trigger_t(uint16_t ssid,
                                                       void *arg_ptr);

/**
 * A structure that holds pointers to the callback functions used for handling
 * executions of the Server Object Resources.
 */
typedef struct {
    sdm_server_obj_registration_update_trigger_t *registration_update_trigger;
    sdm_server_obj_bootstrap_request_trigger_t *bootstrap_request_trigger;
    void *arg_ptr;
} sdm_server_obj_handlers_t;

/**
 * Representation of a single Instance of the Server Object.
 */
typedef struct {
    /** Resource: Short Server ID */
    uint16_t ssid;
    /** Resource: Lifetime */
    int32_t lifetime;
    /** Resource: Default Minimum Period */
    int32_t default_min_period;
    /** Resource: Default Maximum Period, value of 0, means pmax is ignored. */
    int32_t default_max_period;
    /** Resource: Notification Storing When Disabled or Offline */
    bool notification_storing;
    /** Resource: Binding */
    const char *binding;
    /** Resource: Bootstrap on Registration Failure. True if not set. */
    const bool *bootstrap_on_registration_failure;
    /** Resource: Mute Send */
    bool mute_send;
    /** Instance ID. If not set, default value is used. */
    const fluf_iid_t *iid;
} sdm_server_instance_init_t;

/*
 * Complex structure of a whole Server Object entity context that holds the
 * Object and its Instances that are linked to Static Data Model.
 *
 * User is expected to instantiate a structure of this type and not modify it
 * directly throughout the LwM2M Client life.
 */
typedef struct {
    sdm_obj_t obj;
    sdm_obj_inst_t inst[ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER];
    sdm_obj_inst_t *inst_ptr[ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER];
    server_instance_t server_instance[ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER];
    server_instance_t
            cache_server_instance[ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER];
    fluf_op_t op;
    sdm_server_obj_handlers_t server_obj_handlers;
    fluf_iid_t new_instance_iid;
    bool installed;
} sdm_server_obj_t;

/**
 * Initializes the Server Object context. Call this function only once before
 * adding any Instances.
 *
 * @param server_obj_ctx  Context of the Server Object.
 */
void sdm_server_obj_init(sdm_server_obj_t *server_obj_ctx);

/**
 * Adds new Instance of Server Object. After calling @ref
 * sdm_server_obj_install, this function cannot be called.
 *
 * @param server_obj_ctx  Context of the Server Object.
 * @param instance         Server Instance to insert.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int sdm_server_obj_add_instance(sdm_server_obj_t *server_obj_ctx,
                                const sdm_server_instance_init_t *instance);

/**
 * Installs the Server Object into the Static Data Model. Call this function
 * after adding all Instances using @ref sdm_server_obj_add_instance.
 *
 * After calling this function, new Instances can be added only by LwM2M Server.
 *
 * @param dm              Context of the Static Data Model.
 * @param server_obj_ctx  Context of the Server Object.
 * @param handlers        Handlers for the Server Object Executable Resources
 *                        handling.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int sdm_server_obj_install(sdm_data_model_t *dm,
                           sdm_server_obj_t *server_obj_ctx,
                           sdm_server_obj_handlers_t *handlers);

/**
 * Allows to find a Server Object Instance by its Short Server ID.
 *
 * @param      server_obj_ctx Context of the Server Object.
 * @param      ssid           Instance Short Server ID.
 * @param[out] out_iid        Instance ID of the found Instance.
 *
 * @return 0 in case of success, negative value if Instance with given
 *         @p ssid does not exist.
 */
int sdm_server_find_instance_iid(sdm_server_obj_t *server_obj_ctx,
                                 uint16_t ssid,
                                 fluf_iid_t *out_iid);

#endif // ANJ_WITH_DEFAULT_SERVER_OBJ

#ifdef __cplusplus
}
#endif

#endif // SDM_SERVER_OBJ_H
