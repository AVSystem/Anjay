/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_DEVICE_OBJECT_H
#define SDM_DEVICE_OBJECT_H

#include <anj/anj_config.h>

#include <anj/anj_config.h>
#include <anj/sdm_io.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANJ_WITH_DEFAULT_DEVICE_OBJ

/**
 * Device object initialization structure. Should be filled before passing to
 * @ref sdm_device_object_install .
 *
 * NOTE: when passing this structure to @ref sdm_device_object_install, its
 * fields ARE NOT copied internally to SDM, which means all dynamically
 * allocated strings MUST NOT be freed before removing the device object from
 * the SDM.
 */
typedef struct {
    // /3/0/0 resource value, optional
    const char *manufacturer;
    // /3/0/1 resource value, optional
    const char *model_number;
    // /3/0/2 resource value, optional
    const char *serial_number;
    // /3/0/3 resource value, optional
    const char *firmware_version;
    // /3/0/4 resource callback, mandatory
    // if not set, execute on /3/0/4 (reboot) resource will fail
    sdm_res_execute_t *reboot_handler;
    // 3/0/16 resource value, mandatory
    // possible values: U (UDP), M (MQTT), H (HTTP), T (TCP), S (SMS),
    //                  N (Non-IP)
    const char *supported_binding_modes;
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

#endif // ANJ_WITH_DEFAULT_DEVICE_OBJ

#ifdef __cplusplus
}
#endif

#endif // SDM_DEVICE_OBJECT_H
