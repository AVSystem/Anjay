..
   Copyright 2017-2018 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Installing mandatory Objects
============================

In order to be able to connect to some LwM2M Server and handle incoming
packets our client has to have at least LwM2M Security (``/0``) and LwM2M
Server (``/1``) Objects implemented.

Fortunately, Anjay provides both of these Objects in form of preimplemented
modules, and they can be used easily.

.. note::
    That doesn't impact on Anjay flexibility -- i.e. users can still
    provide their own implementation of these Objects if necessary.

When Anjay is first instantiated (as in our previous :ref:`hello world
<anjay-hello-world>` example) it has no knowledge about the Data Model,
i.e. no LwM2M Objects are registered within it. Security and Server objects can
be registered using installation mechanism, presented in the next subsection.

.. _registering-objects:

Installing Objects
^^^^^^^^^^^^^^^^^^

.. highlight:: c

Each LwM2M Object is defined by an instance of the ``anjay_dm_object_def_t``
structure. To add support for a new Object, you'd need to fill that structure
and implement appropriate callback functions, linking it to actual
representation and register created objects within Anjay. However, for now, we
are going to install our preimplemented LwM2M Objects (Security, Server), so
that you don't have to worry about initializing the structure and object
registration on your own. In case you are interested in this topic at that
moment, you may jump to :doc:`BT5_CustomObject` to get more information.

To install objects we are going to use ``anjay_security_object_install()`` and
``anjay_server_object_install()`` functions:

.. snippet-source:: modules/security/include_public/anjay/security.h

    int anjay_security_object_install(anjay_t *anjay);

.. snippet-source:: modules/server/include_public/anjay/server.h

    int anjay_server_object_install(anjay_t *anjay);

Installing Server and Security Objects
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

We are going to modify the code from the :ref:`previous tutorial <anjay-hello-world>`.

.. snippet-source:: examples/tutorial/BT2/src/main.c

    #include <avsystem/commons/log.h>
    #include <anjay/anjay.h>
    #include <anjay/security.h>
    #include <anjay/server.h>

    int main(int argc, char *argv[]) {
        static const anjay_configuration_t CONFIG = {
            .endpoint_name = "urn:dev:os:anjay-tutorial",
            .in_buffer_size = 4000,
            .out_buffer_size = 4000
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }
        int result = 0;

        // Install necessary objects
        if (anjay_security_object_install(anjay)
                || anjay_server_object_install(anjay)) {
            result = -1;
            goto cleanup;
        }

        // ...

        // Event loop will go here

    cleanup:
        anjay_delete(anjay);
        return result;
    }

.. note::

    ``anjay_delete()`` will automatically delete installed modules after
    destruction of Anjay instance.

Adding necessary Security and Server entries
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

OK, we are ready to tell Anjay what is the LwM2M Server address we would like
to connect to. In order to do this, we will create two structure instances
(``anjay_server_instance_t``, ``anjay_security_instance_t``) and fill them
accordingly. After that they are going to be added as respective Object
Instances:

.. snippet-source:: examples/tutorial/BT2/src/main.c

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://127.0.0.1:5683",
        .security_mode = ANJAY_UDP_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = ANJAY_BINDING_U
    };

    anjay_iid_t security_instance_id = ANJAY_IID_INVALID;
    anjay_iid_t server_instance_id = ANJAY_IID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance,
                                       &security_instance_id);
    anjay_server_object_add_instance(anjay, &server_instance,
                                     &server_instance_id);

Great, so far it was really easy. But our client is currently unable to
connect to the specified Server.  It is because we have not implemented
an :doc:`event loop <BT3>` yet. It may be a little bit more complicated, as
you'll see in the next chapter, but the example event loop will be provided,
so that one can just copypaste it and run the Client finally.

Enabling LwM2M Discover on Objects
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::
    LwM2M Specification requires support of LwM2M Discover, therefore we do
    **recommend** to read this section and enable discovery support for all
    objects by default.

When a LwM2M Client registers to an LwM2M Server, it is often desired by the
LwM2M Server to get an overview of the entire Data Model. While :ref:`LwM2M
Register <lwm2m-registration-interface>` brings some information about Data
Model tree (by listing registered Objects and their Instances in the payload),
it does not contain any information about :ref:`Resources <lwm2m-resources>`.

Server-side, the resource discovery could be implemented as :ref:`LwM2M
Read <lwm2m-read>`, or :ref:`LwM2M Discover <lwm2m-discover>`, executed for
each Object.

The former could be suboptimal though, if resources happen to be
long. Therefore allowing the Server to use the LwM2M Discover could be
beneficial in terms of performance as well as bandwidth consumption.

Anjay supports the LwM2M Discover operation, but it must be ensured
that :ref:`Attributes <lwm2m-attributes>` for a given Data Model entity
could be read. However, as you may have noticed: we did not implement any
attribute-related handlers in this tutorial, and thus an attempt to perform
an LwM2M Discover will result in `Method Not Allowed` response.

Lucky for us, this could be fixed quickly as Anjay comes with :doc:`Attribute
Storage <../AdvancedTutorial/AT1>` module, that contains pre-implemented
attribute handlers. Installing it is enough to enable LwM2M Discover.

.. note::
    Of course, one can implement all attribute-related handles on their own
    (if needed), though we believe Attribute Storage should be sufficient
    for most use cases.
