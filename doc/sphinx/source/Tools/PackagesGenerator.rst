..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Package generator
=================

This section describes the ``create_package.py`` script used to generate test packages for
LwM2M objects implemented in the Anjay demo. These packages simulate firmware and software
updates with configurable behaviors and error conditions.

Overview
--------

The Anjay demo implements several LwM2M objects to facilitate firmware and software management.
These objects include:

* object 5 — Firmware Update
* object 9 — Software Management
* object 33629 — Advanced Firmware Update (AFU)

.. note::

   These objects are implemented primarily for testing purposes and are not production-ready. To
   generate compatible update packages for testing, use the create_package.py script located in
   the tests/integration/framework directory. If you need help implementing the required callbacks
   for Objects /5 and /33629, refer to :doc:`Firmware Update tutorial <../FirmwareUpdateTutorial>`.

In order to use the objects, you need packages that contain the appropriate metadata. To generate
these packages, run ``create_package.py`` script located in the ``tests/integration/framework``
directory.

Example usage
-------------

To generate an example package:

1. Run one of the following commands:

.. tabs::

   .. tab:: Package for the object 5 that will be executed during update

      .. code-block:: bash

         python ./tests/integration/framework/create_package.py -o ./package -m ANJAY_FW

      .. note::

         If persistence is enabled, then if the package execution is successful, upon restarting
         the demo (which may occur during an update if the package includes the demo), the resource
         /5/0/5 will be set to the value corresponding to the state `Firmware updated successfully`.

   .. tab:: Package for the object 9 that will be executed through the system's shell interpreter during installation

      .. code-block:: bash

         python ./tests/integration/framework/create_package.py -o ./package -m ANJAY_SW 

   .. tab:: Package for the object 9 that will fail during validation

      .. code-block:: bash

         python ./tests/integration/framework/create_package.py -o ./package -m ANJAY_SW -c 1234

   .. tab:: Package for the object 9 that will fail during installation

      .. code-block:: bash

         python ./tests/integration/framework/create_package.py -o ./package -m ANJAY_SW -e FailedInstall

   .. tab:: Package for the object 9 that will fail during activation

      .. code-block:: bash

         python ./tests/integration/framework/create_package.py -o ./package -m ANJAY_SW -e FailureInPerformActivate

   .. tab:: Package for the zero instance of object 33629 that will be executed during update

      .. code-block:: bash

            python ./tests/integration/framework/create_package.py -o ./package -m ANJAY_APP

      .. note::

            If persistence is enabled and persistence file is set by ``--afu-marker-path`` argument,
            then if the package execution is successful, upon restarting the demo (which may occur
            during an update if the package includes the demo), the resource /33629/0/5 will be set
            to the value corresponding to the state `Firmware updated successfully`.

2. Provide package content, e.g.:

.. code-block:: bash

    #!/bin/sh
    echo installed

Press ``Ctrl+D`` (`EOF`) to complete input.

3. After that, package should exists:

.. code-block:: bash
   
    cat ./package | xxd -p 
    414e4a41595f5357000200000813e38003312e3023212f62696e2f73680a
    6563686f20696e7374616c6c65640a

Package generation arguments
----------------------------

To obtain information about each of the available arguments, display the help message by running
the following command:

.. code-block:: bash

    python tests/integration/framework/create_package.py --help

Manipulating the behavior of objects
------------------------------------

The behavior of the upgrade/install process in the Demo application can be manipulated using
metadata embedded in the package header. You can trigger specific errors during image generation
by passing the ``--error`` argument to the ``create_package.py`` script.
Likewise, the ``--crc`` and ``--magic`` arguments can be used to trigger an error during
package validation.

Errors description
------------------


Firmware Update and Advanced Firmware Update errors
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::
    In the case of AFU, this description is only valid for the zero instance 
    (magic == AJAY_APP). For other instances, the behavior may differ.
   
+----------------------------+---------------------------------------------------------------+
| Error name                 | Behavior                                                      |
+============================+===============================================================+
| NoError                    | The demo will replace the current process with a new one by   |
|                            | executing the downloaded package during update                | 
|                            | (``perform_upgrade`` callback). It will pass along the        |
|                            | arguments specified when running the demo itself.             |
+----------------------------+---------------------------------------------------------------+
| OutOfMemory                | The firmware update will fail after the package is            |
|                            | downloaded, during its validation in the ``stream_finish``    |
|                            | callback, by returning ANJAY_FW_UPDATE_ERR_OUT_OF_MEMORY.     |
+----------------------------+---------------------------------------------------------------+
| FailedUpdate               | The firmware update will fail during update                   |
|                            | (``perform_upgrade`` callback) by returning -1.               |
+----------------------------+---------------------------------------------------------------+
| DelayedSuccess             | The demo will replace the current process with a new one by   |
|                            | executing the downloaded package during update                |
|                            | (``perform_upgrade`` callback). It will pass along the        |
|                            | arguments specified when running the demo, plus an argument   |
|                            | that causes the result to be set to success — provided that   |
|                            | the package contains a demo capable of interpreting this      |
|                            | argument accordingly.                                         |
+----------------------------+---------------------------------------------------------------+
| DelayedFailedUpdate        | The demo will replace the current process with a new one by   |
|                            | executing the downloaded package during update                |
|                            | (``perform_upgrade`` callback). It will pass along the        |
|                            | arguments specified when running the demo, plus an argument   |
|                            | that causes the result to be set to `Firmware update failed`  |
|                            | — provided that the package contains a demo capable of        |
|                            | interpreting this argument accordingly.                       |
+----------------------------+---------------------------------------------------------------+
| SetSuccessInPerformUpgrade | The demo will set the result to success by using the          |
|                            | ``anjay_fw_update_set_result`` (or                            |
|                            | ``anjay_advanced_fw_update_set_state_and_result`` in case of  |
|                            | AFU) function during update (``perform_upgrade`` callback).   |
+----------------------------+---------------------------------------------------------------+
| SetFailureInPerformUpgrade | The demo will set the result to `Firmware update failed` by   |
|                            | using the ``anjay_fw_update_set_result`` (or                  |
|                            | ``anjay_advanced_fw_update_set_state_and_result`` in case of  |
|                            | AFU) function during update (``perform_upgrade`` callback).   |
+----------------------------+---------------------------------------------------------------+
| DoNothing                  | The demo will return 0 in the ``perform_upgrade`` callback.   |
+----------------------------+---------------------------------------------------------------+
| Defer                      | The demo will set the result to deferred by using the         |
|                            | ``anjay_fw_update_set_result`` function during updating       |
|                            | (or return ``ANJAY_ADVANCED_FW_UPDATE_ERR_DEFERRED`` in case  |
|                            | of AFU).                                                      |
+----------------------------+---------------------------------------------------------------+

