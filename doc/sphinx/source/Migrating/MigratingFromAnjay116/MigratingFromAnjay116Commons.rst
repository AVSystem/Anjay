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

Changes in avs_commons
======================

.. contents:: :local:

.. highlight:: c

Introduction
------------

``avs_commons`` 4.1 contains a number of breaking changes compared to version
3.11 used by Anjay 1.16. If you are using any of the ``avs_commons`` APIs
directly (which is especially likely for e.g. the logging API and querying
sockets in the event loop), you will need to adjust your code.

avs_commons header rename
-------------------------

All headers of the ``avs_commons`` component have been renamed to make their
names more unique. Please adjust your ``#include`` directives accordingly.

The general rename patterns are:

* ``avsystem/commons/*.h`` → ``avsystem/commons/avs_*.h``
* ``avsystem/commons/stream/*.h``, ``avsystem/commons/stream/stream_*.h`` →
  ``avsystem/commons/avs_stream_*.h``
* ``avsystem/commons/unit/*.h`` → ``avsystem/commons/avs_unit_*.h``


+------------------------------------------------+-----------------------------------------------------+
| Old header file                                | New header file                                     |
+================================================+=====================================================+
| ``avsystem/commons/addrinfo.h``                | ``avsystem/commons/avs_addrinfo.h``                 |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/base64.h``                  | ``avsystem/commons/avs_base64.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/buffer.h``                  | ``avsystem/commons/avs_buffer.h``                   |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/coap/*``                    | | removed in existing form                          |
|                                                | | replaced by ``avsystem/coap/*``                   |
|                                                | | see :doc:`MigratingFromAnjay116Coap` for details  |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/cleanup.h``                 | ``avsystem/commons/avs_cleanup.h``                  |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/condvar.h``                 | ``avsystem/commons/avs_condvar.h``                  |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/defs.h``                    | ``avsystem/commons/avs_defs.h``                     |
+------------------------------------------------+-----------------------------------------------------+
| ``avsystem/commons/errno.h``                   | removed in existing form, replaced by:              |
|                                                |                                                     |
|                                                | - ``avsystem/commons/avs_errno.h``                  |
|                                                | - ``avsystem/commons/avs_errno_map.h``              |
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
          :ref:`avs-commons-pki-move-116`.

.. _avs-commons-type-renames:

Abstract type renames
---------------------

Some of the abstract object types have been renamed for simplicity and
consistency:

+---------------------------------+----------------------------------------+
| Old type name                   | New type name                          |
+=================================+========================================+
| | ``avs_net_abstract_socket_t`` | | ``avs_net_socket_t``                 |
| | ``avs_socket_t``              |                                        |
| | *(two synonymous aliases)*    |                                        |
+---------------------------------+----------------------------------------+
| | ``avs_stream_abstract_t``     | | ``avs_stream_t``                     |
|                                 | | *(previously available as an alias)* |
+---------------------------------+----------------------------------------+

.. _avs-commons-new-error-handling:

New error handling scheme
-------------------------

A common pattern in ``avs_commons`` 3.x and older was for methods of certain
types of objects (most notably, streams and sockets) to return ``-1`` on error,
and provide the ability to get a more specific error code using a separate "get
errno" method.

Some other functions used the global ``errno`` variable for passing specific
error information.

This has been replaced with a new scheme in which all functions that need to
report different kinds of errors, return a new ``avs_error_t`` type instead.

The ``avs_error_t`` structure is defined in the ``avsystem/commons/avs_errno.h``
header as follows:

.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_errno.h

    /**
     * Generic error representation, containing a category and an actual error code.
     */
    typedef struct {
        /**
         * Error code category. It is intended to be unique application-wide for any
         * source that can return errors. It determines the meaning of the
         * <c>code</c> field.
         */
        uint16_t category;

        /**
         * Error code, valid within the given <c>category</c>. For example, if
         * <c>category</c> is equal to @ref AVS_ERRNO_CATEGORY, <c>code</c> will be
         * one of the @ref avs_errno_t values.
         *
         * NOTE: All categories are REQUIRED to map <c>code</c> value of 0 to
         * "no error". So, <c>code == 0</c> always means success regardless of the
         * <c>category</c>.
         */
        uint16_t code;
    } avs_error_t;

