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
#ifndef ANJAY_IPSO_OBJECTS_H
#define ANJAY_IPSO_OBJECTS_H

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Type of the user provided callbacks to read a basic sensor value.
 *
 * @param iid   IID of the instance for which the value will be read.
 * @param ctx   User provided context.
 * @param value Output.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
typedef int anjay_ipso_basic_sensor_value_reader_t(anjay_iid_t iid,
                                                   void *ctx,
                                                   double *value);

typedef struct anjay_ipso_basic_sensor_impl_struct {
    /**
     * Unit of the measured values.
     *
     * The pointed string won't be copied, so user code must assure that the
     * pointer will remain valid for the lifetime of the object.
     */
    const char *unit;

    /**
     * User context which will be passed to @ref get_value callback.
     */
    void *user_context;

    /**
     * The minimum value that can be measured by the sensor.
     *
     * If the value is NaN the resource won't be created.
     */
    double min_range_value;

    /**
     * The maximum value that can be measured by the sensor.
     *
     * If the value is NaN the resource won't be created.
     */
    double max_range_value;

    /**
     * User provided callback for reading the sensor value.
     */
    anjay_ipso_basic_sensor_value_reader_t *get_value;
} anjay_ipso_basic_sensor_impl_t;

/**
 * Installs a basic sensor object in an Anjay object.
 *
 * @param anjay         Anjay object for which the sensor object is installed.
 * @param oid           OID of the installed object.
 * @param num_instances Maximum number of the instances which will be created
 *                      for the installed object.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_basic_sensor_install(anjay_t *anjay,
                                    anjay_oid_t oid,
                                    size_t num_instances);

/**
 * Adds an instance of a sensor object installed in an Anjay object.
 *
 * @param anjay Anjay object with the installed the sensor object.
 * @param oid   OID of the installed object.
 * @param iid   IID of the added instance. Should be lower than
 *              the number of instances passed to the corresponding
 *              @ref anjay_ipso_basic_sensor_install
 * @param impl  Parameters and callbacks needed to initialize an instance.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_basic_sensor_instance_add(
        anjay_t *anjay,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_ipso_basic_sensor_impl_t impl);

/**
 * Adds an instance of a sensor object installed in an Anjay object.
 *
 * @param anjay Anjay object with the installed the sensor object.
 * @param oid   OID of the installed object.
 * @param iid   IID of the removed instance. Should be lower than
 *              the number of instances passed to the corresponding
 *              @ref anjay_ipso_basic_sensor_install
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_basic_sensor_instance_remove(anjay_t *anjay,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid);

/**
 * Updates a basic sensor object installed in an Anjay object.
 *
 * @param anjay Anjay object with the installed the sensor object.
 * @param oid   OID of the installed object.
 * @param iid   IID of the updated instance.
 */
int anjay_ipso_basic_sensor_update(anjay_t *anjay,
                                   anjay_oid_t oid,
                                   anjay_iid_t iid);

/**
 * Type of the user provided callbacks to read the three-axis sensor value.
 *
 * @param iid     IID of the instance reading the value.
 * @param ctx     User provided context.
 * @param x_value X axis output.
 * @param y_value Y axis output.
 * @param z_value Z axis output.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
typedef int anjay_ipso_3d_sensor_value_reader_t(anjay_iid_t iid,
                                                void *ctx,
                                                double *x_value,
                                                double *y_value,
                                                double *z_value);

typedef struct anjay_ipso_3d_sensor_impl_struct {
    /**
     * Unit of the measured values.
     *
     * The pointed string won't be copied, so user code must assure that the
     * pointer will remain valid for the lifetime of the object.
     */
    const char *unit;
    /**
     * Enables usage of the optional Y axis.
     */
    bool use_y_value;
    /**
     * Enables usage of the optional Z axis.
     */
    bool use_z_value;

    /**
     * User context which will be passed to @ref get_values callback.
     */
    void *user_context;

    /**
     * The minimum value that can be measured by the sensor.
     *
     * If the value is NaN the resource won't be created.
     */
    double min_range_value;

    /**
     * The maximum value that can be measured by the sensor.
     *
     * If the value is NaN the resource won't be created.
     */
    double max_range_value;

    /**
     * User provided callback for reading the sensor value.
     */
    anjay_ipso_3d_sensor_value_reader_t *get_values;
} anjay_ipso_3d_sensor_impl_t;

/**
 * Installs a three-axis sensor object in an Anjay object.
 *
 * @param anjay         Anjay object for which the sensor object is installed.
 * @param oid           OID of the installed object.
 * @param num_instances Maximum number of the instances which will be created
 *                      for the installed object.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_3d_sensor_install(anjay_t *anjay,
                                 anjay_oid_t oid,
                                 size_t num_instances);

/**
 * Adds an instance of a three-axis sensor object installed in an Anjay object.
 *
 * @param anjay Anjay object with the installed the sensor object.
 * @param oid   OID of the installed object.
 * @param iid   IID of the added instance. Should be lower than
 *              the number of instances passed to the corresponding
 *              @ref anjay_ipso_3d_sensor_install
 * @param impl  Parameters and callbacks needed to initialize an instance.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_3d_sensor_instance_add(anjay_t *anjay,
                                      anjay_oid_t oid,
                                      anjay_iid_t iid,
                                      const anjay_ipso_3d_sensor_impl_t impl);

/**
 * Removes an instance of a three-axis sensor object installed in an Anjay
 * object.
 *
 * @param anjay Anjay object with the installed the sensor object.
 * @param oid   OID of the installed object.
 * @param iid   IID of the removed instance.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_3d_sensor_instance_remove(anjay_t *anjay,
                                         anjay_oid_t oid,
                                         anjay_iid_t iid);

/**
 * Updates a three-axis sensor object installed in an Anjay object.
 *
 * @param anjay Anjay object with the installed the sensor object.
 * @param oid   OID of the installed object.
 * @param iid   IID of the updated instance.
 */
int anjay_ipso_3d_sensor_update(anjay_t *anjay,
                                anjay_oid_t oid,
                                anjay_iid_t iid);

/**
 * Installs button object in the given Anjay object.
 *
 * @param anjay         Anjay object for the Push Button Object installation.
 * @param num_instances Maximum number of the instances of the installed object.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_button_install(anjay_t *anjay, size_t num_instances);

/**
 * Add an instance of the Push Button Object installed in an Anjay
 * object.
 *
 * @param anjay            Anjay object with the Push Button Object installed.
 * @param iid              IID of the added instance.
 * @param application_type "Application type" string for the button instance,
 *                         is copied during the instance initialization, should
 *                         not be longer than 40 characters.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_button_instance_add(anjay_t *anjay,
                                   anjay_iid_t iid,
                                   const char *application_type);

/**
 * Remove an instance of the Push Button Object installed in an Anjay
 * object.
 *
 * @param anjay Anjay object with the Push Button Object installed.
 * @param iid   IID of the removed instance.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_button_instance_remove(anjay_t *anjay, anjay_iid_t iid);

/**
 * Updates Push Button Object installed in an Anjay object.
 *
 * @param anjay   Anjay object with the Push Button Object installed.
 * @param iid     IID of the removed instance.
 * @param pressed New state of the button (true if pressed).
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_button_update(anjay_t *anjay, anjay_iid_t iid, bool pressed);

#ifdef __cplusplus
}
#endif

#endif // ANJAY_IPSO_OBJECTS_H
