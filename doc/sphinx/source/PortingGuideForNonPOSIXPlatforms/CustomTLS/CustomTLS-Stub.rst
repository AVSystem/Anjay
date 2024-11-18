..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Introductory stub
=================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/stub
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/stub>`_
    in the Anjay source directory.

Introduction
------------

This article describes a stub with no actual functionality, but contains the
necessary boilerplate that all the following tutorials will be based on.

The example code for this tutorial is an extension of the one built in the
:doc:`../NetworkingAPI/NetworkingAPI-IpStickiness` article. This has been chosen
as a base because it contains all the features of a custom networking layer. We
encourage you to familiarize yourself with the :doc:`../NetworkingAPI` tutorials
before implementing a custom TLS layer, but intricate knowledge of all the
features of the networking layer is not necessary.

Compared to the networking API tutorials, this one is intended to be used with a
version of Anjay that has been compiled without the default TLS layer
implementation, i.e. with this additional CMake flag::

    -DDTLS_BACKEND=custom

Additionally, support for HTTP downloads will be used in later tutorials. When
the examples are built as part of Anjay's ``make examples`` target, this feature
is enabled for all the ``custom-tls`` subprojects with yet another CMake flag::

    -DWITH_HTTP_DOWNLOAD=ON

.. note::

    This new custom network layer implementation will be based on OpenSSL 1.1.1.
    This is not very useful in the real world, as there is a default
    implementation provided for the OpenSSL library. However, this tutorial is
    provided as a reference implementation simpler than the actual default one,
    to make it easier to base your code on it.

Adjustments to the build system
-------------------------------

The `CMakeLists.txt <https://github.com/AVSystem/Anjay/blob/master/examples/custom-tls/stub/CMakeLists.txt>`_
file has been modified to accommodate for this custom TLS implementation:

.. highlight:: cmake
.. snippet-source:: examples/custom-tls/stub/CMakeLists.txt
    :emphasize-lines: 1, 7, 12-13

    cmake_minimum_required(VERSION 3.4)
    project(custom-tls-stub C)

    set(CMAKE_C_STANDARD 99)

    find_package(anjay REQUIRED)
    find_package(OpenSSL REQUIRED)

    add_executable(${PROJECT_NAME}
                   src/main.c
                   src/net_impl.c
                   src/tls_impl.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay OpenSSL::SSL)

Some minor changes have been made here:

* The project is linked with the OpenSSL library by calling ``find_package``
  and adding ``OpenSSL::SSL`` to ``target_link_libraries``.
* Minimum required CMake version has been raised to 3.4 as that is the first
  version in which the new-style ``OpenSSL::SSL`` target is provided.
* The `tls_impl.c
  <https://github.com/AVSystem/Anjay/blob/master/examples/custom-tls/stub/src/tls_impl.c>`_
  file has been added to the executable target. Just like with the custom
  network layer, the functions defined there will be called by Anjay or its
  dependent libraries.

.. note::

    The ``main.c`` and ``net_impl.c`` files are left completely unchanged
    compared to the :doc:`../NetworkingAPI/NetworkingAPI-IpStickiness` version.
    In fact, in the repository, they are symbolic links to the files from that
    tutorial.

    While we encourage you to familiarize yourself with the code in those files,
    it is not really relevant to this article. It is an example code that
    establishes a basic connection with a LwM2M server and implements a simple
    but full-featured networking layer.

Global initialization
---------------------

Just like with the network layer, the APIs that need to be implemented are
private, so we also start with manually including the forward declarations, as
quoted in the :doc:`previous article <../CustomTLS>`:

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c

    avs_error_t _avs_net_initialize_global_ssl_state(void);
    void _avs_net_cleanup_global_ssl_state(void);
    avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket,
                                           const void *socket_configuration);
    avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket,
                                            const void *socket_configuration);

The OpenSSL library needs global initialization, so ``OPENSSL_init_ssl``
function is called in ``_avs_net_initialize_global_ssl_state()``. There is no
need for any explicit cleanup, so the ``_avs_net_cleanup_global_ssl_state()``
function can be left empty:

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c

    avs_error_t _avs_net_initialize_global_ssl_state(void) {
        if (!OPENSSL_init_ssl(OPENSSL_INIT_ADD_ALL_CIPHERS
                                      | OPENSSL_INIT_ADD_ALL_DIGESTS,
                              NULL)) {
            return avs_errno(AVS_EPROTO);
        }
        return AVS_OK;
    }

    void _avs_net_cleanup_global_ssl_state(void) {}

