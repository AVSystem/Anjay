/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define _DEFAULT_SOURCE

#include <unistd.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <anjay_lite/anjay_lite.h>
#include <anjay_lite/anjay_net.h>

#include <anj/sdm_device_object.h>
#include <anj/sdm_io.h>

static int sensor_read_callback(sdm_obj_t *obj,
                                sdm_obj_inst_t *obj_inst,
                                sdm_res_t *res,
                                sdm_res_inst_t *res_inst,
                                fluf_res_value_t *out_value);

static const sdm_res_spec_t sensor_val_res_spec = {
    .rid = 5700,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_DOUBLE
};
static const sdm_res_spec_t sensor_unit_spec = {
    .rid = 5701,
    .operation = SDM_RES_R,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_spec_t sensor_application_type_spec = {
    .rid = 5750,
    .operation = SDM_RES_RW,
    .type = FLUF_DATA_TYPE_STRING
};
static const sdm_res_handlers_t res_handlers = {
    .res_read = sensor_read_callback
};
static sdm_res_t sensor_val_res = {
    .res_handlers = &res_handlers,
    .res_spec = &sensor_val_res_spec
};
static char units[] = "C";
static char application_type[20];
static sdm_res_t resources[3] = {
    {
        .res_spec = &sensor_val_res_spec,
        .res_handlers = &res_handlers
    },
    {
        .res_spec = &sensor_unit_spec,
        .value.res_value.value.bytes_or_string.data = units,
        .value.res_value.value.bytes_or_string.chunk_length = sizeof(units) - 1
    },
    {
        .res_spec = &sensor_application_type_spec,
        .value.res_value.value.bytes_or_string.data = application_type,
        .value.res_value.resource_buffer_size = sizeof(application_type)
    }
};
static sdm_obj_inst_t temperature_obj_inst_1 = {
    .iid = 0,
    .res_count = 3,
    .resources = resources
};
static sdm_obj_inst_t temperature_obj_inst_2 = {
    .iid = 1,
    .res_count = 1,
    .resources = &sensor_val_res
};

static sdm_obj_inst_t *temperature_obj_insts[2] = { &temperature_obj_inst_1,
                                                    &temperature_obj_inst_2 };

static sdm_obj_t temperature_obj = {
    .version = "1.1",
    .oid = 3303,
    .insts = temperature_obj_insts,
    .inst_count = 2,
    .max_inst_count = 2
};

static int sensor_read_callback(sdm_obj_t *obj,
                                sdm_obj_inst_t *obj_inst,
                                sdm_res_t *res,
                                sdm_res_inst_t *res_inst,
                                fluf_res_value_t *out_value) {
    (void) (obj);
    (void) (res);
    (void) (res_inst);

    static double sensor_value_1 = 0.0;
    static double sensor_value_2 = 2.0;

    if (obj_inst == &temperature_obj_inst_1) {
        out_value->double_value = sensor_value_1;
        sensor_value_1 += 1.23;
    } else if (obj_inst == &temperature_obj_inst_2) {
        out_value->double_value = sensor_value_2;
        sensor_value_2 *= 2.0;
    } else {
        return SDM_ERR_BAD_REQUEST;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("No endpoint name given\n");
        return -1;
    }

    anjay_lite_t anjay_lite = {
        .endpoint_name = argv[1],
        .server_conf = {
            .binding = FLUF_BINDING_UDP,
            .ssid = 1,
            .security_mode = ANJAY_SECURITY_NOSEC,
            .hostname = "eu.iot.avsystem.cloud",
            .port = 5683,
            .lifetime = 20,
        }
    };

    sdm_device_object_init_t device_obj_conf = {
        .firmware_version = "1.0",
        .manufacturer = "",
        .reboot_handler = NULL,
        .serial_number = "12345",
        .supported_binding_modes = "U"
    };

    sdm_initialize(&anjay_lite.dm,
                   anjay_lite.objs_array,
                   ANJAY_LITE_ALLOWED_OBJECT_NUMBER);
    if (sdm_add_obj(&anjay_lite.dm, &temperature_obj)) {
        printf("sdm_add_obj error\n");
        return -1;
    }

    if (sdm_device_object_install(&anjay_lite.dm, &device_obj_conf)) {
        printf("sdm_device_object_install error\n");
        return -1;
    }

    if (anjay_lite_init(&anjay_lite)) {
        printf("anjay_lite_init error\n");
        return -1;
    }

    while (1) {
        anjay_lite_process(&anjay_lite);
        usleep(50 * 1000);
    }

    return 0;
}
