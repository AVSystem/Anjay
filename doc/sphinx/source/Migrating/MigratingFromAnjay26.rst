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

Migrating from Anjay 2.5.x or 2.6.x
===================================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Anjay 2.7 is a minor upgrade, but changes in certificate-based security support
in Anjay 2.7 required additional redesign in ``avs_commons`` cryptography
support libraries. While backwards compatibility should be maintained in most
practical usages, there are some changes which might prove to be breaking if
public-key cryptography APIs of ``avs_net`` have been used directly.

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

Compatibility features
""""""""""""""""""""""

The new ``avsystem/commons/avs_crypto_pki_compat.h`` header can be included,
which aliases all the symbols mentioned in this chapter to their old names.
