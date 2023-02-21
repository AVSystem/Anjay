..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Migrating from Anjay 2.2.5
==========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Most changes since Anjay 2.2.5 are minor, so no major changes in code flow are
required when porting from Anjay 2.2. However, there has been a significant
refactor in project structure, so some changes may be breaking for some users.

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

Removal of ssize_t usages from Anjay APIs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Most changes in Anjay proper relate to the removal of usages of the
POSIX-specific ``size_t`` type in Anjay and all related projects. See also
:ref:`ssize-t-removal-in-commons-225`.

Here is a summary of the relevant API changes:

* **Execute argument value getter**

  - **Old API:**
    ::

        ssize_t anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                            char *out_buf,
                                            size_t buf_size);

  - **New API:**

    .. snippet-source:: include_public/anjay/io.h
       :emphasize-lines: 1-2

        int anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                        size_t *out_bytes_read,
                                        char *out_buf,
                                        size_t buf_size);

  - Return value semantics have been aligned with those of
    ``anjay_get_string()`` - 0 is returned for success, negative value for
    error, or ``ANJAY_BUFFER_TOO_SHORT`` (1) if the buffer was too small.

    Length of the extracted data, which is equivalent to the old return value
    semantics, can be retrieved using the new ``out_bytes_read`` argument, but
    it can also be ``NULL`` if that is not necessary.


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

The ``con`` attribute, enabled via the ``WITH_CON_ATTR`` CMake option, has been
previously supported as a custom extension. Since an identical flag has been
standardized as part of LwM2M TS 1.2, it has been included in the public API as
part of preparations to support the new protocol version.

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


Other changes
^^^^^^^^^^^^^

* Declaration of ``anjay_smsdrv_cleanup()`` has been moved from ``anjay/core.h``
  to ``anjay/sms.h`` in versions that include the SMS commercial feature. It has
  been removed altogether from versions that do not support SMS.
* The following compile-time constants have been removed. None of them have been
  actually used in Anjay 2.x:

  * ``MAX_FLOAT_STRING_SIZE``
  * ``MAX_OBSERVABLE_RESOURCE_SIZE``

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


Changes in avs_coap
-------------------

If you are using ``avs_coap`` APIs directly (e.g. when communicating over raw
CoAP protocol), please note that following breaking changes in the ``avs_coap``
component:

* In line with Anjay and ``avs_commons``, to improve file name uniqueness, the
  ``avsystem/coap/config.h`` file has been renamed to
  ``avsystem/coap/avs_coap_config.h``.

*  Moreover, context creation functions now take an explicit PRNG context
   argument:

   * **UDP context creation**

     - **Old API:**
       ::

           avs_coap_ctx_t *
           avs_coap_udp_ctx_create(avs_sched_t *sched,
                                   const avs_coap_udp_tx_params_t *udp_tx_params,
                                   avs_shared_buffer_t *in_buffer,
                                   avs_shared_buffer_t *out_buffer,
                                   avs_coap_udp_response_cache_t *cache);

     - **New API:**

       .. snippet-source:: deps/avs_coap/include_public/avsystem/coap/udp.h
         :emphasize-lines: 7

           avs_coap_ctx_t *
           avs_coap_udp_ctx_create(avs_sched_t *sched,
                                   const avs_coap_udp_tx_params_t *udp_tx_params,
                                   avs_shared_buffer_t *in_buffer,
                                   avs_shared_buffer_t *out_buffer,
                                   avs_coap_udp_response_cache_t *cache,
                                   avs_crypto_prng_ctx_t *prng_ctx);

   * **TCP context creation**

     - **Old API:**
       ::

           avs_coap_ctx_t *avs_coap_tcp_ctx_create(avs_sched_t *sched,
                                                   avs_shared_buffer_t *in_buffer,
                                                   avs_shared_buffer_t *out_buffer,
                                                   size_t max_opts_size,
                                                   avs_time_duration_t request_timeout);

     - **New API:**

       .. snippet-source:: deps/avs_coap/include_public/avsystem/coap/tcp.h
         :emphasize-lines: 6

           avs_coap_ctx_t *avs_coap_tcp_ctx_create(avs_sched_t *sched,
                                                   avs_shared_buffer_t *in_buffer,
                                                   avs_shared_buffer_t *out_buffer,
                                                   size_t max_opts_size,
                                                   avs_time_duration_t request_timeout,
                                                   avs_crypto_prng_ctx_t *prng_ctx);

.. note ::

    It is now **mandatory** to pass a non-NULL value as the ``prng_ctx``
    argument to the functions above.

