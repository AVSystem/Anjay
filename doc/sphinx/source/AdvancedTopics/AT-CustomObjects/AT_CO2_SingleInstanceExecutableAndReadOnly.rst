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

Single-instance read-only object with an executable resource
============================================================

.. include:: Anjay_codegen_note.rst

In this example you will learn:

- what are LwM2M Execute arguments,
- how to parse them using Anjay's API,
- how to implement ``resource_execute`` handler.

The implemented Object will be be based on the previous tutorial
:doc:`AT_CO1_SingleInstanceReadOnly`, but with additional executable resource:

+-------------+-----------+-----------+
| Name        | Object ID | Instances |
+=============+===========+===========+
| Test object | 1234      | Multiple  |
+-------------+-----------+-----------+

Each Object Instance has three Resources:

+------------+-------------+------------+-----------+-----------+---------+
| Name       | Resource ID | Operations | Instances | Mandatory | Type    |
+============+=============+============+===========+===========+=========+
| Label      | 0           | Read       | Single    | Mandatory | String  |
+------------+-------------+------------+-----------+-----------+---------+
| Value      | 1           | Read       | Single    | Mandatory | Integer |
+------------+-------------+------------+-----------+-----------+---------+
| Add        | 2           | Execute    | Single    | Mandatory |         |
+------------+-------------+------------+-----------+-----------+---------+

Our new `Add` resource will be used to perform an addition of integers,
storing the result in the `Value` resource. The integers are to be specified
as arguments to the LwM2M Execute operation.

LwM2M Execute arguments
~~~~~~~~~~~~~~~~~~~~~~~

The LwM2M specification defines a syntax of the Execute argument list as
a formal ABNF grammar.

.. note::

    Just to give you an idea of how the syntax looks like (without getting
    into details of the grammar), here are few examples of valid argument list:

        - ``5``,
        - ``2='10.3'``,
        - ``7,0='https://www.avsystem.com/'``
        - ``0,1,2,3,4``
        - an empty string.

    Also note that argument indices are limited to range 0-9 inclusively.


.. note::

    It is up to the implementation on how to interpret argument lists. One
    may, for example, think of each ``[0-9]='value'`` as a mapping between
    argument index and a corresponding value.

    Then the value of the argument of index `k` could be applied as a `k-th`
    argument to some function for further processing. (we will be, in fact,
    using this interpretation in this tutorial)

While the grammar itself is rather simple, it could be tedious to implement a
correct parser accepting it (with support for all corner-cases). In Anjay
there are methods designed specifically to solve this problem, namely:

.. highlight:: c
.. snippet-source:: include_public/anjay/io.h

    // ... One that returns the next argument from the Execute argument list
    int anjay_execute_get_next_arg(anjay_execute_ctx_t *ctx,
                                   int *out_arg,
                                   bool *out_has_value);

    // ... And the one that obtains its value (if any)
    int anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                          size_t *out_bytes_read,
                                          char *out_buf,
                                          size_t buf_size);

They will greatly simplify parsing process, as you will see in the next section.

Implementation
~~~~~~~~~~~~~~

We start with adding our Resource to the list of supported Resources:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-with-executable/src/main.c

    static int test_list_resources(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_dm_resource_list_ctx_t *ctx) {
        // ...
        anjay_dm_emit_res(ctx, 2, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
        return 0;
    }


Note that the ``kind`` argument is set to ``ANJAY_DM_RES_E`` to signify an
executable resource.

We can now implement ``resource_execute`` handler. Since our new resource will
be used to sum integers we have to store the addition result somewhere. For
simplicity we are going to use a ``static`` variable:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-with-executable/src/main.c

    static long addition_result;


And ``resource_execute`` could be implemented as follows:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-with-executable/src/main.c

    static int test_resource_execute(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     anjay_execute_ctx_t *ctx) {
        switch (rid) {
        case 2: {
            long sum = 0;
            int result;
            do {
                int arg_value = 0;
                if ((result = get_arg_value(ctx, &arg_value)) == 0) {
                    sum += arg_value;
                }
            } while (!result);

            if (result != ANJAY_EXECUTE_GET_ARG_END) {
                return result;
            }
            addition_result = sum;
            return 0;
        }
        default:
            // no other resource is executable
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

Where `get_arg_value` function is:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-with-executable/src/main.c

    static int get_arg_value(anjay_execute_ctx_t *ctx, int *out_value) {
        // we expect arguments of form <0-9>='<integer>'
        int arg_number;
        bool has_value;
        int result = anjay_execute_get_next_arg(ctx, &arg_number, &has_value);
        // note that we do not check against duplicated argument ids
        (void) arg_number;

        if (result < 0 || result == ANJAY_EXECUTE_GET_ARG_END) {
            // an error occured or there is just nothing more to read
            return result;
        }
        if (!has_value) {
            // we expect arguments with values only
            return ANJAY_ERR_BAD_REQUEST;
        }

        char value_buffer[10];
        if (anjay_execute_get_arg_value(ctx, NULL, value_buffer,
                                              sizeof(value_buffer))
                != 0) {
            // the value must have been malformed or it is too long - either way, we
            // don't like it
            return ANJAY_ERR_BAD_REQUEST;
        }
        char *endptr = NULL;
        long value = strtol(value_buffer, &endptr, 10);
        if (!endptr || *endptr != '\0' || value < INT_MIN || value > INT_MAX) {
            // either not an integer or the number is too small / too big
            return ANJAY_ERR_BAD_REQUEST;
        }
        *out_value = (int) value;
        return 0;
    }

Now, we need to update ``resource_read`` handler, so that:
    - it returns 4.05 Method Not Allowed when an attempt to read Executable resource is made,
    - it returns an addition result, when a Read is performed on a `Value` resource.

.. warning::

    It is worth to mention that the LwM2M specification **explicitly forbids**
    existence of executable resources that could be read at the same time.


So, here is how it could look like:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-with-executable/src/main.c

    static int test_resource_read(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_riid_t riid,
                                  anjay_output_ctx_t *ctx) {
        // ...
        switch (rid) {
        // ...
        case 1:
            return anjay_ret_i64(ctx, addition_result);
        case 2:
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        default:
            // control will never reach this part due to test_list_resources
            return 0;
        }
    }

Finally, we need to revisit an object definition to make sure our execute
handler is set as a ``resource_execute`` implementation:

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/read-only-with-executable/src/main.c

    static const anjay_dm_object_def_t OBJECT_DEF = {
    // ...
        .handlers = {
            // ...
            .resource_read = test_resource_read,
            .resource_execute = test_resource_execute
            // ...
        }
    // ...
    };

And that's it.

.. note::

    As before, you can find full source-code of this example in
    `examples/tutorial/AT-CustomObjects/read-only-with-executable` subdirectory
    of the Anjay root source dir.

    More examples of the Execute handlers can be found in object
    implementations of the demo client (in the `demo` subdirectory). Just
    grep for the `resource_execute` keyword.
