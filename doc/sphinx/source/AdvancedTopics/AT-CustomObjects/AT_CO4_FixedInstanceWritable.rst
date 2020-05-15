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

Multi-instance writable object with fixed number of instances
=============================================================

.. include:: Anjay_codegen_note.rst

In this tutorial you will learn:

- how to implement ``resource_write`` to create a writeable Resource,
- what are Partial Update and Replace variants of the LwM2M Write request,
- how to setup ``instance_reset`` handler to handle LwM2M Write (Replace),
- basics of transaction handling in Anjay.

Implemented object is based on :doc:`AT_CO3_MultiInstanceReadOnlyFixed`,
but this time accepts Write requests.

+-------------+-----------+-----------+
| Name        | Object ID | Instances |
+=============+===========+===========+
| Test object | 1234      | Multiple  |
+-------------+-----------+-----------+

Each Object Instance has two Resources:

+-------+-------------+------------+-----------+-----------+---------+
| Name  | Resource ID | Operations | Instances | Mandatory | Type    |
+=======+=============+============+===========+===========+=========+
| Label | 0           | Read/Write | Single    | Mandatory | String  |
+-------+-------------+------------+-----------+-----------+---------+
| Value | 1           | Read/Write | Single    | Mandatory | Integer |
+-------+-------------+------------+-----------+-----------+---------+


Simple variant
--------------

The ``test_instance_t`` needs to be able to store arbitrary data. Let us set
an arbitrary limit of 32 characters in length (including terminating nullbyte):

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed/src/main.c

    typedef struct test_instance {
        char label[32];
        int32_t value;
    } test_instance_t;


Now the ``anjay_dm_resource_write_t`` handler implementation:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed/src/main.c

    static int test_resource_write(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   anjay_input_ctx_t *ctx) {
        (void) anjay; // unused

        test_object_t *test = get_test_object(obj_ptr);

        // IID validity was checked by the `anjay_dm_list_instances_t` handler.
        // If the Object Instance set does not change, or can only be modifed
        // via LwM2M Create/Delete requests, it is safe to assume IID is correct.
        assert((size_t) iid < NUM_INSTANCES);
        struct test_instance *current_instance = &test->instances[iid];

        // We have no Multiple-Instance Resources, so it is safe to assume
        // that RIID is never set.
        assert(riid == ANJAY_ID_INVALID);

        switch (rid) {
        case 0: {
            // `anjay_get_string` may return a chunk of data instead of the
            // whole value - we need to make sure the client is able to hold
            // the entire value
            char buffer[sizeof(current_instance->label)];
            int result = anjay_get_string(ctx, buffer, sizeof(buffer));

            if (result == 0) {
                // value OK - save it
                memcpy(current_instance->label, buffer, sizeof(buffer));
            } else if (result == ANJAY_BUFFER_TOO_SHORT) {
                // the value is too long to store in the buffer
                result = ANJAY_ERR_BAD_REQUEST;
            }

            return result;
        }

        case 1:
            // reading primitive values can be done directly - the value will only
            // be written to the output variable if everything went fine
            return anjay_get_i32(ctx, &current_instance->value);

        default:
            // control will never reach this part due to test_list_resources
            return ANJAY_ERR_INTERNAL;
        }
    }


We also need to update the ``test_list_resources`` handler with the information
that the resources are writable:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed/src/main.c

    static int test_list_resources(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_dm_resource_list_ctx_t *ctx) {
        // ...
        anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
        anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
        return 0;
    }


Everything that was left to do is plugging in handlers. There is a catch though:
any modifying operation (writing a value, creating or deleting an Object
Instance) requires explicitly defined transaction handlers.
``anjay_dm_transaction_NOOP`` placeholder will be used for now, see
:ref:`FixedInstanceWritable-transactional` for an actual implementation of
these.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed/src/main.c

    static const anjay_dm_object_def_t OBJECT_DEF = {
        // ...
        .handlers = {
            // ...

            .resource_write = test_resource_write,

            .transaction_begin = anjay_dm_transaction_NOOP,
            .transaction_validate = anjay_dm_transaction_NOOP,
            .transaction_commit = anjay_dm_transaction_NOOP,
            .transaction_rollback = anjay_dm_transaction_NOOP
        }
   };


