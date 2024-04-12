/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_defs.h>

#include <anj/anj_config.h>

#include <anj/sdm.h>
#include <anj/sdm_device_object.h>

#ifdef ANJ_WITH_DEFAULT_DEVICE_OBJ

#    define ERR_CODE_RES_INST_MAX_COUNT 1
#    define DEVICE_OID 3

enum {
    RID_MANUFACTURER = 0,
    RID_MODEL_NUMBER = 1,
    RID_SERIAL_NUMBER = 2,
    RID_FIRMWARE_VERSION = 3,
    RID_REBOOT = 4,
    RID_ERROR_CODE = 11,
    RID_BINDING_MODES = 16,
};

enum {
    RID_MANUFACTURER_IDX = 0,
    RID_MODEL_NUMBER_IDX,
    RID_SERIAL_NUMBER_IDX,
    RID_FIRMWARE_VERSION_IDX,
    RID_REBOOT_IDX,
    RID_ERROR_CODE_IDX,
    RID_BINDING_MODES_IDX,
    _RID_COUNT
};

static const sdm_res_spec_t manufacturer_spec = {
    .rid = RID_MANUFACTURER,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t model_number_spec = {
    .rid = RID_MODEL_NUMBER,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t serial_number_spec = {
    .rid = RID_SERIAL_NUMBER,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t firmware_version_spec = {
    .rid = RID_FIRMWARE_VERSION,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static const sdm_res_spec_t reboot_spec = {
    .rid = RID_REBOOT,
    .operation = SDM_RES_E
};

static const sdm_res_spec_t error_code_spec = {
    .rid = RID_ERROR_CODE,
    .type = FLUF_DATA_TYPE_INT,
    .operation = SDM_RES_RM
};

static const sdm_res_spec_t supported_binding_modes_spec = {
    .rid = RID_BINDING_MODES,
    .type = FLUF_DATA_TYPE_STRING,
    .operation = SDM_RES_R
};

static sdm_res_inst_t err_code_res_inst[ERR_CODE_RES_INST_MAX_COUNT] = {
    {
        .riid = 0,
        /*
         * HACK: error handling is not supported so in order to comply with the
         * definition of the object only one instance of Error Code resource is
         * defined with the value set to 0 which means "no errors".
         */
        .res_value =
                &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(0))
    }
};

static sdm_res_inst_t *err_code_res_insts[ERR_CODE_RES_INST_MAX_COUNT] = {
    &err_code_res_inst[0],
};

static sdm_res_execute_t *reboot_cb;

static int res_execute(sdm_obj_t *obj,
                       sdm_obj_inst_t *obj_inst,
                       sdm_res_t *res,
                       const char *execute_arg,
                       size_t execute_arg_len) {
    (void) obj;
    (void) obj_inst;
    (void) execute_arg;
    (void) execute_arg_len;

    fluf_rid_t rid = res->res_spec->rid;

    switch (rid) {
    case RID_REBOOT:
        return !reboot_cb ? SDM_ERR_INTERNAL
                          : reboot_cb(obj,
                                      obj_inst,
                                      res,
                                      execute_arg,
                                      execute_arg_len);
    default:
        break;
    }

    return SDM_ERR_NOT_FOUND;
}

static int res_read(sdm_obj_t *obj,
                    sdm_obj_inst_t *obj_inst,
                    sdm_res_t *res,
                    sdm_res_inst_t *res_inst,
                    fluf_res_value_t *out_value) {
    (void) obj;
    (void) obj_inst;
    (void) res_inst;
    (void) out_value;

    fluf_rid_t rid = res->res_spec->rid;
    fluf_rid_t riid = res_inst->riid;

    switch (rid) {
    case RID_ERROR_CODE: {
        if (res->value.res_inst.inst_count < riid - 1) {
            return SDM_ERR_INPUT_ARG;
        }
        *out_value = res_inst->res_value->value;
        return 0;
    }
    default:
        break;
    }

    return SDM_ERR_NOT_FOUND;
}

static const sdm_res_handlers_t res_handlers = {
    .res_execute = res_execute,
    .res_read = res_read,
};

static sdm_res_t device_res[_RID_COUNT] = {
    [RID_MANUFACTURER_IDX] = {
        .res_spec = &manufacturer_spec,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    [RID_MODEL_NUMBER_IDX] = {
        .res_spec = &model_number_spec,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    [RID_SERIAL_NUMBER_IDX] = {
        .res_spec = &serial_number_spec,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    [RID_FIRMWARE_VERSION_IDX] = {
        .res_spec = &firmware_version_spec,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
    [RID_REBOOT_IDX] = {
        .res_spec = &reboot_spec,
        .res_handlers = &res_handlers
    },
    [RID_ERROR_CODE_IDX] = {
        .res_spec = &error_code_spec,
        .value.res_inst.insts = err_code_res_insts,
        .value.res_inst.inst_count = 1,
        .value.res_inst.max_inst_count = ERR_CODE_RES_INST_MAX_COUNT,
        .res_handlers = &res_handlers
    },
    [RID_BINDING_MODES_IDX] = {
        .res_spec = &supported_binding_modes_spec,
        .value.res_value = &SDM_MAKE_RES_VALUE(0)
    },
};

static sdm_obj_inst_t obj_inst = {
    .iid = 0,
    .resources = device_res,
    .res_count = AVS_ARRAY_SIZE(device_res)
};

static sdm_obj_inst_t *device_obj_inst[1] = { &obj_inst };

static sdm_obj_t device_obj = {
    .oid = DEVICE_OID,
    .version = "1.2",
    .inst_count = 1,
    .max_inst_count = 1,
    .insts = device_obj_inst,
    .obj_handlers = NULL
};

static void string_res_initialize(sdm_res_t *res, const char *value) {
    memset(&res->value.res_value->value, 0, sizeof(*res->value.res_value));
    res->value.res_value->value.bytes_or_string.data =
            (void *) (intptr_t) value;
    if (value) {
        res->value.res_value->value.bytes_or_string.chunk_length =
                strlen(value);
    }
}

static void res_values_initialize(sdm_device_object_init_t *obj_init) {
    string_res_initialize(&device_res[RID_MANUFACTURER_IDX],
                          obj_init->manufacturer);
    string_res_initialize(&device_res[RID_MODEL_NUMBER_IDX],
                          obj_init->model_number);
    string_res_initialize(&device_res[RID_SERIAL_NUMBER_IDX],
                          obj_init->serial_number);
    string_res_initialize(&device_res[RID_FIRMWARE_VERSION_IDX],
                          obj_init->firmware_version);
    string_res_initialize(&device_res[RID_BINDING_MODES_IDX],
                          obj_init->supported_binding_modes);
    reboot_cb = obj_init->reboot_handler;
}

int sdm_device_object_install(sdm_data_model_t *dm,
                              sdm_device_object_init_t *obj_init) {
    assert(dm);
    assert(obj_init);

    res_values_initialize(obj_init);

    return sdm_add_obj(dm, &device_obj);
}

#endif // ANJ_WITH_DEFAULT_DEVICE_OBJ
