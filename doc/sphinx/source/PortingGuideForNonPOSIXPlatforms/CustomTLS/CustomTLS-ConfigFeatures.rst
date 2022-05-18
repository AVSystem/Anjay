..
   Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Advanced configuration features
===============================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/config-features
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/config-features>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one <CustomTLS-Resumption>` and
adds support for some of the more advanced features configurable via the
``avs_net_ssl_configuration_t`` structure.

Specifically, the following features are implemented:

* Configurable DTLS version
* Configurable DTLS handshake timers
* Configurable ciphersuite list
* Overriding the hostname used for Server Name Identification

The following features present in ``avs_net_ssl_configuration_t`` are **NOT**
implemented here:

* Support for the ``additional_configuration_clb`` field. The semantics of this
  field are backend-specific, so you are free to handle it in any way you wish
  if you decide to do so. In the default integrations it is called at the very
  end of ``_avs_net_create_ssl_socket()``/``_avs_net_create_dtls_socket()`` and
  passed the ``SSL_CTX *``, ``mbedtls_ssl_config *`` or ``dtls_context_t *``
  pointer, depending on the backend.
* Support for DTLS Connection ID extension - there is no support for this in any
  version of OpenSSL for now. If you wish to implement this, you should just
  enable support for it if the ``use_connection_id`` field is set to ``true``.
* Support for the ``prng_ctx`` field. The contract for this field actually
  requires it to be populated and the intention is that the PRNG provided that
  way will be used when generating cryptographic material that depends on
  randomness. However, it is generally OK to ignore this field if entropy source
  is provided by other means.

It is generally fine to ignore some or all of the
``avs_net_ssl_configuration_t`` advanced feature settings - most of these
features are intended to expose more fine-grained control to the user, and that
can just be baked directly into the implementation if it's custom.

However, please bear in mind that ciphersuite list and Server Name
Identification hostname can be controlled through the LwM2M data model, when
LwM2M 1.1 is in use.

Configurable DTLS version
-------------------------

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c

    static avs_error_t configure_dtls_version(tls_socket_impl_t *sock,
                                              avs_net_ssl_version_t version) {
        switch (version) {
        case AVS_NET_SSL_VERSION_DEFAULT:
            return AVS_OK;
        case AVS_NET_SSL_VERSION_TLSv1:
        case AVS_NET_SSL_VERSION_TLSv1_1:
            SSL_CTX_set_min_proto_version(sock->ctx, DTLS1_VERSION);
            return AVS_OK;
        case AVS_NET_SSL_VERSION_TLSv1_2:
            SSL_CTX_set_min_proto_version(sock->ctx, DTLS1_2_VERSION);
            return AVS_OK;
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

The ``version`` values comes from the ``version`` field of the
``avs_net_ssl_configuration_t`` structure. This function will be called early on
during the socket object initialization, to configure the minimum supported
version for the given ``SSL_CTX`` object.

Version numbers related to the TLS protocol as used over TCP are used for this
field. By convention, for DTLS that shall translate to the DTLS version that was
directly based on the specified TLS version, with the exception of TLS 1.0,
which by convention also translates to DTLS 1.0, even though it was based on
TLS 1.1.

These are the intended semantics of this field - to configure the minimum
version that is allowed for communication, i.e. attempting to connect to a
server that only supports versions earlier than the one configured shall fail.

In some cases, it might only be possible to configure the *only* version
supported, i.e. configuring the context for DTLS 1.0 would cause the client to
specifically negotiate DTLS 1.0 and disallow any other version, either older or
newer. This is also an acceptable behavior for this option, although configuring
only the *minimum* version is preferred whenever possible.

Configurable DTLS handshake timers
----------------------------------

When exchanging application data, DTLS acts as a thin wrapper over the
underlying transport protocol (e.g. UDP) and does not implement any reliability
mechanisms of its own. However, basic reliability is provided during the
handshake phase.

Handshake messages are retransmitted if a response is not received for a given
timeout period. This period raises exponentially with each attempt.

By default, the first timeout is 1 second, and it is doubled with each
following retransmission, with the final timeout being 60 seconds (instead of 64
- the calculated timeout is clamped to the upper limit), after which the client
gives up and reports failure. Maximum time between the first message
transmission attempt and the failure report is thus 123 seconds (1 + 2 + 3 + 4 +
8 + 16 + 32 + 60).

Anjay APIs allow customizing these lower and upper limits of 1 and 60 seconds.
In OpenSSL, this logic can be implemented using a callback that overrides the
default doubling logic, configured using ``DTLS_set_timer_cb()``.

These values need to be stored somewhere so that we can read them during the
handshake. OpenSSL APIs use microseconds represented as ``unsigned int`` for
this purpose, so let's use that in ``tls_socket_impl_t`` as well:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c
    :emphasize-lines: 16-17

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

        char server_name_indication[256];
        unsigned int dtls_hs_timeout_min_us;
        unsigned int dtls_hs_timeout_max_us;
    } tls_socket_impl_t;

These values shall be populated based on the ``dtls_handshake_timeouts`` field
in ``avs_net_ssl_configuration_t``, and default to 1 and 60 seconds if that is
absent:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c

    static avs_error_t configure_dtls_handshake_timeouts(
            tls_socket_impl_t *sock,
            const avs_net_dtls_handshake_timeouts_t *dtls_handshake_timeouts) {
        uint64_t min_us = 1000000, max_us = 60000000;
        if (dtls_handshake_timeouts) {
            avs_time_duration_to_scalar(&min_us, AVS_TIME_US,
                                        dtls_handshake_timeouts->min);
            avs_time_duration_to_scalar(&max_us, AVS_TIME_US,
                                        dtls_handshake_timeouts->max);
        }
        sock->dtls_hs_timeout_min_us = (unsigned int) min_us;
        sock->dtls_hs_timeout_max_us = (unsigned int) max_us;
        return AVS_OK;
    }

We can now implement and apply the timer callback function:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c
    :emphasize-lines: 1-16, 45

    static unsigned int dtls_timer_cb(SSL *s, unsigned int timer_us) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) SSL_get_app_data(s);
        if (!timer_us) {
            return sock->dtls_hs_timeout_min_us;
        } else if (timer_us >= sock->dtls_hs_timeout_max_us) {
            // maximum number of retransmissions reached, let's give up
            avs_net_socket_shutdown(sock->backend_socket);
            return 0;
        } else {
            timer_us *= 2;
            if (timer_us > sock->dtls_hs_timeout_max_us) {
                timer_us = sock->dtls_hs_timeout_max_us;
            }
            return timer_us;
        }
    }

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
        DTLS_set_timer_cb(sock->ssl, dtls_timer_cb);

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

Note the call to ``avs_net_socket_shutdown(sock->backend_socket)`` - OpenSSL has
a hardcoded number of retransmissions regardless of how the timers are
calculated, so to break the process early, if needed, we ensure that the
underlying socket will not be able to receive or transmit data. This will cause
``SSL_connect()`` to fail due to failing ``send()`` or ``recv()``.

In other TLS implementations, this might not be a problem. In Mbed TLS for
example, a simple call to ``mbedtls_ssl_conf_handshake_timeout()`` already
provides the expected semantics.

Configurable ciphersuite list
-----------------------------

OpenSSL allows configuring the ciphersuite list using a specially prepared
string. For example, to configure the use of the two ciphersuites mentioned in
the LwM2M specification for the PSK mode (``TLS_PSK_WITH_AES_128_CCM_8`` and
``TLS_PSK_WITH_AES_128_CBC_SHA256``) and no others, you can configure the
ciphersuite list as ``"-ALL:PSK-AES128-CCM8:PSK-AES128-CBC-SHA256"``. The
``-ALL`` part disables the default ciphersuite list, while the other two parts
are OpenSSL-specific names for the ciphersuites.

In ``avs_net``, the ciphersuites are passed as an array of integers with
ciphersuite IDs as transmitted over the wire in TLS, so, for example,
``TLS_PSK_WITH_AES_128_CCM_8`` is represented as ``0xC0A8`` - see
https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-4
for a full list of known IDs.

We need to write a function that converts this array into the string format
expected by OpenSSL:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c
    :emphasize-lines: 11, 22

    static avs_error_t
    configure_ciphersuites(tls_socket_impl_t *sock,
                           const avs_net_socket_tls_ciphersuites_t *ciphersuites) {
        if (!ciphersuites->num_ids) {
            return AVS_OK;
        }
        SSL *dummy_ssl = SSL_new(sock->ctx);
        if (!dummy_ssl) {
            return avs_errno(AVS_ENOMEM);
        }
        char cipher_list[1024] = "-ALL";
        char *cipher_list_ptr = cipher_list + strlen(cipher_list);
        const char *const cipher_list_end = cipher_list + sizeof(cipher_list);
        for (size_t i = 0; i < ciphersuites->num_ids; ++i) {
            unsigned char id_as_chars[] = {
                (unsigned char) (ciphersuites->ids[i] >> 8),
                (unsigned char) (ciphersuites->ids[i] & 0xFF)
            };
            const SSL_CIPHER *cipher = SSL_CIPHER_find(dummy_ssl, id_as_chars);
            if (cipher) {
                const char *name = SSL_CIPHER_get_name(cipher);
                if (!!strstr(name, "PSK") == !!sock->psk_size
                        && cipher_list_ptr + 1 + strlen(name) < cipher_list_end) {
                    *cipher_list_ptr++ = ':';
                    strcpy(cipher_list_ptr, name);
                    cipher_list_ptr += strlen(name);
                }
            }
        }
        SSL_free(dummy_ssl);
        SSL_CTX_set_cipher_list(sock->ctx, cipher_list);
        return AVS_OK;
    }

The ``-ALL`` at the beginning disables the default configuration, which is not
done implicitly in OpenSSL.

Note the other highlighted line, with the
``!!strstr(name, "PSK") == !!sock->psk_size`` condition. This ensures that only
PSK-compatible ciphersuites are configured when PSK is in use, and that those
are not used when certificate-based security is in use (certificate support has
not been yet discussed in this tutorial, but it will be in subsequent chapters).
This is required for proper interoperability with some servers - a ciphersuite
incompatible with the intended security mode might be selected, preventing the
handshake from succeeding. This may especially occur if the server supports both
PSK and certificate modes on the same port.

Overriding the hostname used for SNI
------------------------------------

We already have most of the logic related to SNI implemented in the
``perform_handshake()`` function. However, this is currently locked to the
hostname provided to the ``connect`` operation. However, it is relatively simple
to allow overriding this value.

First, we need to reserve a place to store the overridden hostname:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c
    :emphasize-lines: 15

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

        char server_name_indication[256];
        unsigned int dtls_hs_timeout_min_us;
        unsigned int dtls_hs_timeout_max_us;
    } tls_socket_impl_t;

This value shall be populated based on the ``server_name_indication`` field
in ``avs_net_ssl_configuration_t``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c

    static avs_error_t configure_sni(tls_socket_impl_t *sock,
                                     const char *server_name_indication) {
        if (server_name_indication) {
            if (strlen(server_name_indication)
                    >= sizeof(sock->server_name_indication)) {
                return avs_errno(AVS_ENOBUFS);
            }
            strcpy(sock->server_name_indication, server_name_indication);
        }
        return AVS_OK;
    }

This value can now, if present, override the hostname when calling
``perform_handshake()``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c
    :emphasize-lines: 10-13

    static avs_error_t
    tls_connect(avs_net_socket_t *sock_, const char *host, const char *port) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        if (sock->ssl) {
            return avs_errno(AVS_EBADF);
        }
        avs_error_t err;
        if (avs_is_err((
                    err = avs_net_socket_connect(sock->backend_socket, host, port)))
                || avs_is_err((err = perform_handshake(
                                       sock, sock->server_name_indication[0]
                                                     ? sock->server_name_indication
                                                     : host)))) {
            if (sock->ssl) {
                SSL_free(sock->ssl);
                sock->ssl = NULL;
            }
            avs_net_socket_close(sock->backend_socket);
        }
        return err;
    }

Applying the configuration
--------------------------

Having written all the ``configure_*`` functions, they can be called during
socket creation in ``_avs_net_create_dtls_socket()``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/config-features/src/tls_impl.c
    :emphasize-lines: 23-25, 36-43

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
            err = configure_dtls_version(socket, configuration->version);
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
        if (avs_is_err(err)
                || avs_is_err((
                           err = configure_dtls_handshake_timeouts(
                                   socket, configuration->dtls_handshake_timeouts)))
                || avs_is_err((err = configure_ciphersuites(
                                       socket, &configuration->ciphersuites)))
                || avs_is_err((err = configure_sni(
                                       socket,
                                       configuration->server_name_indication)))) {
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