On most architectures, this structure will be 4 bytes (32 bits) in size, which
means that it will be passed between functions in a single 32-bit register in
most cases. However, it is intentionally declared as a structure and **not** as
a bit-mapped integer for improved type safety.

The following functions are designed to ease checking whether a returned value
is a success or error code, if that is everything one needs in a given
situation:

.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_errno.h

    static inline bool avs_is_ok(avs_error_t error) {
        return error.code == 0;
    }

    static inline bool avs_is_err(avs_error_t error) {
        return !avs_is_ok(error);
    }

The canonical way of returning a success is to use the ``AVS_OK`` constant:

.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_errno.h

    static const avs_error_t AVS_OK = { 0, 0 };

The error categories known at the time of writing this article are:

* .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_errno.h

      #define AVS_ERRNO_CATEGORY 37766 // 'errno' on phone keypad

  Error codes are values of the ``avs_errno_t`` enum, which is intended to be a
  platform-independent alternative to system ``errno`` values. ``avs_error_t``
  values can be quickly created using the ``avs_errno()`` function. The
  ``avs_map_errno()`` function, declared in
  ``avsystem/commons/avs_errno_map.h``, can be used to convert system ``errno``
  values to ``avs_errno_t``.

* .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream.h

      #define AVS_EOF_CATEGORY 363 // 'EOF' on phone keypad

  Whole category used to represent an end-of-file or end-of-stream condition,
  used mostly by some ``avs_stream`` input methods. The ``AVS_EOF`` constant and
  ``avs_is_eof()`` function are canonically used to deal with this condition.

* .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

      #define AVS_NET_SSL_ALERT_CATEGORY 8572 // 'TLSA' on phone keypad

  Used by the (D)TLS socket implementations to wrap TLS alerts as
  ``avs_errno_t`` when reporting related failures. The most-significant and
  least-significant 8-bit halves of the error code represent the "level" and
  "description" field of a TLS alert, respectively.

* .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_http.h

      #define AVS_HTTP_ERROR_CATEGORY 4887 // 'HTTP' on phone keypad

  Used by ``avs_http`` to return HTTP layer errors. The error code is a
  non-success HTTP status code (e.g. 404, 501).

* .. snippet-source:: deps/avs_coap/include_public/avsystem/coap/ctx.h

      #define AVS_COAP_ERR_CATEGORY 22627 // 'acoap' on phone keypad

  Error codes are values of the ``avs_coap_error_t`` enum, representing various
  error conditions within the ``avs_coap`` library.

.. warning::

    If you decide to use ``avs_error_t`` in your own code, you may want to
    define your own category codes. This is generally fine, but **please note
    that no strict way of enforcing uniqueness of category codes exists**.

    This also means that in any future version of ``avs_commons``, ``avs_coap``
    or Anjay, a new category may be introduced, whose category code might by
    chance conflict with your custom category. **We do not make any guarantees
    about interoperability of code that uses custom error categories in the
    future.**

    In other words, it is not wrong to do this, but **you are on your own**.

Specific API changes related to this new mechanism are listed in the sections
below.

Changes in public avs_stream APIs
---------------------------------

Changes to global caller functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

List of functions that changed return value from ``int`` to ``avs_error_t``,
without any other signature changes (aside from type renames mentioned in
:ref:`avs-commons-type-renames`):

