/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef CORE_OBJECTS_SDM_DEVICE
#define CORE_OBJECTS_SDM_DEVICE

#include <anj/sdm_io.h>

/**
 * /3/0/11 resource error codes.
 */
typedef enum {
    SDM_DEVICE_OBJ_ERR_CODE_NO_ERROR = 0,
    SDM_DEVICE_OBJ_ERR_CODE_LOW_BATTERY_POWER = 1,
    SDM_DEVICE_OBJ_ERR_CODE_EXT_POWER_SUPPLY_OFF = 2,
    SDM_DEVICE_OBJ_ERR_CODE_GPS_MODULE_FAILURE = 3,
    SDM_DEVICE_OBJ_ERR_CODE_LOW_RECV_SIGNAL_STRENGTH = 4,
    SDM_DEVICE_OBJ_ERR_CODE_OUT_OF_MEMORY = 5,
    SDM_DEVICE_OBJ_ERR_CODE_SMS_FAILURE = 6,
    SDM_DEVICE_OBJ_ERR_CODE_IP_CONN_FAILURE = 7,
    SDM_DEVICE_OBJ_ERR_CODE_PERIPHERAL_MALFUNCTION = 8,
} sdm_device_obj_err_code_t;

/**
 * Device object initialization structure. Should be filled before passing to
 * @ref sdm_device_object_install .
 */
typedef struct {
    // /3/0/0 resource value, optional
    char *manufacturer;
    // /3/0/1 resource value, optional
    char *model_number;
    // /3/0/2 resource value, optional
    char *serial_number;
    // /3/0/3 resource value, optional
    char *firmware_version;
    // /3/0/4 resource callback, mandatory
    // if not set, execute on /3/0/4 (reboot) resource will fail
    sdm_res_execute_t *reboot_handler;
    // 3/0/16 resource value, mandatory
    // possible values: U (UDP), M (MQTT), H (HTTP), T (TCP), S (SMS),
    //                  N (Non-IP)
    char *supported_binding_modes;
} sdm_device_object_init_t;

/**
 * Installs device object (/3) in SDM.
 *
 * Example usage:
 * @code
 * static int reboot_cb(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst,
 *                      sdm_res_t *res, const char *execute_arg,
 *                      size_t execute_arg_len) {
 *     // perform reboot
 * }
 * ...
 * sdm_device_object_init_t dev_obj_init = {
 *     .manufacturer = "manufacturer",
 *     .model_number = "model_number",
 *     .serial_number = "serial_number",
 *     .firmware_version = "firmware_version",
 *     .reboot_handler = reboot_cb
 * };
 * sdm_device_object_install(&dm, &dev_obj_init);
 * @endcode
 *
 * @param dm       Pointer to a SDM object. Must be non-NULL.
 *
 * @param obj_init Pointer to a device object's initialization structure. Must
 *                 be non-NULL.
 *
 * @returns non-zero value on error (i.e. object is already installed),
 *          0 otherwise.
 */
int sdm_device_object_install(sdm_data_model_t *dm,
                              sdm_device_object_init_t *obj_init);

#endif // CORE_OBJECTS_SDM_DEVICE
