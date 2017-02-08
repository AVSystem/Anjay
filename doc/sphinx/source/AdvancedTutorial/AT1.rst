Attribute storage
=================

.. highlight:: c

The Write Attributes and Discover RPCs, as well as the Information Reporting
interface, use a concept of Attributes that may be set for Resources, Object
Instances or Objects.

.. seealso::
    :doc:`/LwM2M` chapter contains more information about LwM2M Attributes.


When the library needs to read or write these attributes, it calls
``resource_read_attrs`` or ``resource_write_attrs`` handlers, respectively, in
case of Resources, or appropriately ``instance_read_default_attrs``,
``instance_write_default_attrs``, ``object_read_default_attrs`` or
``object_write_default_attrs``, in case of Object Instances or Objects. Note
that the library will automatically call the "default" handlers if there are
some unset attributes on the more specific level.

``attr_storage`` module
-----------------------

As the cases described above are very common and generic -- and as such, usually
implemented in exactly the same manner for all objects in the code base, the
library includes a module that implements all the attribute-related handlers in
a generic and reusable way.

To use the Attribute Storage module, you first need to include the appropriate
header:

.. snippet-source:: examples/tutorial/AT2/src/main.c

    #include <anjay/attr_storage.h>


Then, in the main program logic, you need to initialize the attribute storage
object. Its life cycle is supposed to be very similar to the main ``anjay_t``
object.

So, we can modify part of our ``main()`` function to initialize and destroy
``anjay_attr_storage_t`` as well:

.. snippet-source:: examples/tutorial/AT2/src/main.c

    anjay_t *anjay = anjay_new(&CONFIG);

    // ...

    // Instantiate necessary objects
    const anjay_dm_object_def_t **security_obj = anjay_security_object_create();
    const anjay_dm_object_def_t **server_obj = anjay_server_object_create();

    anjay_attr_storage_t *attr_storage = anjay_attr_storage_new(anjay);

    // For some reason we were unable to instantiate objects.
    if (!security_obj || !server_obj || !attr_storage) {
        result = -1;
        goto cleanup;
    }

    // ...

    cleanup:
        anjay_delete(anjay);
        // ...
        anjay_attr_storage_delete(attr_storage);
        return result;

While this may seem counter-intuitive, it is actually the intended and preferred
flow to destroy ``attr_storage`` after ``anjay`` (i.e. the same order they were 
created). This is because when there are objects managed by the module, the
Anjay object might access them, even during de-initialization.

On the other hand, the attribute storage module will not access the Anjay
object during destruction. In fact, attribute storage module uses Anjay
object only if attribute :ref:`persistence <persistence>` is being used via
``anjay_attr_storage_persist`` or ``anjay_attr_storage_restore``.

.. _wrapping-objects:

Wrapping objects
^^^^^^^^^^^^^^^^

To actually make use of the ``attr_storage`` module, objects, as defined
according to the rules described in the previous chapter, need to be wrapped by
it.

For example, the following snippet:

.. snippet-source:: examples/tutorial/BT2/src/main.c

    if (anjay_register_object(anjay, security_obj)
            || anjay_register_object(anjay, server_obj)) {
        result = -1;
        goto cleanup;
    }

can be replaced with:

.. snippet-source:: examples/tutorial/AT2/src/main.c

    if (anjay_register_object(anjay, anjay_attr_storage_wrap_object(
                                             attr_storage, security_obj))
        || anjay_register_object(anjay, anjay_attr_storage_wrap_object(
                                                attr_storage, server_obj))) {
        result = -1;
        goto cleanup;
    }

The ``anjay_attr_storage_wrap_object()`` function will replace all the
unimplemented (set to ``NULL``) attribute-related handlers in the original
object with its own implementation. For a detailed description on how does
the handler replacement behave when only some of the handlers are implemented,
refer to the `documentation <../../api/attr__storage_8h.html>`_.

No additional actions are necessary. Any resources allocated for the wrapped
object will be freed during the call to ``anjay_attr_storage_delete()``.

.. _persistence:

Persistence
^^^^^^^^^^^

To facilitate storing attribute values between executions of the program, the
``attr_storage`` module contains a persistence code, that can be used to
serialize and deserialize all the stored attributes to some kind of external
memory.

These two functions can be used for this purpose:

.. snippet-source:: modules/attr_storage/include_public/anjay/attr_storage.h

    int anjay_attr_storage_persist(anjay_attr_storage_t *attr_storage,
                                   avs_stream_abstract_t *out_stream);

    int anjay_attr_storage_restore(anjay_attr_storage_t *attr_storage,
                                   avs_stream_abstract_t *in_stream);


The data are read or written to and from objects of the
``avs_stream_abstract_t`` type. Please refer to documentation of
`AVSystem Commons <https://github.com/AVSystem/avs_commons>`_ for information on
what it is and how to create one.

For the simple case, the ``avs_stream_file_`` family of functions may be useful.

.. note:: The persistence functions shall be called after registering all the
          LWM2M Objects in the Anjay object and fully loading the data model
          structure (i.e. instantiating all the Object Instances that are
          supposed to be instantiated). Otherwise, attributes stored for
          non-existent Objects or their Instances will be discarded.
