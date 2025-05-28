..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Attribute storage
=================

.. highlight:: c

The Write Attributes and Discover operations, as well as the Information
Reporting interface, use a concept of Attributes that may be set for Resources,
Object Instances or Objects.

.. seealso::
    :doc:`/LwM2M` chapter contains more information about LwM2M Attributes.


When the library needs to read or write these attributes, it calls
``resource_read_attrs`` or ``resource_write_attrs`` handlers, respectively, in
case of Resources, or appropriately ``instance_read_default_attrs``,
``instance_write_default_attrs``, ``object_read_default_attrs`` or
``object_write_default_attrs``, in case of Object Instances or Objects. Note
that the library will automatically call the "default" handlers if there are
some unset attributes on the more specific level.

Pre-implemented attribute storage subsystem
-------------------------------------------

As the cases described above are very common and generic -- and as such, usually
implemented in exactly the same manner for all objects in the code base, the
library includes a subsystem that implements all the attribute-related handlers
in a generic and reusable way.

To use the Attribute Storage subsystem, make sure that the library is compiled
with it enabled, which means that:

* If building using CMake: the ``WITH_ATTR_STORAGE`` CMake option needs to be
  enabled, e.g. by specifying ``-DWITH_ATTR_STORAGE``. Note that it is enabled
  by default.
* If using an alternative build system: the ``ANJAY_WITH_ATTR_STORAGE`` macro
  needs to be defined in the ``anjay_config.h`` file. Note that this is true for
  all the sample configurations from the ``example_configs`` directory.

No additional steps are necessary - the Attribute Storage subsystem will provide
implementations of attribute-related handlers in the original objects with its
own implementation, unless some handlers are actually already implemented (set
to anything else than ``NULL``). For a detailed description on how does the
handler replacement behave when only some of the handlers are implemented, refer
to the `documentation <../api/attr__storage_8h.html>`_.

.. _persistence:

Persistence
^^^^^^^^^^^

To facilitate storing attribute values between executions of the program, the
Attribute Storage subsystem contains a persistence code, that can be used to
serialize and deserialize all the stored attributes to some kind of external
memory.

To use the persistence code, you first need to include the appropriate header:

.. code-block:: c

    #include <anjay/attr_storage.h>

It contains declarations of the two functions that can be used to manage the
persistent information:

.. snippet-source:: include_public/anjay/attr_storage.h

    avs_error_t anjay_attr_storage_persist(anjay_t *anjay,
                                           avs_stream_t *out_stream);

.. snippet-source:: include_public/anjay/attr_storage.h

    avs_error_t anjay_attr_storage_restore(anjay_t *anjay, avs_stream_t *in_stream);


The data are read or written to and from objects of the
``avs_stream_t`` type. Please refer to documentation of
`AVSystem Commons <https://github.com/AVSystem/avs_commons>`_ for information on
what it is and how to create one.

For the simple case, the ``avs_stream_file_`` family of functions may be useful.

.. note:: The persistence functions shall be called after registering all the
          LwM2M Objects in the Anjay object and fully loading the data model
          structure (i.e. instantiating all the Object Instances that are
          supposed to be instantiated). Otherwise, attributes stored for
          non-existent Objects or their Instances will be discarded.
