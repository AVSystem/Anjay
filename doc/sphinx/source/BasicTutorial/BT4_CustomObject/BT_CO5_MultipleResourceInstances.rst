Objects with Multiple Instance Resources
========================================

In this tutorial you will learn:

- how to retrieve a Multiple Instance Resource using Anjay API,

- how to send a Multiple Instance Resource using Anjay API.

We will extend the Test object from `previous tutorial <BT_CO4_MultiInstanceDynamic>`_
by allowing `Value` Resource to contain multiple values.


API for Multiple Instance Resources management
----------------------------------------------

Reading Multiple Instance Resources or writing some in a response requires
special contexts to be instantiated by the user, i.e. ``anjay_input_ctx_t``
and ``anjay_output_ctx_t`` out of contexts passed by Anjay to ``resource_write``
and ``resource_read`` handlers.

.. warning::
    Trying to use original contexts to read / write Multiple Instance Resource
    results in undefined behavior.

.. warning::
    Trying to use original contexts after special contexts were created on top of
    them results in undefined behavior.

To instantiate them one uses functions:

.. highlight:: c
.. snippet-source:: include_public/anjay/anjay.h

    anjay_input_ctx_t *anjay_get_array(anjay_input_ctx_t *ctx);

and

.. snippet-source:: include_public/anjay/anjay.h

    anjay_output_ctx_t *anjay_ret_array_start(anjay_output_ctx_t *ctx);

respectively.

From now on, created contexts shall be used to receive or send Resource
Instance ID (`index`) and the Resource Instance Value (`value`).


Preparing Test object for Multiple Instance Resources
-----------------------------------------------------

We define following structure to represent a single Instance of our Multiple
Instance Resource:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-resources-dynamic/src/test_object.c

    typedef struct test_value_instance {
        anjay_riid_t index;
        int32_t value;
    } test_value_instance_t;

.. note::

    ``anjay_riid_t`` is used for the first time in the tutorial. It is a data type
    able to store all valid Resource Instance IDs.

We also edit ``test_instance_t`` structure definition:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-resources-dynamic/src/test_object.c

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

Handling Multiple Instance Resources in Write RPC
-------------------------------------------------

.. topic:: General flow of function calls when LwM2M Write operation was
           issued on Multiple Instance Resource.

    1. ``resource_write`` handler is being called by Anjay, giving the control
       to the user.

    2. User creates additional ``anjay_input_context_t`` out of provided (as an argument
       to ``resource_write`` handler) input context by calling ``anjay_get_array``.

    3. User calls ``anjay_get_array_index`` and ``anjay_get_*`` until either
       the former fails, due to some kind of error, or finishes, indicating end
       of a message by returning ``ANJAY_GET_INDEX_END``.

OK, based on the above, we are ready to create helper function which is going
to read whole sequence of `(index, value)` pairs and store them on the list.

.. snippet-source:: examples/tutorial/custom-object/multi-instance-resources-dynamic/src/test_object.c

    static int test_array_write(AVS_LIST(test_value_instance_t) *out_instances,
                                anjay_input_ctx_t *input_array) {
        int result;
        test_value_instance_t instance;
        assert(*out_instances == NULL && "Nonempty list provided");

        while ((result = anjay_get_array_index(input_array, &instance.index)) == 0) {
            if (anjay_get_i32(input_array, &instance.value)) {
                // An error occurred during the read.
                result = ANJAY_ERR_INTERNAL;
                goto failure;
            }

            AVS_LIST(test_value_instance_t) *insert_it;

            // Duplicate detection, and searching for the place to insert
            // note that it makes the whole function O(n^2).
            AVS_LIST_FOREACH_PTR(insert_it, out_instances) {
                if ((*insert_it)->index == instance.index) {
                    // duplicate
                    result = ANJAY_ERR_BAD_REQUEST;
                    goto failure;
                } else if ((*insert_it)->index > instance.index) {
                    break;
                }
            }

            AVS_LIST(test_value_instance_t) new_element =
                    AVS_LIST_NEW_ELEMENT(test_value_instance_t);

            if (!new_element) {
                // out of memory
                result = ANJAY_ERR_INTERNAL;
                goto failure;
            }

            *new_element = instance;
            AVS_LIST_INSERT(insert_it, new_element);
        }

        if (result && result != ANJAY_GET_INDEX_END) {
            // malformed request
            result = ANJAY_ERR_BAD_REQUEST;
            goto failure;
        }

        return 0;

    failure:
        AVS_LIST_CLEAR(out_instances);
        return result;
    }

On input, the function takes a pointer to the list where values shall be
stored, and mentioned array input context. It is pretty dense, indeed,
but this is the cost of being correct.

.. note::

    If you had looked at ``anjay_get_array`` documentation you've seen the
    warning about possible interpretations of requests containing duplicated
    Resource Instance IDs. Presented implementation returns an error on such
    requests, but the LwM2M specification seem to not forbid implementations
    from accepting it, as long as it does not break any invariants defined
    within specification. We would however recommend to take approach
    as shown in this tutorial.

Last thing to do is to modify ``test_resource_write`` implementation to make use
of our helper function:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-resources-dynamic/src/test_object.c

    static int test_resource_write(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_input_ctx_t *ctx) {
        // ...
        switch (rid) {
        // ...
        case 1: {
            anjay_input_ctx_t *input_array = anjay_get_array(ctx);
            if (!input_array) {
                // could not create input context for some reason
                return ANJAY_ERR_INTERNAL;
            }

            // free memory associated with old values
            AVS_LIST_CLEAR(&current_instance->values);

            // try to read new values from an RPC
            int result = test_array_write(&current_instance->values,
                                          input_array);

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

Handling Multiple Instance Resources in Read RPC
------------------------------------------------

.. topic:: General flow of function calls when LwM2M Read operation was
           issued on Multiple Instance Resource.

    1. ``resource_read`` handler is being called by Anjay, giving the control
       to the user.

    2. User creates additional ``anjay_output_context_t`` out of provided (as an argument
       to ``resource_read`` handler) output context by calling ``anjay_ret_array_start``.

    3. User calls ``anjay_ret_array_index`` and ``anjay_ret_*`` until they are done.

    4. In the end, user calls ``anjay_ret_array_finish`` to tell Anjay that the
       Multiple Instance Resource response is ready.

In the end, the read handler could look like this:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-resources-dynamic/src/test_object.c

    static int test_resource_read(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_output_ctx_t *ctx) {
        // ...
        switch (rid) {
        // ...
        case 1: {
            anjay_output_ctx_t *array_output = anjay_ret_array_start(ctx);
            if (!array_output) {
                // cannot instantiate array output context
                return ANJAY_ERR_INTERNAL;
            }

            AVS_LIST(const test_value_instance_t) it;
            AVS_LIST_FOREACH(it, current_instance->values) {
                int result = anjay_ret_array_index(array_output, it->index);
                if (result) {
                    // failed to return an index
                    return result;
                }

                result = anjay_ret_i32(array_output, it->value);
                if (result) {
                    // failed to return value
                    return result;
                }
            }
            return anjay_ret_array_finish(array_output);
        }
        // ...
        }
    }

As always, you can find source code of this tutorial in
`examples/tutorial/custom-object/multi-instance-resources-dynamic` in Anjay
source directory.