* ``avs_stream_cleanup()``
* ``avs_stream_file_length()``
* ``avs_stream_file_offset()``
* ``avs_stream_file_seek()``
* ``avs_stream_finish_message()``
* ``avs_stream_ignore_to_end()``
* ``avs_stream_membuf_fit()``
* ``avs_stream_net_setsock()``
* ``avs_stream_outbuf_set_offset()``
* ``avs_stream_peekline()``
* ``avs_stream_read_reliably()``
* ``avs_stream_reset()``
* ``avs_stream_write()``
* ``avs_stream_write_f()``
* ``avs_stream_write_fv()``
* ``avs_stream_write_some()``

The following functions retain **mostly** the same signatures - aside from the
change from ``int`` to ``avs_error_t``, they take an ``out_message_finished``
argument, whose type changed from ``char *`` to ``bool *``:

* ``avs_stream_getline()``
* ``avs_stream_read()``

The following functions underwent more significant refactors:

* ``avs_stream_errno()`` **has been removed**

  * Detailed error information is now returned directly from each of the stream
    methods as ``avs_error_t`` values.

* **Get character**

  * **Old API:**
    ::

        int avs_stream_getch(avs_stream_abstract_t *stream, char *out_message_finished);

  * **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream.h

        avs_error_t avs_stream_getch(avs_stream_t *stream,
                                     char *out_value,
                                     bool *out_message_finished);

  * Retrieved character, returned directly in the old version, is now returned
    through the new ``out_value`` argument. ``out_message_finished`` argument
    has been refactored as ``bool *``. End-of-stream condition, previously
    mapped to an ``EOF`` constant, is now signalled by returning ``AVS_EOF``.
    Error conditions, previously mapped to unspecified "negative value different
    than ``EOF``", are now reported using specific ``avs_error_t`` values.

* **Peek byte**

  * **Old API:**
    ::

        int avs_stream_peek(avs_stream_abstract_t *stream, size_t offset);

  * **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream.h

        avs_error_t
        avs_stream_peek(avs_stream_t *stream, size_t offset, char *out_value);

  * The semantic changes are equivalent to those in ``avs_stream_getch()``.

* **Non-blocking readiness checkers**

  * **Old APIs:**
    ::

        int avs_stream_nonblock_read_ready(avs_stream_abstract_t *stream);
        // ...
        int avs_stream_nonblock_write_ready(avs_stream_abstract_t *stream,
                                            size_t *out_ready_capacity_bytes);

  * **New APIs:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream.h

        bool avs_stream_nonblock_read_ready(avs_stream_t *stream);
        // ...
        size_t avs_stream_nonblock_write_ready(avs_stream_t *stream);

  * The ability to explicitly return errors has been removed from these
    functions. Error conditions are now mapped to ``false`` (for the read
    operation) or ``0`` (for the write operation). For this reason, the ``int``
    return code has been replaced with a simple ``bool`` (for the read
    operation) and ``size_t`` (replacing the output argument for the write
    operation).

Changes to vtable method signatures
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These changes will be relevant if you implement your own implementations of the
``avs_stream`` interface.

List of methods in various ``avs_stream``-related vtables that changed return
value from ``int`` to ``avs_error_t``, without any other signature changes
(aside from type renames mentioned in :ref:`avs-commons-type-renames`):

+---------------------------------+-------------------------------------+
| Function pointer type name      | ``avs_stream_v_table_t`` field name |
+=================================+=====================================+
| ``avs_stream_close_t``          | ``close``                           |
+---------------------------------+-------------------------------------+
| ``avs_stream_finish_message_t`` | ``finish_message``                  |
+---------------------------------+-------------------------------------+
| ``avs_stream_reset_t``          | ``reset``                           |
+---------------------------------+-------------------------------------+
| ``avs_stream_write_some_t``     | ``write_some``                      |
+---------------------------------+-------------------------------------+

+------------------------------+----------------------------------------------------+
| Function pointer type name   | ``avs_stream_v_table_extension_file_t`` field name |
+==============================+====================================================+
| ``avs_stream_file_length_t`` | ``length``                                         |
+------------------------------+----------------------------------------------------+
| ``avs_stream_file_offset_t`` | ``offset``                                         |
+------------------------------+----------------------------------------------------+
| ``avs_stream_file_seek_t``   | ``seek``                                           |
+------------------------------+----------------------------------------------------+

