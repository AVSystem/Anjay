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

.. code-block:: c

    #include <anjay/attr_storage.h>


Then, in the main program logic, you need to initialize the attribute storage
module by calling ``anjay_attr_storage_install()``. After installation, its life
cycle is automatically managed - it will be removed during the
``anjay_delete()`` call.

No additional steps are necessary - the Attribute Storage module will take over
the implementations of attribute-related handlers in the original objects with
its own implementation, unless some handlers are actually already implemented
(set to anything else than ``NULL``). For a detailed description on how does the
handler replacement behave when only some of the handlers are implemented, refer
to the `documentation <../api/attr__storage_8h.html>`_.

.. _persistence:

Persistence
^^^^^^^^^^^

To facilitate storing attribute values between executions of the program, the
``attr_storage`` module contains a persistence code, that can be used to
serialize and deserialize all the stored attributes to some kind of external
memory.

These two functions can be used for this purpose:

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
