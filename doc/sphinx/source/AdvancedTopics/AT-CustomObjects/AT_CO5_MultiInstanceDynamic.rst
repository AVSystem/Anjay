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

Multi-instance writable object with dynamic number of instances
===============================================================

.. include:: Anjay_codegen_note.rst

In this tutorial you will learn:

- how to implement ``instance_create`` handler to create Instances,
- how to implement ``instance_remove`` handler to remove Instances,
- how to assign instance identifiers to a newly created Instances,
- basics of the ``AVS_LIST`` utility library.

Implemented object will be roughly based on :doc:`AT_CO4_FixedInstanceWritable`.

+-------------+-----------+-----------+
| Name        | Object ID | Instances |
+=============+===========+===========+
| Test object | 1234      | Multiple  |
+-------------+-----------+-----------+

As before, each Object Instance has two Resources:

+-------+-------------+------------+-----------+-----------+---------+
| Name  | Resource ID | Operations | Instances | Mandatory | Type    |
+=======+=============+============+===========+===========+=========+
| Label | 0           | Read/Write | Single    | Mandatory | String  |
+-------+-------------+------------+-----------+-----------+---------+
| Value | 1           | Read/Write | Single    | Mandatory | Integer |
+-------+-------------+------------+-----------+-----------+---------+

The code is based on :doc:`previous tutorial <AT_CO4_FixedInstanceWritable>`,
yet in this chapter all Test object related code was moved to separate files to
keep everything clean.

Updating the object structure
-----------------------------

First of all, we have to update our ``test_object_t`` structure to support
storing multiple object instances. For that, we need some kind of dynamically
sized container. We could choose plain-old, manually managed C arrays, but
that comes with unnecessary distraction, and as a matter of fact is a bit
error-prone.  Therefore, in this tutorial, we are going to use ``AVS_LIST``,
which is an abstraction over singly linked list, and has been used in Anjay
with success since the beginning of the project.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    typedef struct test_object {
        // handlers
        const anjay_dm_object_def_t *obj_def;

        // object state
        AVS_LIST(test_instance_t) instances;

        AVS_LIST(test_instance_t) backup_instances;
    } test_object_t;

.. note::
    ``AVS_LIST(x)`` is actually a macro that expands to a pointer of type ``x``.
    It therefore has pointer semantics and can be treated like that (standard
    dereferencing and dereferencing with assignment will work as expected). This
    is however, not everything you need to know about them, as they are more
    complicated than that. We recommend you to refer to the `documentation
    <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_list.h>`_.

In the previous tutorial, our Instances had hardcoded Instance IDs.  We no
longer have such comfort, and have to be able to uniquely identify Object
Instances. As a consequence, we will add ``anjay_iid_t iid`` field to
``test_instance_t``:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    typedef struct test_instance {
        anjay_iid_t iid;

        bool has_label;
        char label[32];

        bool has_value;
        int32_t value;
    } test_instance_t;

Initialization and cleanup
--------------------------

We must reconsider the way our Test object is being initialized. Up to this point
it was allocated on stack and required no cleanup. Again, times have changed, and
we won't be able to proceed further without allocating memory on demand.

.. topic:: Wait, you could still allocate objects on stack, and initialize them later!

    Yes, but it may be considered bad coding style for at least two reasons:

    1. Having partially initialized or uninitialized objects floating around is
       almost always a bad idea, as it is easy to forget about initialization
       and accessing uninitialized data is undefined behavior. Allocating
       objects on a heap however, allows the implementation to fully initialize
       an object before returning a reference to it.

    2. End user of an Object should NOT be exposed to its internal representation,
       i.e. notice how handles to all objects shown to you so far were returned as
       pointers to ``anjay_dm_object_def_t`` -- clearly, it indicates to the user
       that they have nothing interesting to do with such reference (besides being
       able to register it), unless some additional public API is provided.

To achieve proper control over object lifetime and initialization, we are
going to introduce two functions, namely ``create_test_object``:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    const anjay_dm_object_def_t **create_test_object(void) {
        test_object_t *repr =
                (test_object_t *) avs_calloc(1, sizeof(test_object_t));
        if (repr) {
            repr->obj_def = &OBJECT_DEF;
            return &repr->obj_def;
        }
        return NULL;
    }

.. topic:: Shouldn't ``test_object_t::instances`` and ``test_object_t::backup_instances`` be
           be initialized in some special way?

        No, ``NULL`` is a valid (and only) representation of an empty ``AVS_LIST``.

and ``delete_test_object``:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    void delete_test_object(const anjay_dm_object_def_t **obj) {
        if (!obj) {
            return;
        }
        test_object_t *repr = get_test_object(obj);
        AVS_LIST_CLEAR(&repr->instances);
        AVS_LIST_CLEAR(&repr->backup_instances);
        avs_free(repr);
    }

As you can see, dynamic memory management is semi-automatically handled by ``AVS_LIST``.

.. note::
    ``AVS_LIST_CLEAR`` iterates over the list, in the process frees memory allocated for
    each element, and in the end sets list handle to ``NULL``.