+-----------------------------+------------------------------------------------------+
| Function pointer type name  | ``avs_stream_v_table_extension_membuf_t`` field name |
+=============================+======================================================+
| ``avs_stream_membuf_fit_t`` | ``fit``                                              |
+-----------------------------+------------------------------------------------------+

+------------------------------+---------------------------------------------------+
| Function pointer type name   | ``avs_stream_v_table_extension_net_t`` field name |
+==============================+===================================================+
| ``avs_stream_net_setsock_t`` | ``setsock``                                       |
+------------------------------+---------------------------------------------------+

The following methods underwent more significant refactors:

* ``get_errno`` **method of** ``avs_stream_v_table_t`` **and the corresponding**
  ``avs_stream_errno_t`` **function pointer type have been removed**

  * Detailed error information shall now be returned directly from each of the
    stream methods as ``avs_error_t`` values.

* ``read`` **method of** ``avs_stream_v_table_t``

  * **Old API:**
    ::

        typedef int (*avs_stream_read_t)(avs_stream_abstract_t *stream,
                                         size_t *out_bytes_read,
                                         char *out_message_finished,
                                         void *buffer,
                                         size_t buffer_length);

  * **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream_v_table.h

        typedef avs_error_t (*avs_stream_read_t)(avs_stream_t *stream,
                                                 size_t *out_bytes_read,
                                                 bool *out_message_finished,
                                                 void *buffer,
                                                 size_t buffer_length);

  * Aside from changing the return type from ``int`` to ``avs_error_t``, the
    ``out_message_finished`` argument has been changed from ``char *`` to
    ``bool *``.

* ``peek`` **method of** ``avs_stream_v_table_t``

  * **Old API:**
    ::

        typedef int (*avs_stream_peek_t)(avs_stream_abstract_t *stream, size_t offset);

  * **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream_v_table.h

        typedef avs_error_t (*avs_stream_peek_t)(avs_stream_t *stream,
                                                 size_t offset,
                                                 char *out_value);

  * Peeked character, returned directly in the old version, shall now be
    returned through the new ``out_value`` argument. End-of-stream condition,
    previously mapped to an ``EOF`` constant, shall now signalled by returning
    ``AVS_EOF``. Error conditions, previously mapped to unspecified "negative
    value different than ``EOF``", shall now be reported using specific
    ``avs_error_t`` values.

* ``read_ready`` **and** ``write_ready`` **methods of**
  ``avs_stream_v_table_extension_nonblock_t``

  * **Old APIs:**
    ::

        typedef int (*avs_stream_nonblock_read_ready_t)(avs_stream_abstract_t *stream);
        // ...
        typedef int (*avs_stream_nonblock_write_ready_t)(
                avs_stream_abstract_t *stream,
                size_t *out_ready_capacity_bytes);

  * **New APIs:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream_v_table.h

        typedef bool (*avs_stream_nonblock_read_ready_t)(avs_stream_t *stream);
        // ...
        typedef size_t (*avs_stream_nonblock_write_ready_t)(avs_stream_t *stream);

  * The ability to explicitly return errors has been removed from these
    methods. Error conditions shall now be mapped to ``false`` (for the read
    operation) or ``0`` (for the write operation). For this reason, the ``int``
    return code has been replaced with a simple ``bool`` (for the read
    operation) and ``size_t`` (replacing the output argument for the write
    operation).

* ``getsock`` **method of** ``avs_stream_v_table_extension_net_t``

  * **Old API:**
    ::

        typedef int (*avs_stream_net_getsock_t)(avs_stream_abstract_t *stream,
                                                avs_net_abstract_socket_t **out_socket);

  * **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_stream_net.h

        typedef avs_net_socket_t *(*avs_stream_net_getsock_t)(avs_stream_t *stream);

  * The ability to explicitly return errors has been removed from this method.
    Error conditions shall now be mapped to ``NULL``. For this reason, the
    ``out_socket`` argument has been removed, and the socket pointer shall now
    be passed directly as the return value, as the ``int`` code is no longer
    necessary.

