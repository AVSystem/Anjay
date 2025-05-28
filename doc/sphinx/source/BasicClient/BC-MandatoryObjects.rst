..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Installing mandatory Objects
============================

To connect to a LwM2M server and handle incoming packets, the client must support the following mandatory LwM2M Objects:
  
  - `LwM2M Security <https://www.openmobilealliance.org/tech/profiles/LWM2M_Security-v1_0.xml>`_ (``/0``)
  - `LwM2M Server <https://www.openmobilealliance.org/tech/profiles/LWM2M_Server-v1_0.xml>`_ (``/1``)

Anjay provides pre-implemented modules for all two objects, making setup
straightforward.

.. note::
    Users can still provide their own implementation of these Objects if needed
    — Anjay remains fully flexible.

When Anjay is first instantiated (as in our previous :ref:`hello world
<anjay-hello-world>` example), it has no knowledge about the Data Model, i.e.,
no LwM2M Objects are registered within it. You must explicitly install the
required Objects, as shown below.

Installing Objects
^^^^^^^^^^^^^^^^^^

Each LwM2M Object is defined by an instance of the ``anjay_dm_object_def_t``
structure. To add support for a new Object, you'd need to:

  - fill the ``anjay_dm_object_def_t`` structure,
  - implement appropriate callback functions,
  - register created object in Anjay.

However, for now, we are going to install our pre-implemented LwM2M Objects
(Security, Server), so that you don't have to worry about initializing the
structure and object registration on your own. In case you are interested in
this topic, :doc:`BC-ObjectImplementation` section provides more information on
this subject.

Use the following functions to install the Objects:

  - ``anjay_security_object_install()``
  - ``anjay_server_object_install()``

.. important::

    To use these function you must include the following headers:

      - ``anjay/security.h``
      - ``anjay/server.h``

Setting up Server and Security Objects
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This section shows how to implement and register the mandatory Objects:
Security and Server. It builds upon the setup from the
:ref:`previous tutorial <anjay-hello-world>`.

Security Object
---------------

The Security Object holds connection parameters for the LwM2M server. In this
example, we configure a non-secure connection to the Coiote IoT Device
Management platform. A secure connection setup will be described in a later
section.

To use Coiote:

  - Create an account at avsystem.com/coiote-iot-device-management-platform.
  - Add your device entry in the Coiote interface using the following URI for
    the connection: ``coap://eu.iot.avsystem.cloud:5683``

If you are using another server, replace the URI with your target address.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-MandatoryObjects/src/main.c

    // Installs Security Object and adds and instance of it.
    // An instance of Security Object provides information needed to connect to
    // LwM2M server.
    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        const anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "coap://eu.iot.avsystem.cloud:5683",
            .security_mode = ANJAY_SECURITY_NOSEC
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                               &security_instance_id)) {
            return -1;
        }

        return 0;
    }

Server Object
-------------

The Server Object defines registration parameters like lifetime and binding
mode.

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-MandatoryObjects/src/main.c

    // Installs Server Object and adds and instance of it.
    // An instance of Server Object provides the data related to a LwM2M Server.
    static int setup_server_object(anjay_t *anjay) {
        if (anjay_server_object_install(anjay)) {
            return -1;
        }

        const anjay_server_instance_t server_instance = {
            // Server Short ID
            .ssid = 1,
            // Client will send Update message often than every 60 seconds
            .lifetime = 60,
            // Disable Default Minimum Period resource
            .default_min_period = -1,
            // Disable Default Maximum Period resource
            .default_max_period = -1,
            // Disable Disable Timeout resource
            .disable_timeout = -1,
            // Sets preferred transport to UDP
            .binding = "U"
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
        if (anjay_server_object_add_instance(anjay, &server_instance,
                                             &server_instance_id)) {
            return -1;
        }

        return 0;
    }

Both Security and Server instances are linked together by the Short Server ID
Resource (``ssid``). That is why the ssid value must match between the Security
and Server instances.

Integrate Object Installation
-----------------------------

Once the installation functions are implemented, call them from your ``main()``
function:

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-MandatoryObjects/src/main.c
    :emphasize-lines: 21-24

    int main(int argc, char *argv[]) {
        if (argc != 2) {
            avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
            return -1;
        }

        const anjay_configuration_t CONFIG = {
            .endpoint_name = argv[1],
            .in_buffer_size = 4000,
            .out_buffer_size = 4000,
            .msg_cache_size = 4000
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        int result = 0;
        // Setup necessary objects
        if (setup_security_object(anjay) || setup_server_object(anjay)) {
            result = -1;
        }

        if (!result) {
            result = anjay_event_loop_run(
                    anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
        }

        anjay_delete(anjay);
        return result;
    }

.. note::

    ``anjay_delete()`` will automatically delete installed modules after
    destruction of Anjay instance.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/BC-MandatoryObjects` subdirectory of main Anjay project
    repository.

Logs example
~~~~~~~~~~~~

After running the client, you should see ``registration successful, location =
/rd/<server-dependent identifier>`` once and ``registration successfully
updated`` every 30 seconds in logs. It means, that the client has connected to
the server and successfully sends Update messages. You can now perform
operations like Read from the server side.

Application events
^^^^^^^^^^^^^^^^^^

The example code shown above covers events managed internally by the Anjay
library. However, most real-world applications also need to handle their own
logic. How to implement application-specific functionality will be explained
in the following sections.

Coiote experience
^^^^^^^^^^^^^^^^^

At this stage, you can log in to Coiote IoT Device Management and open the
**Device Center** for your registered device to explore the platform
functionality. Check the **Data Model tab** to see which LwM2M Objects are
currently exposed. You will notice that the Server object is visible, but the
Security object is not. This is expected behavior defined by the LwM2M
specification — the Security object is neither readable nor discoverable from
the device to protect sensitive configuration data.

