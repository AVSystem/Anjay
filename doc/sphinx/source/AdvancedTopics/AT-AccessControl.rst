..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Access Control in multi-server environment
==========================================

LwM2M Client may be connected to more than one LwM2M Server. In such situation,
restricting Server access to some part of the :ref:`data model <data-model>`
may be required.

To resolve this problem the LwM2M Specification defines an Access Control
Object that allows setting specific access rights on data model Instances
either during a Bootstrap Phase or normal runtime by properly authorized
LwM2M Server.

In a multi-server environment every Object Instance (except Instances of
the Access Control Object) has associated Access Control Instance:

+----------------------+-------------+------------------------------------------------+
| Resource             | Resource ID | Meaning of the value                           |
+======================+=============+================================================+
| Object ID            | 0           | ID of the Object this instance targets         |
+----------------------+-------------+------------------------------------------------+
| Object Instance ID   | 1           | Object Instance ID this instance targets       |
|                      |             | (also see the note below)                      |
+----------------------+-------------+------------------------------------------------+
|                      |             | List of pairs (`Short Server ID`,              |
| ACL                  | 2           | `Access mask`) describing access rights        |
|                      |             | assigned to LwM2M Servers                      |
+----------------------+-------------+------------------------------------------------+
|                      |             | Short Server ID of the Server which owns       |
| Access Control Owner | 3           | this Access Control Instance (i.e. the         |
|                      |             | one that can modify access rights)             |
+----------------------+-------------+------------------------------------------------+

.. note::
    ``65535`` is a special `Object Instance ID` value reserved for use during
    the Bootstrap Phase and is valid only in combination with **Create**
    access flag.

ACL Resource
------------

ACL Resource of the Access Control Object Instance is populated with pairs
of form (`Short Server ID`, `Access mask`). Access mask is a combination of
the following access flags (combined by bitwise OR operator):

+-----------------------+--------------------------+-------------------------------+
| Access flag           | Allowed LwM2M Operations | Anjay representation          |
+=======================+==========================+===============================+
| ``R`` (00001 binary)  | Read, Observe            | ``ANJAY_ACCESS_MASK_READ``    |
+-----------------------+--------------------------+-------------------------------+
| ``W`` (00010 binary)  | Write                    | ``ANJAY_ACCESS_MASK_WRITE``   |
+-----------------------+--------------------------+-------------------------------+
| ``E`` (00100 binary)  | Execute                  | ``ANJAY_ACCESS_MASK_EXECUTE`` |
+-----------------------+--------------------------+-------------------------------+
| ``D`` (01000 binary)  | Delete                   | ``ANJAY_ACCESS_MASK_DELETE``  |
+-----------------------+--------------------------+-------------------------------+
| ``C`` (10000 binary)  | Create                   | ``ANJAY_ACCESS_MASK_CREATE``  |
+-----------------------+--------------------------+-------------------------------+

.. note::
    Discover operation is **always** allowed and LwM2M protocol does not provide
    a mechanism for forbidding it.

Note on data-model instances lifetime
-------------------------------------

Access Control Object also helps in managing Object Instance lifetime. Whenever
some Object Instance is orphaned (i.e. no LwM2M Server is an Access Control
Owner of the Access Control Instance associated with this Object Instance) it
MUST be removed by the Client. Anjay's pre-implemented Access Control Object
does this automatically.

.. note::
    Of course, if necessary, you may implement your own Access Control
    Object and use it instead. Nonetheless it is worth noting that it
    is an extremely complex Object to implement in a correct way.

Example usage
-------------

In this example, we are going to setup multiple-server
environment. We will assign LwM2M Server with SSID 1 the **Create**
permission on the :doc:`Test Object developed in another tutorial
<AT-CustomObjects/AT_CO5_MultiInstanceDynamic>`.

Additionally, we will allow both LwM2M Servers to read their respective LwM2M
Server Instances.

.. note::
    What we do here is roughly equivalent to **Factory Bootstrap** phase,
    which is just one of the bootstrapping options. Same thing could be
    achieved by a Bootstrap Server, which in conformance with the LwM2M
    Specification would instantiate Access Control Object, and assign access
    permissions on its own in a multiple-server environment.

.. highlight:: c

We start with installation of the Attribute Storage, Access Control, Security
Object and Server Object modules:

.. snippet-source:: examples/tutorial/AT-AccessControl/src/main.c

    int result;
    if (anjay_attr_storage_install(anjay) || anjay_access_control_install(anjay)
            || anjay_security_object_install(anjay)
            || anjay_server_object_install(anjay)) {
        result = -1;
    }

Then we setup two LwM2M Servers:

.. snippet-source:: examples/tutorial/AT-AccessControl/src/main.c

    // LwM2M Server account with SSID = 1
    const anjay_security_instance_t security_instance1 = {
        .ssid = 1,
        .server_uri = "coap://try-anjay.avsystem.com:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance1 = {
        .ssid = 1,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    // LwM2M Server account with SSID = 2
    const anjay_security_instance_t security_instance2 = {
        .ssid = 2,
        .server_uri = "coap://127.0.0.1:5683",
        .security_mode = ANJAY_SECURITY_NOSEC
    };

    const anjay_server_instance_t server_instance2 = {
        .ssid = 2,
        .lifetime = 86400,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U"
    };

    // Setup first LwM2M Server
    anjay_iid_t server_instance_iid1 = ANJAY_ID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance1,
                                       &(anjay_iid_t) { ANJAY_ID_INVALID });
    anjay_server_object_add_instance(anjay, &server_instance1,
                                     &server_instance_iid1);

    // Setup second LwM2M Server
    anjay_iid_t server_instance_iid2 = ANJAY_ID_INVALID;
    anjay_security_object_add_instance(anjay, &security_instance2,
                                       &(anjay_iid_t) { ANJAY_ID_INVALID });
    anjay_server_object_add_instance(anjay, &server_instance2,
                                     &server_instance_iid2);

And finally, we are ready to set access lists:

.. snippet-source:: examples/tutorial/AT-AccessControl/src/main.c

    // Set LwM2M Create permission rights for SSID = 1, this will make SSID=1
    // an exclusive owner of the Test Object
    anjay_access_control_set_acl(anjay, 1234, ANJAY_ID_INVALID, 1,
                                 ANJAY_ACCESS_MASK_CREATE);

    // Allow both LwM2M Servers to read their Server Instances
    anjay_access_control_set_acl(anjay, 1, server_instance_iid1,
                                 server_instance1.ssid, ANJAY_ACCESS_MASK_READ);
    anjay_access_control_set_acl(anjay, 1, server_instance_iid2,
                                 server_instance2.ssid, ANJAY_ACCESS_MASK_READ);

That way we have ensured an exclusive access of Server with SSID 1 to Test
Object (``/1234``) Instances.

Later on, this Server will be able to set some access rights for other Servers,
by writing to proper Access Control Instances (i.e. Instances this Server
is an owner of, which corresponds to instances it has created), but that's
outside of the scope of this tutorial. We recommend you to look at the LwM2M
Specification for more details on Access Control Object, as well as at our
`API docs <../api/index.html>`_.

.. note::

    Please notice ``cleanup`` tag at end of ``main()`` function. It is important
    to delete your own implemented objects after calling ``anjay_delete()``, as
    during the instance destruction Anjay may still try to refer to object's
    data and premature object deletion could be disastrous in effects.

    .. snippet-source:: examples/tutorial/AT-AccessControl/src/main.c

        cleanup:
            anjay_delete(anjay);
            delete_test_object(test_obj);
            return result;