Using functions defined above to create, register and free the Test object is similar
as in previous tutorials.

.. note::

    Before calling ``delete_test_object()`` the object must be unregistered. You
    can do this by calling ``anjay_unregister_object()``. Also ``anjay_delete()``
    will deregister (but not delete) all of registered objects before destruction
    of Anjay instance.

Updating old, already implemented handlers to use ``AVS_LIST``
--------------------------------------------------------------

To simplify matters, we have to agree upon one contract:

#. We establish a natural Instance ordering on their Instance IDs, exploiting
   the fact they MUST be unique.

#. We store Instances of the Test object in an ordered (as above) list.

OK, now that we made our assumptions, we are ready to implement utility function
``get_instance`` which retrieves Test object instance with specified Instance ID.
It will happen to be very useful in the next couple of subsections:

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    static AVS_LIST(test_instance_t) get_instance(test_object_t *repr,
                                                  anjay_iid_t iid) {
        AVS_LIST(test_instance_t) it;
        AVS_LIST_FOREACH(it, repr->instances) {
            if (it->iid == iid) {
                return it;
            } else if (it->iid > iid) {
                // Since list of instances is sorted by Instance ID,
                // Instance with given iid does not exist on that list
                break;
            }
        }
        // Instance was not found.
        return NULL;
    }

And one more method, presenting another functionality of ``AVS_LISTs``
before going into details of ``instance_create`` and ``instance_remove``
and leaving the rest of the work of this kind as an exercise for the reader
(or, if they are lazy, they can always look at the code):

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    static int test_list_instances(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_dm_list_ctx_t *ctx) {
        (void) anjay; // unused

        // iterate over all instances and return their IDs
        AVS_LIST(test_instance_t) it;
        AVS_LIST_FOREACH(it, get_test_object(obj_ptr)->instances) {
            anjay_dm_emit(ctx, it->iid);
        }

        return 0;
    }

Note that as we keep the Instances in sorted order, this implementation
satisfies the contract for this handler.

That's all for this section. As noted above, implementation of other methods
is as always available in the source code provided with the tutorial. We
do however strongly recommend you to port the methods to use ``AVS_LISTs``
on your own, especially **remember about updating transaction handlers**.

``instance_create`` handler
---------------------------

Let's have a look on ``anjay_dm_instance_create_t`` handler type signature:

.. snippet-source:: include_public/anjay/dm.h

    typedef int
    anjay_dm_instance_create_t(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid);

The ``iid`` parameter is the most important for us at the moment, as this is the
ID of the Instance we need to create.

LwM2M Create requests do not necessarily have to contain preferred Instance ID.
However, Anjay makes it transparent to the application - if the Instance ID is
not specified by the server, it will iterate over existing instances using the
``anjay_dm_list_instances_t`` handler, and find the lowest ID that is not
already occupied. If the Instance ID is specified by the server, Anjay will call
the ``anjay_dm_list_instances_t`` handler and ensure that there is no such
Instance ID already existing.

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    static int test_instance_create(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid) {
        (void) anjay; // unused

        test_object_t *repr = get_test_object(obj_ptr);

        AVS_LIST(test_instance_t) new_instance =
                AVS_LIST_NEW_ELEMENT(test_instance_t);

        if (!new_instance) {
            // out of memory
            return ANJAY_ERR_INTERNAL;
        }

        new_instance->iid = iid;

        // find a place where instance should be inserted,
        // insert it and claim a victory
        AVS_LIST(test_instance_t) *insert_ptr;
        AVS_LIST_FOREACH_PTR(insert_ptr, &repr->instances) {
            if ((*insert_ptr)->iid > new_instance->iid) {
                break;
            }
        }
        AVS_LIST_INSERT(insert_ptr, new_instance);
        return 0;
    }

.. note::
    There is a lot going on in this function, also new concepts regarding
    ``AVS_LIST`` are being used. We advise you to look at ``AVS_LIST_FOREACH_PTR``,
    ``AVS_LIST_NEW_ELEMENT`` and ``AVS_LIST_INSERT`` documentation for more details.

``instance_remove`` handler
---------------------------

``instance_remove`` handler does not have to perform anything other than
removing the instance from our list.

.. snippet-source:: examples/tutorial/AT-CustomObjects/multi-instance-dynamic/src/test_object.c

    static int test_instance_remove(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid) {
        (void) anjay; // unused
        test_object_t *repr = get_test_object(obj_ptr);

        AVS_LIST(test_instance_t) *it;
        AVS_LIST_FOREACH_PTR(it, &repr->instances) {
            if ((*it)->iid == iid) {
                AVS_LIST_DELETE(it);
                return 0;
            }
        }
        // should never happen as Anjay checks whether instance is present
        // prior to issuing instance_remove
        return ANJAY_ERR_INTERNAL;
    }

.. note::

    Complete code of this example can be found in
    `examples/tutorial/AT-CustomObjects/multi-instance-dynamic` subdirectory
    of main Anjay project repository.
