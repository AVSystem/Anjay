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

Migrating from Anjay 2.9.x-2.14.x
=================================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.14 are minor, upgrade to ``avs_commons`` 4.10
includes refactoring of the APIs related to (D)TLS PSK credentials.

The API remains compatible for most common use cases. However, you may need to
adjust your code if it accesses the ``avs_net_security_info_t`` structure
directly. This is especially likely if you maintain your own implementation of
the TLS layer.

Renamed configuration macro in avs_commons_config.h
---------------------------------------------------

The ``AVS_COMMONS_NET_WITH_PSK`` configuration macro in ``avs_commons_config.h``
has been renamed to ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK``.

You may need to update your configuration files if you are not using CMake, or
your preprocessor directives if you check this macro in your code.

To improve backwards compatibility, if the ``AVS_COMMONS_NET_WITH_PSK`` macro is
defined (e.g. in a legacy ``avs_commons_config.h`` file), it is interpreted as
equivalent to ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK``. A warning message is
displayed in that case.

Refactor of PSK credential handling
-----------------------------------

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

Additional function in the hardware security engine API
-------------------------------------------------------

A new API has been added to the hardware security engine API in ``avs_commons``:

.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_pki.h

    avs_error_t
    avs_crypto_pki_engine_key_store(const char *query,
                                    const avs_crypto_private_key_info_t *key_info,
                                    avs_crypto_prng_ctx_t *prng_ctx);

If you use the commercial version of Anjay and implement your own hardware
security engine backend implementation, you may need to provide an
implementation of this function.

This new API is used by the Security object implementation's features related
to the ``anjay_security_object_install_with_hsm()``. If you don't use these
features to store private keys in the hardware security engine, it is OK to
provide a dummy implementation such as ``return avs_errno(AVS_ENOTSUP);``.
