..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Session resumption support
==========================

.. contents:: :local:

Introduction
------------

This tutorial builds up on :doc:`the previous one <CustomTLS-Minimal>` and adds
support for DTLS session resumption mechanism.

DTLS session resumption support is essential for LwM2M device operation,
especially if relatively frequent connectivity drops are anticipated and/or if
network traffic is considered expensive. Each full DTLS handshake creates a new
CoAP endpoint association, which forces the device to send a new Register
message, in turn forcing the server to re-establish any Observe requests
required.

Session resumption solves this problem - a resumed session is considered a
continuation of the previous CoAP endpoint association, so as long as the
registration lifetime has not expired, communication can continue as if nothing
happened, even if the device's IP address changed.

Two approaches to handling session resumption will be considered, one based on
the TLS library's native session handling capabilities, and one involving a raw
buffer provided by the Anjay library.

Simple session persistence
--------------------------

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/resumption-simple
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/resumption-simple>`_
    in the Anjay source directory.

OpenSSL provides an object type that represents a (D)TLS session,
``SSL_SESSION``, and a "session cache" mechanism that allows storing the
sessions and resuming them on demand as needed.

Unfortunately, while the session cache is completely automatic for server-side
connections, on the client side the session objects need to be stored and
restored manually.

So we will start by adding a field to the ``tls_socket_impl_t`` structure that
will hold the session information even while the socket is disconnected:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-simple/src/tls_impl.c
    :emphasize-lines: 12

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        avs_net_socket_t *backend_socket;
        SSL_CTX *ctx;
        SSL *ssl;

        char psk[256];
        size_t psk_size;
        char identity[128];
        size_t identity_size;

        SSL_SESSION *last_session;
    } tls_socket_impl_t;

Of course, we need to make sure that this object is cleaned up in
``tls_cleanup``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-simple/src/tls_impl.c
    :emphasize-lines: 10-12

    static avs_error_t tls_cleanup(avs_net_socket_t **sock_ptr) {
        avs_error_t err = AVS_OK;
        if (sock_ptr && *sock_ptr) {
            tls_socket_impl_t *sock = (tls_socket_impl_t *) *sock_ptr;
            tls_close(*sock_ptr);
            avs_net_socket_cleanup(&sock->backend_socket);
            if (sock->ctx) {
                SSL_CTX_free(sock->ctx);
            }
            if (sock->last_session) {
                SSL_SESSION_free(sock->last_session);
            }
            avs_free(sock);
            *sock_ptr = NULL;
        }
        return err;
    }

.. note::

    In some TLS implementations, session persistence and resumption may be
    implemented within the library itself. For example, in the nrfxlib Modem
    library, session persistence happens automatically if the
    `NRF_SO_SEC_SESSION_CACHE <https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrfxlib/nrf_modem/doc/api.html#c.NRF_SO_SEC_SESSION_CACHE>`_
    option is enabled.

    With such implementations, it is not necessary to add dedicated fields or
    save/restore logic, and it is OK to just enable the built-in mechanisms.

Saving the session
^^^^^^^^^^^^^^^^^^

