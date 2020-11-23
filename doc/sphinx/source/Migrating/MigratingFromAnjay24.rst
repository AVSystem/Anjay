..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Migrating from Anjay 2.3.x or 2.4.x
===================================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Anjay 2.5 through 2.8 are minor upgrades, but EST support in commercial version
of Anjay 2.5 and changes in certificate-based security support in Anjay 2.7 and
2.8 required significant redesign in ``avs_commons`` cryptography support
libraries. While backwards compatibility should be maintained in most practical
usages, there are some changes which might prove to be breaking if public-key
cryptography APIs of ``avs_net`` have been used directly.

Additional slight updates might be necessary if you are using any alternative
build system instead of CMake to compile your project, of if you maintain your
own implementation of the socket layer.

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
                                          const char *raw_url);

  * The security configuration is now returned through an output argument with
    any necessary internal buffers cached inside the Anjay object instead of
    using heap allocation. Please refer to the Doxygen-based documenation of
    this function for details.

    Due to the change in lifetime requirements, no compatibility variant is
    provided.

Changes in avs_commons
----------------------

Move of public-key cryptography APIs from avs_net to avs_crypto
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Public key cryptography APIs, previously defined in
``avsystem/commons/avs_socket.h``, have been moved into a new header called
``avsystem/commons/avs_crypto_pki.h``.

Additionally, client-side and server-side certificate info structures are no
longer separate, and both have been merged into a single type.

Here is a summary of renames:

+-----------------------------------------------+-----------------------------------------------------+
| Old symbol name                               | New symbol name                                     |
+===============================================+=====================================================+
| | ``avs_net_trusted_cert_info_t``             | ``avs_crypto_certificate_chain_info_t``             |
| | ``avs_net_client_cert_info_t``              |                                                     |
+-----------------------------------------------+-----------------------------------------------------+
| ``avs_net_client_key_info_t``                 | ``avs_crypto_private_key_info_t``                   |
+-----------------------------------------------+-----------------------------------------------------+
| ``avs_net_security_info_union_t``             | ``avs_crypto_security_info_union_t``                |
+-----------------------------------------------+-----------------------------------------------------+
| | ``avs_net_trusted_cert_info_from_buffer()`` | ``avs_crypto_certificate_chain_info_from_buffer()`` |
| | ``avs_net_client_cert_info_from_buffer()``  |                                                     |
+-----------------------------------------------+-----------------------------------------------------+
| | ``avs_net_trusted_cert_info_from_file()``   | ``avs_crypto_certificate_chain_info_from_file()``   |
| | ``avs_net_client_cert_info_from_file()``    |                                                     |
+-----------------------------------------------+-----------------------------------------------------+
| ``avs_net_client_key_info_from_buffer()``     | ``avs_crypto_private_key_info_from_buffer()``       |
+-----------------------------------------------+-----------------------------------------------------+
| ``avs_net_client_key_info_from_file()``       | ``avs_crypto_private_key_info_from_file()``         |
+-----------------------------------------------+-----------------------------------------------------+
| ``avs_net_trusted_cert_info_from_path()``     | ``avs_crypto_certificate_chain_info_from_path()``   |
+-----------------------------------------------+-----------------------------------------------------+

Renamed CMake configuration options
"""""""""""""""""""""""""""""""""""

The ``WITH_X509`` CMake configuration option is now deprecated; the new
equivalent option is ``WITH_PKI``. Please update CMake invocations in your
configuration scripts.

Renamed configuration macros in avs_commons_config.h
""""""""""""""""""""""""""""""""""""""""""""""""""""

The following configuration macros in ``avs_commons_config.h`` has been renamed.
You may need to update your configuration files if you are not using CMake, or
your preprocessor directives if you check these macros in your code:

+-----------------------------------+------------------------------------------+
| Old macro name                    | New macro name                           |
+===================================+==========================================+
| ``AVS_COMMONS_NET_WITH_X509``     | ``AVS_COMMONS_WITH_AVS_CRYPTO_PKI``      |
+-----------------------------------+------------------------------------------+
| ``AVS_COMMONS_NET_WITH_VALGRIND`` | ``AVS_COMMONS_WITH_AVS_CRYPTO_VALGRIND`` |
+-----------------------------------+------------------------------------------+

Compatibility features
""""""""""""""""""""""

Because the changes are minor, attempts to improve backwards compatibility have
been taken, specifically:

* The new ``avsystem/commons/avs_net_pki_compat.h`` header can be included,
  which aliases all the symbols mentioned in this chapter to their old names.
* If ``WITH_X509`` CMake variable is manually defined (e.g. by the ``-D``
  command-line option), the ``WITH_PKI`` variable is automatically set
  accordingly. A warning message is displayed in that case.
* If the ``AVS_COMMONS_NET_WITH_X509`` macro is defined (e.g. in a legacy
  ``avs_commons_config.h`` file), it is interpreted as equivalent to
  ``AVS_COMMONS_WITH_AVS_CRYPTO_PKI``, and additionally causes the
  aforementioned ``avsystem/commons/avs_net_pki_compat.h`` header to be included
  from ``avsystem/commons/avs_socket.h``. A warning message is displayed in that
  case.

Separation of avs_url module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

URL handling routines, previously a part of ``avs_net``, are now a separate
component of ``avs_commons``. The specific consequences of that may vary
depending on your build process, e.g.:

* You will need to add ``#define AVS_COMMONS_WITH_AVS_URL`` to your
  ``avs_commons_config.h`` if you specify it manually
* You may need to add ``-lavs_url`` to your link command if you're using
  ``avs_commons`` that has been manually compiled separately using CMake

Refactor of avs_net_validate_ip_address()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_validate_ip_address()`` is now no longer used by Anjay or
``avs_commons``. It was previously necessary to implement it as part of the
socket implementation. This is no longer required, and in fact, keeping that
implementation might lead to problems - for compatibility, the function has been
reimplemented as a ``static inline`` function that wraps
``avs_net_addrinfo_*()`` APIs.
