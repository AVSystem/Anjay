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

Bootstrap awareness
===================

In this tutorial you will learn:

- how to use ``resource_operations`` handler to prevent Anjay from calling
  some handlers in non-bootstrap context,
- how to setup Resources writable only by the LwM2M Bootstrap Server.


Handling LwM2M Bootstrap Server
-------------------------------

LwM2M Bootstrap Server is an unusual entity: it is allowed to modify Instances
and Resources not accessible to "regular" LwM2M Servers. That is useful for
setting up sensitive data that servers should not change (or even read) during
normal operation of the device - DTLS keys or certificates, for example.

Whenever an LwM2M Bootstrap Server sends a Bootstrap Write request, Anjay uses
the same ``instance_create`` and ``resource_write`` handlers as for "regular"
LwM2M Server operations. The only difference is that Anjay *does not call*
``resource_operations`` *handler* for bootstrap requests, allowing Bootstrap
Server to do anything it wants.

If a device has to implement a Resource that must only be writable by the LwM2M
Bootstrap Server, its handlers should be implemented like so:

- ``resource_write`` handler must be able to set the value of a Resource,
  as if it was a writable one,

- ``resource_operations`` handler should not set the
  ``ANJAY_DM_RESOURCE_OP_BIT_W`` in returned access flags. That will prevent
  Anjay from calling the ``resource_write`` handler for this particular Resource
  when the request is issued by a non-bootstrap server.


Example: bootstrap-writable Resource
------------------------------------

As an example, we will modify the Test Object from
:doc:`BT_CO4_FixedInstanceWritable` to prevent changing the value of ``Label``
Resource by non-bootstrap servers:

+-------------+-----------+-----------+
| Name        | Object ID | Instances |
+=============+===========+===========+
| Test object | 1234      | Multiple  |
+-------------+-----------+-----------+

Each Object Instance has two Resources:

+-------+-------------+------------+-----------+-----------+---------+
| Name  | Resource ID | Operations | Instances | Mandatory | Type    |
+=======+=============+============+===========+===========+=========+
| Label | 0           | Read       | Single    | Mandatory | String  |
+-------+-------------+------------+-----------+-----------+---------+
| Value | 1           | Read/Write | Single    | Mandatory | Integer |
+-------+-------------+------------+-----------+-----------+---------+


To achieve that, ``anjay_dm_resource_operations_t`` handler is required:

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/bootstrap-awareness/src/main.c

   static int test_resource_operations(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_rid_t rid,
                                       anjay_dm_resource_op_mask_t *out) {
       (void) anjay;
       (void) obj_ptr;
       *out = ANJAY_DM_RESOURCE_OP_NONE;

       switch (rid) {
       case 0:
           // only allow reading Resource 0 by LwM2M Servers
           // this will be ignored for LwM2M Bootstrap Server
           *out = ANJAY_DM_RESOURCE_OP_BIT_R;
           break;
       case 1:
           // Value Resource can be read/written by LwM2M Servers
           *out = ANJAY_DM_RESOURCE_OP_BIT_R | ANJAY_DM_RESOURCE_OP_BIT_W;
           break;
       default:
           break;
       }

       return 0;
   }

   // ...

   static const anjay_dm_object_def_t OBJECT_DEF = {
       // Object ID
       .oid = 1234,
       // ...
       .handlers = {
           // ... other handlers

           .resource_operations = test_resource_operations,
           // ... other handlers
       }
   };


That leaves one more issue with the example project: it has a pre-configured
LwM2M Server that is not a bootstrap one. Changing it to an LwM2M Bootstrap
Server is a matter of setting ``bootstrap_server = true`` on
``anjay_security_instance_t`` struct when initializing the Security Object:

.. snippet-source:: examples/tutorial/custom-object/bootstrap-awareness/src/main.c

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .bootstrap_server = true,
        .server_uri = "coap://127.0.0.1:5683",
        .security_mode = ANJAY_UDP_SECURITY_NOSEC
    };


It is worth noting that the LwM2M Bootstrap Server has only a Security Object
instance and no Server Object instances. For that reason, the example project
deliberately does not initialize any Server Object Instances.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/custom-object/bootstrap-awareness` subdirectory of main
    Anjay project repository.