Now that there is a field to store this information, we may proceed with
configuring the session cache so that each new session is written there:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-simple/src/tls_impl.c
    :emphasize-lines: 1-11, 49-52

    static int new_session_cb(SSL *ssl, SSL_SESSION *sess) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) SSL_get_app_data(ssl);
        SSL_SESSION *sess_dup = SSL_SESSION_dup(sess);
        if (sess_dup) {
            if (sock->last_session) {
                SSL_SESSION_free(sock->last_session);
            }
            sock->last_session = sess_dup;
        }
        return 0;
    }

    avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket_ptr,
                                            const void *configuration_) {
        assert(socket_ptr);
        assert(!*socket_ptr);
        assert(configuration_);
        const avs_net_ssl_configuration_t *configuration =
                (const avs_net_ssl_configuration_t *) configuration_;
        tls_socket_impl_t *socket =
                (tls_socket_impl_t *) avs_calloc(1, sizeof(tls_socket_impl_t));
        if (!socket) {
            return avs_errno(AVS_ENOMEM);
        }
        *socket_ptr = (avs_net_socket_t *) socket;
        socket->operations = &TLS_SOCKET_VTABLE;

        avs_error_t err = AVS_OK;
        if (avs_is_ok((err = avs_net_udp_socket_create(
                               &socket->backend_socket,
                               &configuration->backend_configuration)))
                && !(socket->ctx = SSL_CTX_new(DTLS_method()))) {
            err = avs_errno(AVS_ENOMEM);
        }
        if (avs_is_ok(err)) {
            switch (configuration->security.mode) {
            case AVS_NET_SECURITY_PSK:
                err = configure_psk(socket, &configuration->security.data.psk);
                break;
            default:
                err = avs_errno(AVS_ENOTSUP);
            }
        }
        if (avs_is_err(err)) {
            avs_net_socket_cleanup(socket_ptr);
            return err;
        }
        SSL_CTX_set_mode(socket->ctx, SSL_MODE_AUTO_RETRY);
        SSL_CTX_set_session_cache_mode(socket->ctx,
                                       SSL_SESS_CACHE_CLIENT
                                               | SSL_SESS_CACHE_NO_INTERNAL_STORE);
        SSL_CTX_sess_set_new_cb(socket->ctx, new_session_cb);
        return AVS_OK;
    }

Note how ``SSL_SESSION_dup()`` is used in the ``new_session_cb`` function - this
is because the ``SSL_SESSION`` object also contains the transient state of the
session that might change later and make it impossible to restore it later, e.g.
when the connection is closed. This is why we want an exact clone of the session
state as it was just after the handshake.

Restoring the session
^^^^^^^^^^^^^^^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-simple/src/tls_impl.c
    :emphasize-lines: 29-35

    static avs_error_t perform_handshake(tls_socket_impl_t *sock,
                                         const char *host) {
        union {
            struct sockaddr addr;
            struct sockaddr_storage storage;
        } peername;
        const void *fd_ptr = avs_net_socket_get_system(sock->backend_socket);
        if (!fd_ptr
                || getpeername(*(const int *) fd_ptr, &peername.addr,
                               &(socklen_t) { sizeof(peername) })) {
            return avs_errno(AVS_EBADF);
        }

        sock->ssl = SSL_new(sock->ctx);
        if (!sock->ssl) {
            return avs_errno(AVS_ENOMEM);
        }

        SSL_set_app_data(sock->ssl, sock);
        SSL_set_tlsext_host_name(sock->ssl, host);

        BIO *bio = BIO_new_dgram(*(const int *) fd_ptr, 0);
        if (!bio) {
            return avs_errno(AVS_ENOMEM);
        }
        BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peername.addr);
        SSL_set_bio(sock->ssl, bio, bio);

        if (sock->last_session) {
            SSL_SESSION *session_dup = SSL_SESSION_dup(sock->last_session);
            if (session_dup) {
                SSL_set_session(sock->ssl, session_dup);
                SSL_SESSION_free(session_dup);
            }
        }

        if (SSL_connect(sock->ssl) <= 0) {
            return avs_errno(AVS_EPROTO);
        }
        return AVS_OK;
    }

If there is a stored session, restoring it just requires calling
``SSL_set_session()`` right before ``SSL_connect()``. We also use
``SSL_SESSSION_dup()`` here to avoid modifying the object already stored in
the ``last_session`` field.

The ``AVS_NET_SOCKET_OPT_SESSION_RESUMED`` option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Anjay library needs to know whether the "connect" operation resulted in an
establishment of a completely new session, or a resumption of an existing one.
This information is used to determine whether sending the Register message is
necessary.

The interface to get this information from the socket is the
``AVS_NET_SOCKET_OPT_SESSION_RESUMED`` option, used through the "get_opt"
operation.

