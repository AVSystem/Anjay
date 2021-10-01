..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. highlight:: c

Anjay IPSO Objects implementation
=================================

Among many Objects defined in
`OMA LightweightM2M (LwM2M) Object and Resource Registry
<https://technical.openmobilealliance.org/OMNA/LwM2M/LwM2MRegistry.html>`_
there are some which are used quite frequently.
Anjay provides an easy to use API for implementing some of them, i.e. for a few
kinds of IPSO objects:

 * basic sensors (like Temperature or Humidity Objects),
 * three axis sensors (like Accelerometer or Gyrometer Objects) and
 * Push Button Object.

The API is declared in `anjay/ipso_objects.h <../api/ipso__objects_8h.html>`_ header.

Object installation
-------------------

To install an Anjay IPSO Object we can use one of the following functions, each
of them corresponding to a specific class of objects:

 * `anjay_ipso_basic_sensor_install <../api/ipso__objects_8h.html#a8a95f45e84db077652f65d272ccbf730>`_,
 * `anjay_ipso_3d_sensor_install <../api/ipso__objects_8h.html#a9911f0f48d8cdebcbd8bfd9859f43358>`_,
 * `anjay_ipso_button_install <../api/ipso__objects_8h.html#a11e68bd571d70da7d17ee5c73cff6e0d>`_.

As you can see, it needs only the maximum number of
instances which will be used and, in case of sensor objects, OID of the
installed objects (in the case of the Push Button Object it is always the same
and equals 3347). 

For example, if we want to create a Temperature Object (which has OID equal to
3303) that could have up to 2 instances, we would call
``anjay_ipso_basic_sensor_install`` function like this:

.. code-block:: c

    anjay_ipso_basic_sensor_install(anjay, 3303, 2);

Instance addition
-----------------

To add an instance of an existing Anjay IPSO Object we can use one of the following functions,
each of them corresponding to a specific class of objects:

 * `anjay_ipso_basic_sensor_instance_add <../api/ipso__objects_8h.html#adc74272152c265197c86eff505bde54a>`_,
 * `anjay_ipso_3d_sensor_instance_add <../api/ipso__objects_8h.html#a822eca024f1b55d83ca6828b56b02bef>`_,
 * `anjay_ipso_button_instance_add <../api/ipso__objects_8h.html#ae981fe67ce9c2e9032284f26fa5fb3c3>`_.

As you can see, in case of basic or three-axis sensors we need to provide the
OID, IID and the implementation. Let's have a look at
``anjay_ipso_basic_sensor_impl_t`` structure:


.. snippet-source:: include_public/anjay/ipso_objects.h

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

The most important field of the structure is ``get_value`` which is a callback
called whenever the sensor value needs to be read (so either when it is
updated explicitly by the client or when the value is read by the server).
User might pass some additional context to the callback using the ``user_ctx``
field.

Let's assume that we want to add an instance of the installed Temperature
Object and let ``get_the_temperature(thermometer_t *)`` be a given system
function (which takes as an argument some thermometer instance of 
type ``thermometer_t *``) for reading the temperature. The object has allocated
space for two instances - so our IID should be 0 or 1, let it be 1.
Because ``get_the_temperature`` function need the thermometer argument we will
pass it using the ``user_ctx`` pointer:

.. code-block:: c

    int get_value(void *user_ctx, double *value) {
        thermometer_t *thermometer = (thermometer_t *) user_ctx;

        *value = get_the_temperature(thermometer);

        return 0;
    }

The proper temperature unit are degrees Celsius (as defined in 
`SenML RFC <https://datatracker.ietf.org/doc/html/rfc8428#section-12.1>`_).
Let assume that our thermometer measures temperatures between 0 and 100 degrees
Celsius. Knowing this we can prepare an instance of
``anjay_ipso_basic_sensor_impl_t`` and pass it to
``anjay_ipso_basic_sensor_add_instance`` function:

.. code-block:: c

    anjay_ipso_basic_sensor_instance_add(
        anjay, 3303, 1,
        (anjay_ipso_basic_sensor_impl_t) {
            .unit = "Cel",
            .user_ctx = (void *) thermometer,
            .min_range_value = (double) 0,
            .max_range_value = (double) 100,
            .get_value = get_value,
        }));

The implementation struct for the three axis objects is quite similar to this
for basic objects - there are three major differences:

 * there are additional ``use_y_value`` and ``use_z_value`` fields for enabling
   optional Y and Z axes,

 * callback needs to take three output pointers, one for each of the axes.

Let's have a look on the whole structure:

.. snippet-source:: include_public/anjay/ipso_objects.h

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

In case of the Push Button Object, neither implementation nor OID is required.
Instead, we need to provide the initial string for the "Application Type" field.

In both cases it is allowed to overwrite an existing instance of an object
(but in the case of the Push Button Object it can change only "Application Type"
field).

Instance update
---------------

To update an instance of an existing Anjay IPSO Object we can use one of the following functions,
each of them corresponding to a specific class of objects:

 * `anjay_ipso_basic_sensor_instance_update <../api/ipso__objects_8h.html#adb1d4d64c728ad7e77f35c8c28eb74bf>`_,
 * `anjay_ipso_3d_sensor_instance_update <../api/ipso__objects_8h.html#a254fafed91f3ef3613ae29de05a67449>`_,
 * `anjay_ipso_button_instance_update <../api/ipso__objects_8h.html#a84a9bf58b9cff7e1bd5fe9083576cfa2>`_.

In case of the sensor objects they just force an update of the sensor value
for the proper instance of the sensor object instance. To keep the value of
the sensor object current, it is usually a good practice to call it frequently.

In the case of the Push Button Object the update function is a bit more
significant - it is meant to be called every time the button is pressed or
released and it is the only way to update the state of the Object Instance.
In addition to IID it passes a new state of the button. Thus, when the button
corresponding to the instance with IID 7 is pressed we should call:

.. code-block:: c

    anjay_ipso_button_update(anjay, 7, true);

and when it is released:

.. code-block:: c

    anjay_ipso_button_update(anjay, 7, false);

.. note:

    It is not safe to call update functions for the IPSO objects (as all of the
    Anjay API functions) from an ISR context.

Instance removal
----------------

To remove an instance of an existing Anjay IPSO Object we can use one of the following functions,
each of them corresponding to a specific class of objects:

 * `anjay_ipso_basic_sensor_instance_remove <../api/ipso__objects_8h.html#a50e8c38ac2271e9d702d305349ea79c3>`_,
 * `anjay_ipso_3d_sensor_instance_remove <../api/ipso__objects_8h.html#a2bd255f62cf4817ea567b65ddae6644c>`_,
 * `anjay_ipso_button_instance_remove <../api/ipso__objects_8h.html#af53a1881ef4ed8de52cb000700a0dbb9>`_.

For example, to remove the created instance of the Temperature Object, the
following call will be proper:

.. code-block:: c

    anjay_ipso_basic_sensor_remove(anjay, 3303, 1);

Further reading
---------------

To learn more about Anjay IPSO Objects API you can look how they are used in
our demo `demo/objects/ipso_objects.c <../../../../../demo/objects/ipso_objects.c>`_
and our integrations: `Anjay Zephyr Client <https://github.com/AVSystem/Anjay-zephyr-client>`_
and `Anjay FreeRTOS Client <https://github.com/AVSystem/Anjay-freertos-client>`_.
