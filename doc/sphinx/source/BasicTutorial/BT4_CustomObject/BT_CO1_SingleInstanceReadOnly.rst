Single-instance read-only object
================================

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

- an LwM2M server sends a Read request,

- the Resource is being observed and the library needs to send a Notify message
  (see :doc:`Notifications`),

- value of the Resource is required for the library to function correctly
  (mostly related to Objects 0 (Security), 1 (Server) and 2 (Access Control)).

The Read handler for our test object might be implemented as follows:

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/read-only/src/main.c

   static int test_resource_read(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_output_ctx_t *ctx) {
       // These arguments may seem superfluous now, but they will come in handy
       // while defining more complex objects
       (void) anjay;   // unused
       (void) obj_ptr; // unused: the object holds no state
       (void) iid;     // unused: will always be 0 for single-instance Objects

       switch (rid) {
       case 0:
           return anjay_ret_string(ctx, "Test object");
       case 1:
           return anjay_ret_i32(ctx, (int32_t)time(NULL));
       default:
           // control will never reach this part due to object's rid_bound
           return 0;
       }
   }


What happens here?

- ``rid`` value is compared against all known Resource IDs to determine what value
  should be returned to the library.
- Resource value is passed to the library via one of ``anjay_ret_*`` functions,
  depending on the actual data type of a Resource. The value returned
  by an appropriate call is then forwarded up - this ensures correct error
  handling in case anything goes wrong.


Having the Read handler implemented, one can initialize the
``anjay_dm_object_def_t`` structure:

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/read-only/src/main.c

   static const anjay_dm_object_def_t OBJECT_DEF = {
       // Object ID
       .oid = 1234,

       // Object does not contain any Resources with IDs >= 2
       .rid_bound = 2,

       // single-instance Objects can use these pre-implemented handlers:
       .instance_it = anjay_dm_instance_it_SINGLE,
       .instance_present = anjay_dm_instance_present_SINGLE,

       // if the Object implements all Resources from ID 0 up to its
       // `rid_bound`, it can use this predefined `resource_supported` handler:
       .resource_supported = anjay_dm_resource_supported_TRUE,

       // if all supported Resources are always available, one can use
       // a pre-implemented `resource_present` handler too:
       .resource_present = anjay_dm_resource_present_TRUE,

       .resource_read = test_resource_read

       // all other handlers can be left NULL if only Read operation is required
   };


.. topic:: Why are all these handlers required?

   When the library attempts to perform an operation (e.g. Read) on a Resource
   it first performs a number of checks to ensure the target path is correct
   and the operation itself is allowed. Assuming an LwM2M Server requests
   some operation on the path /1/2/3:

   #. First, the library checks whether an Object with ID = 1 is registered.
      If not, a Not Found response is issued.

   #. ``instance_present`` handler of Object 1 is called to determine whether
      Instance 2 exists. If not, a Not Found response is issued.

   #. If multiple LwM2M Servers are configured, the library inspects Access
      Control Object to check whether the server requesting an operation should
      be allowed to perform it.

      .. note::

          More info: :doc:`AccessControlSupport`

   #. Resource ID 3 is compared against ``rid_bound`` defined for the
      object. If ``rid_bound`` is strictly lower, a Not Found response
      is issued.

   #. ``resource_supported`` handler of the object is called to determine
      whether the Object is able to perform operations on Resource 3.
      If the handler returns 0, a Not Found response is issued.

   #. ``resource_present`` handler is called to ensure that Resource 3
      is instantiated for Object Instance 2. If the handler returns 0,
      a Not Found response is issued.

   #. ``resource_operations`` handler, if present, is used to determine whether
      requested operation is valid for given target. In case it is not (e.g.
      Execute request on a read-write Resource), Method Not Allowed response
      is issued.

      .. note::

          ``resource_operations`` handler will be explained in detail in
          further tutorials.

   #. Finally, if all other checks succeeded, a specific handler (e.g.
      ``resource_read`` for Read operation) is called.

   Any of the handlers above may also fail with a specific CoAP error code
   (see `ANJAY_ERR_* constants <../../api/anjay_8h.html>`_), aborting the
   sequence early and - if the Read was triggered by a server request - causing
   the library to respond with returned error code.


.. topic:: Why `resource_supported`/`resource_present` are separate handlers?

   An LwM2M client may be able to handle a Resource that has no default value.
   Such Resource is always *supported*, but becomes *present* only after
   an LwM2M Server sets its value first. Examples include Default Minimum Period
   and Default Maximum Period Resources of the LwM2M Server object.


When the Object Definition is ready, the only thing left to do is registering
it in the library:

.. snippet-source:: examples/tutorial/custom-object/read-only/src/main.c

   int main() {
       // ... Anjay initialization

       // note: in this simple case the object does not have any state,
       // so it's fine to use a plain double pointer to its definition struct
       const anjay_dm_object_def_t *test_object_def_ptr = &OBJECT_DEF;

       anjay_register_object(anjay, &test_object_def_ptr);

       // ... event loop
   }


After registering the object, whenever an LwM2M Server issues a Read request
on Object 1234 or any of its Resources, Anjay will take care of preparing
a response containing the value of requested Resource.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/custom-object/read-only` subdirectory of main Anjay
    project repository.
