..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

LwM2M Gateway API
=================

.. contents:: :local:

LwM2M Gateway object
--------------------

Before using the LwM2M Gateway object, we need to inform Anjay of its existence.
Anjay includes a built-in implementation of the LwM2M Gateway object
`LwM2M Gateway <https://github.com/OpenMobileAlliance/lwm2m-registry/blob/prod/25.xml>`_
(``/25``), which can be easily utilized.

.. note::
   Anjay implements version 2.0 of Object ``/25`` containing all three
   mandatory resources: ``Device ID``, ``Prefix``, and ``IoT Device Object``.

Once the Anjay object is created, the Gateway object can be installed using
the ``anjay_lwm2m_gateway_install()`` function.

.. note::
   Complete code of this example can be found in
   ``examples/commercial-features/CF-LwM2M-Gateway`` subdirectory of the main
   Anjay project repository.

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/main.c
    :emphasize-lines: 12

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = 0;
    if (setup_security_object(anjay) || setup_server_object(anjay)) {
        result = -1;
    }

    if (!result && anjay_lwm2m_gateway_install(anjay)) {
        avs_log(tutorial, ERROR, "Failed to add /25 Gateway Object");
        result = -1;
    }

.. _lwm2m_gateway_register_device:

Managing End Devices
--------------------

Managing a new End Device involves two key steps:

1. **Establishing communication** between the Gateway and the End Device.
2. **Registering the device and its objects** in the LwM2M Gateway.

Communication with End Devices
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since different End Devices may have unique requirements, the communication
method varies. The example provided in the next section includes a Python script
``examples/commercial-features/CF-LwM2M-Gateway/end_device.py``. This script
communicates with the Anjay example code using **UNIX sockets**. However, it is
the user’s responsibility to implement the appropriate communication mechanism
for their specific End Devices.

.. note::
    The Python script can be started multiple times, and each instance of the
    script will automatically connect to the Gateway and simulate a
    single End Device. Stopping the execution of the script will dynamically
    disconnect the associated End Device from the LwM2M Gateway.

Registering an End Device
^^^^^^^^^^^^^^^^^^^^^^^^^

When a new End Device sends an **attach request**, the LwM2M Gateway must
register it by adding a new instance of the ``/25`` Gateway Object. This is
done using the ``anjay_lwm2m_gateway_register_device()`` function.

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/gateway_server.c
    :emphasize-lines: 8-9

    static int setup_end_device(gateway_srv_t *gateway_srv,
                                end_device_t *end_device,
                                const char *msg) {
        anjay_iid_t iid = ANJAY_ID_INVALID;
        anjay_t *anjay = gateway_srv->anjay;

        strcpy(end_device->end_device_name, msg);
        if (anjay_lwm2m_gateway_register_device(anjay, end_device->end_device_name,
                                                &iid)) {
            avs_log(tutorial, ERROR, "Failed to add End Device");
            return -1;
        }
        end_device->iid = iid;
        end_device->evaluation_period = DEFAULT_MAXIMAL_EVALUATION_PERIOD;

Instance ID (iid) management
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

You can specify an **Instance ID (iid)** manually or set it to
``ANJAY_ID_INVALID`` to let Anjay assign the first available ID automatically.  
Always check the return code of ``anjay_lwm2m_gateway_register_device()``,
especially when assigning ``iid`` manually. If a collision occurs, the function
will return a **negative value** without modifying the passed ``iid``.

.. important::
    The ``device_id`` parameter passed to ``anjay_lwm2m_gateway_register_device()``
    must remain **valid until the device is deregistered**. The value is not
    copied internally.

Registering LwM2M objects for an End Device
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Once an End Device is registered, its supported LwM2M objects must also be
registered. In the provided example, only one object: **Temperature Object**
(``/3303``) (`LwM2M Temperature Object Specification  
<https://github.com/OpenMobileAlliance/lwm2m-registry/blob/prod/3303.xml>`_) is
registered.

To register an object, use the ``anjay_lwm2m_gateway_register_object()``  
function. The object implementation should handle communication with the End  
Device, but otherwise follows the same pattern as ``anjay_register_object()``.

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/gateway_server.c
    :emphasize-lines: 9

    const anjay_dm_object_def_t **obj =
            temperature_object_create(iid, gateway_srv);
    if (!obj) {
        avs_log(tutorial, ERROR, "Failed to create Temperature Object");
        return -1;
    }
    end_device->temperature_object = obj;

    if (anjay_lwm2m_gateway_register_object(anjay, iid, obj)) {
        avs_log(tutorial, ERROR, "Failed to register Temperature Object");
        return -1;
    }

.. note::
    The Temperature Object implementation  
    (``examples/tutorial/LwM2M-Gateway/src/temperature_object.c``)
    interacts with the Python script simulating an End Device to perform actual
    read and write operations.

.. note::
    The Anjay LwM2M Gateway **automatically assigns Prefixes** (``/25/*/1``).  
    However, when using the LwM2M Gateway API, users should rely on the integer
    **End Device Instance ID** returned by ``anjay_lwm2m_gateway_register_device()``.  
    It is recommended to **store the Instance ID** in a structure representing
    the object registered on the End Device. This makes it easier to match
    the correct End Device when using shared object implementations.

Cleaning up
-----------

When a device disconnects, the user should perform two actions:

- Unregister all of the objects of a given End Device using  
  ``anjay_lwm2m_gateway_unregister_object()``
- Deregister the End Device with ``anjay_lwm2m_gateway_deregister_device()``

.. highlight:: c
.. snippet-source:: examples/tutorial/LwM2M-Gateway/src/gateway_server.c
    :emphasize-lines: 2-3,8

    if (end_device->temperature_object) {
        if (anjay_lwm2m_gateway_unregister_object(
                    anjay, end_device->iid, end_device->temperature_object)) {
            avs_log(tutorial, ERROR, "Failed to unregister Temperature Object");
        }
        temperature_object_release(end_device->temperature_object);
    }
    if (anjay_lwm2m_gateway_deregister_device(anjay, end_device->iid)) {
        avs_log(tutorial, ERROR, "Failed to deregister End Device");
    }
