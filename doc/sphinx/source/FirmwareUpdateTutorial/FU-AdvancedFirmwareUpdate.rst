..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Advanced Firmware Update
========================

**Advanced Firmware Update** object (``/33629``) is an optional object which
extends the definition of **Firmware Update** object (``/5``) and allows for
multiple instances, with each instance representing a separate “component” of
the device's firmware that can be upgraded independently. The significance of
such components is implementation-defined, however, the intention is that they
might refer to components such as: bootloaders, application code, cellular
modem firmwares, security processor firmwares, etc.

It is expected that firmware components can be upgraded independently in most
cases, however, the object provides a mechanism for checking version
dependencies when a certain order of updates is required, or when multiple
components need to be upgraded in tandem.


:download:`Download: Advanced Firmware Update Object Definition XML <FU-AdvancedFirmwareUpdate/_files/33629.xml>`

.. toctree::
   :glob:
   :titlesonly:

   FU-AdvancedFirmwareUpdate/FU-AFU-ResourceDefinitions.rst
   FU-AdvancedFirmwareUpdate/FU-AFU-StateDiagram.rst
   FU-AdvancedFirmwareUpdate/FU-AFU-Examples.rst
   FU-AdvancedFirmwareUpdate/FU-AFU-BasicImplementation.rst
