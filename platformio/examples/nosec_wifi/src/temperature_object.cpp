/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <Arduino.h>

#include <anj/sdm_io.h>

#include <temperature_object.hpp>

static int sensor_read_callback(sdm_obj_t *obj,
                                sdm_obj_inst_t *obj_inst,
                                sdm_res_t *res,
                                sdm_res_inst_t *res_inst,
                                fluf_res_value_t *out_value);

static const sdm_res_spec_t sensor_val_res_spec = {
    .rid = 5700,
    .type = FLUF_DATA_TYPE_DOUBLE,
    .operation = SDM_RES_R
};
static const sdm_res_spec_t sensor_unit_spec = {
    .rid = 5701,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};
static const sdm_res_spec_t sensor_application_type_spec = {
    .rid = 5750,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_RW
};

static const sdm_res_handlers_t res_handlers = {
    .res_read = sensor_read_callback
};
static char units[] = "C";
static char application_type[20];

static sdm_res_value_t sensor_unit_res_value = {
    .value = {
        .bytes_or_string = {
            .data = units
        }
    }
};

static sdm_res_value_t sensor_application_type_res_value = {
    .value = {
        .bytes_or_string = {
            .data = application_type
        }
    },
    .resource_buffer_size = sizeof(application_type)
};

static sdm_res_t resources_of_inst_1[3] = {
    {
        .res_spec = &sensor_val_res_spec,
        .res_handlers = &res_handlers,
    },
    {
        .res_spec = &sensor_unit_spec,
        .res_handlers = nullptr,
        .value = {
            .res_value = &sensor_unit_res_value
        }
    },
    {
        .res_spec = &sensor_application_type_spec,
        .res_handlers = nullptr,
        .value = {
            .res_value = &sensor_application_type_res_value
        }
    }
};

static sdm_res_t resources_of_inst_2[1] = {
    {
        .res_spec = &sensor_val_res_spec,
        .res_handlers = &res_handlers
    }
};

static sdm_obj_inst_t temperature_obj_inst_1 = {
    .iid = 0,
    .resources = resources_of_inst_1,
    .res_count = 3
};
static sdm_obj_inst_t temperature_obj_inst_2 = {
    .iid = 1,
    .resources = resources_of_inst_2,
    .res_count = 1
};

static sdm_obj_inst_t *temperature_obj_insts[2] = { &temperature_obj_inst_1,
                                                    &temperature_obj_inst_2 };

static sdm_obj_t temperature_obj = {
    .oid = 3303,
    .version = "1.1",
    .obj_handlers = nullptr,
    .insts = temperature_obj_insts,
    .max_inst_count = 2,
    .inst_count = 2,
    .in_transaction = false
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

int temperature_object_add(sdm_data_model_t *dm) {
    return sdm_add_obj(dm, &temperature_obj);
}
