..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

.. highlight:: c

IPSO objects implementation
=================================

.. contents:: :local:

Introduction
------------

IPSO (Internet Protocol for Smart Objects) objects are a collection of LwM2M
objects that can be used to expose some common features of many IoT devices,
like sensors, buttons, actuators or control switches. Using predefined objects
for these purposes enables higher interoperability of applications, i.e. all
devices that have a temperature sensor can report the readings in a standardized
way, making it possible to easily process such measurements from different,
nonhomogeneous devices on the cloud.

In practice, IPSO objects are most importantly a convenient way to report sensor
data over LwM2M. All IPSO objects of aforementioned certain kinds share
common set of resources, and thanks to that these objects in a large part can be
easily preimplemented.

Anjay provides a ready-to-use implementation of:

- basic (i.e. scalar) sensor objects (e.g. Temperature Object or Pressure
  Object),
- three-axis sensor objects (e.g. Accelerometer Object or Magnetometer Object),
- Push Button Object.

User's only responsibility is to retrieve those values from actual sensors
and supply them to Anjay, making it very easy to implement LwM2M devices with
sensor support.

The API is declared in ``include_public/anjay/ipso_objects.h`` and
``include_public/anjay/ipso_objects_v2.h`` headers. To use them, enable them
first by either defining ``ANJAY_WITH_MODULE_IPSO_OBJECTS`` and/or
``ANJAY_WITH_MODULE_IPSO_OBJECTS_V2`` in the Anjay's configuration files, or, if
using CMake, enabling ``WITH_MODULE_ipso_objects`` and/or
``WITH_MODULE_ipso_objects_v2`` options.

.. important::

    The APIs for basic and 3D IPSO sensors objects explained in this tutorial
    are new, experimental variants declared in
    ``include_public/anjay/ipso_objects_v2.h``.

Supported objects
-----------------

Implementation of IPSO objects in Anjay supports objects that have the following
set of resources:

.. flat-table::
   :header-rows: 2

   * - :cspan:`3` **Basic (scalar) sensor objects**
   * - **Resource ID**
     - **Resource Name**
     - **Must be supported by object**
   * - 5601
     - Min Measured Value
     - no
   * - 5602
     - Max Measured Value
     - no
   * - 5603
     - Min Range Value
     - no
   * - 5604
     - Max Range Value
     - no
   * - 5605
     - Reset Min and Max Measured Values
     - no
   * - 5700
     - Sensor Value
     - **yes**
   * - 5701
     - Sensor Units
     - no

.. flat-table::
   :header-rows: 2

   * - :cspan:`3` **Three-axis sensor objects**
   * - **Resource ID**
     - **Resource Name**
     - **Must be supported by object**
   * - 5508
     - Min X Value
     - no
   * - 5509
     - Max X Value
     - no
   * - 5510
     - Min Y Value
     - no
   * - 5511
     - Max Y Value
     - no
   * - 5512
     - Min Z Value
     - no
   * - 5513
     - Max Z Value
     - no
   * - 5603
     - Min Range Value
     - no
   * - 5604
     - Max Range Value
     - no
   * - 5605
     - Reset Min and Max Measured Values
     - no
   * - 5701
     - Sensor Units
     - no
   * - 5702
     - X Value
     - **yes**
   * - 5703
     - Y Value
     - no
   * - 5704
     - Z Value
     - no

As of December 13th, 2023, objects registered by IPSO Alliance that meet these
requirements are:
3300 (Generic Sensor),
3301 (Illuminance),
3303 (Temperature),
3304 (Humidity),
3313 (Accelerometer),
3314 (Magnetometer),
3315 (Barometer),
3316 (Voltage),
3317 (Current),
3318 (Frequency),
3319 (Depth),
3320 (Percentage),
3321 (Altitude),
3322 (Load),
3323 (Pressure),
3324 (Loudness),
3325 (Concentration),
3326 (Acidity),
3327 (Conductivity),
3328 (Power),
3329 (Power Factor),
3330 (Distance),
3334 (Gyrometer),
3345 (Multiple Axis Joystick),
3346 (Rate).

Additionally, object 3347 (Push Button) is supported with a separate API.

Usage example
-------------

This tutorial builds up on the :doc:`../../BasicClient/BC-MandatoryObjects`
tutorial which contains an implementation of a minimal, but complete LwM2M
client.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-IpsoObjects` subdirectory of main Anjay project
    repository.

In this example we'll implement a simple application that simulates a few
thermometers, accelerometers and buttons.

Installing objects and instances
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To setup an IPSO object, you must install it first using one of the following
methods:

 * `anjay_ipso_v2_basic_sensor_install <../api/ipso__objects__v2_8h.html#ac3200c3c61ea62f76eb4e606adfcd90f>`_,
 * `anjay_ipso_v2_3d_sensor_install <../api/ipso__objects__v2_8h.html#a154a62e2adafe9890cbd66c91bb8f20a>`_,
 * `anjay_ipso_button_install <../api/ipso__objects_8h.html#a11e68bd571d70da7d17ee5c73cff6e0d>`_.

For sensors, the API accepts Object ID, object version and maximum number of
instances that'll be installed later. For button, the Object ID and version is
defined upfront.

.. important::

    It's important to set appropriate object version number. Without configuring
    it a LwM2M server may fail to interpret resources that were added in newer
    versions of an object. Such an example is Gyrometer Object, which has the
    "Reset Min and Max Measured Values" resource available only since version
    1.1.

    In this example all enabled resources are available in version 1.0 of these
    objects, to which passing ``NULL`` defaults to.

