..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Porting guide for non-POSIX platforms
=====================================

By default, Anjay makes use of POSIX-specific interfaces for retrieving time
and handling network traffic. If no such interfaces are provided by the
toolchain, the user needs to provide custom implementations.

The articles below show additional information about the specific functions that
need to be implemented.

.. toctree::
   :titlesonly:

   PortingGuideForNonPOSIXPlatforms/TimeAPI
   PortingGuideForNonPOSIXPlatforms/ThreadingAPI
   PortingGuideForNonPOSIXPlatforms/NetworkingAPI
   PortingGuideForNonPOSIXPlatforms/CustomTLS
