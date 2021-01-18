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

Migrating from Anjay 2.7.x
==========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.7 are minor, the advancements in HSM
integration in ``avs_commons`` required some breaking changes in they way
compile-time configuration of that library is performed.

**No manual changes** (aside from possibly upgrading CMake) **should be
necessary if you are using CMake to compile your project and using the default
POSIX socket integration.** If you are using any alternative build system, you
might need to make adjustments to your configuration headers. If you maintain
your own socket integration, you might need to make slight adjustments to your
code.

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

Separation of avs_url module
----------------------------

URL handling routines, previously a part of ``avs_net``, are now a separate
component of ``avs_commons``. The specific consequences of that may vary
depending on your build process, e.g.:

* You will need to add ``#define AVS_COMMONS_WITH_AVS_URL`` to your
  ``avs_commons_config.h`` if you specify it manually
* You may need to add ``-lavs_url`` to your link command if you're using
  ``avs_commons`` that has been manually compiled separately using CMake

Refactor of avs_net_validate_ip_address() and avs_net_local_address_for_target_host()
-------------------------------------------------------------------------------------

``avs_net_validate_ip_address()`` is now no longer used by Anjay or
``avs_commons``. It was previously necessary to implement it as part of the
socket implementation. This is no longer required. For compatibility, the
function has been reimplemented as a ``static inline`` function that wraps
``avs_net_addrinfo_*()`` APIs. Please remove your version of
``avs_net_validate_ip_address()`` from your socket implementation if you have
one, as having two alternative variants may lead to conflicts.

Since Anjay 2.9 and ``avs_commons`` 4.6,
``avs_net_local_address_for_target_host()`` underwent a similar refactor. It was
previously a function to be optionally implemented as part of the socket
implementation, but now it is a ``static inline`` function that wraps
``avs_net_socket_*()`` APIs. Please remove your version of
``avs_net_local_address_for_target_host()`` from your socket implementation if
you have one, as having two alternative variants may lead to conflicts.

Reorganization of HSM support
-----------------------------

.. note::

    Low-level HSM support is available in open-source ``avs_commons``, but
    integration of these features with Anjay is only available in the commercial
    version.

Coupling of the Hardware Security Module support in ``avs_commons`` has been
loosened, making it possible to replace the reference implementation based on
``libp11`` with a custom one.

* New CMake configuration flag ``WITH_AVS_CRYPTO_ENGINE``, and its corresponding
  configuration header macro ``AVS_COMMONS_WITH_AVS_CRYPTO_ENGINE`` have been
  added.
* Enabling the aforementioned flag is now a dependency for enabling
  ``WITH_OPENSSL_PKCS11_ENGINE`` (CMake) /
  ``AVS_COMMONS_WITH_OPENSSL_PKCS11_ENGINE`` (header)