For OpenSSL, this can be forwarded to the call to ``SSL_session_reused()``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-simple/src/tls_impl.c
    :emphasize-lines: 15-17

    static avs_error_t tls_get_opt(avs_net_socket_t *sock_,
                                   avs_net_socket_opt_key_t option_key,
                                   avs_net_socket_opt_value_t *out_option_value) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        switch (option_key) {
        case AVS_NET_SOCKET_OPT_INNER_MTU: {
            avs_error_t err = avs_net_socket_get_opt(sock->backend_socket,
                                                     AVS_NET_SOCKET_OPT_INNER_MTU,
                                                     out_option_value);
            if (avs_is_ok(err)) {
                out_option_value->mtu = AVS_MAX(out_option_value->mtu - 64, 0);
            }
            return err;
        }
        case AVS_NET_SOCKET_OPT_SESSION_RESUMED:
            out_option_value->flag = (sock->ssl && SSL_session_reused(sock->ssl));
            return AVS_OK;
        default:
            return avs_net_socket_get_opt(sock->backend_socket, option_key,
                                          out_option_value);
        }
    }

.. note::

    In many TLS implementations, this information may be very hard or impossible
    to query. If that is the case, the TLS integration layer may assume one of
    two possible strategies:

    * **Always assume that a new session has been established**, or indeed **do
      not support session resumption** at all. This is the safe option, as it
      ensures proper interoperability and behavior at all times.

      However, this might lead to a lot of network traffic being wasted for the
      Register and Observe messages after each handshake.

    * **Always assume that a previous session has been resumed.** Note that the
      session state is not the only factor in deciding whether to send the
      Register message, so it will still be sent e.g. if lifetime of the
      previous registration expired, or if it is the first registration with a
      given server.

      However, this assumption **might be dangerous** in case of false positives
      - if a full handshake has been performed, but a session resumption is
      reported by the code, the connection, depending on the specific LwM2M
      server implementation, may be unusable until the registration lifetime
      expires, which is expected to eventually trigger the Register message.
      In such circumstances, non-confirmable Notify messages will be lost,
      delivery of confirmable Notify messages will fail, but not trigger any
      additional actions (see
      :doc:`../../AdvancedTopics/AT-NetworkErrorHandling`), and delivery of Send
      messages will also fail (with failures reported to the user code) during
      this time.

      You might nevertheless consider this strategy if you are confident that
      the session resumption will succeed most of the time in your environment,
      or if the trade-off of temporary connectivity loss for up to one
      connection lifetime period is acceptable for your application.

    If the ``AVS_NET_SOCKET_OPT_SESSION_RESUMED`` option is unimplemented or
    querying it fails, the library will behave the same way as if ``false`` was
    returned, i.e. a fresh session will be assumed, prioritizing safety over
    network traffic usage.

Limitations
^^^^^^^^^^^

As implemented above, the session is only persisted for as long as the socket
object exists. This is fine for most of the cases. However, the commercial
version of Anjay offers the ``anjay_new_from_core_persistence()`` and
``anjay_delete_with_core_persistence()`` APIs that allow persisting the
transient connection state to non-volatile memory. This transient state includes
the (D)TLS session information.

For this reason, the ``avs_net`` socket API includes configuration options that
allow specifying a dedicated buffer for storing the session information. The
next section of this article will showcase an alternate implementation of the
session resumption mechanism that implements it.

.. note::

    Even if you implement session resumption without the use of the
    ``session_resumption_buffer`` APIs, Anjay will allocate memory for such
    buffers, one for each server connection. You can control the size of those
    buffers via the ``DTLS_SESSION_BUFFER_SIZE`` CMake option or the
    ``ANJAY_DTLS_SESSION_BUFFER_SIZE`` macro in ``anjay_config.h``.

    This value will be used in array size declarations, so the value of 0 may
    not be acceptable for some compilers.

