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

Single-instance read-only object
================================

.. include:: Anjay_codegen_note.rst

This is the simplest possible case. Let us implement following custom Object:

+-------------+-----------+-----------+
| Name        | Object ID | Instances |
+=============+===========+===========+
| Test object | 1234      | Single    |
+-------------+-----------+-----------+

With two simple Resources:

+-------------+-------------+------------+-----------+-----------+---------+
| Name        | Resource ID | Operations | Instances | Mandatory | Type    |
+=============+=============+============+===========+===========+=========+
| Object name | 0           | Read       | Single    | Mandatory | String  |
+-------------+-------------+------------+-----------+-----------+---------+
| Timestamp   | 1           | Read       | Single    | Mandatory | Integer |
+-------------+-------------+------------+-----------+-----------+---------+

In this case, the most interesting handler type is ``anjay_dm_resource_read_t``,
called whenever the library needs to get a value of a Resource. This might
happen if:

- a LwM2M Server sends a Read request,

- the Resource is being observed and the library needs to send a Notify message,

- value of the Resource is required for the library to function correctly
  (mostly related to Objects 0 (Security), 1 (Server) and 2 (Access Control)).

The Read handler for our test object might be implemented as follows:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only/src/main.c

    static int test_resource_read(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_output_ctx_t *ctx) {
        // These arguments may seem superfluous now, but they will come in handy
        // while defining more complex objects
        (void) anjay;   // unused
        (void) obj_ptr; // unused: the object holds no state
        (void) iid;     // unused: will always be 0 for single-instance Objects
        (void) riid;    // unused: will always be ANJAY_ID_INVALID

        switch (rid) {
        case 0:
            return anjay_ret_string(ctx, "Test object");
        case 1:
            return anjay_ret_i64(ctx, avs_time_real_now().since_real_epoch.seconds);
        default:
            // control will never reach this part due to test_list_resources
            return 0;
        }
    }


What happens here?

- ``rid`` value is compared against all known Resource IDs to determine what
  value should be returned to the library.
- Resource value is passed to the library via one of ``anjay_ret_*`` functions,
  depending on the actual data type of a Resource. The value returned
  by an appropriate call is then forwarded up - this ensures correct error
  handling in case anything goes wrong.


The code above makes reference to a ``test_list_resources`` function - this is
another handler, used by the library to determine which resources are supported
and present in a given Object Instance. A LwM2M Client may be able to handle a
Resource that has no default value - in that case, the Resource is always
*supported*, but becomes *present* only after a LwM2M Server sets its value
first. Before that, it can be treated as non-existent - it will not be reported
via the Discover RPC, for example. Examples include Default Minimum Period and
Default Maximum Period Resources of the LwM2M Server object.

In our case, Resources 0 and 1 are always present in the only Instance we have,
so we can implement the ``test_resource_preset`` handler simply as:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only/src/main.c

    static int test_list_resources(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_dm_resource_list_ctx_t *ctx) {
        (void) anjay;   // unused
        (void) obj_ptr; // unused
        (void) iid;     // unused

        anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
        anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
        return 0;
    }


The ``anjay_dm_emit_res()`` function, used in the above snippet, is declared as:

.. highlight:: c
.. snippet-source:: include_public/anjay/io.h

    void anjay_dm_emit_res(anjay_dm_resource_list_ctx_t *ctx,
                           anjay_rid_t rid,
                           anjay_dm_resource_kind_t kind,
                           anjay_dm_resource_presence_t presence);

It shall be called for all *supported* resources. The ``presence`` argument
informs the library whether a given resource is *present* at the moment.

The ``kind`` argument informs the library what kind of operations are legal to
perform on the resource. Valid values are:

   * ``ANJAY_DM_RES_R`` - read-only single instance resource
   * ``ANJAY_DM_RES_W`` - write-only single instance resource
   * ``ANJAY_DM_RES_RW`` - read/write single instance resource
   * ``ANJAY_DM_RES_RM`` - read-only multiple instance resource
   * ``ANJAY_DM_RES_WM`` - write-only multiple instance resource
   * ``ANJAY_DM_RES_RWM`` - read/write multiple instance resource
   * ``ANJAY_DM_RES_E`` - executable resource

Note that when communicating with a Bootstrap Server may be able to ignore this
information, see :doc:`AT_CO7_BootstrapAwareness` for more information.

Note that the resources MUST be returned in a strictly ascending, sorted order.

Having the Read and List Resources handlers implemented, one can initialize the
``anjay_dm_object_def_t`` structure:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only/src/main.c

    static const anjay_dm_object_def_t OBJECT_DEF = {
        // Object ID
        .oid = 1234,

        .handlers = {
            // single-instance Objects can use this pre-implemented handler:
            .list_instances = anjay_dm_list_instances_SINGLE,

            .list_resources = test_list_resources,
            .resource_read = test_resource_read

            // all other handlers can be left NULL if only Read operation is
            // required
        }
    };


.. topic:: Why are all these handlers required?

   When the library attempts to perform an operation (e.g. Read) on a Resource
   it first performs a number of checks to ensure the target path is correct
   and the operation itself is allowed. Assuming a LwM2M Server requests
   some operation on the path /1/2/3:

   #. First, the library checks whether an Object with ID = 1 is registered.
      If not, a Not Found response is issued.

   #. ``list_instances`` handler of Object 1 is called to determine whether
      Instance 2 exists. If not, a Not Found response is issued.

   #. If multiple LwM2M Servers are configured, the library inspects Access
      Control Object to check whether the server requesting an operation should
      be allowed to perform it.

      .. note::

          More info: :doc:`../AT-AccessControl`

   #. ``list_resources`` handler is called to ensure that Resource 3
      is supported and present for Object Instance 2. If the handler returns 0,
      a Not Found response is issued.

   #. Finally, if all other checks succeeded, a specific handler (e.g.
      ``resource_read`` for Read operation) is called.

   Any of the handlers above may also fail with a specific CoAP error code
   (see `ANJAY_ERR_* constants <../../api/core_8h.html>`_), aborting the
   sequence early and - if the Read was triggered by a server request - causing
   the library to respond with returned error code.


When the Object Definition is ready, the only thing left to do is registering
it in the library:

.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only/src/main.c

   int main(int argc, char *argv[]) {
       // ... Anjay initialization

       // note: in this simple case the object does not have any state,
       // so it's fine to use a plain double pointer to its definition struct
       const anjay_dm_object_def_t *test_object_def_ptr = &OBJECT_DEF;

       anjay_register_object(anjay, &test_object_def_ptr);

       // ... event loop
   }


After registering the object, whenever a LwM2M Server issues a Read request
on Object 1234 or any of its Resources, Anjay will take care of preparing
a response containing the value of requested Resource.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/read-only` subdirectory of main Anjay
    project repository.
