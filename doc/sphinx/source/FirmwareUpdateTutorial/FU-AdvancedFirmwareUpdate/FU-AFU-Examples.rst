..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Examples
========

.. contents:: :local:

Example client data model - initial state
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Lwm2M Firmware update Object - Application component [/33629/0]
***************************************************************

This component governs the package for the main application firmware that
implements the core device functionality.

.. flat-table::
   :header-rows: 1
   :widths: 50 20 25 20 35

   * - Resource Name
     - Resource ID
     - Resource Instance ID
     - Value
     - Notes
   * - Package
     - 0
     -
     -
     -
   * - Package URI
     - 1
     -
     -
     -
   * - State
     - 3
     -
     - 0
     - Idle
   * - Update Result
     - 5
     -
     - 0
     - Initial value
   * - :rspan:`3` Firmware Update Protocol Support
     - :rspan:`3` 8
     - 0
     - 0
     - CoAP
   * - 1
     - 1
     - CoAPs
   * - 2
     - 2
     - HTTP
   * - 3
     - 3
     - HTTPS
   * - Firmware Update Delivery Method
     - 9
     -
     - 2
     - Both push and pull
   * - Component Name
     - 14
     -
     - Application
     -
   * - Current version
     - 15
     -
     - 1.0
     -
   * - Linked Instances
     - 16
     -
     -
     -
   * - Conflicting Instances
     - 17
     -
     -
     -

Lwm2M Firmware update Object - Trusted firmware component [/33629/1]
********************************************************************

This component governs the TEE (Trusted Execution Environment) firmware package.

.. flat-table::
   :header-rows: 1
   :widths: 50 20 25 20 35

   * - Resource Name
     - Resource ID
     - Resource Instance ID
     - Value
     - Notes
   * - Package
     - 0
     -
     -
     -
   * - Package URI
     - 1
     -
     -
     -
   * - State
     - 3
     -
     - 0
     - Idle
   * - Update Result
     - 5
     -
     - 0
     - Initial value
   * - :rspan:`3` Firmware Update Protocol Support
     - :rspan:`3` 8
     - 0
     - 0
     - CoAP
   * - 1
     - 1
     - CoAPs
   * - 2
     - 2
     - HTTP
   * - 3
     - 3
     - HTTPS
   * - Firmware Update Delivery Method
     - 9
     -
     - 2
     - Both push and pull
   * - Component Name
     - 14
     -
     - TEE
     -
   * - Current version
     - 15
     -
     - 1.1
     -
   * - Linked Instances
     - 16
     -
     -
     -
   * - Conflicting Instances
     - 17
     -
     -
     -

Lwm2M Firmware update Object - Bootloader [/33629/2]
****************************************************

This component governs the package for the device's bootloader.

.. flat-table::
   :header-rows: 1
   :widths: 50 20 25 20 35

   * - Resource Name
     - Resource ID
     - Resource Instance ID
     - Value
     - Notes
   * - Package
     - 0
     -
     -
     -
   * - Package URI
     - 1
     -
     -
     -
   * - State
     - 3
     -
     - 0
     - Idle
   * - Update Result
     - 5
     -
     - 0
     - Initial value
   * - :rspan:`3` Firmware Update Protocol Support
     - :rspan:`3` 8
     - 0
     - 0
     - CoAP
   * - 1
     - 1
     - CoAPs
   * - 2
     - 2
     - HTTP
   * - 3
     - 3
     - HTTPS
   * - Firmware Update Delivery Method
     - 9
     -
     - 2
     - Both push and pull
   * - Component Name
     - 14
     -
     - Bootloader
     -
   * - Current version
     - 15
     -
     - 2.1
     -
   * - Linked Instances
     - 16
     -
     -
     -
   * - Conflicting Instances
     - 17
     -
     -
     -

Lwm2M Firmware update Object - Modem [/33629/3]
***********************************************

This component governs the firmware for the cellular module that the device
uses for radio communication. The module may be located on a separate IC
package from the main processor or microcontroller.

.. flat-table::
   :header-rows: 1
   :widths: 50 20 25 20 35

   * - Resource Name
     - Resource ID
     - Resource Instance ID
     - Value
     - Notes
   * - Package
     - 0
     -
     -
     -
   * - Package URI
     - 1
     -
     -
     -
   * - State
     - 3
     -
     - 0
     - Idle
   * - Update Result
     - 5
     -
     - 0
     - Initial value
   * - :rspan:`3` Firmware Update Protocol Support
     - :rspan:`3` 8
     - 0
     - 0
     - CoAP
   * - 1
     - 1
     - CoAPs
   * - 2
     - 2
     - HTTP
   * - 3
     - 3
     - HTTPS
   * - Firmware Update Delivery Method
     - 9
     -
     - 2
     - Both push and pull
   * - Component Name
     - 14
     -
     - Modem
     -
   * - Current version
     - 15
     -
     - 22.01
     -
   * - Linked Instances
     - 16
     -
     -
     -
   * - Conflicting Instances
     - 17
     -
     -
     -

Example upgrade scenarios
^^^^^^^^^^^^^^^^^^^^^^^^^

Version conflict
****************

Let's assume that the Application firmware version 2.0 requires the TEE
firmware to also be updated to version 2.0 or later.

#. The LwM2M Server writes ``http://example.com/app_2.0.bin`` to resource
   ``/33629/0/1`` (Package URI of the Application component)
#. The instance enters the Downloading state, downloads the package, and enters
   the Downloaded state