Software Management errors
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::
   To enable the Software Management object, define ``ANJAY_WITH_MODULE_SW_MGMT`` e.g. by running
   CMake or ``./devconfig`` with ``-DANJAY_WITH_MODULE_SW_MGMT=ON`` argument.

+---------------------------------+----------------------------------------------------------+
| Error name                      | Behavior                                                 |
+=================================+==========================================================+
| NoError                         | The demo will create a child process during installation |
|                                 | (``pkg_install`` callback) by executing the downloaded   |
|                                 | package through the system's shell interpreter and will  |
|                                 | wait until the end of its execution. The result will be  |
|                                 | set to success and activation state to `Disable` by      |
|                                 | using ``anjay_sw_mgmt_finish_pkg_install`` function.     |
+---------------------------------+----------------------------------------------------------+
| FailedInstall                   | The software installation will fail during installation  |
|                                 | (``pkg_install`` callback) by returning -1.              |
+---------------------------------+----------------------------------------------------------+
| DelayedSuccessInstall           | The demo will replace the current process with a new one | 
|                                 | by executing the downloaded package during installation  |
|                                 | (``pkg_install`` callback). It will pass along the       |
|                                 | arguments specified when running the demo, plus an       |
|                                 | argument that causes the result to be set to success —   |
|                                 | provided that the package contains a demo capable        |
|                                 | of interpreting this argument accordingly.               |
+---------------------------------+----------------------------------------------------------+
| DelayedFailedInstall            | The demo will replace the current process with a new one | 
|                                 | by executing the downloaded package during installation  |
|                                 | (``pkg_install`` callback). It will pass along the       |
|                                 | arguments specified when running the demo, plus an       |   
|                                 | argument that causes the result to be set to `Software   |
|                                 | installation failure` — provided that the package        |
|                                 | contains a demo capable of interpreting this argument    |
|                                 | accordingly.                                             |
+---------------------------------+----------------------------------------------------------+
| SuccessInPerformInstall         | The demo will set the result to success by using the     |
|                                 | ``anjay_sw_mgmt_finish_pkg_install`` function during     |
|                                 | installation (``pkg_install`` callback).                 |
+---------------------------------+----------------------------------------------------------+
| SuccessInPerformInstallActivate | The demo will set the result to success and activation   |
|                                 | state to `Enable` by using the                           |
|                                 | ``anjay_sw_mgmt_finish_pkg_install`` function during     |
|                                 | installation (``pkg_install`` callback).                 |
+---------------------------------+----------------------------------------------------------+
| FailureInPerformInstall         | The demo will set the result to `Software installation   |
|                                 | failure` by using the                                    |
|                                 | ``anjay_sw_mgmt_finish_pkg_install`` function during     |
|                                 | installation (``pkg_install`` callback).                 |
+---------------------------------+----------------------------------------------------------+
| FailureInPerformUninstall       | The software uninstallation will fail in the             |
|                                 | ``pkg_uninstall`` callback by returning -1.              |
+---------------------------------+----------------------------------------------------------+
| FailureInPerformActivate        | The software activation will fail in the                 |
|                                 | ``activate`` callback by returning -1.                   |
+---------------------------------+----------------------------------------------------------+
| FailureInPerformDeactivate      | The software deactivation will fail in the               |
|                                 | ``deactivate`` callback by returning -1.                 |
+---------------------------------+----------------------------------------------------------+
| FailureInPerformPrepareForUpdate| The software update preparation will fail in the         |
|                                 | ``prepare_for_update`` callback by returning -1.         |
+---------------------------------+----------------------------------------------------------+
| DoNothing                       | The demo will return 0 in the ``pkg_install``            |
|                                 | callback.                                                |
+---------------------------------+----------------------------------------------------------+

Package version
---------------

You can set the package version for all objects by using the ``--version`` argument.
However, the version of the Firmware Update and Software Management package must be set to
`1.0` (this is due to how these objects have been implemented in the demo and is not a
limitation of the Anjay library itself). In the case of Advanced Firmware Update, the
version can be anything as long as it is shorter than 24 characters.

Removing persistence files
--------------------------

Each object can store its state in a persistence file.  To ensure you’re working with an object
in its initial state, remove the persistence file. By default, these files are located in the
following paths:

* Firmware Update — ``/tmp/anjay-fw-updated``
* Software Management — ``/tmp/anjay-sw-mgmt``
* Advanced Firmware Update — by default there is no persistence file

Additionally, you can set the persistence file location using the following arguments:

* Firmware Update — ``--fw-updated-marker-path``
* Software Management — ``--sw-mgmt-persistence-file``
* Advanced Firmware Update — ``--afu-marker-path``
