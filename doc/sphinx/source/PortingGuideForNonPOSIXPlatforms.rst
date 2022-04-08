..
   Copyright 2017-2022 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

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
