..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

File System Data Model
======================

.. contents:: :local:

General description
-------------------

File System Data Model feature is implemented in a separate application called
Svetovid that uses Anjay library for communication with a LwM2M server. Svetovid
runs on any Linux-based system what makes it ideal for quick prototyping and 
integration with existing infrastructure. File System Data Model is easy
to use. After installing Svetovid LwM2M objects can be represented as scripts 
on the file system using any programming language. This allows creating LwM2M 
objects without C/C++ programming knowledge. Additionally the File System Data
Model comes with a stub generator for Python and shell script to create a LwM2M
object template. 

Supported features
------------------

The following features are implemented:

* Mapping of directory on the file system to LwM2M Objects, Instances and Resources
* Handling of multi-instance objects
* FSDM key-value store for keeping volatile object state
* Object stub generator for Python and sh to get started easily

Technical documentation
-----------------------

Directory mapping
^^^^^^^^^^^^^^^^^

When enabled, File System Data Model maps a specific directory to LwM2M Objects,
Instances and Resources. Default mapped directory is ``/etc/svetovid/dm`` but it
can be changed in the ``/etc/svetovid/config/fsdm.json`` file. The mapped directory
is expected to have the following structure:

- ``/etc/svetovid/dm/`` (default)

  - ``$OBJECT_ID/`` - directory representing a LwM2M Object with given ID.

    - ``resources/`` - directory containing scripts used to access individual
      Resources.

      - ``$RESOURCE_ID`` - executable scripts representing individual Resource 
        of a given ID.

    - ``instances`` - (optional) executable script used for managing object
      instances.

    - ``transaction`` - (optional) executable script used to handle
      transactional processing of object resources.

Installing Svetovid
^^^^^^^^^^^^^^^^^^^
To install Svetovid from source and use File System Data Model run those commands:

.. code-block:: bash

    $ ./devconfig -DTARGET_PLATFORM=[TARGET]
    $ make
    $ sudo make install

.. note::
   Use "generic" as ``TARGET`` to compile Svetovid on a standard Linux distribution.  

.. note:: 
   Note that this installs a ``svetovid.service`` systemd service, automatically
   enabled, and starts-up the Client immediately. You may want to disable the 
   service using systemctl command.

Configuring Svetovid
^^^^^^^^^^^^^^^^^^^^
Svetovid is configured using JSONs files. The default location of those JSONs is 
``/etc/svetovid/config``. 

.. note::
   Default configuration directory may be overwritten by passing ``--conf-dir`` as
   a command line argument when starting a Svetovid binary. 

The configuration is stored in those files: 

* ``security.json`` - represents the Security LwM2M object
* ``server.json`` - represents the Server LwM2M object
* ``svd.json`` - global settings

Example of ``security.json``:

.. code-block:: json

    {
        "2": {
                "server_uri": "coaps://eu.iot.avsystem.cloud:5684",
                "security_mode": "psk",
                "pubkey_or_identity_hex": "6d7a2d737665746f766964",
                "privkey_or_psk_hex": "737665746f76696431323334",
                "ssid": "7",
                "is_bootstrap": "0"
        }
    }

Example of ``server.json``:

.. code-block:: json

    {
        "0": {
                "ssid": "7",
                "lifetime": "60",
                "binding": "U"
        }
    }

Example of ``svd.json``:

.. code-block:: json

    {
        "device": {
                "endpoint_name": "svetovid-example"
        },
        "logging": {
                "default_log_level": "info",
                "log_level": {
                    "svd": "debug"
                }
        },
        "in_buffer_size_b": 10240,
        "out_buffer_size_b": 10240,
        "msg_cache_size_b": 65536
    }

.. note:: 
   For detailed description of configuration files format please refer to the 
   full documentation.

Developing custom object example
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

After installation Svetovid is run as a service and can be controlled by
systemctl

To implement a Time Object (/3333) you can start by generating a stub

.. code-block:: bash

   $ sudo svetovid-fsdmtool generate --object 3333 --output-dir /etc/svetovid/dm --generator python

This will create ``/etc/svetovid/dm/3333`` directory containing Python scripts 
that represent LwM2M resources. The hierarchy of the filesystem mapped to a LwM2M 
object is:

.. code-block:: bash

    /etc/svetovid/dm/3333
    ├── Application_Type -> resources/5750
    ├── Current_Time -> resources/5506
    ├── Fractional_Time -> resources/5507
    ├── instances
    ├── Measurement_Quality_Indicator -> resources/6042
    ├── Measurement_Quality_Level -> resources/6049
    └── resources
        ├── 5506
        ├── 5507
        ├── 5750
        ├── 6042
        └── 6049

The scripts generated this way contain placeholders for the ``read``, ``write`` 
and ``reset`` functions that need to be filled out by the user. For example
the ``Fraction Time`` (/3333/\*/5507) resource can be implemented as:

.. code-block:: python

    #!/usr/bin/env python
    # -*- encoding: utf-8 -*-
    
    from fsdm import ResourceHandler, CoapError, DataType, KvStore
    
    
    class ResourceHandler_3333_5506(ResourceHandler):
        NAME = "Current Time"
        DESCRIPTION = '''\
    Unix Time. A signed integer representing the number of seconds since
     * Jan 1st, 1970 in the UTC time zone.'''
        DATATYPE = DataType.TIME
        EXTERNAL_NOTIFY = False
    
        def read(self,
                 instance_id,            # int
                 resource_instance_id):  # int for multiple resources, None otherwise
            # It's just that simple!
            import time
            sys.stdout.write(str(int(time.time())))
    
    
        def write(self,
                  instance_id,            # int
                  resource_instance_id):  # int for multiple resources, None otherwise
            # NOTE: Implement this if you want to be able to change time on your system.
            raise CoapError.NOT_IMPLEMENTED
    
        def reset(self,
                  instance_id):  # int
            # NOTE: reset resource to its original state. You can either set it to
            # a default value or delete the resource.
            pass
    
    
    
    if __name__ == '__main__':
        ResourceHandler_3333_5506().main()

To implement ``Application Type`` resource (/3333/\*/5750) we can use a simple
implementation of a key-value store which is accessible from Python via ``KvStore``
class. The class implements a simple interface:

* ``KvStore(namespace)`` - constructor that takes ``namespace`` as an argument.
  This can be set as anything as long as it can be uniquely distinguished between different
  objects. A good idea is to use an Object ID as a ``namespace`` used by the ``KvStore``.
* ``get(key, default=None)`` - method for getting a value for a given ``key``.
  If the value is not present it will return the ``default`` value.
* ``set(key, value)`` - method for setting a ``value`` for a given ``key``.
* ``delete(key)`` - method for deleting a ``key`` with associated value from
  the ``KvStore``.  


.. code-block:: python

    #!/usr/bin/env python
    # -*- encoding: utf-8 -*-

    from fsdm import ResourceHandler, CoapError, DataType, KvStore

    import sys # for sys.stdout.write() and sys.stdin.read()

    class ResourceHandler_3333_5750(ResourceHandler):
        NAME = "Application Type"
        DESCRIPTION = '''\
    The application type of the sensor or actuator as a string depending
     * on the use case.'''
        DATATYPE = DataType.STRING
        EXTERNAL_NOTIFY = False

        def read(self,
                 instance_id,            # int
                 resource_instance_id):  # int for multiple resources, None otherwise
            value = KvStore(namespace=3333).get('application_type')
            if value is None:
                # The value was not set, so it's not found.
                raise CoapError.NOT_FOUND

            # The value is present within the store, thus we can print it on stdout.
            # The important thing here is to remember to return string-typed resources
            # with sys.stdout.write(), as print() adds unnecessary newline character, so
            # if we used it instead, the value presented to the server would contain that
            # trailing newline character.
            sys.stdout.write(value)


        def write(self,
                  instance_id,            # int
                  resource_instance_id):  # int for multiple resources, None otherwise
            # All we need to do is to assign a value to the application_type key.
            KvStore(namespace=3333).set('application_type', sys.stdin.read())


        def reset(self,
                  instance_id):  # int
            # We reset the resource to its original state by simply deleting the application_type
            # key
            KvStore(namespace=3333).delete('application_type')



    if __name__ == '__main__':
        ResourceHandler_3333_5750().main()
