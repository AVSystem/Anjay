#include <stdlib.h>

#include <anjay/anjay.h>
#include <anjay/ipso_objects.h>
#include <anjay/ipso_objects_v2.h>
#include <anjay/security.h>
#include <anjay/server.h>

#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_sched.h>

#define TEMPERATURE_OBJ_OID 3303
#define ACCELEROMETER_OBJ_OID 3313

#define THERMOMETER_COUNT 3
#define ACCELEROMETER_COUNT 2
#define BUTTON_COUNT 4

static const anjay_ipso_v2_basic_sensor_meta_t thermometer_meta = {
    .unit = "Cel",
    .min_max_measured_value_present = true,
    .min_range_value = -20.0,
    .max_range_value = 120.0
};

static const anjay_ipso_v2_3d_sensor_meta_t accelerometer_meta = {
    .unit = "m/s2",
    .min_range_value = -20.0,
    .max_range_value = 20.0,
    .y_axis_present = true,
    .z_axis_present = true
};

static double get_random_in_range(double min, double max) {
    return min + (max - min) * rand() / RAND_MAX;
}

static double get_thermometer_value(void) {
    return get_random_in_range(thermometer_meta.min_range_value,
                               thermometer_meta.max_range_value);
}

static anjay_ipso_v2_3d_sensor_value_t get_accelerometer_value(void) {
    return (anjay_ipso_v2_3d_sensor_value_t) {
        .x = get_random_in_range(accelerometer_meta.min_range_value,
                                 accelerometer_meta.max_range_value),
        .y = get_random_in_range(accelerometer_meta.min_range_value,
                                 accelerometer_meta.max_range_value),
        .z = get_random_in_range(accelerometer_meta.min_range_value,
                                 accelerometer_meta.max_range_value)
    };
}

static bool get_button_state(void) {
    return rand() % 2 == 0;
}

static int setup_temperature_object(anjay_t *anjay) {
    if (anjay_ipso_v2_basic_sensor_install(anjay, TEMPERATURE_OBJ_OID, NULL,
                                           THERMOMETER_COUNT)) {
        return -1;
    }

    for (anjay_iid_t iid = 0; iid < THERMOMETER_COUNT; iid++) {
        if (anjay_ipso_v2_basic_sensor_instance_add(
                    anjay, TEMPERATURE_OBJ_OID, iid, 20.0, &thermometer_meta)) {
            return -1;
        }
    }

    return 0;
}

static int setup_accelerometer_object(anjay_t *anjay) {
    if (anjay_ipso_v2_3d_sensor_install(anjay, ACCELEROMETER_OBJ_OID, NULL,
                                        ACCELEROMETER_COUNT)) {
        return -1;
    }

    for (anjay_iid_t iid = 0; iid < ACCELEROMETER_COUNT; iid++) {
        anjay_ipso_v2_3d_sensor_value_t initial_value = {
            .x = 0.0,
            .y = 0.0,
            .z = 0.0
        };

        if (anjay_ipso_v2_3d_sensor_instance_add(anjay, ACCELEROMETER_OBJ_OID,
                                                 iid, &initial_value,
                                                 &accelerometer_meta)) {
            return -1;
        }
    }

    return 0;
}

static int setup_button_object(anjay_t *anjay) {
    if (anjay_ipso_button_install(anjay, BUTTON_COUNT)) {
        return -1;
    }

    for (anjay_iid_t iid = 0; iid < BUTTON_COUNT; iid++) {
        if (anjay_ipso_button_instance_add(anjay, iid, "")) {
            return -1;
        }
    }

    return 0;
}

static int setup_security_object(anjay_t *anjay) {
    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://eu.iot.avsystem.cloud:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    if (anjay_security_object_install(anjay)) {
        return -1;
    }

    // let Anjay assign an Object Instance ID
    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        return -1;
    }

    return 0;
}

static int setup_server_object(anjay_t *anjay) {
    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 60,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    if (anjay_server_object_install(anjay)) {
        return -1;
    }

    anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
    if (anjay_server_object_add_instance(anjay, &server_instance,
                                         &server_instance_id)) {
        return -1;
    }

    return 0;
}

static void update_sensor_values(avs_sched_t *sched, const void *anjay_ptr) {
    anjay_t *anjay = *(anjay_t *const *) anjay_ptr;

    for (anjay_iid_t iid = 0; iid < THERMOMETER_COUNT; iid++) {
        (void) anjay_ipso_v2_basic_sensor_value_update(
                anjay, TEMPERATURE_OBJ_OID, iid, get_thermometer_value());
    }

    for (anjay_iid_t iid = 0; iid < ACCELEROMETER_COUNT; iid++) {
        anjay_ipso_v2_3d_sensor_value_t value = get_accelerometer_value();

        (void) anjay_ipso_v2_3d_sensor_value_update(
                anjay, ACCELEROMETER_OBJ_OID, iid, &value);
    }

    for (anjay_iid_t iid = 0; iid < BUTTON_COUNT; iid++) {
        (void) anjay_ipso_button_update(anjay, iid, get_button_state());
    }

    AVS_SCHED_DELAYED(sched, NULL, avs_time_duration_from_scalar(1, AVS_TIME_S),
                      update_sensor_values, &anjay, sizeof(anjay));
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
        return -1;
    }

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000
    };

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        return -1;
    }

    int result = 0;

    if (setup_security_object(anjay) || setup_server_object(anjay)
            || setup_temperature_object(anjay)
            || setup_accelerometer_object(anjay)
            || setup_button_object(anjay)) {
        result = -1;
    }

    if (!result) {
        update_sensor_values(anjay_get_scheduler(anjay), &anjay);
        result = anjay_event_loop_run(
                anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
    }

    anjay_delete(anjay);
    return result;
}
