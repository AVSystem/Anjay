..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Migrating from Anjay 2.8.x
==========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.8 are minor, some of them (changes to commonly
used APIs such as Attribute Storage and offline mode control) are breaking.
There is a change to the way the ``con`` attribute is handled in the API.
Additionally, the upgrade to ``avs_commons`` 5.0 includes refactoring of the
APIs related to (D)TLS PSK credentials and further refinements in the network
integration layer.


You may also need to adjust your code if you maintain your own socket
integration, or if it accesses the ``avs_net_security_info_t`` structure
directly. The latter is especially likely if you maintain your own
implementation of the TLS layer.

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

Conditional compilation for structured security credential support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``anjay_ret_certificate_chain_info()`` and ``anjay_ret_private_key_info()``
APIs, as well as avs_crypto-based fields in ``anjay_security_instance_t``, have
been put under a new conditional compilation flag,
``ANJAY_WITH_SECURITY_STRUCTURED``.

When using CMake, this flag is controlled with the ``WITH_SECURITY_STRUCTURED``
option and enabled by default if available. Otherwise, it might need to be
enabled by defining ``ANJAY_WITH_SECURITY_STRUCTURED`` in ``anjay_config.h``.


Changes in avs_commons
----------------------

Renamed CMake configuration options
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``WITH_AVS_CRYPTO_ENGINE`` CMake configuration option has been removed; the
new equivalent option is ``WITH_AVS_CRYPTO_PKI_ENGINE``. Please update CMake
invocations in your configuration scripts.

Renamed configuration macro in avs_commons_config.h
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following configuration macros in ``avs_commons_config.h`` has been renamed.
You may need to update your configuration files if you are not using CMake, or
your preprocessor directives if you check these macros in your code:

+----------------------------------------+--------------------------------------------+
| Old macro name                         | New macro name                             |
+========================================+============================================+
| ``AVS_COMMONS_WITH_AVS_CRYPTO_ENGINE`` | ``AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE`` |
+----------------------------------------+--------------------------------------------+
| ``AVS_COMMONS_NET_WITH_PSK``           | ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK``        |
+----------------------------------------+--------------------------------------------+

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

Refactor of avs_net_local_address_for_target_host()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_local_address_for_target_host()`` has never been used by Anjay or any
other part of ``avs_commons``. However, it was previously a function to be
optionally implemented as part of the socket implementation. It has now been
reimplemented as a ``static inline`` function that wraps
``avs_net_socket_*()`` APIs. Please remove your version of
``avs_net_local_address_for_target_host()`` from your socket implementation if
you have one, as having two alternative variants may lead to conflicts.

Additional function in the hardware security engine API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A new API has been added to the hardware security engine API in ``avs_commons``:

.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_pki.h

    avs_error_t
    avs_crypto_pki_engine_key_store(const char *query,
                                    const avs_crypto_private_key_info_t *key_info,
                                    avs_crypto_prng_ctx_t *prng_ctx);

If you implement your own hardware security engine backend implementation, you may
need to provide an implementation of this function.

This new API is used by the Security object implementation's features related
to the ``anjay_security_object_install_with_hsm()``. If you don't use these
features to store private keys in the hardware security engine, it is OK to
provide a dummy implementation such as ``return avs_errno(AVS_ENOTSUP);``.
