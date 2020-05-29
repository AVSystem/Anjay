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

Bootstrap awareness
===================

In this tutorial you will learn how to setup Resources writable only by the
LwM2M Bootstrap Server.


Handling LwM2M Bootstrap Server
-------------------------------

LwM2M Bootstrap Server is an unusual entity: it is allowed to modify Instances
and Resources not accessible to "regular" LwM2M Servers. That is useful for
setting up sensitive data that servers should not change (or even read) during
normal operation of the device - DTLS keys or certificates, for example.

Whenever a LwM2M Bootstrap Server sends a Bootstrap Write request, Anjay uses
the same ``instance_create`` and ``resource_write`` handlers as for "regular"
LwM2M Server operations. The only difference is that Anjay *ignores the
operations declared through the* ``kind`` *argument to* ``anjay_dm_emit_res()``
*calls inside the* ``list_resources`` *handler* for bootstrap requests, allowing
Bootstrap Server to do anything it wants.

If a device has to implement a Resource that must only be writable by the LwM2M
Bootstrap Server, its handlers should be implemented like so:

- ``resource_write`` handler must be able to set the value of a Resource,
  as if it was a writable one,

- ``list_resources`` handler should pass a ``kind`` argument that does not allow
  writing to ``anjay_dm_emit_res()``. That will prevent Anjay from calling the
  ``resource_write`` handler for this particular Resource when the request is
  issued by a non-bootstrap server.


Example: bootstrap-writable Resource
------------------------------------

As an example, we will modify the Test Object from
:doc:`AT_CO4_FixedInstanceWritable` to prevent changing the value of ``Label``
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


To achieve that, we need to update the ``test_list_resources`` handler so that
it disallows writing to Resource 0:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/bootstrap-awareness/src/main.c

    static int test_list_resources(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_dm_resource_list_ctx_t *ctx) {
        // ...

        // only allow reading Resource 0 by LwM2M Servers
        // this will be ignored for LwM2M Bootstrap Server
        anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
        // Value Resource can be read/written by LwM2M Servers
        anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
        return 0;
    }


That leaves one more issue with the example project: it has a pre-configured
LwM2M Server that is not a bootstrap one. Changing it to a LwM2M Bootstrap
Server is a matter of setting ``bootstrap_server = true`` on
``anjay_security_instance_t`` struct when initializing the Security Object:

.. snippet-source:: examples/tutorial/AT-CustomObjects/bootstrap-awareness/src/main.c

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .bootstrap_server = true,
        .server_uri = "coap://try-anjay.avsystem.com:5693",
        .security_mode = ANJAY_SECURITY_NOSEC
    };


It is worth noting that the LwM2M Bootstrap Server has only a Security Object
instance and no Server Object instances. For that reason, the example project
deliberately does not initialize any Server Object Instances.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/bootstrap-awareness` subdirectory of main
    Anjay project repository.
