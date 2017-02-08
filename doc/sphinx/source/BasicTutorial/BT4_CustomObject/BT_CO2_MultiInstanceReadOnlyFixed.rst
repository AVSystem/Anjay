Multi-instance read-only object with fixed number of instances
==============================================================

In this example, you will learn:

- how to implement ``instance_it`` and ``instance_present`` handlers
  for a multi-instance LwM2M Object,

- how to use ``container_of`` pattern to access registered object state.


The implemented Object will look as in :doc:`BT_CO1_SingleInstanceReadOnly`,
but will now support multiple Object Instances:

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
| Value | 1           | Read       | Single    | Mandatory | Integer |
+-------+-------------+------------+-----------+-----------+---------+


.. note::

    The code is based on :doc:`BT_CO1_SingleInstanceReadOnly`.


First of all, let us define a structure that will hold both handlers and object
state, initialize and register it:

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/read-only-multiple-fixed/src/main.c

    typedef struct test_instance {
        const char *label;
        int32_t value;
    } test_instance_t;

    typedef struct test_object {
        // handlers
        const anjay_dm_object_def_t *obj_def;

        // object state
        test_instance_t instances[2];
    } test_object_t;

    // ...

    // initialize and register the test object
    const test_object_t test_object = {
        .obj_def = &OBJECT_DEF,
        .instances = {
            { "First", 1 },
            { "Second", 2 }
        }
    };

    anjay_register_object(anjay, &test_object.obj_def);


Now, to inform the library the Object contains two Instances with IDs 0 and 1,
two more handlers are required:

- ``instance_present`` - checking whether given Object Instance ID
  exists - in this case, it needs to return 1 for Instance IDs 0 and 1
  and 0 otherwise:

  .. highlight:: c
  .. snippet-source:: examples/tutorial/custom-object/read-only-multiple-fixed/src/main.c

      static test_object_t *
      get_test_object(const anjay_dm_object_def_t *const *obj) {
          assert(obj);

          // use the container_of pattern to retrieve test_object_t pointer
          // AVS_CONTAINER_OF macro provided by avsystem/commons/defs.h
          return AVS_CONTAINER_OF(obj, test_object_t, obj_def);
      }

      // ...

      static int test_instance_present(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid) {
          (void) anjay;   // unused

          test_object_t *test = get_test_object(obj_ptr);

          // return 1 (true) if `iid` is a valid index of `TEST_INSTANCES` array
          return (size_t)iid < sizeof(test->instances) / sizeof(test->instances[0]);
      }


- ``instance_it`` - used by the library to enumerate all existing Object
  Instance IDs:

  .. highlight:: c
  .. snippet-source:: examples/tutorial/custom-object/read-only-multiple-fixed/src/main.c

      static int test_instance_it(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t *out,
                                  void **cookie) {
          (void) anjay;   // unused

          anjay_iid_t curr = 0;
          test_object_t *test = get_test_object(obj_ptr);

          // if `*cookie == NULL`, then the iteration has just started,
          // otherwise `*cookie` contains iterator value saved below
          if (*cookie) {
              curr = (anjay_iid_t)(intptr_t)*cookie;
          }

          if ((size_t)curr < sizeof(test->instances) / sizeof(test->instances[0])) {
              *out = curr;
          } else {
              // no more Object Instances available
              *out = ANJAY_IID_INVALID;
          }

          // use `*cookie` to store the iterator
          *cookie = (void*)(intptr_t)(curr + 1);
          return 0;
      }


.. warning::

    Any iterator data stored in `*cookie` **must not** require cleanup.
    The `instance_it` handler is used by the library as follows::

        int result = 0;
        void *cookie = NULL;
        anjay_iid_t iid;

        while ((result = (*object)->instance_it(anjay, object, &iid, &cookie)) == 0
                    && iid != ANJAY_IID_INVALID) {
            if (some_operation(iid) == SHOULD_BREAK_ITERATION) {
                // NOTE: the iteration may stop at any point before all
                // available Object Instances are returned by the handler
                break;
            }
        }

    For more details see `anjay_dm_instance_it_t documentation
    <../../api/anjay_8h.html>`_.


.. topic:: Why is enumerating Object Instances necessary?

   LwM2M does not require existing Object Instances to have consecutive IDs.
   It is perfectly fine to implement an Object that only contains Instances
   3 and 40235. Without ``instance_it`` handler the library would need
   to iterate over all possible Instance IDs to be able to prepare a list
   of available Object Instances whenever the LwM2M Server requests one.


Having done that, ``resource_read`` handler needs to be slightly modified
to correctly handle requests to different Object Instance IDs.

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/read-only-multiple-fixed/src/main.c

   static int test_resource_read(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_output_ctx_t *ctx) {
      (void) anjay;   // unused

      test_object_t *test = get_test_object(obj_ptr);

      // IID validity was checked by the `anjay_dm_instance_present_t` handler.
      // If the Object Instance set does not change, or can only be modifed
      // via LwM2M Create/Delete requests, it is safe to assume IID is correct.
      assert((size_t)iid < sizeof(test->instances) / sizeof(test->instances[0]));
      const struct test_instance *current_instance = &test->instances[iid];

      switch (rid) {
      case 0:
          return anjay_ret_string(ctx, current_instance->label);
      case 1:
          return anjay_ret_i32(ctx, current_instance->value);
      default:
          // control will never reach this part due to object's rid_bound
          return ANJAY_ERR_INTERNAL;
      }
   }


The only thing left to do is plugging created handlers into the Object
Definition struct:

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/read-only-multiple-fixed/src/main.c

    static const anjay_dm_object_def_t OBJECT_DEF = {
        // Object ID
        .oid = 1234,

        // Object does not contain any Resources with IDs >= 2
        .rid_bound = 2,

        .instance_it = test_instance_it,
        .instance_present = test_instance_present,

        // ... other handlers
   };


.. note::

    Complete code of this example can be found in
    `examples/tutorial/custom-object/read-only-multiple-fixed` subdirectory of
    main Anjay project repository.
