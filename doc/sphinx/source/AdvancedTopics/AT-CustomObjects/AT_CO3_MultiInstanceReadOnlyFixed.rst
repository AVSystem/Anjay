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

Multi-instance read-only object with fixed number of instances
==============================================================

.. include:: Anjay_codegen_note.rst

In this example you will learn how to implement ``list_instances`` handler for a
multi-instance LwM2M Object.

The implemented Object will look as in :doc:`AT_CO1_SingleInstanceReadOnly`,
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

    The code is based on :doc:`AT_CO1_SingleInstanceReadOnly`.


First of all, let us define a structure that will hold both handlers and object
state, initialize and register it:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-multiple-fixed/src/main.c

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
        .instances = { { "First", 1 }, { "Second", 2 } }
    };

    anjay_register_object(anjay, &test_object.obj_def);


Now, to inform the library the Object contains two Instances with IDs 0 and 1,
one additional handler is required:

- ``list_instances`` - used by the library to enumerate all existing Object
  Instance IDs:

  .. highlight:: c
  .. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-multiple-fixed/src/main.c

        static int test_list_instances(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_dm_list_ctx_t *ctx) {
            (void) anjay; // unused

            test_object_t *test = get_test_object(obj_ptr);

            for (anjay_iid_t iid = 0;
                 (size_t) iid < sizeof(test->instances) / sizeof(test->instances[0]);
                 ++iid) {
                anjay_dm_emit(ctx, iid);
            }

            return 0;
        }

  Note that the instances MUST be returned in a strictly ascending, sorted
  order.

.. topic:: Why is enumerating Object Instances necessary?

   LwM2M does not require existing Object Instances to have consecutive IDs.
   It is perfectly fine to implement an Object that only contains Instances
   3 and 40235. Without ``list_instances`` handler the library would need
   to iterate over all possible Instance IDs to be able to prepare a list
   of available Object Instances whenever the LwM2M Server requests one.


Having done that, ``resource_read`` handler needs to be slightly modified
to correctly handle requests to different Object Instance IDs.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-multiple-fixed/src/main.c

    static int test_resource_read(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_output_ctx_t *ctx) {
        (void) anjay; // unused

        test_object_t *test = get_test_object(obj_ptr);

        // IID validity was checked by the `anjay_dm_list_instances_t` handler.
        // If the Object Instance set does not change, or can only be modifed
        // via LwM2M Create/Delete requests, it is safe to assume IID is correct.
        assert((size_t) iid < sizeof(test->instances) / sizeof(test->instances[0]));
        const struct test_instance *current_instance = &test->instances[iid];

        // We have no Multiple-Instance Resources, so it is safe to assume
        // that RIID is never set.
        assert(riid == ANJAY_ID_INVALID);

        switch (rid) {
        case 0:
            return anjay_ret_string(ctx, current_instance->label);
        case 1:
            return anjay_ret_i32(ctx, current_instance->value);
        default:
            // control will never reach this part due to test_list_resources
            return ANJAY_ERR_INTERNAL;
        }
    }


The only thing left to do is plugging created handler into the Object Definition
struct:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-multiple-fixed/src/main.c

    static const anjay_dm_object_def_t OBJECT_DEF = {
        // Object ID
        .oid = 1234,

        .handlers = {
            .list_instances = test_list_instances,

            // ... other handlers
        }
    };


.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/read-only-multiple-fixed` subdirectory of
    main Anjay project repository.