Changes in public avs_net APIs
------------------------------

List of functions that changed return value from ``int`` to ``avs_error_t``,
without any other signature changes (aside from type renames mentioned in
:ref:`avs-commons-type-renames`):

* ``avs_net_local_address_for_target_host()``
* ``avs_net_resolved_endpoint_get_host_port()`` [#compat]_
* ``avs_net_resolved_endpoint_get_host()``
* ``avs_net_socket_accept()``
* ``avs_net_socket_bind()``
* ``avs_net_socket_cleanup()``
* ``avs_net_socket_close()``
* ``avs_net_socket_connect()``
* ``avs_net_socket_decorate()``
* ``avs_net_socket_get_local_host()``
* ``avs_net_socket_get_local_port()``
* ``avs_net_socket_get_opt()``
* ``avs_net_socket_get_remote_host()``
* ``avs_net_socket_get_remote_hostname()``
* ``avs_net_socket_get_remote_port()``
* ``avs_net_socket_interface_name()``
* ``avs_net_socket_receive()``
* ``avs_net_socket_receive_from()``
* ``avs_net_socket_send()``
* ``avs_net_socket_send_to()``
* ``avs_net_socket_set_opt()``
* ``avs_net_socket_shutdown()``
* ``avs_url_percent_encode()``

.. [#compat] This function may need to be implemented by the user if a custom
             (non-POSIX) socket implementation is used. Please refer to
             :ref:`non-posix-socket-api-changes` for details.

Additional changes in public ``avs_net`` APIs:

* ``avs_net_socket_errno()`` **has been removed**

  * Detailed error information is now returned directly from each of the socket
    methods as ``avs_error_t`` values.

* **Refactored socket creation functions**

  * **Old APIs:**
    ::

        int avs_net_socket_create(avs_net_abstract_socket_t **socket,
                                  avs_net_socket_type_t sock_type,
                                  const void *configuration);

  * **New APIs:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

        avs_error_t
        avs_net_udp_socket_create(avs_net_socket_t **socket,
                                  const avs_net_socket_configuration_t *config);

        avs_error_t
        avs_net_tcp_socket_create(avs_net_socket_t **socket,
                                  const avs_net_socket_configuration_t *config);

        #ifdef AVS_COMMONS_WITH_AVS_CRYPTO
        avs_error_t
        avs_net_dtls_socket_create(avs_net_socket_t **socket,
                                   const avs_net_ssl_configuration_t *config);

        avs_error_t
        avs_net_ssl_socket_create(avs_net_socket_t **socket,
                                  const avs_net_ssl_configuration_t *config);
        #endif // AVS_COMMONS_WITH_AVS_CRYPTO

  * The ``avs_net_socket_type_t`` enum is no longer used for socket creation.
    Separate functions are used instead, allowing for type-safe passing of the
    configuration structures.

* **Refactored in-place (D)TLS socket decoration functions**

  * **Old APIs:**
    ::

        int avs_net_socket_decorate_in_place(avs_net_abstract_socket_t **socket,
                                             avs_net_socket_type_t new_type,
                                             const void *configuration);

  * **New APIs:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

        avs_error_t avs_net_dtls_socket_decorate_in_place(
                avs_net_socket_t **socket, const avs_net_ssl_configuration_t *config);

        avs_error_t
        avs_net_ssl_socket_decorate_in_place(avs_net_socket_t **socket,
                                             const avs_net_ssl_configuration_t *config);

  * This change is analogous to the one above.

* **New, mandatory** ``prng_ctx`` **field in** ``avs_net_ssl_configuration_t``

  * Note: With the introduction of the ``prng_ctx`` field in
    ``avs_net_ssl_configuration_t``, the
    ``WITH_MBEDTLS_CUSTOM_ENTROPY_INITIALIZER`` compile-time option and the
    option to use a user-provided ``avs_net_mbedtls_entropy_init()`` function
    have been **removed**. If you relied on those features in your non-POSIX
    environment, please replace them with the new PRNG context mechanism.
    See :doc:`../MigratingCustomEntropy` for details.

.. _non-posix-socket-api-changes:

Changes in avs_net non-POSIX porting APIs
-----------------------------------------

avs_net socket vtable method signatures
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

List of ``avs_net_socket_v_table_t`` methods that changed return value from
``int`` to ``avs_error_t``, without any other signature changes (aside from type
renames mentioned in :ref:`avs-commons-type-renames`):

+------------------------------------------+-----------------------------------------+
| Function pointer type name               | ``avs_net_socket_v_table_t`` field name |
+==========================================+=========================================+
| ``avs_net_socket_accept_t``              | ``accept``                              |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_bind_t``                | ``bind``                                |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_cleanup_t``             | ``cleanup``                             |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_close_t``               | ``close``                               |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_connect_t``             | ``connect``                             |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_decorate_t``            | ``decorate``                            |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_interface_t``       | ``get_interface_name``                  |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_local_host_t``      | ``get_local_host``                      |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_local_port_t``      | ``get_local_port``                      |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_opt_t``             | ``get_opt``                             |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_remote_host_t``     | ``get_remote_host``                     |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_remote_hostname_t`` | ``get_remote_hostname``                 |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_get_remote_port_t``     | ``get_remote_port``                     |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_receive_t``             | ``receive``                             |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_receive_from_t``        | ``receive_from``                        |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_set_opt_t``             | ``set_opt``                             |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_send_t``                | ``send``                                |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_send_to_t``             | ``send_to``                             |
+------------------------------------------+-----------------------------------------+
| ``avs_net_socket_shutdown_t``            | ``shutdown``                            |
+------------------------------------------+-----------------------------------------+

Additional changes:

* ``get_errno`` **method and the corresponding** ``avs_net_socket_errno_t``
  **function pointer type have been removed**

  * Detailed error information shall now be returned directly from each of the
    socket methods as ``avs_error_t`` values.

* **Changed signature for the** ``get_system_socket`` **method**

  * **Old API:**
    ::

        typedef int (*avs_net_socket_get_system_t)(avs_net_abstract_socket_t *socket,
                                                   const void **out);

  * **New API:**

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket_v_table.h

        typedef const void *(*avs_net_socket_get_system_t)(avs_net_socket_t *socket);

  * Implementations shall now return ``NULL`` on error. Detailed error
    information is not supported for this method.

Global function signatures
^^^^^^^^^^^^^^^^^^^^^^^^^^

The following global functions that the user may need to implement as part of
porting for a non-POSIX platform, have changed return value from ``int`` to
``avs_error_t`` without any other signature changes (aside from type renames
mentioned in :ref:`avs-commons-type-renames`):

* ``avs_net_resolved_endpoint_get_host_port()``
* ``_avs_net_create_tcp_socket()``
* ``_avs_net_create_udp_socket()``
* ``_avs_net_initialize_global_compat_state()``

Refactor of avs_net_validate_ip_address()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_net_validate_ip_address()`` is now no longer used by Anjay or
``avs_commons``. It was previously necessary to implement it as part of the
socket implementation. This is no longer required, and in fact, keeping that
implementation might lead to problems - for compatibility, the function has been
reimplemented as a ``static inline`` function that wraps
``avs_net_addrinfo_*()`` APIs.

.. _avs-commons-pki-move-116:

Move of public-key cryptography APIs from avs_net to avs_crypto
---------------------------------------------------------------

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

.. _avs-commons-persistence-changes:

Changes in public avs_persistence APIs
--------------------------------------

List of functions that changed return value from ``int`` to ``avs_error_t``,
without any other signature changes:

* ``avs_persistence_bool()``
* ``avs_persistence_bytes()``
* ``avs_persistence_custom_allocated_list()`` [#persistence-callback-changes]_
* ``avs_persistence_custom_allocated_tree()`` [#persistence-callback-changes]_
* ``avs_persistence_double()``
* ``avs_persistence_float()``
* ``avs_persistence_i8()``
* ``avs_persistence_i16()``
* ``avs_persistence_i32()``
* ``avs_persistence_i64()``
* ``avs_persistence_list()`` [#persistence-callback-changes]_
* ``avs_persistence_magic()``
* ``avs_persistence_magic_string()``
* ``avs_persistence_sized_buffer()``
* ``avs_persistence_string()``
* ``avs_persistence_tree()`` [#persistence-callback-changes]_
* ``avs_persistence_u8()``
* ``avs_persistence_u16()``
* ``avs_persistence_u32()``
* ``avs_persistence_u64()``
* ``avs_persistence_version()``

.. [#persistence-callback-changes]
   Signatures of these functions depend on callback function pointer types,
   which also have changed signatures. See below.

List of callback function pointer types that changed return value from ``int``
to ``avs_error_t``, without any other signature changes:

+---------------------------------------------------------------+-----------------------------------------------+
| Function pointer type name                                    | Referencing methods                           |
+===============================================================+===============================================+
| | ``avs_persistence_handler_collection_element_t``            | | ``avs_persistence_list()``                  |
|                                                               | | ``avs_persistence_tree()``                  |
+---------------------------------------------------------------+-----------------------------------------------+
| | ``avs_persistence_handler_custom_allocated_list_element_t`` | | ``avs_persistence_custom_allocated_list()`` |
+---------------------------------------------------------------+-----------------------------------------------+
| | ``avs_persistence_handler_custom_allocated_tree_element_t`` | | ``avs_persistence_custom_allocated_tree()`` |
+---------------------------------------------------------------+-----------------------------------------------+

Additionally, the following methods have been removed:

* | ``avs_persistence_store_context_new()`` **and**
    ``avs_persistence_restore_context_new()``
  | Removed in favor of the newer ``*_create()`` variants that allow avoiding
    use of the heap.
* | ``avs_persistence_ignore_context_create()`` **and**
    ``avs_persistence_ignore_context_new()``
  | The concept of "ignoring context" have been completely removed due to its
    bugginess and limited usability.

Changes in public avs_http APIs
-------------------------------

* ``avs_http_open_stream()`` now returns ``avs_error_t``. The rest of the
  signature remains equivalent.

* Old HTTP pseudo-error constants have been removed in favor of new error
  handling scheme based on ``avs_error_t``:

  * ``AVS_HTTP_ERROR_GENERAL`` is no longer used. More specific errors are
    always returned.
  * ``AVS_HTTP_ERROR_TOO_MANY_REDIRECTS`` condition is now reported by returning
    an error of ``AVS_HTTP_ERROR_CATEGORY`` category, with the error code in the
    300-399 range (which is the status code that the last redirect request).
  * ``AVS_HTTP_ERRNO_BACKEND`` and ``AVS_HTTP_ERRNO_DECODER`` are no longer
    used. Error codes from the backend or decoder stream are forwarded verbatim
    instead.

.. _ssize-t-removal-in-commons-116:

Removal of ssize_t usages from avs_commons APIs
-----------------------------------------------

All usages of the POSIX-specific ``ssize_t`` type in public APIs have been
removed. Instead of replacing it with some other signed integer type, additional
out-arguments have been introduced to functions that used it.

Below is a reference of related changes:

* **Base64 decode**

  - **Old APIs:**
    ::

        ssize_t
        avs_base64_decode_strict(uint8_t *out, size_t out_length, const char *input);
        // ...
        ssize_t avs_base64_decode(uint8_t *out, size_t out_length, const char *input);

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

Changes to public configuration macros
--------------------------------------

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
