/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */
#ifndef ANJAY_IPSO_OBJECTS_V2_H
#define ANJAY_IPSO_OBJECTS_V2_H

#include <anjay/dm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * IPSO basic sensor object instance metadata.
 */
typedef struct anjay_ipso_v2_basic_sensor_meta_struct {
    /**
     * Unit of the measured values.
     *
     * This value is optional; "Sensor Unit" resource will not be created if
     * this value is set to <c>NULL</c>.
     *
     * The pointed string won't be copied, so user code must assure that the
     * pointer will remain valid for the lifetime of the object.
     */
    const char *unit;

    /**
     * Set to <c>true</c> to enable "Min Measured Value", "Max Measured Value",
     * and "Reset Min and Max Measured Values" resources.
     */
    bool min_max_measured_value_present;

    /**
     * The minimum value that can be measured by the sensor.
     *
     * This value is optional; "Min Range Value" resource will not be created if
     * this value is set to NaN.
     */
    double min_range_value;

    /**
     * The maximum value that can be measured by the sensor.
     *
     * This value is optional; "Min Range Value" resource will not be created if
     * this value is set to NaN.
     */
    double max_range_value;
} anjay_ipso_v2_basic_sensor_meta_t;

/**
 * IPSO three-axis sensor object instance metadata.
 */
typedef struct anjay_ipso_v2_3d_sensor_meta_struct {
    /**
     * Unit of the measured values.
     *
     * This value is optional; "Sensor Unit" resource will not be created if
     * this value is set to <c>NULL</c>.
     *
     * The pointed string won't be copied, so user code must assure that the
     * pointer will remain valid for the lifetime of the object.
     */
    const char *unit;

    /**
     * Set to <c>true</c> to enable "Y Value" resource.
     */
    bool y_axis_present;

    /**
     * Set to <c>true</c> to enable "Z Value" resource.
     */
    bool z_axis_present;

    /**
     * Set to <c>true</c> to enable:
     * - "Min X Value", "Max X Value",
     * - "Min Y Value", "Max Y Value" (if <c>y_axis_present</c>),
     * - "Min Z Value", "Max Z Value" (if <c>z_axis_present</c>),
     * - "Reset Min and Max Measured Values"
     *
     * resources.
     */
    bool min_max_measured_value_present;

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
} anjay_ipso_v2_3d_sensor_meta_t;

/**
 * Value of IPSO three-axis sensor object.
 */
typedef struct anjay_ipso_v2_3d_sensor_value_struct {
    /**
     * Value of X axis. Must always be set.
     */
    double x;

    /**
     * Value of Y axis. Must be set only if Y axis is present.
     */
    double y;

    /**
     * Value of Z axis. Must be set only if Z axis is present.
     */
    double z;
} anjay_ipso_v2_3d_sensor_value_t;

/**
 * Installs a basic IPSO object.
 *
 * @param anjay          Anjay object for which the object is installed.
 * @param oid            Object ID of installed object.
 * @param version        Object version. This value is optional; version will
 *                       not be reported if this value is set to <c>NULL</c>.
 *                       The pointed string is not copied, so user code must
 *                       assure that the pointer will remain valid for the
 *                       lifetime of the object.
 * @param instance_count Maximum count of instances of installed object.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_basic_sensor_install(anjay_t *anjay,
                                       anjay_oid_t oid,
                                       const char *version,
                                       size_t instance_count);

/**
 * Adds an instance of basic IPSO object. Requires the object to be installed
 * first with @ref anjay_ipso_v2_basic_sensor_install .
 *
 * @param anjay         Anjay object for which the instance is added.
 * @param oid           Object ID of added object instance.
 * @param iid           Instance ID of added object instance. Must be lower
 *                      than number of <c>instance_count</c> parameter passed to
 *                      @ref anjay_ipso_v2_basic_sensor_install
 * @param initial_value Initial sensor value.
 * @param meta          Metadata about added object instance.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_basic_sensor_instance_add(
        anjay_t *anjay,
        anjay_oid_t oid,
        anjay_iid_t iid,
        double initial_value,
        const anjay_ipso_v2_basic_sensor_meta_t *meta);

/**
 * Updates sensor value of basic IPSO object, and also minimum and maximum
 * measured values.
 *
 * This method should be called frequently if user needs LwM2M observations to
 * behave responsively.
 *
 * CAUTION: Do not call this method from interrupts.
 *
 * @param anjay Anjay object with an installed basic IPSO object.
 * @param oid   Object ID of object instance of which the sensor value is
 *              updated.
 * @param iid   Instance ID of object instance of which the sensor value is
 *              updated.
 * @param value New sensor value.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_basic_sensor_value_update(anjay_t *anjay,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid,
                                            double value);

/**
 * Removes an instance of basic IPSO object.
 *
 * @param anjay Anjay object with an installed basic IPSO object.
 * @param oid   Object ID of object instance to remove.
 * @param iid   Instance ID of object instance to remove.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_basic_sensor_instance_remove(anjay_t *anjay,
                                               anjay_oid_t oid,
                                               anjay_iid_t iid);

/**
 * Installs a three-axis IPSO object.
 *
 * @param anjay          Anjay object for which the object is installed.
 * @param oid            Object ID of installed object.
 * @param version        Object version. This value is optional; version will
 *                       not be reported if this value is set to <c>NULL</c>.
 *                       The pointed string is not copied, so user code must
 *                       assure that the pointer will remain valid for the
 *                       lifetime of the object.
 * @param instance_count Maximum count of instances of installed object.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_3d_sensor_install(anjay_t *anjay,
                                    anjay_oid_t oid,
                                    const char *version,
                                    size_t instance_count);

/**
 * Adds an instance of three-axis IPSO object. Requires the object to be
 * installed first with @ref anjay_ipso_v2_3d_sensor_install .
 *
 * @param anjay         Anjay object for which the instance is added.
 * @param oid           Object ID of added object instance.
 * @param iid           Instance ID of added object instance. Must be lower
 *                      than number of <c>instance_count</c> parameter passed to
 *                      @ref anjay_ipso_v2_3d_sensor_install
 * @param initial_value Initial sensor value.
 * @param meta          Metadata about added object instance.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_3d_sensor_instance_add(
        anjay_t *anjay,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_ipso_v2_3d_sensor_value_t *initial_value,
        const anjay_ipso_v2_3d_sensor_meta_t *meta);

/**
 * Updates sensor value of three-axis IPSO object.
 *
 * This method should be called frequently if user needs LwM2M observations to
 * behave responsively.
 *
 * CAUTION: Do not call this method from interrupts.
 *
 * @param anjay Anjay object with an installed three-axis IPSO object.
 * @param oid   Object ID of object instance of which the sensor value is
 *              updated.
 * @param iid   Instance ID of object instance of which the sensor value is
 *              updated.
 * @param value New sensor value.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_3d_sensor_value_update(
        anjay_t *anjay,
        anjay_oid_t oid,
        anjay_iid_t iid,
        const anjay_ipso_v2_3d_sensor_value_t *value);

/**
 * Removes an instance of three-axis IPSO object.
 *
 * @param anjay Anjay object with an installed three-axis IPSO object.
 * @param oid   Object ID of object instance to remove.
 * @param iid   Instance ID of object instance to remove.
 *
 * @returns 0 on success, or a negative value in case of error.
 */
int anjay_ipso_v2_3d_sensor_instance_remove(anjay_t *anjay,
                                            anjay_oid_t oid,
                                            anjay_iid_t iid);

#ifdef __cplusplus
}
#endif

#endif // ANJAY_IPSO_OBJECTS_V2_H
