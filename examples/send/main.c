#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <anj/sdm_device_object.h>
#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>
#include <anj/sdm_security_object.h>
#include <anj/sdm_server_object.h>

#include "event_loop.h"
#include "example_config.h"

static sdm_device_object_init_t device_obj_conf = {
    .firmware_version = "0.1",
    .supported_binding_modes = "U"
};

static sdm_server_instance_init_t server_inst = {
    .ssid = 1,
    .lifetime = 50,
    .binding = "U",
    .bootstrap_on_registration_failure = &(bool) { false }
};

#ifdef EXAMPLE_WITH_DTLS_PSK
static const char PSK_IDENTITY[] = "identity";
static const char PSK_KEY[] = "P4s$w0rd";
#endif // EXAMPLE_WITH_DTLS_PSK

static sdm_security_instance_init_t security_inst = {
    .ssid = 1,
#ifdef EXAMPLE_WITH_DTLS_PSK
    .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
    .security_mode = SDM_SECURITY_PSK,
    .public_key_or_identity = PSK_IDENTITY,
    .public_key_or_identity_size = sizeof(PSK_IDENTITY) - 1,
    .secret_key = PSK_KEY,
    .secret_key_size = sizeof(PSK_KEY) - 1
#else  // EXAMPLE_WITH_DTLS_PSK
    .server_uri = "coap://eu.iot.avsystem.cloud:5683",
    .security_mode = SDM_SECURITY_NOSEC
#endif // EXAMPLE_WITH_DTLS_PSK
};

static int sensor_read_callback(sdm_obj_t *obj,
                                sdm_obj_inst_t *obj_inst,
                                sdm_res_t *res,
                                sdm_res_inst_t *res_inst,
                                fluf_res_value_t *out_value);

static const sdm_res_handlers_t RES_HANDLERS = {
    .res_read = sensor_read_callback
};
static char units[] = "C";
static char application_type[20];

static const sdm_res_spec_t SENSOR_VAL_SPEC =
        SDM_MAKE_RES_SPEC(5700, FLUF_DATA_TYPE_DOUBLE, SDM_RES_R);
static const sdm_res_spec_t SENSOR_UNIT_SPEC =
        SDM_MAKE_RES_SPEC(5701, FLUF_DATA_TYPE_STRING, SDM_RES_R);
static const sdm_res_spec_t SENSOR_APPLICATION_TYPE_SPEC =
        SDM_MAKE_RES_SPEC(5750, FLUF_DATA_TYPE_STRING, SDM_RES_RW);

static sdm_res_t resources_of_inst_1[3] = {
    SDM_MAKE_RES(&SENSOR_VAL_SPEC, &RES_HANDLERS, NULL),
    SDM_MAKE_RES(&SENSOR_UNIT_SPEC,
                 NULL,
                 &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(
                         0, SDM_INIT_RES_VAL_STRING(units))),
    SDM_MAKE_RES(&SENSOR_APPLICATION_TYPE_SPEC,
                 NULL,
                 &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(sizeof(application_type),
                                                     SDM_INIT_RES_VAL_STRING(
                                                             application_type)))
};
static sdm_res_t resources_of_inst_2[1] = {
    SDM_MAKE_RES(&SENSOR_VAL_SPEC, &RES_HANDLERS, NULL)
};
static sdm_obj_inst_t temperature_obj_inst_1 = {
    .iid = 0,
    .res_count = 3,
    .resources = resources_of_inst_1
};
static sdm_obj_inst_t temperature_obj_inst_2 = {
    .iid = 1,
    .res_count = 1,
    .resources = resources_of_inst_2
};
static sdm_obj_inst_t *temperature_obj_insts[2] = { &temperature_obj_inst_1,
                                                    &temperature_obj_inst_2 };

static sdm_obj_t temperature_obj = {
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

static event_loop_ctx_t event_loop;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("No endpoint name given\n");
        return -1;
    }
    // Initialize event_loop structure, install data model with three basic
    // objects
    if (event_loop_init(&event_loop, argv[1], &device_obj_conf, &server_inst,
                        &security_inst)) {
        printf("event_loop_init error\n");
        return -1;
    }
    if (sdm_add_obj(&event_loop.dm, &temperature_obj)) {
        printf("install_temperature_object error\n");
        return -1;
    }

    while (true) {
        event_loop_run(&event_loop);
        usleep(50 * 1000);
    }
    return 0;
}