.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/writable-multiple-fixed` subdirectory of
    main Anjay project repository.


LwM2M Write operation modes
^^^^^^^^^^^^^^^^^^^^^^^^^^^

A LwM2M Server may perform a Write on the *entire Object Instance*. Two
variants of such requests are available - both replace values of Instance
Resources with received values, but they differ in what happens to Resources
not present in the request:

- Partial Update - they are leaved unchanged,

- Replace - Optional Resources are reset to "missing" state. All mandatory
  Resources MUST be specified in the request, otherwise it MUST be considered
  invalid.

  .. note::
      Anjay does not distinguish Mandatory and Optional Resources - the user
      is responsible for ensuring the state of an Object is still valid after
      handling a request. This topic is covered in detail in
      :ref:`FixedInstanceWritable-transactional`; we will ignore it in this
      particular example.

Anjay implements Partial Update as a series of ``resource_write`` calls and
Replace one as ``instance_reset`` + series of ``resource_write`` calls.
To properly support both Write variants, ``instance_reset`` needs to be
implemented too.


Anjay transaction handlers
^^^^^^^^^^^^^^^^^^^^^^^^^^

In some cases, like LwM2M Write requests targeting an Object Instance, Anjay
calls multiple handlers modifying the data model to perform a single atomic
operation. In such case, transactions ensure that data model is not left
in an inconsistent state when one of handler calls fails. To achieve correct
behavior, used-defined handlers should behave as described below:

- ``anjay_dm_resource_write_t`` - needs to ensure that the value being written
  is correct and store new value in such a way that rolling back to the previous
  one is still possible. It must not check constraints that depend on values
  of other Resources, as these Resources may also change during the same
  transaction.

- ``anjay_dm_transaction_begin_t`` - always called before any operation that
  changes the data model (LwM2M Write, Create, Delete). It should do whatever
  actions are necessary for correct transaction handling - in the simplest
  possible case, it could save a snapshot of the current state of the Object
  to restore it when rollback is required.

- ``anjay_dm_transaction_validate_t`` - called after all write/create/remove
  handlers succeed. It should verify cross-Resource or cross-Instance
  constraints and fail if the data model state is not consistent.
  **Example**: LwM2M requires that at most one Security (/0) object instance
  has Bootstrap Server Resource set to 1. If a LwM2M Create operation adds
  an instance with this Bootstrap Server = 1 when there already was one, this
  handler needs to fail.

- ``anjay_dm_transaction_commit_t`` - called after the transaction was
  successfully validated by the ``anjay_dm_transaction_validate_t`` handler.
  It should atomically apply all changes performed since last call to
  ``anjay_dm_transaction_begin_t`` for the same object.

  .. warning::
      This handler must not fail, unless a critical and unrecoverable error
      (e.g. hardware failure) occurs.

- ``anjay_dm_transaction_rollback_t`` - called after one of
  write/create/remove/validate handlers fails. It should atomically restore
  the object to a state it was at the last ``anjay_dm_transaction_begin_t``
  call.

  .. warning::
      This handler must not fail, unless a critical and unrecoverable error
      (e.g. hardware failure) occurs.


This tutorial shows an example of implementing transactions by storing
a snapshot of the entire state of a LwM2M Object.

.. warning::

    Such implementation, while simple, effectively doubles amount of RAM used
    by the Object.


.. _FixedInstanceWritable-transactional:

Transactional variant
---------------------

Knowing all about different LwM2M Write variants and Anjay transaction handlers,
we can start implementing a transaction-aware client capable of handling
all LwM2M Write requests.

Let's start by implementing the ``instance_reset`` handler. Two boolean flags
need to be added to ``test_instance_t`` to detect a situation where a value
is considered "unset":

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed-transactional/src/main.c

    typedef struct test_instance {
        bool has_label;
        char label[32];

        bool has_value;
        int32_t value;
    } test_instance_t;


``instance_reset`` and ``resource_write`` should then use these to appropriately
mark Resources as set/unset:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed-transactional/src/main.c


    static int test_instance_reset(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid) {
        (void) anjay; // unused

        test_object_t *test = get_test_object(obj_ptr);

        // IID validity was checked by the `anjay_dm_list_instances_t` handler.
        // If the Object Instance set does not change, or can only be modifed
        // via LwM2M Create/Delete requests, it is safe to assume IID is correct.
        assert((size_t) iid < NUM_INSTANCES);

        // mark all Resource values for Object Instance `iid` as unset
        test->instances[iid].has_label = false;
        test->instances[iid].has_value = false;
        return 0;
    }

    // ...

    static int test_resource_write(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   anjay_input_ctx_t *ctx) {
        // ...

        switch (rid) {
        case 0: {
                // ...

                // value OK - save it
                memcpy(current_instance->label, buffer, sizeof(buffer));
                current_instance->has_label = true;

                // ...
        }

        case 1: {
            // reading primitive values can be done directly - the value will only
            // be written to the output variable if everything went fine
            int result = anjay_get_i32(ctx, &current_instance->value);
            if (result == 0) {
                current_instance->has_value = true;
            }
            return result;
        }

        // ...
    }


Having ``has_label``/``has_value`` flags ready, we can finally implement
transaction handlers:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/writable-multiple-fixed-transactional/src/main.c

    static int test_transaction_begin(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay; // unused

        test_object_t *test = get_test_object(obj_ptr);

        // store a snapshot of object state
        memcpy(test->backup_instances, test->instances, sizeof(test->instances));
        return 0;
    }

    static int
    test_transaction_validate(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay; // unused

        test_object_t *test = get_test_object(obj_ptr);

        // ensure all Object Instances contain all Mandatory Resources
        for (size_t i = 0; i < NUM_INSTANCES; ++i) {
            if (!test->instances[i].has_label || !test->instances[i].has_value) {
                // validation failed: Object state invalid, rollback required
                return ANJAY_ERR_BAD_REQUEST;
            }
        }

        // validation successful, can commit
        return 0;
    }

    static int
    test_transaction_commit(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay;   // unused
        (void) obj_ptr; // unused

        // no action required in this implementation; if object state snapshot was
        // dynamically allocated, this would be the place for releasing it
        return 0;
    }

    static int
    test_transaction_rollback(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr) {
        (void) anjay; // unused

        test_object_t *test = get_test_object(obj_ptr);

        // restore saved object state
        memcpy(test->instances, test->backup_instances, sizeof(test->instances));
        return 0;
    }

    // ...

    static const anjay_dm_object_def_t OBJECT_DEF = {
        // ...
        .handlers = {
            // ...

            .instance_reset = test_instance_reset,

            // ...

            .transaction_begin = test_transaction_begin,
            .transaction_validate = test_transaction_validate,
            .transaction_commit = test_transaction_commit,
            .transaction_rollback = test_transaction_rollback
        }
    };


That is everything one needs to set up a complete, transaction-aware writable
Resource.

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/writable-multiple-fixed-transactional`
    subdirectory of main Anjay project repository.