Changes in avs_commons
----------------------

``avs_commons`` 4.1 and later contain a number of breaking changes compared to
version 4.0 used by Anjay 2.2. If you are using any of the ``avs_commons`` APIs
directly (which is especially likely for e.g. the logging API and querying
sockets in the event loop), you will need to adjust your code.

avs_commons header rename
^^^^^^^^^^^^^^^^^^^^^^^^^

All headers of the ``avs_commons`` component have been renamed to make their
names more unique. Please adjust your ``#include`` directives accordingly.

The general rename patterns are:

* ``avsystem/commons/*.h`` → ``avsystem/commons/avs_*.h``
* ``avsystem/commons/stream/*.h``, ``avsystem/commons/stream/stream_*.h`` →
  ``avsystem/commons/avs_stream_*.h``
* ``avsystem/commons/unit/*.h`` → ``avsystem/commons/avs_unit_*.h``

Below is a detailed list of all renamed files:

+------------------------------------------------+-------------------------------------------------+
| Old header file                                | New header file                                 |
+================================================+=================================================+
| ``avsystem/commons/addrinfo.h``                | ``avsystem/commons/avs_addrinfo.h``             |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/aead.h``                    | ``avsystem/commons/avs_aead.h``                 |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/base64.h``                  | ``avsystem/commons/avs_base64.h``               |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/buffer.h``                  | ``avsystem/commons/avs_buffer.h``               |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/cleanup.h``                 | ``avsystem/commons/avs_cleanup.h``              |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/condvar.h``                 | ``avsystem/commons/avs_condvar.h``              |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/defs.h``                    | ``avsystem/commons/avs_defs.h``                 |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/errno.h``                   | ``avsystem/commons/avs_errno.h``                |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/errno_map.h``               | ``avsystem/commons/avs_errno_map.h``            |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/hkdf.h``                    | ``avsystem/commons/avs_hkdf.h``                 |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/http.h``                    | ``avsystem/commons/avs_http.h``                 |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/init_once.h``               | ``avsystem/commons/avs_init_once.h``            |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/list.h``                    | ``avsystem/commons/avs_list.h``                 |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/log.h``                     | ``avsystem/commons/avs_log.h``                  |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/memory.h``                  | ``avsystem/commons/avs_memory.h``               |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/mutex.h``                   | ``avsystem/commons/avs_mutex.h``                |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/net.h``                     | ``avsystem/commons/avs_net.h``                  |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/persistence.h``             | ``avsystem/commons/avs_persistence.h``          |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/rbtree.h``                  | ``avsystem/commons/avs_rbtree.h``               |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/sched.h``                   | ``avsystem/commons/avs_sched.h``                |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/shared_buffer.h``           | ``avsystem/commons/avs_shared_buffer.h``        |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/socket.h``                  | | ``avsystem/commons/avs_socket.h``             |
|                                                | | ``avsystem/commons/avs_crypto_pki.h`` [#pki]_ |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/socket_v_table.h``          | ``avsystem/commons/avs_socket_v_table.h``       |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream.h``                  | ``avsystem/commons/avs_stream.h``               |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_buffered.h``  | ``avsystem/commons/avs_stream_buffered.h``      |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_file.h``      | ``avsystem/commons/avs_stream_file.h``          |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_inbuf.h``     | ``avsystem/commons/avs_stream_inbuf.h``         |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/md5.h``              | ``avsystem/commons/avs_stream_md5.h``           |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_membuf.h``    | ``avsystem/commons/avs_stream_membuf.h``        |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_net.h``       | ``avsystem/commons/avs_stream_net.h``           |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/netbuf.h``           | ``avsystem/commons/avs_stream_netbuf.h``        |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_outbuf.h``    | ``avsystem/commons/avs_stream_outbuf.h``        |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream/stream_simple_io.h`` | ``avsystem/commons/avs_stream_simple_io.h``     |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/stream_v_table.h``          | ``avsystem/commons/avs_stream_v_table.h``       |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/time.h``                    | ``avsystem/commons/avs_time.h``                 |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/unit/memstream.h``          | ``avsystem/commons/avs_unit_memstream.h``       |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/unit/mock_helpers.h``       | ``avsystem/commons/avs_unit_mock_helpers.h``    |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/unit/mocksock.h``           | ``avsystem/commons/avs_unit_mocksock.h``        |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/unit/test.h``               | ``avsystem/commons/avs_unit_test.h``            |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/url.h``                     | ``avsystem/commons/avs_url.h``                  |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/utils.h``                   | ``avsystem/commons/avs_utils.h``                |
+------------------------------------------------+-------------------------------------------------+
| ``avsystem/commons/vector.h``                  | ``avsystem/commons/avs_vector.h``               |
+------------------------------------------------+-------------------------------------------------+

.. [#pki] Some symbols related to public-key cryptography have been refactored
          by moving from ``avsystem/commons/avs_socket.h`` to
          ``avsystem/commons/avs_crypto_pki.h``, with additional renames. For
          details, see :ref:`avs-commons-pki-move-225`.

Changes to avs_net socket API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Below is a reference of changes made to the ``avs_net`` socket API:

.. list-table::
   :widths: 20 20 40
   :header-rows: 1

   * - Old identifiers
     - New identifiers
     - Notes
   * - | ``avs_net_socket_create()``
     - | ``avs_net_udp_socket_create()``
       | ``avs_net_tcp_socket_create()``
       | ``avs_net_dtls_socket_create()``
       | ``avs_net_ssl_socket_create()``
     - | The ``avs_net_socket_type_t`` enum is no longer used for socket
         creation. Separate functions are used instead, allowing for type-safe
         passing of the configuration structures.
   * - | ``avs_net_socket_decorate_in_place()``
     - | ``avs_net_dtls_socket_decorate_in_place()``
       | ``avs_net_ssl_socket_decorate_in_place()``
     - | This change is analogous to the one above.
   * - | *implicit*
     - | ``prng_ctx`` field in ``avs_net_ssl_configuration_t``
     - | **Note:** It is now **mandatory** to fill this field when instantiating
         a (D)TLS socket.

.. note::

    With the introduction of the ``prng_ctx`` field in
    ``avs_net_ssl_configuration_t``, the
    ``WITH_MBEDTLS_CUSTOM_ENTROPY_INITIALIZER`` compile-time option and the
    option to use a user-provided ``avs_net_mbedtls_entropy_init()`` function
    have been **removed**. If you relied on those features in your non-POSIX
    environment, please replace them with the new PRNG context mechanism.
    See :doc:`MigratingCustomEntropy` for details.

Introduction of new socket option
"""""""""""""""""""""""""""""""""

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
it is recommended that you add support for this option.

.. _ssize-t-removal-in-commons-225:

Removal of ssize_t usages from avs_commons APIs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

All usages of the POSIX-specific ``ssize_t`` type in public APIs have been
removed. Instead of replacing it with some other signed integer type, additional
out-arguments have been introduced to functions that used it.

Below is a reference of related changes:

* **Base64 decode**

  - **Old APIs:**
    ::

        ssize_t avs_base64_decode_custom(uint8_t *out,
                                         size_t out_length,
                                         const char *input,
                                         avs_base64_config_t config);
        // ...
        static inline ssize_t
        avs_base64_decode_strict(uint8_t *out, size_t out_length, const char *input) {
            // ...
        }
        // ...
        static inline ssize_t
        avs_base64_decode(uint8_t *out, size_t out_length, const char *input) {
            // ...
        }

  - **New APIs:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_base64.h
       :emphasize-lines: 1,7,14

        int avs_base64_decode_custom(size_t *out_bytes_decoded,
                                     uint8_t *out,
                                     size_t out_length,
                                     const char *input,
                                     avs_base64_config_t config);
        // ...
        static inline int avs_base64_decode_strict(size_t *out_bytes_decoded,
                                                   uint8_t *out,
                                                   size_t out_length,
                                                   const char *input) {
            // ...
        }
        // ...
        static inline int avs_base64_decode(size_t *out_bytes_decoded,
                                            uint8_t *out,
                                            size_t out_length,
                                            const char *input) {
            // ...
        }

* **Hexlify**

  - **Old API:**
    ::

        ssize_t avs_hexlify(char *out_hex,
                            size_t out_size,
                            const void *input,
                            size_t input_size);

  - **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_utils.h
       :emphasize-lines: 1,3

        int avs_hexlify(char *out_hex,
                        size_t out_size,
                        size_t *out_bytes_hexlified,
                        const void *input,
                        size_t input_size);

* **Unhexlify**

  - **Old API:**
    ::

        ssize_t avs_unhexlify(uint8_t *output,
                              size_t out_size,
                              const char *input,
                              size_t in_size);

  - **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_utils.h
       :emphasize-lines: 1

        int avs_unhexlify(size_t *out_bytes_written,
                          uint8_t *output,
                          size_t out_size,
                          const char *input,
                          size_t in_size);

.. note::

    The new functions return 0 in all cases in which the old versions returned
    non-negative values. The value previously returned through the non-negative
    return value can be retrieved using the additional out-arguments, which have
    the same semantics. ``NULL`` can be passed to those out-arguments as well if
    that value is not needed.

    The seemingly irregular placement of the new out-argument in
    ``avs_hexlify()`` is due to the fact that the semantics of that value is
    related to the ``input`` argument (hence it directly precedes it), not to
    the output buffer as is the case with the rest of these functions.

.. _avs-commons-pki-move-225:

Move of public-key cryptography APIs from avs_net to avs_crypto
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Public key cryptography APIs, previously defined in
``avsystem/commons/socket.h``, have been moved into a new header called
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

Changes to public configuration macros
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_commons`` 4.1 introduced a new header file,
``avsystem/commons/avs_commons_config.h``, that encapsulates all its
compile-time configuration, allowing compiling the library without the use of
CMake, among other improvements.

This file is included by all other ``avs_commons`` headers, so this is not a
breaking change in and of itself. However, some configuration macros that were
previously ``#define``-d in ``avsystem/commons/defs.h`` have been renamed for
better namespace separation.

If your code checks for these macros using ``#ifdef`` etc., it will need
adjustments.

+---------------------------------------------------------+-------------------------------------+
| Old macro name                                          | New macro name                      |
+=========================================================+=====================================+
| ``WITH_IPV4``                                           | ``AVS_COMMONS_NET_WITH_IPV4``       |
+---------------------------------------------------------+-------------------------------------+
| ``WITH_IPV6``                                           | ``AVS_COMMONS_NET_WITH_IPV6``       |
+---------------------------------------------------------+-------------------------------------+
| ``WITH_X509``                                           | ``AVS_COMMONS_WITH_AVS_CRYPTO_PKI`` |
+---------------------------------------------------------+-------------------------------------+
| ``WITH_AVS_MICRO_LOGS``                                 | ``AVS_COMMONS_WITH_MICRO_LOGS``     |
+---------------------------------------------------------+-------------------------------------+
| ``HAVE_NET_IF_H``                                       | ``AVS_COMMONS_HAVE_NET_IF_H``       |
+---------------------------------------------------------+-------------------------------------+
| ``AVS_SSIZE_T_DEFINED``                                 | *removed completely*                |
+---------------------------------------------------------+-------------------------------------+
| ``HAVE_SYS_TYPES_H``                                    | *removed completely*                |
+---------------------------------------------------------+-------------------------------------+
| ``AVS_COMMONS_WITH_MBEDTLS_CUSTOM_ENTROPY_INITIALIZER`` | *removed completely*                |
+---------------------------------------------------------+-------------------------------------+

.. important::

    In the case of ``WITH_X509``, the corresponding CMake variable has also been
    renamed to ``WITH_PKI``. Attempting to use ``WITH_X509`` will trigger an
    error.

.. note::

    Aside from the one variable mentioned above, and those removed completely,
    the CMake variable names have not changed - the renames affect **only** the
    C preprocessor.

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

Changes in component dependencies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* ``avs_net`` now depends on ``avs_crypto``

  * ``avs_crypto`` itself was previously only used for advanced features, only
    used by the OSCORE commercial feature.
  * In the new version, ``avs_crypto`` also contains an abstraction over
    cryptographically-safe PRNGs.
  * The functionality that comprised the "old" ``avs_crypto`` is now controlled
    by the ``AVS_COMMONS_WITH_AVS_CRYPTO_ADVANCED_FEATURES`` compile-time
    option.

* ``avs_vector`` is no longer compiled by default when building Anjay

* URL handling routines, previously a part of ``avs_net``, are now a separate
  component called ``avs_url``

  * You may need to add ``-lavs_url`` to your link command if you're not using
    CMake to handle dependencies between your project and Anjay

Removal of the legacy CoAP component
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

While the new ``avs_coap`` has been used as the CoAP implementation in all
versions of Anjay 2.x, the old CoAP component of ``avs_commons`` remained in the
repository in the 4.0 branch of ``avs_commons``.

This has been removed in ``avs_commons`` 4.1 and Anjay 2.3. If your code used
the raw CoAP APIs of that component, you will need to migrate to either the new
``avs_coap`` library or an entirely different CoAP implementation.

.. note::

    The new ``avs_coap`` library has a higher-level API, designed to abstract
    away the differences between e.g. UDP and TCP transports. Some of the
    functionality of the legacy library, especially that related to parsing,
    serializing, sending and receiving raw, isolated messages (as opposed to
    proper, conformant CoAP exchanges), is not provided in the public API for
    this reason.
