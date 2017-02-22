..
   Copyright 2017 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Using Anjay with embedded UDP/IP stacks
=======================================

LwIP
----

Enabling LwIP support
^^^^^^^^^^^^^^^^^^^^^

When implementing an Anjay-powered LwM2M client, it is possible to use LwIP as a lightweight UDP/IP stack. In order to achieve that, pass ``-DWITH_LWIP=ON`` option as ``AVS_COMMONS_ADDITIONAL_CMAKE_OPTIONS`` when configuring Anjay with CMake, like so:

.. code-block:: cmake

   cmake -DAVS_COMMONS_ADDITIONAL_CMAKE_OPTIONS="-DWITH_LWIP=ON" .


See also: :doc:`Compiling_client_applications`.


Required LwIP compile-time options
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you have Anjay libraries compiled with LwIP support, the only other thing you need to do is make sure ``lwipopts.h`` defines following macros:

.. code-block:: c

    // Enable UDP support
    #define LWIP_UDP 1

    // Provide POSIX socket API, used by Anjay
    #define LWIP_SOCKET 1

    // Disable macros redefining socket operations to "lwip_" prefixed variants
    #define LWIP_COMPAT_SOCKETS 0