After installing objects, instances of these objects can be added using
following APIs:

 * `anjay_ipso_v2_basic_sensor_instance_add <../api/ipso__objects__v2_8h.html#ae92a38b4eba14909b00233088e6256b5>`_,
 * `anjay_ipso_v2_3d_sensor_instance_add <../api/ipso__objects__v2_8h.html#a760f33f44690447409e77066b4c86295>`_,
 * `anjay_ipso_button_instance_add <../api/ipso__objects_8h.html#ae981fe67ce9c2e9032284f26fa5fb3c3>`_.

For basic and 3D sensors, these methods accept an initial value of the sensor
and a structure that provides metadata about each instance:
`anjay_ipso_v2_basic_sensor_meta_t <../api/ipso__objects__v2_8h.html#a2e0cd9b35002025a91edb96842cd29cf>`_
and
`anjay_ipso_v2_3d_sensor_meta_t <../api/ipso__objects__v2_8h.html#a34fe615fc03fa7313a2dffabd326058f>`_,
respectively.

These structs are used to configure unit, reported minimum and maximum values
that can be measured by a sensor, and presence of optional Y and Z axis in case
of 3D objects.

In our example, let's define some macros and necessary metadata structs first:

.. snippet-source:: examples/tutorial/AT-IpsoObjects/src/main.c

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

.. note::

    It's a good practice to report values using units defined in
    `SenML Units Registry <https://www.rfc-editor.org/rfc/rfc8428.html#section-12.1>`_,
    the up to date list can be
    `found here <https://www.iana.org/assignments/senml/senml.xhtml>`_.

Then, let's introduce some helper methods that will install our sensor objects
and add all instances upfront:

.. snippet-source:: examples/tutorial/AT-IpsoObjects/src/main.c

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

Finally, let's call these methods in initialization code, in ``main()`` method:

.. snippet-source:: examples/tutorial/AT-IpsoObjects/src/main.c
    :emphasize-lines: 5-7

    int main(int argc, char *argv[]) {
        // ...

        if (setup_security_object(anjay) || setup_server_object(anjay)
                || setup_temperature_object(anjay)
                || setup_accelerometer_object(anjay)
                || setup_button_object(anjay)) {
            result = -1;
        }

        // ...
    }

Updating values
^^^^^^^^^^^^^^^

To update reported value of a sensor, use one of following methods:

 * `anjay_ipso_v2_basic_sensor_value_update <../api/ipso__objects__v2_8h.html#ab9ee3d855e885a2dc25ae73f466dd228>`_,
 * `anjay_ipso_v2_3d_sensor_value_update <../api/ipso__objects__v2_8h.html#a2166bd5daae8fb235f96064d8b97c740>`_,
 * `anjay_ipso_button_update <../api/ipso__objects_8h.html#a84a9bf58b9cff7e1bd5fe9083576cfa2>`_.

.. important::

    Keep in mind that a LwM2M Server is allowed to configure resource
    observations with attributes that require the client to report the data very
    frequently or when some threshold value is exceeded, even for a very short
    moment. If you want to ensure that server is notified of every change of
    resource value that could meet such conditions, **you must update the value
    very frequently**.

.. important::

    These methods (as all methods in Anjay's public API) cannot be called from
    an interrupt. In case ``ANJAY_WITH_THREAD_SAFETY`` is disabled Anjay APIs
    are not safe to call from other contexts than method which runs event loop
    and ``avs_sched`` tasks, while if ``ANJAY_WITH_THREAD_SAFETY`` is enabled
    calling such methods will attempt to lock a mutex from an interrupt which
    also is wrong.

    If your application retrieves new sensor values and/or button state changes
    in an interrupt, you must find a way to pass these values to a non-interrupt
    execution context.

In our example we're simulating values of these sensors, so let's add some
utility methods first:

.. snippet-source:: examples/tutorial/AT-IpsoObjects/src/main.c

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

Then, let's implement a scheduler task that will update all sensors. The task
schedules itself to run every second:

.. snippet-source:: examples/tutorial/AT-IpsoObjects/src/main.c

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

Lastly, let's call this method once before entering event loop. From that moment
the task will keep running infinitely.

.. snippet-source:: examples/tutorial/AT-IpsoObjects/src/main.c
    :emphasize-lines: 5

    int main(int argc, char *argv[]) {
        // ...

        if (!result) {
            update_sensor_values(anjay_get_scheduler(anjay), &anjay);
            result = anjay_event_loop_run(
                    anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
        }

        // ...
    }

Removing instances
^^^^^^^^^^^^^^^^^^

In case you need to change the set of instances of installed IPSO objects, those
instances can be removed using following methods:

 * `anjay_ipso_v2_basic_sensor_instance_remove <../api/ipso__objects__v2_8h.html#af53a1881ef4ed8de52cb000700a0dbb9>`_,
 * `anjay_ipso_v2_3d_sensor_instance_remove <../api/ipso__objects__v2_8h.html#a2bd255f62cf4817ea567b65ddae6644c>`_,
 * `anjay_ipso_button_instance_remove <../api/ipso__objects_8h.html#af53a1881ef4ed8de52cb000700a0dbb9>`_.

In our example instance set doesn't change. All objects and instances are
automatically deleted when ``anjay_delete()`` is called.
