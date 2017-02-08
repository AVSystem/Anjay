Multi-instance writable object with dynamic number of instances
===============================================================

In this tutorial you will learn:

- how to implement ``instance_create`` handler to create Instances,

- how to implement ``instance_remove`` handler to remove Instances,

- how to assign instance identifiers to a newly created Instances,

- basics of the ``AVS_LIST`` utility library.

Implemented object will be roughly based on :doc:`BT_CO3_FixedInstanceWritable`.

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

The code is based on :doc:`previous tutorial <BT_CO3_FixedInstanceWritable>`, yet in this
chapter all Test object related code was moved to a separate files to keep
everything clean.

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
.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    typedef struct test_object {
        // handlers
        const anjay_dm_object_def_t *obj_def;

        // object state
        AVS_LIST(test_instance_t) instances;

        AVS_LIST(test_instance_t) backup_instances;
    } test_object_t;

.. note::
    ``AVS_LIST(x)`` is actually a macro that expands to a pointer of type ``x``. It
    has therefore a pointer semantics and can be treated like that (standard dereferencing
    and dereferencing with assignment will work as expected). This is however, not everything
    you need to know about them, as they are more complicated than that. We recommend
    you to refer to the `documentation <TODO LINK>`_.

In a previous tutorial, our Instances had hardcoded Instance IDs.  We no
longer have such comfort, and have to be able to uniquely identify Object
Instances. As a consequence, we will add ``anjay_iid_t iid`` field to 
``test_instance_t``:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

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
       able to register and/or wrap it), unless some additional public API is
       provided.

