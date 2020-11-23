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

Migrating from Anjay 2.2.5
==========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Anjay 2.3 is a minor upgrade in terms of feature set, so no major changes in
code flow are required when porting from Anjay 2.2. However, there has been a
significant refactor in project structure, so some changes may be breaking for
some users.

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


Other changes
^^^^^^^^^^^^^

* Declaration of ``anjay_smsdrv_cleanup()`` (only relevant for the commercial
  version) has been moved from ``anjay/core.h`` to ``anjay/sms.h``.
* The following compile-time constants have been removed. None of them have been
  actually used in Anjay 2.x:

  * ``MAX_FLOAT_STRING_SIZE``
  * ``MAX_OBSERVABLE_RESOURCE_SIZE``
  * ``WITH_ATTR_STORAGE``

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


.. note ::

    It is now **mandatory** to pass a non-NULL value as the ``prng_ctx``
    argument to the functions above.

Changes in avs_commons
----------------------

``avs_commons`` 4.1 contains a number of breaking changes compared to version
4.0 used by Anjay 2.2. If you are using any of the ``avs_commons`` APIs directly
(which is especially likely for e.g. the logging API and querying sockets in the
event loop), you will need to adjust your code.

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

+------------------------------------------------+-----------------------------------------------------+
| Old header file                                | New header file                                     |
+================================================+=====================================================+
| ``avsystem/commons/addrinfo.h``                | ``avsystem/commons/avs_addrinfo.h``                 |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/aead.h``                    | ``avsystem/commons/avs_aead.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/base64.h``                  | ``avsystem/commons/avs_base64.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/buffer.h``                  | ``avsystem/commons/avs_buffer.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/cleanup.h``                 | ``avsystem/commons/avs_cleanup.h``                  |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/condvar.h``                 | ``avsystem/commons/avs_condvar.h``                  |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/defs.h``                    | ``avsystem/commons/avs_defs.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/errno.h``                   | ``avsystem/commons/avs_errno.h``                    |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/errno_map.h``               | ``avsystem/commons/avs_errno_map.h``                |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/hkdf.h``                    | ``avsystem/commons/avs_hkdf.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/http.h``                    | ``avsystem/commons/avs_http.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/init_once.h``               | ``avsystem/commons/avs_init_once.h``                |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/list.h``                    | ``avsystem/commons/avs_list.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/log.h``                     | ``avsystem/commons/avs_log.h``                      |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/memory.h``                  | ``avsystem/commons/avs_memory.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/mutex.h``                   | ``avsystem/commons/avs_mutex.h``                    |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/net.h``                     | ``avsystem/commons/avs_net.h``                      |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/persistence.h``             | ``avsystem/commons/avs_persistence.h``              |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/rbtree.h``                  | ``avsystem/commons/avs_rbtree.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/sched.h``                   | ``avsystem/commons/avs_sched.h``                    |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/shared_buffer.h``           | ``avsystem/commons/avs_shared_buffer.h``            |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/socket.h``                  | | ``avsystem/commons/avs_socket.h``                 |
|                                                | | ``avsystem/commons/avs_crypto_pki.h`` [#pki]_     |
|                                                | | ``avsystem/commons/avs_net_pki_compat.h`` [#pki]_ |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/socket_v_table.h``          | ``avsystem/commons/avs_socket_v_table.h``           |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream.h``                  | ``avsystem/commons/avs_stream.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_buffered.h``  | ``avsystem/commons/avs_stream_buffered.h``          |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_file.h``      | ``avsystem/commons/avs_stream_file.h``              |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_inbuf.h``     | ``avsystem/commons/avs_stream_inbuf.h``             |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/md5.h``              | ``avsystem/commons/avs_stream_md5.h``               |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_membuf.h``    | ``avsystem/commons/avs_stream_membuf.h``            |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_net.h``       | ``avsystem/commons/avs_stream_net.h``               |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/netbuf.h``           | ``avsystem/commons/avs_stream_netbuf.h``            |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_outbuf.h``    | ``avsystem/commons/avs_stream_outbuf.h``            |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream/stream_simple_io.h`` | ``avsystem/commons/avs_stream_simple_io.h``         |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/stream_v_table.h``          | ``avsystem/commons/avs_stream_v_table.h``           |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/time.h``                    | ``avsystem/commons/avs_time.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/unit/memstream.h``          | ``avsystem/commons/avs_unit_memstream.h``           |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/unit/mock_helpers.h``       | ``avsystem/commons/avs_unit_mock_helpers.h``        |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/unit/mocksock.h``           | ``avsystem/commons/avs_unit_mocksock.h``            |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/unit/test.h``               | ``avsystem/commons/avs_unit_test.h``                |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/url.h``                     | ``avsystem/commons/avs_url.h``                      |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/utils.h``                   | ``avsystem/commons/avs_utils.h``                    |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/vector.h``                  | ``avsystem/commons/avs_vector.h``                   |
+------------------------------------------------+-----------------------------------------------------+

.. [#pki] Some symbols related to public-key cryptography have been refactored
          by moving from ``avsystem/commons/avs_socket.h`` to
          ``avsystem/commons/avs_crypto_pki.h``, with additional renames. Old
          names are available for compatibility via
          ``avsystem/commons/avs_net_pki_compat.h``. For details, see
          :ref:`avs-commons-pki-move-225`.

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

Changes to public configuration macros
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_commons`` 4.1 introduces a new header file,
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
    renamed to ``WITH_PKI``. The old name is still recognized, but deprecated.

.. note::

    Aside from the one variable mentioned above, and those removed completely,
    the CMake variable names have not changed - the renames affect **only** the
    C preprocessor.

Refactor of avs_net_validate_ip_address()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_validate_ip_address()`` is now no longer used by Anjay or
``avs_commons``. It was previously necessary to implement it as part of the
socket implementation. This is no longer required, and in fact, keeping that
implementation might lead to problems - for compatibility, the function has been
reimplemented as a ``static inline`` function that wraps
``avs_net_addrinfo_*()`` APIs.

Changes in component dependencies
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* ``avs_net`` now depends on ``avs_crypto``

  * ``avs_crypto`` itself was previously only used for advanced features, only
    used by the OSCORE component in the commercial version of Anjay.
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

This has been removed in version 4.1 that Anjay 2.3 uses. If your code used the
raw CoAP APIs of that component, you will need to migrate to either the new
``avs_coap`` library or an entirely different CoAP implementation.

.. note::

    The new ``avs_coap`` library has a higher-level API, designed to abstract
    away the differences between e.g. UDP and TCP transports. Some of the
    functionality of the legacy library, especially that related to parsing,
    serializing, sending and receiving raw, isolated messages (as opposed to
    proper, conformant CoAP exchanges), is not provided in the public API for
    this reason.
