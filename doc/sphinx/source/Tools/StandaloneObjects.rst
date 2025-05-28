..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Standalone LwM2M Object implementations
=======================================

As described in :doc:`../BasicClient/BC-MandatoryObjects`, Anjay contains
implementations of some LwM2M Core Objects, including the LwM2M Security (0) and
LwM2M Server (1) objects.

These implementations should be appropriate for most users, but some users might
have specific requirements that deviate from the default Anjay behavior. For
this reason, standalone versions of the Security and Server objects are provided
in the ``standalone`` directory of the repository (or commercial distribution
package).

.. warning::

    Customizing the logic of Core Objects is likely to violate the LwM2M
    technical specification. Please proceed with care.

Using the standalone objects
----------------------------

To use the standalone objects:

* Copy the ``standalone/security`` and/or ``standalone/server`` directories into
  your project, and make sure that all the ``*.c`` files are compiled.

* The ``standalone_security.h`` and ``standalone_server.h`` files mirror the
  public header files of the default implementations. Please include them in
  your application code to use the object implementations.

* Make sure to account for the following differences between the default and
  standalone implementations:

  * Prefix for public APIs (including public function and type names) is changed
    from ``anjay_`` to ``standalone_``

  * The install functions (i.e., ``standalone_security_object_install()``,
    ``standalone_security_object_install_with_hsm()`` and
    ``standalone_server_object_install()``), unlike their default versions,
    return ``const anjay_dm_object_def_t **`` pointers. Please store this value
    during installation, as it needs to be passed for further API calls.
    However, you **do not** need to call `anjay_register_object()
    <../api/dm_8h.html#a1468b47fa9169474920c8c86d533b991>`_ as the install
    functions already call it.

  * All other public APIs take the aforementioned
    ``const anjay_dm_object_def_t *const *`` pointer instead of the `anjay_t
    <../api/core_8h.html#a6c9664a3b0c2d5629c9639dce7b1dbfb>`_ object. Adjust the
    calls accordingly.

  * Unlike the default implementation, the standalone objects are **not
    automatically cleaned up** at the time of `anjay_delete()
    <../api/core_8h.html#a243f18f976bca57b5a7b0714bfb99095>`_. If your code ever
    cleans up the Anjay object, please make sure to call
    ``standalone_security_object_cleanup()`` and/or
    ``standalone_server_object_cleanup()`` afterwards.

This will replicate the functionality of the default implementations. You can
apply any modifications you need from there.

.. note::

    Even though these implementations are standalone, they still contain
    conditional compilation directives that refer to Anjay configuration
    options, including those related to Security and Server objects, e.g.
    ``ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT``. If you wish to disable the
    modules completely, please update the code accordingly.

Limitations
-----------

The standalone implementations have the same limitations as application code -
they cannot access internal or private Anjay APIs. For this reason, the "OSCORE
Security Mode" Resource in the Security object (/0/\*/17) is not validated, as
the code does not have access to the OSCORE object implementation.

Also please note that when upgrading Anjay, you will be responsible for porting
any fixes and improvements that may be made to the Security and Server object
implementations between releases.
