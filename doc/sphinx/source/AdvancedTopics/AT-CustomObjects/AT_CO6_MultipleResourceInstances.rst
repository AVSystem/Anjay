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

Objects with Multiple Instance Resources
========================================

.. include:: Anjay_codegen_note.rst

In this tutorial you will learn:

- how to retrieve a Multiple Instance Resource using Anjay API,
- how to send a Multiple Instance Resource using Anjay API.

We will extend the Test object from :doc:`previous tutorial
<AT_CO5_MultiInstanceDynamic>` by allowing `Value` Resource to contain multiple
values.


API for Multiple Instance Resources management
----------------------------------------------

Dealing with Multiple Instance Resources in the data model requires implementing
additional handlers. The most important in the ``list_resource_instances``
handler:

.. highlight:: c
.. snippet-source:: include_public/anjay/dm.h

    typedef int
    anjay_dm_list_resource_instances_t(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid,
                                       anjay_dm_list_ctx_t *ctx);

This handler needs to be implemented for any Object that has some Multiple
Instance Resource. It will only be called on Multiple Resources, as determined
by the ``kind`` argument passed to ``anjay_dm_emit_res()`` in the
``list_resources`` handler. It shall list (via the passed
``anjay_dm_list_ctx_t``) all the currently existing instances of the resource.

To allow writing to Multiple Instance Resources, the ``resource_reset`` handler
needs to be implemented as well:

.. snippet-source:: include_public/anjay/dm.h

    typedef int
    anjay_dm_resource_reset_t(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid);

This handler will only be called on Resources that has been determined to have
multiple instances. It shall put the Resource in a state that it is present, but
having zero instances.

The actual reads and writes are performed using the usual ``resource_read`` and
``resource_write`` handler. The ``riid`` argument that we have previously been
ignoring, is used to determine the Resource Instance that is targeted.


Preparing Test object for Multiple Instance Resources
-----------------------------------------------------

First of all, we need to update the List Resources handler so that the library
knows that Resource 1 now has multiple instances:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    static int test_list_resources(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_dm_resource_list_ctx_t *ctx) {
        // ...
        anjay_dm_emit_res(ctx, 0, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT);
        anjay_dm_emit_res(ctx, 1, ANJAY_DM_RES_RWM, ANJAY_DM_RES_PRESENT);
        return 0;
    }


We define following structure to represent a single Instance of our Multiple
Instance Resource:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    typedef struct test_value_instance {
        anjay_riid_t index;
        int32_t value;
    } test_value_instance_t;

.. note::

    ``anjay_riid_t`` is used for the first time in the tutorial. It is a data type
    able to store all valid Resource Instance IDs.

We also edit ``test_instance_t`` structure definition:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    typedef struct test_instance {
        anjay_iid_t iid;

        bool has_label;
        char label[32];

        bool has_values;
        AVS_LIST(test_value_instance_t) values;
    } test_instance_t;

.. topic::  Why does ``test_instance_t`` still contain boolean flag indicating presence of the value?

    There is certainly a difference between lack of presence of Multiple
    Instance Resource value, and Multiple Instance Resource containing
    zero Instances (i.e. lack of list presence vs an empty list).


Implementing the List Resource Instances handler
------------------------------------------------

Here is how the List Resource Instances is implemented for our test object:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    static int
    test_list_resource_instances(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_dm_list_ctx_t *ctx) {
        (void) anjay; // unused
        test_instance_t *current_instance =
                (test_instance_t *) get_instance(get_test_object(obj_ptr), iid);

        // this handler can only be called for Multiple-Instance Resources
        assert(rid == 1);

        AVS_LIST(test_value_instance_t) it;
        AVS_LIST_FOREACH(it, current_instance->values) {
            anjay_dm_emit(ctx, it->index);
        }
        return 0;
    }

As you can see, the ``anjay_dm_emit()`` function is used to pass all the
existing Resource Instances to Anjay, similar to the ``list_instances`` and
``list_resources`` handlers.

Note that the resource instances MUST be returned in a strictly ascending,
sorted order. We will keep the resource instances in sorted order, so this
implementation satisfies this contract.

Handling Multiple Instance Resources in Read RPC
------------------------------------------------

``resource_read`` handler is being called by Anjay for each Resource Instance
referenced by the server, giving the control to the user. Thus, the read handler
could look like this:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    static int test_resource_read(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_output_ctx_t *ctx) {
        // ...
        switch (rid) {
        // ...
        case 1: {
            AVS_LIST(const test_value_instance_t) it;
            AVS_LIST_FOREACH(it, current_instance->values) {
                if (it->index == riid) {
                    return anjay_ret_i32(ctx, it->value);
                }
            }
            // Resource Instance not found
            return ANJAY_ERR_NOT_FOUND;
        }
        // ...
        }
    }


Implementing the Resource Reset handler
---------------------------------------

.. topic:: General flow of function calls when LwM2M Write operation was
           issued on Multiple Instance Resource.

    1. ``resource_reset`` handler is being called by Anjay, to clear the
       Multiple Instance Resource.

    2. ``resource_write`` handler is being called by Anjay for each Resource
       Instance referenced by the server, giving the control to the user. The
       handler shall add or replace the Resource with the instance it is being
       called for.

The above means that the Resource Reset handler is rather simple to implement,
as it only needs to clear the resource:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    static int test_resource_reset(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid) {
        (void) anjay; // unused

        test_instance_t *current_instance =
                (test_instance_t *) get_instance(get_test_object(obj_ptr), iid);

        // this handler can only be called for Multiple-Instance Resources
        assert(rid == 1);

        // free memory associated with old values
        AVS_LIST_CLEAR(&current_instance->values);
        current_instance->has_values = true;
        return 0;
    }


Handling Multiple Instance Resources in Write RPC
-------------------------------------------------

Now we are ready to actually implement the write operation. We will create a
helper function for actually updating the Resource Instance list with a newly
written value.

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    static int test_array_write(AVS_LIST(test_value_instance_t) *out_instances,
                                anjay_riid_t index,
                                anjay_input_ctx_t *input_ctx) {
        test_value_instance_t instance = {
            .index = index
        };

        if (anjay_get_i32(input_ctx, &instance.value)) {
            // An error occurred during the read.
            return ANJAY_ERR_INTERNAL;
        }

        AVS_LIST(test_value_instance_t) *insert_it;

        // Searching for the place to insert;
        // note that it makes the whole function O(n).
        AVS_LIST_FOREACH_PTR(insert_it, out_instances) {
            if ((*insert_it)->index >= instance.index) {
                break;
            }
        }

        if ((*insert_it)->index != instance.index) {
            AVS_LIST(test_value_instance_t) new_element =
                    AVS_LIST_NEW_ELEMENT(test_value_instance_t);

            if (!new_element) {
                // out of memory
                return ANJAY_ERR_INTERNAL;
            }

            AVS_LIST_INSERT(insert_it, new_element);
        }

        assert((*insert_it)->index == instance.index);
        **insert_it = instance;

        return 0;
    }

Last thing to do is to modify ``test_resource_write`` implementation to make use
of our helper function:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic/src/test_object.c

    static int test_resource_write(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   anjay_input_ctx_t *ctx) {
        // ...
        switch (rid) {
        // ...
        case 1: {
            int result = test_array_write(&current_instance->values, riid, ctx);
            if (!result) {
                current_instance->has_values = true;
            }

            // either test_array_write succeeded and result is 0, or not
            // in which case result contains appropriate error code.
            return result;
        }
        // ...
        }
    }

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/multi-instance-resources-dynamic`
    subdirectory of main Anjay project repository.