TLS socket structure stub
-------------------------

In The TLS socket object, the following information will need to be kept:

* The "backend socket", the underlying unencrypted socket. We will use the
  previously implemented ``avs_net`` socket for that purpose, so that as many
  features as possible can be simply forwarded.

* The ``SSL_CTX`` object that contains the state that OpenSSL intends to be
  reused between similar connections.

* The ``SSL`` object that contains the per-connection OpenSSL state.

The way TLS-related APIs are designed in Anjay makes it impossible to share an
``SSL_CTX`` object between multiple connections, so both of the above need to be
present for each connection.

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        avs_net_socket_t *backend_socket;
        SSL_CTX *ctx;
        SSL *ssl;
    } tls_socket_impl_t;

Implementing socket methods
---------------------------

Forwarded functions
^^^^^^^^^^^^^^^^^^^

Most of the auxiliary functions not related to actual data transmission or
handshakes, can just forward the calls to the underlying backend socket:

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c
    :emphasize-lines: 9-12

    static avs_error_t
    tls_bind(avs_net_socket_t *sock_, const char *address, const char *port) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_bind(sock->backend_socket, address, port);
    }

    static avs_error_t tls_close(avs_net_socket_t *sock_) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        if (sock->ssl) {
            SSL_free(sock->ssl);
            sock->ssl = NULL;
        }
        return avs_net_socket_close(sock->backend_socket);
    }

    static avs_error_t tls_shutdown(avs_net_socket_t *sock_) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_shutdown(sock->backend_socket);
    }

    static avs_error_t tls_cleanup(avs_net_socket_t **sock_ptr) {
        return avs_errno(AVS_ENOTSUP);
    }

    static const void *tls_system_socket(avs_net_socket_t *sock_) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_get_system(sock->backend_socket);
    }

    static avs_error_t tls_remote_host(avs_net_socket_t *sock_,
                                       char *out_buffer,
                                       size_t out_buffer_size) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_get_remote_host(sock->backend_socket, out_buffer,
                                              out_buffer_size);
    }

    static avs_error_t tls_remote_hostname(avs_net_socket_t *sock_,
                                           char *out_buffer,
                                           size_t out_buffer_size) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_get_remote_hostname(sock->backend_socket, out_buffer,
                                                  out_buffer_size);
    }

    static avs_error_t tls_remote_port(avs_net_socket_t *sock_,
                                       char *out_buffer,
                                       size_t out_buffer_size) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_get_remote_port(sock->backend_socket, out_buffer,
                                              out_buffer_size);
    }

    static avs_error_t tls_local_port(avs_net_socket_t *sock_,
                                      char *out_buffer,
                                      size_t out_buffer_size) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_get_local_port(sock->backend_socket, out_buffer,
                                             out_buffer_size);
    }

    static avs_error_t tls_get_opt(avs_net_socket_t *sock_,
                                   avs_net_socket_opt_key_t option_key,
                                   avs_net_socket_opt_value_t *out_option_value) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_get_opt(sock->backend_socket, option_key,
                                      out_option_value);
    }

    static avs_error_t tls_set_opt(avs_net_socket_t *sock_,
                                   avs_net_socket_opt_key_t option_key,
                                   avs_net_socket_opt_value_t option_value) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        return avs_net_socket_set_opt(sock->backend_socket, option_key,
                                      option_value);
    }

The ``tls_close`` function additionally frees the ``SSL`` object, as OpenSSL
documentation recommends creating new one for each connection - see
`SSL_clear <https://www.openssl.org/docs/man1.1.1/man3/SSL_clear.html>`_ for
details.

Connect method stub
^^^^^^^^^^^^^^^^^^^

