..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Migrating from Anjay 2.8.x
==========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.8 are minor, upgrade to ``avs_commons`` 4.6
includes further refinements in the network integration layer. **No manual
changes** (aside from possibly upgrading CMake) **should be necessary if you are
the default POSIX socket integration.** If you maintain your own socket
integration, you might need to make slight adjustments to your code.

Change to minimum CMake version
-------------------------------

Declared minimum CMake version necessary for CMake-based compilation, as well as
for importing the installed library through ``find_package()``, is now 3.6. If
you're using some Linux distribution that only has an older version in its
repositories (notably, Ubuntu 16.04), we recommend using one of the following
install methods instead:

* `Kitware APT Repository for Debian and Ubuntu <https://apt.kitware.com/>`_
* `Snap Store <https://snapcraft.io/cmake>`_ (``snap install cmake``)
* `Python Package Index <https://pypi.org/project/cmake/>`_
  (``pip install cmake``)

This change does not affect users who compile the library using some alternative
approach, without using the provided CMake scripts.

Refactor of avs_net_local_address_for_target_host()
---------------------------------------------------

``avs_net_local_address_for_target_host()`` has never been used by Anjay or any
other part of ``avs_commons``. However, it was previously a function to be
optionally implemented as part of the socket implementation. It has now been
reimplemented as a ``static inline`` function that wraps
``avs_net_socket_*()`` APIs. Please remove your version of
``avs_net_local_address_for_target_host()`` from your socket implementation if
you have one, as having two alternative variants may lead to conflicts.
