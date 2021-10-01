/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#include <anjay/ipso_objects.h>

#include <avsystem/commons/avs_log.h>

#include "../demo_utils.h"
#include "../objects.h"

#define ANJAY_DEMO_TEMPERATURE_UNIT "Cel"
#define ANJAY_DEMO_TEMPERATURE_OID 3303
#define ANJAY_DEMO_TEMPERATURE_MAX_VALUE 42
#define ANJAY_DEMO_TEMPERATURE_CHANGE 13

#define ANJAY_DEMO_TEMPERATURE_MAX_INSTANCE_NUM 16

static int temperature_get_value(anjay_iid_t iid, void *ctx, double *value) {
    (void) iid;
    (void) ctx;

    static int counter = 1;
    *value = (double) counter;
    counter = (counter + ANJAY_DEMO_TEMPERATURE_CHANGE)
              % (ANJAY_DEMO_TEMPERATURE_MAX_VALUE + 1);
    return 0;
}

int install_temperature_object(anjay_t *anjay) {
    if (anjay_ipso_basic_sensor_install(anjay,
                                        ANJAY_DEMO_TEMPERATURE_OID,
                                        ANJAY_DEMO_TEMPERATURE_MAX_INSTANCE_NUM)
            || anjay_ipso_basic_sensor_instance_add(
                       anjay,
                       ANJAY_DEMO_TEMPERATURE_OID,
                       0,
                       (anjay_ipso_basic_sensor_impl_t) {
                           .unit = ANJAY_DEMO_TEMPERATURE_UNIT,
                           .get_value = temperature_get_value,
                           .min_range_value = 0,
                           .max_range_value =
                                   (double) ANJAY_DEMO_TEMPERATURE_MAX_VALUE
                       })) {
        avs_log(ipso, ERROR, "Could not install Temperature object");
        return -1;
    }

    return 0;
}

void temperature_update_handler(anjay_t *anjay) {
    (void) anjay_ipso_basic_sensor_update(anjay, ANJAY_DEMO_TEMPERATURE_OID, 0);
}

void temperature_add_instance(anjay_t *anjay, anjay_iid_t iid) {
    (void) anjay_ipso_basic_sensor_instance_add(
            anjay,
            ANJAY_DEMO_TEMPERATURE_OID,
            iid,
            (anjay_ipso_basic_sensor_impl_t) {
                .unit = ANJAY_DEMO_TEMPERATURE_UNIT,
                .get_value = temperature_get_value,
                .min_range_value = 0,
                .max_range_value = (double) ANJAY_DEMO_TEMPERATURE_MAX_VALUE
            });
}

void temperature_remove_instance(anjay_t *anjay, anjay_iid_t iid) {
    (void) anjay_ipso_basic_sensor_instance_remove(
            anjay, ANJAY_DEMO_TEMPERATURE_OID, iid);
}

#define ANJAY_DEMO_ACCELEROMETER_UNIT "m/s2"
#define ANJAY_DEMO_ACCELEROMETER_OID 3313
#define ANJAY_DEMO_ACCELEROMETER_MAX 42
#define ANJAY_DEMO_ACCELEROMETER_CHANGE 17

#define ANJAY_DEMO_ACCELEROMETER_MAX_INSTANCE_NUM 16

static int accelerometer_get_values(anjay_iid_t iid,
                                    void *ctx,
                                    double *x_value,
                                    double *y_value,
                                    double *z_value) {
    (void) iid;
    (void) ctx;

    static int counter = 1;
    *x_value = (double) counter;
    counter = (counter + ANJAY_DEMO_ACCELEROMETER_CHANGE)
              % (ANJAY_DEMO_ACCELEROMETER_MAX + 1);
    *y_value = (double) counter;
    counter = (counter + ANJAY_DEMO_ACCELEROMETER_CHANGE)
              % (ANJAY_DEMO_ACCELEROMETER_MAX + 1);
    *z_value = (double) counter;
    counter = (counter + ANJAY_DEMO_ACCELEROMETER_CHANGE)
              % (ANJAY_DEMO_ACCELEROMETER_MAX + 1);
    return 0;
}

int install_accelerometer_object(anjay_t *anjay) {
    if (anjay_ipso_3d_sensor_install(anjay,
                                     ANJAY_DEMO_ACCELEROMETER_OID,
                                     ANJAY_DEMO_ACCELEROMETER_MAX_INSTANCE_NUM)
            || anjay_ipso_3d_sensor_instance_add(
                       anjay,
                       ANJAY_DEMO_ACCELEROMETER_OID,
                       0,
                       (anjay_ipso_3d_sensor_impl_t) {
                           .unit = ANJAY_DEMO_ACCELEROMETER_UNIT,
                           .get_values = accelerometer_get_values,
                           .use_y_value = true,
                           .use_z_value = true,
                           .min_range_value = 0.0,
                           .max_range_value =
                                   (double) ANJAY_DEMO_ACCELEROMETER_MAX
                       })) {
        avs_log(ipso, ERROR, "Could not install Accelerometer object");
        return -1;
    }

    return 0;
}

void accelerometer_update_handler(anjay_t *anjay) {
    (void) anjay_ipso_3d_sensor_update(anjay, ANJAY_DEMO_ACCELEROMETER_OID, 0);
}

void accelerometer_add_instance(anjay_t *anjay, anjay_iid_t iid) {

    (void) anjay_ipso_3d_sensor_instance_add(
            anjay,
            ANJAY_DEMO_ACCELEROMETER_OID,
            iid,
            (anjay_ipso_3d_sensor_impl_t) {
                .unit = ANJAY_DEMO_ACCELEROMETER_UNIT,
                .get_values = accelerometer_get_values,
                .use_y_value = true,
                .use_z_value = true,
                .min_range_value = 0.0,
                .max_range_value = (double) ANJAY_DEMO_ACCELEROMETER_MAX
            });
}

void accelerometer_remove_instance(anjay_t *anjay, anjay_iid_t iid) {
    (void) anjay_ipso_3d_sensor_instance_remove(
            anjay, ANJAY_DEMO_ACCELEROMETER_OID, iid);
}

#define ANJAY_DEMO_PUSH_BUTTON_MAX_INSTANCE_NUM 16

int install_push_button_object(anjay_t *anjay) {
    if (anjay_ipso_button_install(anjay,
                                  ANJAY_DEMO_PUSH_BUTTON_MAX_INSTANCE_NUM)
            || anjay_ipso_button_instance_add(anjay, 0, "Fake demo Button")) {
        avs_log(ipso, ERROR, "Could not install Push Button object");
        return -1;
    }

    return 0;
}