The ``connect`` method is supposed to perform the TLS handshake, but this is
beyond the scope of this boilerplate stub. However, let's extract a separate
function that will be used for this purpose:

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c

    static avs_error_t perform_handshake(tls_socket_impl_t *sock,
                                         const char *host) {
        return avs_errno(AVS_ENOTSUP);
    }

    static avs_error_t
    tls_connect(avs_net_socket_t *sock_, const char *host, const char *port) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        avs_error_t err;
        if (avs_is_err((
                    err = avs_net_socket_connect(sock->backend_socket, host, port)))
                || avs_is_err((err = perform_handshake(sock, host)))) {
            if (sock->ssl) {
                SSL_free(sock->ssl);
                sock->ssl = NULL;
            }
            avs_net_socket_close(sock->backend_socket);
        }
        return err;
    }

Send method
^^^^^^^^^^^

The ``send`` method is very similar to the one in the unencrypted socket
implementation, but using ``SSL_write`` instead of ``send``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c

    static avs_error_t
    tls_send(avs_net_socket_t *sock_, const void *buffer, size_t buffer_length) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        int result = SSL_write(sock->ssl, buffer, (int) buffer_length);
        if (result < 0 || (size_t) result < buffer_length) {
            return avs_errno(AVS_EPROTO);
        }
        return AVS_OK;
    }

Receive method
^^^^^^^^^^^^^^

The ``receive`` method is also very similar to the one in the unencrypted socket
implementation. However, as we do not have direct access to the file descriptor
and the configured receive timeout, we need to extract them from the backend
socket before actually calling ``poll()``.

These additional operations may be unnecessary if we implemented the TLS socket
so that OpenSSL would actually call the underlying unencrypted socket. However,
in this tutorial, the default BIO implementations from OpenSSL will be used for
simplicity.

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c
    :emphasize-lines: 6-13

    static avs_error_t tls_receive(avs_net_socket_t *sock_,
                                   size_t *out_bytes_received,
                                   void *buffer,
                                   size_t buffer_length) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        const void *fd_ptr = avs_net_socket_get_system(sock->backend_socket);
        avs_net_socket_opt_value_t timeout;
        if (!fd_ptr
                || avs_is_err(avs_net_socket_get_opt(
                           sock->backend_socket, AVS_NET_SOCKET_OPT_RECV_TIMEOUT,
                           &timeout))) {
            return avs_errno(AVS_EBADF);
        }
        struct pollfd pfd = {
            .fd = *(const int *) fd_ptr,
            .events = POLLIN
        };
        int64_t timeout_ms;
        if (avs_time_duration_to_scalar(&timeout_ms, AVS_TIME_MS,
                                        timeout.recv_timeout)) {
            timeout_ms = -1;
        } else if (timeout_ms < 0) {
            timeout_ms = 0;
        }
        if (poll(&pfd, 1, (int) timeout_ms) == 0) {
            return avs_errno(AVS_ETIMEDOUT);
        }
        int bytes_received = SSL_read(sock->ssl, buffer, (int) buffer_length);
        if (bytes_received < 0) {
            return avs_errno(AVS_EPROTO);
        }
        *out_bytes_received = (size_t) bytes_received;
        if (buffer_length > 0 && (size_t) bytes_received == buffer_length) {
            return avs_errno(AVS_EMSGSIZE);
        }
        return AVS_OK;
    }

Virtual method table and constructor function stubs
---------------------------------------------------

With all the methods implemented or stubbed, we are ready to declare the virtual
method table. However, actual socket creation will be described in the next
tutorial:

.. highlight:: c
.. snippet-source:: examples/custom-tls/stub/src/tls_impl.c

    static const avs_net_socket_v_table_t TLS_SOCKET_VTABLE = {
        .connect = tls_connect,
        .send = tls_send,
        .receive = tls_receive,
        .bind = tls_bind,
        .close = tls_close,
        .shutdown = tls_shutdown,
        .cleanup = tls_cleanup,
        .get_system_socket = tls_system_socket,
        .get_remote_host = tls_remote_host,
        .get_remote_hostname = tls_remote_hostname,
        .get_remote_port = tls_remote_port,
        .get_local_port = tls_local_port,
        .get_opt = tls_get_opt,
        .set_opt = tls_set_opt
    };

    avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket_ptr,
                                            const void *configuration) {
        return avs_errno(AVS_ENOTSUP);
    }

    avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket_ptr,
                                           const void *configuration) {
        return avs_errno(AVS_ENOTSUP);
    }