Buffer-based session persistence
--------------------------------

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/resumption-buffer
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/resumption-buffer>`_
    in the Anjay source directory.

.. note::

    In some implementations, such as the `nrfxlib Modem library
    <https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrfxlib/nrf_modem/README.html>`_,
    session information may be persisted in non-volatile memory across reboots
    directly by the underlying implementation. In that case, it is fine to rely
    on that mechanism and ignore the ``session_resumption_buffer`` field, even
    if the ``anjay_new_from_core_persistence()`` and
    ``anjay_delete_with_core_persistence()`` APIs will be utilized.

This variant is very similar to the previous one, but to address the limitation
mentioned above, we will serialize the session information into the buffer
supplied via socket configuration.

That means that instead of keeping an ``SSL_SESSION`` object in the socket
state, we need to store the information about the buffer:


.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-buffer/src/tls_impl.c
    :emphasize-lines: 12-13

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        avs_net_socket_t *backend_socket;
        SSL_CTX *ctx;
        SSL *ssl;

        char psk[256];
        size_t psk_size;
        char identity[128];
        size_t identity_size;

        void *session_resumption_buffer;
        size_t session_resumption_buffer_size;
    } tls_socket_impl_t;

This buffer is allocated outside the socket object, and not owned by it, so we
are not putting any deallocation in ``tls_cleanup`` this time.

However, we need to copy this pointer and size information from the
configuration structure when initializing the socket:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-buffer/src/tls_impl.c
    :emphasize-lines: 37-47

    avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket_ptr,
                                            const void *configuration_) {
        assert(socket_ptr);
        assert(!*socket_ptr);
        assert(configuration_);
        const avs_net_ssl_configuration_t *configuration =
                (const avs_net_ssl_configuration_t *) configuration_;
        tls_socket_impl_t *socket =
                (tls_socket_impl_t *) avs_calloc(1, sizeof(tls_socket_impl_t));
        if (!socket) {
            return avs_errno(AVS_ENOMEM);
        }
        *socket_ptr = (avs_net_socket_t *) socket;
        socket->operations = &TLS_SOCKET_VTABLE;

        avs_error_t err = AVS_OK;
        if (avs_is_ok((err = avs_net_udp_socket_create(
                               &socket->backend_socket,
                               &configuration->backend_configuration)))
                && !(socket->ctx = SSL_CTX_new(DTLS_method()))) {
            err = avs_errno(AVS_ENOMEM);
        }
        if (avs_is_ok(err)) {
            switch (configuration->security.mode) {
            case AVS_NET_SECURITY_PSK:
                err = configure_psk(socket, &configuration->security.data.psk);
                break;
            default:
                err = avs_errno(AVS_ENOTSUP);
            }
        }
        if (avs_is_err(err)) {
            avs_net_socket_cleanup(socket_ptr);
            return err;
        }
        SSL_CTX_set_mode(socket->ctx, SSL_MODE_AUTO_RETRY);
        if (configuration->session_resumption_buffer_size > 0) {
            assert(configuration->session_resumption_buffer);
            socket->session_resumption_buffer =
                    configuration->session_resumption_buffer;
            socket->session_resumption_buffer_size =
                    configuration->session_resumption_buffer_size;
            SSL_CTX_set_session_cache_mode(
                    socket->ctx,
                    SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
            SSL_CTX_sess_set_new_cb(socket->ctx, new_session_cb);
        }
        return AVS_OK;
    }

Saving the session
^^^^^^^^^^^^^^^^^^

The snippet above already includes the ``SSL_CTX_set_session_cache_mode()`` and
``SSL_CTX_sess_set_new_cb()`` calls introduced in the previous version. However
this time, the ``new_session_cb`` callback looks very different:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-buffer/src/tls_impl.c

    static int new_session_cb(SSL *ssl, SSL_SESSION *sess) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) SSL_get_app_data(ssl);
        int serialized_size = i2d_SSL_SESSION(sess, NULL);
        if (serialized_size > 0
                && (size_t) serialized_size
                               <= sock->session_resumption_buffer_size) {
            unsigned char *ptr = (unsigned char *) sock->session_resumption_buffer;
            i2d_SSL_SESSION(sess, &ptr);
        }
        return 0;
    }

OpenSSL provides APIs for serializing and deserializing the ``SSL_SESSION``
objects, and that is naturally what we use for this purpose.

When implementing session serialization in your code, you don't need to adhere
to any particular data format. However, please bear the following things in
mind:

* The data is expected to be stored relatively short-term, either within a
  single execution of the application, or across a single restart (using
  ``anjay_new_from_core_persistence()`` and
  ``anjay_delete_with_core_persistence()``).

* A firmware update **MAY** happen during that aforementioned single restart.

* While it is preferred to retain compatibility of the format across firmware
  versions, it is also acceptable to reject old incompatible data.

  * Full handshake shall be performed in such circumstance, and this fact shall
    be appropriately reported via the ``AVS_NET_SOCKET_OPT_SESSION_RESUMED``
    option.

  * The restoring code shall be prepared to handle invalid input. In case of
    invalid input, it should gracefully fail and revert to performing full
    handshake.

* The serialized session data is not intended to be moved across hardware units.
  It is not a problem if the session data is only restorable on the same machine
  that generated it.

Restoring the session
^^^^^^^^^^^^^^^^^^^^^

Much like in the previous version, we need to call ``SSL_set_session()`` with
the restored session before calling ``SSL_connect()``.

However, in this version, we need to deserialize the session using
``d2i_SSL_SESSION()`` first:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-buffer/src/tls_impl.c
    :emphasize-lines: 29-39

    static avs_error_t perform_handshake(tls_socket_impl_t *sock,
                                         const char *host) {
        union {
            struct sockaddr addr;
            struct sockaddr_storage storage;
        } peername;
        const void *fd_ptr = avs_net_socket_get_system(sock->backend_socket);
        if (!fd_ptr
                || getpeername(*(const int *) fd_ptr, &peername.addr,
                               &(socklen_t) { sizeof(peername) })) {
            return avs_errno(AVS_EBADF);
        }

        sock->ssl = SSL_new(sock->ctx);
        if (!sock->ssl) {
            return avs_errno(AVS_ENOMEM);
        }

        SSL_set_app_data(sock->ssl, sock);
        SSL_set_tlsext_host_name(sock->ssl, host);

        BIO *bio = BIO_new_dgram(*(const int *) fd_ptr, 0);
        if (!bio) {
            return avs_errno(AVS_ENOMEM);
        }
        BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peername.addr);
        SSL_set_bio(sock->ssl, bio, bio);

        if (sock->session_resumption_buffer) {
            const unsigned char *ptr =
                    (const unsigned char *) sock->session_resumption_buffer;
            SSL_SESSION *session =
                    d2i_SSL_SESSION(NULL, &ptr,
                                    sock->session_resumption_buffer_size);
            if (session) {
                SSL_set_session(sock->ssl, session);
                SSL_SESSION_free(session);
            }
        }

        if (SSL_connect(sock->ssl) <= 0) {
            return avs_errno(AVS_EPROTO);
        }
        return AVS_OK;
    }

Of course, we also need to support the ``AVS_NET_SOCKET_OPT_SESSION_RESUMED``
option, which in this case looks identical:

.. highlight:: c
.. snippet-source:: examples/custom-tls/resumption-buffer/src/tls_impl.c
    :emphasize-lines: 15-17

    static avs_error_t tls_get_opt(avs_net_socket_t *sock_,
                                   avs_net_socket_opt_key_t option_key,
                                   avs_net_socket_opt_value_t *out_option_value) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        switch (option_key) {
        case AVS_NET_SOCKET_OPT_INNER_MTU: {
            avs_error_t err = avs_net_socket_get_opt(sock->backend_socket,
                                                     AVS_NET_SOCKET_OPT_INNER_MTU,
                                                     out_option_value);
            if (avs_is_ok(err)) {
                out_option_value->mtu = AVS_MAX(out_option_value->mtu - 64, 0);
            }
            return err;
        }
        case AVS_NET_SOCKET_OPT_SESSION_RESUMED:
            out_option_value->flag = (sock->ssl && SSL_session_reused(sock->ssl));
            return AVS_OK;
        default:
            return avs_net_socket_get_opt(sock->backend_socket, option_key,
                                          out_option_value);
        }
    }
