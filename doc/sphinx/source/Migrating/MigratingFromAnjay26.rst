..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Migrating from Anjay 2.5.x or 2.6.x
===================================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.6 are minor, some of them (changes to commonly
used APIs such as Attribute Storage and offline mode control) are breaking.
Additionally, ``avs_commons`` cryptography support libraries have undergone a
significant redesign and there are some changes which might prove to be breaking
if old APIs have been used directly.


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
    using heap allocation. Please refer to the Doxygen-based documentation of
    this function for details.

    Due to the change in lifetime requirements, no compatibility variant is
    provided.

Refactor of the Attribute Storage module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Attribute Storage feature is no longer a standalone module and has been
moved to the library core. From the user perspective, this has the following
consequences:

* Explicit installation of this module in runtime is no longer necessary. The
  ``anjay_attr_storage_install()`` method has been removed.
* The ``ANJAY_WITH_MODULE_ATTR_STORAGE`` configuration macro in
  ``anjay_config.h`` has been renamed to ``ANJAY_WITH_ATTR_STORAGE``.
* The ``WITH_MODULE_attr_storage`` CMake option (equivalent to the macro
  mentioned above) has been renamed to ``WITH_ATTR_STORAGE``.

Additionally, the behavior of ``anjay_attr_storage_restore()`` has been
changed - from now on, this function fails if supplied source stream is
invalid and the Attribute Storage remains untouched. This change makes the
function consistent with other ``anjay_*_restore()`` APIs.

Refactor of offline mode control API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since Anjay 2.4, offline mode is configurable independently per every
transport. Below is a list of removed functions and counterparts that should
be used:

+--------------------------------+------------------------------------------+
| Removed function               | Counterpart                              |
+--------------------------------+------------------------------------------+
| ``anjay_is_offline()``         | ``anjay_transport_is_offline()``         |
+--------------------------------+------------------------------------------+
| ``anjay_enter_offline()``      | ``anjay_transport_enter_offline()``      |
+--------------------------------+------------------------------------------+
| ``anjay_exit_offline()``       | ``anjay_transport_exit_offline()``       |
+--------------------------------+------------------------------------------+
| ``anjay_schedule_reconnect()`` | ``anjay_transport_schedule_reconnect()`` |
+--------------------------------+------------------------------------------+

New functions should be called with ``transport_set`` argument set to
``ANJAY_TRANSPORT_SET_ALL`` to achieve the same behavior.

Addition of the con attribute to public API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``con`` attribute, enabled via the ``ANJAY_WITH_CON_ATTR`` compile-time
option, has been previously supported as a custom extension. Since an identical
flag has been standardized as part of LwM2M TS 1.2, it has been included in the
public API as part of preparations to support the new protocol version.

If you initialize ``anjay_dm_oi_attributes_t`` or ``anjay_dm_r_attributes_t``
objects manually, you may need to initialize the new ``con`` field as well,
since the empty ``ANJAY_DM_CON_ATTR_NONE`` value is **NOT** the default
zero-initialized value.

As more new attributes may be added in future versions of Anjay, it is
recommended to initialize such structures with ``ANJAY_DM_OI_ATTRIBUTES_EMPTY``
or ``ANJAY_DM_R_ATTRIBUTES_EMPTY`` constants, and then fill in the attributes
you actually intend to set.

Default (D)TLS version
^^^^^^^^^^^^^^^^^^^^^^

When the `anjay_configuration_t::dtls_version
<../api/structanjay__configuration.html#ab32477e7370a36e02db5b7e7ccbdd89d>`_
field is set to ``AVS_NET_SSL_VERSION_DEFAULT`` (which includes the case of
zero-initialization), Anjay 3.0 and earlier automatically mapped this setting to
``AVS_NET_SSL_VERSION_TLSv1_2`` to ensure that (D)TLS 1.2 is used as mandated by
the LwM2M specification.

This mapping has been removed in Anjay 3.1, which means that the default version
configuration of the underlying (D)TLS library will be used. This has been done
to automatically allow the use of newer protocols and deprecate old versions
when the backend library is updated, without the need to update Anjay code.
However, depending on the (D)TLS backend library used, this may lead to (D)TLS
1.1 or earlier being used if the server does not properly negotiate a higher
version. Please explicitly set ``dtls_version`` to
``AVS_NET_SSL_VERSION_TLSv1_2`` if you want to disallow this.

Please note that Mbed TLS 3.0 has dropped support for TLS 1.1 and earlier, so
this change will not affect behavior with that library.


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

Introduction of new socket option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

avs_commons 4.10.1 bundled with Anjay 2.15.1 adds a new socket option key:
``AVS_NET_SOCKET_HAS_BUFFERED_DATA``. This is used to make sure that when
control is returned to the event loop, the ``poll()`` call will not stall
waiting for new data that in reality has been already buffered and could be
retrieved using the avs_commons APIs.

This is usually meaningful for (D)TLS connections, but for almost all simple
unencrypted socket implementations, this should always return ``false``.

This was previously achieved by always trying to receive more packets with
timeout set to zero. However, it has been determined that such logic could lead
to heavy blocking of the event loop in case communication with the network stack
is relatively slow, e.g. on devices which implement TCP/IP sockets through modem
AT commands.

If you maintain your own socket integration layer or (D)TLS integration layer,
it is recommended that you add support for this option. This is not, however, a
breaking change - if the option is not supported, the library will continue to
use the old behavior.

Refactor of PSK credential handling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_psk_info_t`` structure has been changed to use new types based on
``avs_crypto_security_info_union_t`` instead of raw buffers. This change also
affects ``avs_net_security_info_t`` structure which contains the former.

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
      } avs_net_psk_info_t;

      // ...

      typedef struct {
          avs_net_security_mode_t mode;
          union {
              avs_net_psk_info_t psk;
              avs_net_certificate_info_t cert;
          } data;
      } avs_net_security_info_t;

      avs_net_security_info_t
      avs_net_security_info_from_psk(avs_net_psk_info_t psk);

This change is breaking for code that accesses the ``data.psk`` field
of ``avs_net_security_info_t`` directly.

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