To achieve proper control over object lifetime and initialization, we are
going to introduce two functions, namely ``create_test_object``:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    const anjay_dm_object_def_t **create_test_object() {
        test_object_t *repr = (test_object_t *) calloc(1, sizeof(test_object_t));
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

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    void delete_test_object(const anjay_dm_object_def_t **obj) {
        if (!obj) {
            return;
        }
        test_object_t *repr = get_test_object(obj);
        AVS_LIST_CLEAR(&repr->instances);
        AVS_LIST_CLEAR(&repr->backup_instances);
        free(repr);
    }

As you can see, dynamic memory management is semi-automatically handled by ``AVS_LIST``.

.. note::
    ``AVS_LIST_CLEAR`` iterates over the list, in the process frees memory allocated for
    each element, and in the end sets list handle to ``NULL``.

Using functions defined above to create, register and free the Test object is similar
as in previous tutorials.

Updating old, already implemented handlers to use ``AVS_LIST``
--------------------------------------------------------------

To simplify matters, we have to agree upon one contract:

#. We establish a natural Instace ordering on their Instance IDs, exploiting the
   fact they MUST be unique.

#. We store Instaces of the Test object in an ordered (as above) list.

OK, now that we made our assumptions, we are ready to implement utility function
``get_instance`` which retrieves Test object instance with specified Instance ID.
It will happen to be very useful in the next couple of subsections:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    static AVS_LIST(test_instance_t)
    get_instance(test_object_t *repr, anjay_iid_t iid) {
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

Let's reimplement ``test_instance_present`` in terms of just defined ``get_instance``:

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    static int test_instance_present(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t iid) {
        (void) anjay;   // unused

        return get_instance(get_test_object(obj_ptr), iid) != NULL;
    }

And one more method, presenting another functionality of ``AVS_LISTs``
before going into details of ``instance_create`` and ``instance_remove``
and leaving the rest of the work of this kind as an exercise for the reader
(or, if they are lazy, they can always look at the code):

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    static int test_instance_it(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t *out,
                                void **cookie) {
        (void) anjay;   // unused

        AVS_LIST(test_instance_t) curr = NULL;

        // if `*cookie == NULL`, then the iteration has just started,
        // otherwise `*cookie` contains iterator value saved below
        if (*cookie) {
            curr = (AVS_LIST(test_instance_t)) *cookie;
            // get the next element
            curr = AVS_LIST_NEXT(curr);
        } else {
            // first instance is also a list head
            curr = get_test_object(obj_ptr)->instances;
        }

        if (curr) {
            *out = curr->iid;
        } else {
            // when last element is reached curr is NULL
            *out = ANJAY_IID_INVALID;
        }

        // use `*cookie` to store the iterator
        *cookie = (void *) curr;
        return 0;
    }

That's all for this section. As noted above, implementation of other methods
is as always available in the source code provided with the tutorial. We
do however strongly recommend you to port the methods to use ``AVS_LISTs``
on your own, especially **remember about updating transaction handlers**.

Assigning Instance IDs
----------------------

LwM2M Create requests are not always equipped with preferred Instance ID, forcing
the LwM2M Client to assign Instance ID by itself. In Anjay, this responsibility
lies on the implementor of the Object being instantiated.

We are going to take an easy approach for Instance ID assignation. Our algorithm
will simply traverse Object Instance list, looking for any gaps in Instance IDs
between consecutive Instances. If no gap is found, we'd take an upper bound of
discovered Instance IDs during the iteration as a new Instance ID.

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    static int assign_new_iid(test_object_t *repr, anjay_iid_t *out_iid) {
        anjay_iid_t preferred_iid = 0;
        AVS_LIST(test_instance_t) it;
        AVS_LIST_FOREACH(it, repr->instances) {
            if (it->iid == preferred_iid) {
                ++preferred_iid;
            } else if (it->iid > preferred_iid) {
                // found a hole
                break;
            }
        }

        // all valid Instance IDs are already reserved
        if (preferred_iid == ANJAY_IID_INVALID) {
            return -1;
        }
        *out_iid = preferred_iid;
        return 0;
    }

Our ``assign_new_iid`` is indeed simple, yet its pessimistic complexity is
`O(n)` (where `n` stands for the number of Object Instances). This means
that special care must be taken if good performance of instance creation
is required (e.g. by using some kind of hash-map or tree-map for Instance
storage).

``instance_create`` handler
---------------------------

Let's have a look on ``anjay_dm_instance_create_t`` handler type signature:

.. snippet-source:: include_public/anjay/anjay.h

    typedef int anjay_dm_instance_create_t(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t *inout_iid,
                                           anjay_ssid_t ssid);

The ``inout_iid`` parameter is the most important for us at the moment, as
if the instantiation succeeds we MUST tell the library the id of the newly
created Instance by setting ``*inout_iid`` properly.

As we previously discussed, LwM2M Create requests do not necessairly have
to contain preferred Instance ID. However if they do, then Anjay first makes
sure no Object Instance with given Instance ID exists.

To sum up, we are left with the situation when either ``*inout_iid`` is a
valid Instance ID and we should use it, or it is unset, in which case we
are going to assign Instance ID ourselves:

.. note:: Unset Instance ID is represented by ``ANJAY_IID_INVALID`` constant.

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

    static int test_instance_create(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t *inout_iid,
                                    anjay_ssid_t ssid) {
        (void) anjay; // unused
        (void) ssid; // unused

        test_object_t *repr = get_test_object(obj_ptr);

        if (*inout_iid == ANJAY_IID_INVALID) {
            // Create request did not contain preferred Instance ID,
            // therefore we assign one on our own if possible
            if (assign_new_iid(repr, inout_iid)) {
                // unfortunately assigning new iid failed, nothing
                // we can do about it
                return -1;
            }
        }

        AVS_LIST(test_instance_t) new_instance =
                AVS_LIST_NEW_ELEMENT(test_instance_t);

        if (!new_instance) {
            // out of memory
            return ANJAY_ERR_INTERNAL;
        }

        new_instance->iid = *inout_iid;

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

Fortunately ``instance_remove`` handler is much easier to implement as it does not
have to perform anything other than removing the instance from our list.

.. snippet-source:: examples/tutorial/custom-object/multi-instance-dynamic/src/test_object.c

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

