..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Bootstrap
=========

The LwM2M Protocol Specification defines the Bootstrap Interface, whose primary
role is to provision LwM2M-enabled devices with the necessary configuration and
credentials required to establish a connection with the LwM2M Server.

The most common use case of this interface, and the one covered in this example,
involves delivering the LwM2M Server Object Instance together with appropriate
security credentials. However, the bootstrap process is far more versatile.

**LwM2M Bootstrap Server**

A LwM2M Bootstrap Server is a special entity in the LwM2M architecture, as it is
allowed to modify object instances and resources that are otherwise inaccessible
to regular LwM2M Servers, ignoring the Read-Only property.

Security Object instance that is related to the connection with LwM2M Bootstrap
Server (has ``Bootstrap-Server`` Resource set to true, as well as URI and security
credentials for LwM2M Bootstrap Server) is often called a
**LwM2M Bootstrap-Server Account**. LwM2M Bootstrap Server connection requires
only ``/0`` Security Object instance, without a corresponding ``/1`` Server
Object instance (with matching SSID).

**Key Operations**

- ``Bootstrap-Delete /0``: Removes all Security Object instances except the one related to the Bootstrap Server.

- ``Bootstrap-Discover``: Identifies the Security Object instance ID for the Bootstrap Server.

- ``Bootstrap-Write``: Updates server URI or credentials.

Bootstrap Interface support is enabled with ``ANJAY_WITH_BOOTSTRAP`` configuration
flag or, if using CMake, with ``WITH_BOOTSTRAP`` option.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-Bootstrap` subdirectory of main Anjay project
    repository.
    Comparing it to `examples/tutorial/BC-MandatoryObjects` can give a good
    insight on the difference between how LwM2M Bootstrap server is handled.


Add a Bootstrap Account in Anjay
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The Security Object holds connection parameters for the LwM2M server. In this
example, we configure a non-secure connection to the Coiote IoT Device
Management platform. `anjay_security_instance_t.bootstrap_server <https://docs.avsystem.com/hubfs/Anjay_Docs/api/api_generated/structanjay__security__instance__t.html#_CPPv4N25anjay_security_instance_t16bootstrap_serverE>`_
flag needs to be set to `true`. Also, LwM2M Bootstrap Server has a different IP
port than a regular LwM2M Server.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-Bootstrap/src/main.c
    :emphasize-lines: 11-12

    // Installs Security Object and adds an instance of it.
    // An instance of Security Object provides information needed to connect to
    // LwM2M Bootstrap server.
    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        const anjay_security_instance_t security_instance = {
            .ssid = 1,
            .bootstrap_server = true,
            .server_uri = "coap://eu.iot.avsystem.cloud:5693",
            .security_mode = ANJAY_SECURITY_NOSEC,
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                            &security_instance_id)) {
            return -1;
        }

        return 0;
    }

The LwM2M Bootstrap Server doesn't have a /1 Server Object instance. However,
you must still install the Server Object in Anjay data model to allow the
Bootstrap Server to create the Server Object dynamically.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-Bootstrap/src/main.c

    // Installs Server Object but does not add any instances of it. This is
    // necessary to allow LwM2M Bootstrap Server to create Server Object instances.
    static int setup_server_object(anjay_t *anjay) {
        if (anjay_server_object_install(anjay)) {
            return -1;
        }

        return 0;
    }


Configure Bootstrap
^^^^^^^^^^^^^^^^^^^

Anjay will automatically try to connect to the LwM2M Bootstrap Server if it
does not have a LwM2M Server configured in the data model, or if the connection
to the LwM2M Server has failed.

The Bootstrap Procedure is considered failed if a LwM2M Client does not receive
the "Bootstrap-Finish" operation after the last received Bootstrap-Server command
in a certain period. The LwM2M Specification suggest setting it to the
value of CoAP Parameter ``EXCHANGE_LIFETIME`` and it is calculated based on 
`anjay_configuration_t::udp_tx_params <https://docs.avsystem.com/hubfs/Anjay_Docs/api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration13udp_tx_paramsE>`_ or `anjay_configuration_t::coap_tcp_request_timeout <https://docs.avsystem.com/hubfs/Anjay_Docs/api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration24coap_tcp_request_timeoutE>`_.

The default values are as follows:
 - 247 seconds for UDP
 - 215.5 seconds for TCP

The following Bootstrap-related Resources are also implemented in the Anjay's
build-in Security Object:

- `anjay_security_instance_t::client_holdoff_s <https://docs.avsystem.com/hubfs/Anjay_Docs/api/api_generated/structanjay__security__instance__t.html#_CPPv4N25anjay_security_instance_t16client_holdoff_sE>`_ - the time that Anjay waits
  before performing a Client Initiated Bootstrap once it determines that it
  should initiate this bootstrap mode.

- `anjay_security_instance_t::bootstrap_timeout_s <https://docs.avsystem.com/hubfs/Anjay_Docs/api/api_generated/structanjay__security__instance__t.html#_CPPv4N25anjay_security_instance_t19bootstrap_timeout_sE>`_ - if set, Anjay will automatically
  purge the LwM2M Bootstrap-Server Account after this timeout value if a Bootstrap
  procedure ends successfully. By default, the Bootstrap-Server Account lifetime
  is infinite.

There is also a legacy Server-Initiated Bootstrap mechanism based on an
interpretation of LwM2M 1.0 TS. To learn more, see
`anjay_configuration_t::disable_legacy_server_initiated_bootstrap <https://docs.avsystem.com/hubfs/Anjay_Docs/api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration41disable_legacy_server_initiated_bootstrapE>`_.

Coiote LwM2M Server
^^^^^^^^^^^^^^^^^^^

To Bootstrap your device using AVSystem Coiote LwM2M Server, refer to
`Add device via the Bootstrap server guide <https://eu.iot.avsystem.cloud/doc/user/getting-started/add-devices/#add-device-via-the-bootstrap-server>`_ 
in the Coiote documentation.
