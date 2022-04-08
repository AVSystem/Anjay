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

Migrating from Anjay 2.5.x or 2.6.x
===================================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.6 are minor, improvements in security support
required some redesigns in ``avs_commons`` cryptography support libraries. While
backwards compatibility should be maintained in most practical usages, there are
some changes which might prove to be breaking if old APIs have been used
directly.

Additional slight updates might be necessary if you are using any alternative
build system instead of CMake to compile your project, of if you maintain your
own implementation of the socket layer.

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

Changes in Anjay proper
-----------------------

Change of security configuration lifetime
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* **Getter function for retrieving security information from data model**

  * **Old API:**
    ::

        anjay_security_config_t *anjay_security_config_from_dm(anjay_t *anjay,
                                                               const char *uri);
  * **New API:**

    .. snippet-source:: include_public/anjay/core.h

        int anjay_security_config_from_dm(anjay_t *anjay,
                                          anjay_security_config_t *out_config,
                                          const char *uri);

  * The security configuration is now returned through an output argument with
    any necessary internal buffers cached inside the Anjay object instead of
    using heap allocation. Please refer to the Doxygen-based documenation of
    this function for details.

    Due to the change in lifetime requirements, no compatibility variant is
    provided.

Changes in avs_commons
----------------------

Changes in public-key cryptography APIs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Client-side and server-side certificate info structures are no longer separate,
and both have been merged into a single type. Additionally, the client key info
structure have also been renamed for consistency.

Here is a summary of renames:

+--------------------------------------------------+-------------------------------------------------------+
| Old symbol name                                  | New Symbol name                                       |
+==================================================+=======================================================+
| | ``avs_crypto_trusted_cert_info_t``             | ``avs_crypto_certificate_chain_info_t``               |
| | ``avs_crypto_client_cert_info_t``              |                                                       |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_client_key_info_t``                 | ``avs_crypto_private_key_info_t``                     |
+--------------------------------------------------+-------------------------------------------------------+
| | ``avs_crypto_trusted_cert_info_from_file()``   | ``avs_crypto_certificate_chain_info_from_file()``     |
| | ``avs_crypto_client_cert_info_from_file()``    |                                                       |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_trusted_cert_info_from_path()``     | ``avs_crypto_certificate_chain_info_from_path()``     |
+--------------------------------------------------+-------------------------------------------------------+
| | ``avs_crypto_trusted_cert_info_from_buffer()`` | ``avs_crypto_certificate_chain_info_from_buffer()``   |
| | ``avs_crypto_client_cert_info_from_buffer()``  |                                                       |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_trusted_cert_info_from_array()``    | ``avs_crypto_certificate_chain_info_from_array()``    |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_trusted_cert_info_copy_as_array()`` | ``avs_crypto_certificate_chain_info_copy_as_array()`` |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_trusted_cert_info_from_list()``     | ``avs_crypto_certificate_chain_info_from_list()``     |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_trusted_cert_info_copy_as_list()``  | ``avs_crypto_certificate_chain_info_copy_as_list()``  |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_client_key_info_from_file()``       | ``avs_crypto_private_key_info_from_file()``           |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_client_key_info_from_buffer()``     | ``avs_crypto_private_key_info_from_buffer()``         |
+--------------------------------------------------+-------------------------------------------------------+
| ``avs_crypto_client_cert_expiration_date()``     | ``avs_crypto_certificate_expiration_date()``          |
+--------------------------------------------------+-------------------------------------------------------+

Renamed configuration macro in avs_commons_config.h
"""""""""""""""""""""""""""""""""""""""""""""""""""

The ``AVS_COMMONS_NET_WITH_PSK`` configuration macro in ``avs_commons_config.h``
has been renamed to ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK``.

You may need to update your configuration files if you are not using CMake, or
your preprocessor directives if you check this macro in your code.

Compatibility features
""""""""""""""""""""""

Because the changes are minor, attempts to improve backwards compatibility have
been taken, specifically:

* The new ``avsystem/commons/avs_net_pki_compat.h`` header can be included,
  which aliases all the symbols mentioned in this chapter to their old names.
* If the ``AVS_COMMONS_NET_WITH_PSK`` macro is defined (e.g. in a legacy
  ``avs_commons_config.h`` file), it is interpreted as equivalent to
  ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK``. A warning message is displayed in that
  case.

Refactor of PSK credential handling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``avs_net_security_info_t`` structure has been updated to use the new type,
``avs_net_generic_psk_info_t``, to encapsulate the PSK credentials. The new
type uses new types based on ``avs_crypto_security_info_union_t`` instead of
raw buffers.

* **Old API:**
  ::

      /**
       * A PSK/identity pair with borrowed pointers. avs_commons will never attempt
       * to modify these values.
       */
      typedef struct {
          const void *psk;
          size_t psk_size;
          const void *identity;
          size_t identity_size;
      } avs_net_psk_info_t;

      // ...

      typedef struct {
          avs_net_security_mode_t mode;
          union {
              avs_net_psk_info_t psk;
              avs_net_certificate_info_t cert;
          } data;
      } avs_net_security_info_t;

      avs_net_security_info_t avs_net_security_info_from_psk(avs_net_psk_info_t psk);

* **New API:**

  .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_psk.h

      typedef struct {
          avs_crypto_security_info_union_t desc;
      } avs_crypto_psk_identity_info_t;

      // ...

      avs_crypto_psk_identity_info_t
      avs_crypto_psk_identity_info_from_buffer(const void *buffer,
                                               size_t buffer_size);

      // ...

      typedef struct {
          avs_crypto_security_info_union_t desc;
      } avs_crypto_psk_key_info_t;

      // ...

      avs_crypto_psk_key_info_t
      avs_crypto_psk_key_info_from_buffer(const void *buffer, size_t buffer_size);

  .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

      /**
       * A PSK/identity pair. avs_commons will never attempt to modify these values.
       */
      typedef struct {
          avs_crypto_psk_key_info_t key;
          avs_crypto_psk_identity_info_t identity;
      } avs_net_generic_psk_info_t;

      // ...

      typedef struct {
          avs_net_security_mode_t mode;
          union {
              avs_net_generic_psk_info_t psk;
              avs_net_certificate_info_t cert;
          } data;
      } avs_net_security_info_t;

      avs_net_security_info_t
      avs_net_security_info_from_generic_psk(avs_net_generic_psk_info_t psk);

The old ``avs_net_psk_info_t`` type is still available for compatibility. The
``avs_crypto_psk_key_info_from_buffer()`` function has also been reimplemented
as a ``static inline`` function that wraps calls to
``avs_crypto_psk_identity_info_from_buffer()``,
``avs_crypto_psk_key_info_from_buffer()`` and
``avs_net_security_info_from_generic_psk()``.

However, code that accesses the ``data.psk`` field of
``avs_net_security_info_t`` directly will need to be updated.

Separation of avs_url module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

URL handling routines, previously a part of ``avs_net``, are now a separate
component of ``avs_commons``. The specific consequences of that may vary
depending on your build process, e.g.:

* You will need to add ``#define AVS_COMMONS_WITH_AVS_URL`` to your
  ``avs_commons_config.h`` if you specify it manually
* You may need to add ``-lavs_url`` to your link command if you're using
  ``avs_commons`` that has been manually compiled separately using CMake

Refactor of avs_net_validate_ip_address() and avs_net_local_address_for_target_host()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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