#. Resource ``/33629/0/17`` (Conflicting instances) changes the value to: ``[0]
   = 33629:1``; this informs the server that instance ``/33629/1`` requires
   attention due to dependency conflict
#. If the server nevertheless issues Execute on ``/33629/0/2``, the device
   immediately reverts to the Downloaded state, with Update Result set to 13
   (Dependency error)
#. The LwM2M Server then writes ``http://example.com/tee_2.0.bin`` to resource
   ``/33629/1/1`` (Package URI of the TEE component)
#. The instance enters the Downloading state, downloads the package, and enters
   the Downloaded state
#. Resource ``/33629/0/17`` (Conflicting instances) changes value back to empty.
#. Resource ``/33629/0/16`` (Linked instances) changes the value to: ``[0] =
   33629:1``; resource ``/33629/1/16`` (Linked instances) changes the value to:
   ``[0] = 33629:0``; this signifies that by default, the device will upgrade
   both when the Update operation is executed on either of the instances
#. The LwM2M Server issues Execute on ``/33629/0/2``
#. The device enters Updating state, performs an update of both components,
   reboots, and reconnects to the server
#. The State on both ``/33629/0`` and ``/33629/1`` instances read 0 (idle),
   Update Result on both is 1 (success), and the version numbers have been
   updated so that ``/33629/0/15`` = 2.0 and ``/33629/0/16`` = 2.0

Multi-component package
***********************

#. The LwM2M Server writes ``http://example.com/app_tee_2.1.zip`` to
   ``/33629/0/1`` (Package URI of the Application component)
#. Instance 0 enters the Downloading state, downloads the package, and the
   device logic detects that the file contains packages for both the Application
   and TEE components
#. Both instance 0 and instance 1 enter the Downloaded state

Conflicting downloads
*********************

#. The LwM2M Server writes ``http://example.com/tee_2.0.bin`` to ``/33629/1/1``
   (Package URI of the TEE component)
#. Instance 1 enters the Downloading state, downloads the package, and enters
   the Downloaded state
#. The LwM2M Server writes ``http://example.com/app_tee_2.1.zip`` to
   ``33629/0/1`` (Package URI of the Application component)
#. Instance 0 enters the Downloading state, downloads at least some part of the
   package, and then the device logic detects that the file contains packages
   for both the Application and TEE components
#. The device aborts the download; instance 0 reverts to the Idle state, with
   Update Result set to 12 (Conflicting state); instance 1 remains in the
   Downloaded state, with the 2.0 package in memory.
#. Resource ``/33629/0/17`` changes the value to: ``[0] = 33629:1``; this serves
   as the detail for the “Conflicting state” error, informing on which instances
   were conflicting

Implicit and explicit linked updates
************************************

#. The LwM2M server writes the following values:
    #. ``http://example.com/app_2.0.bin`` to resource ``/33629/0/1`` (Package
       URI of the Application component)
    #. ``http://example.com/tee_2.0.bin`` to resource ``/33629/1/1`` (Package
       URI of the TEE component)
    #. ``http://example.com/boot_2.2.bin`` to resource ``/33629/2/1`` (Package
       URI of the Bootloader component)
    #. ``http://example.com/modem_22.02.bin`` to resource ``/33629/3/1``
       (Package URI of the Modem component)
#. The device downloads the packages, eventually all the instances enter the
   Downloaded state
#. Resource ``/33629/0/16`` (Linked instances) changes the value to: ``[0] =
   33629:1``; resource ``/33629/1/16`` (Linked instances) changes the value to:
   ``[0] = 33629:0``; this signifies that by default, the device will upgrade
   instances 0 and 1 in one go when the Update operation is executed on either
   of them
#. Either of the sub-scenarios described below follows

Implicit linked update
**********************

#. The LwM2M server issues Execute with no arguments on ``/33629/0/2`` or
   ``/33629/1/2``
#. The device enters Updating state, performs update of the Application and TEE
   components, reboots, and reconnects to the server
#. The State on both ``/33629/0`` and ``/33629/1`` instances read 0 (idle),
   Update Result on both is 1 (success), and the version numbers have been
   updated
#. Instances ``/33629/2`` and ``/33629/3`` remain in the Downloaded state if
   possible

Explicit linked update
**********************

#. The LwM2M server issues Execute on ``/33629/0/2`` with argument payload:
   ``“0='</33629/1>,</33629/2>,</33629/3>'”``
#. The device enters Updating state, performs an update of all four components,
   reboots, and reconnects to the server
#. The State on all instances read 0 (idle), Update Result on all of them is 1
   (success), and the version numbers have been updated

Explicit single component update
********************************

#. The LwM2M server issues Execute on ``/33629/1/2`` with argument payload:
   ``“0”``
#. Instance 1 enters Updating state, the device performs update of only the TEE
   component (ignoring the Linked Instances), and reconnects to the server
#. The State on instance ``/33629/1`` reads 0 (idle), corresponding Update
   Result is 1 (success), and the version number has been updated
#. Instances ``/33629/0``, ``/33629/2`` and ``/33629/3`` remain in the
   Downloaded state if possible

Explicit single component update - unsuccessful
***********************************************

#. The LwM2M server issues Execute on ``/33629/0/2`` with argument payload:
   ``“0”``
#. Instance 0 immediately reverts to the Downloaded state, with Update Result
   set to 13 (Dependency error), because application version 2.0 requires the
   TEE component to be updated to version 2.0 first or at the same time
#. Resource ``/33629/0/17`` changes value to: ``[0] = 33629:1``; this serves as
   the detail for the “Dependency error” result, informing on which instances
   were conflicting
