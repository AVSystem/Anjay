..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Basic certificate support
=========================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/certificates-basic
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/certificates-basic>`_
    in the Anjay source directory.

Introduction
------------

This tutorials builds up on :doc:`the previous one <CustomTLS-ConfigFeatures>`
and adds basic support for the security mode based on certificates and public
key infrastructure.

.. note::

    In this tutorial, the ``main.c`` file has been replaced with a slightly
    modified version of the one from the
    :doc:`../../AdvancedTopics/AT-Certificates` tutorial.

    This also means that for simplicity, the tutorial project now depends on
    the ``WITH_EVENT_LOOP`` CMake option enabled in Anjay, and the
    :doc:`../NetworkingAPI/NetworkingAPI-EventLoopSupport` in the networking
    layer.

    Compared to the ``main.c`` file from the
    :doc:`../../AdvancedTopics/AT-Certificates` tutorial, logic related to
    loading the server certificate has been removed. This means that the
    security information is now configured as follows:

    .. highlight:: c
    .. snippet-source:: examples/custom-tls/certificates-basic/src/main.c

            if (load_buffer_from_file(
                        (uint8_t **) &security_instance.public_cert_or_psk_identity,
                        &security_instance.public_cert_or_psk_identity_size,
                        "client_cert.der")
                    || load_buffer_from_file(
                               (uint8_t **) &security_instance.private_cert_or_psk_key,
                               &security_instance.private_cert_or_psk_key_size,
                               "client_key.der")) {
                result = -1;
                goto cleanup;
            }

.. warning::

    Verifying the server certificate, as specified in LwM2M, does not work with
    the set of features implemented in this article. See the
    :ref:`custom-tls-api-certificates-basic-limitations` section and the next
    article for details.

Adding support for the certificate mode
---------------------------------------

In ``avs_net``, the security mode of the socket - either PSK or certificates -
is configured by the ``mode`` field in the ``avs_net_security_mode_t`` structure
(which is a tagged union, with the ``mode`` field acting as the tag), which
itself is contained in the ``security`` field of
``avs_net_ssl_configuration_t``.

:ref:`In the minimal implementation tutorial <custom-tls-api-create>`, we have
written a ``switch`` over that field in ``_avs_net_create_dtls_socket()``, even
though only the PSK mode was supported there. It is now time to add the second
case for the certificate mode:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c
    :emphasize-lines: 31-33

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
            case AVS_NET_SECURITY_CERTIFICATE:
                err = configure_certs(socket, &configuration->security.data.cert);
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

The ``configure_certs()`` function mentioned in the snippet above is an analog
of ``configure_psk()``, that loads and configures all the necessary security
credentials:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c

    static avs_error_t configure_certs(tls_socket_impl_t *sock,
                                       const avs_net_certificate_info_t *certs) {
        if (certs->server_cert_validation) {
            if (!certs->ignore_system_trust_store) {
                SSL_CTX_set_default_verify_paths(sock->ctx);
            }
            X509_STORE *store = SSL_CTX_get_cert_store(sock->ctx);
            avs_error_t err;
            if (avs_is_err((err = configure_trusted_certs(
                                    store, &certs->trusted_certs.desc)))
                    || avs_is_err((err = configure_cert_revocation_lists(
                                           store,
                                           &certs->cert_revocation_lists.desc)))) {
                return err;
            }
            SSL_CTX_set_verify(sock->ctx, SSL_VERIFY_PEER, NULL);
        } else {
            SSL_CTX_set_verify(sock->ctx, SSL_VERIFY_NONE, NULL);
        }

        if (certs->client_cert.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
            avs_error_t err;
            if (avs_is_err((err = configure_client_cert(sock->ctx,
                                                        &certs->client_cert)))
                    || avs_is_err(err = configure_client_key(sock->ctx,
                                                             &certs->client_key))) {
                return err;
            }
        }

        return AVS_OK;
    }

The ``server_cert_validation`` field acts as a master switch that controls
whether the peer certificate shall be verified at all. This controls the
verification mode set using ``SSL_CTX_set_verify()``, but also all logic related
to loading the trust store is disabled if it is set to ``false``.

The ``ignore_system_trust_store`` flag controls whether the default system trust
store shall be loaded for this socket. In Anjay, it is usually set to ``true``.
It may only be ``false``, if the ``use_system_trust_store`` is enabled in
``anjay_configuration_t``. If your platform does not have a concept of a system
trust store, it is safe to ignore this setting altogether.

The rest of the code in this function calls auxiliary functions that load all
the security credential types: trusted certificates, certificate revocation
lists, the client certificate and the client private key.

Loading security credentials
----------------------------

The security credentials related to the public key infrastructure utilize the
``avs_crypto_security_info_union_t`` that has been previously described in
:ref:`custom-tls-security-info-union-type` chapter. You might want to recap the
information contained there before continuing with this tutorial.

.. important::

    The security credential objects passed to the
    ``_avs_net_create_dtls_socket()`` may be deleted after that call completes.
    For this reason, the credential data needs to be actually copied.

    Please carefully check whether credentials are passed by value or by
    reference in the TLS backend you are integrating with.

Loading client certificates
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Client certificates is the simplest case, as we only need to load a single
certificate, handling the ``AVS_CRYPTO_DATA_SOURCE_BUFFER`` case:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c

    static avs_error_t
    configure_client_cert(SSL_CTX *ctx,
                          const avs_crypto_certificate_chain_info_t *client_cert) {
        switch (client_cert->desc.source) {
        case AVS_CRYPTO_DATA_SOURCE_BUFFER: {
            const unsigned char *ptr =
                    (const unsigned char *) client_cert->desc.info.buffer.buffer;
            X509 *cert = d2i_X509(NULL, &ptr,
                                  (long) client_cert->desc.info.buffer.buffer_size);
            if (!cert) {
                return avs_errno(AVS_EPROTO);
            }

            int result = SSL_CTX_use_certificate(ctx, cert);
            X509_free(cert);
            if (result != 1) {
                return avs_errno(AVS_EPROTO);
            }
            return AVS_OK;
        }
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

.. note::

    In this tutorial, only DER-encoded credentials are supported. This is most
    important and enough for compatibility with LwM2M. However, you may want to
    also support the PEM format. If both formats are supported, they shall be
    autodetected based on the contents of the file or buffer.

Loading client private keys
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The code for loading client private keys is very similar, although we want to
make sure that the ``password`` field is not used.

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c
    :emphasize-lines: 6-8

    static avs_error_t
    configure_client_key(SSL_CTX *ctx,
                         const avs_crypto_private_key_info_t *client_key) {
        switch (client_key->desc.source) {
        case AVS_CRYPTO_DATA_SOURCE_BUFFER: {
            if (client_key->desc.info.buffer.password) {
                return avs_errno(AVS_ENOTSUP);
            }
            const unsigned char *ptr =
                    (const unsigned char *) client_key->desc.info.buffer.buffer;
            EVP_PKEY *key = d2i_AutoPrivateKey(
                    NULL, &ptr, (long) client_key->desc.info.buffer.buffer_size);
            if (!key) {
                return avs_errno(AVS_EPROTO);
            }

            int result = SSL_CTX_use_PrivateKey(ctx, key);
            EVP_PKEY_free(key);
            if (result != 1) {
                return avs_errno(AVS_EPROTO);
            }
            return AVS_OK;
        }
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

Loading trusted certificates
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For the trusted certificates, we need to support the empty and compound sources
in addition to loading a simple single buffer:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c
    :emphasize-lines: 7, 12-13, 27-28, 33-52

    #include <openssl/err.h>

    // ...

    static avs_error_t
    configure_trusted_certs(X509_STORE *store,
                            const avs_crypto_security_info_union_t *trusted_certs) {
        if (!trusted_certs) {
            return avs_errno(AVS_EINVAL);
        }
        switch (trusted_certs->source) {
        case AVS_CRYPTO_DATA_SOURCE_EMPTY:
            return AVS_OK;
        case AVS_CRYPTO_DATA_SOURCE_BUFFER: {
            const unsigned char *ptr =
                    (const unsigned char *) trusted_certs->info.buffer.buffer;
            X509 *cert = d2i_X509(NULL, &ptr,
                                  (long) trusted_certs->info.buffer.buffer_size);
            if (!cert) {
                return avs_errno(AVS_EPROTO);
            }

            ERR_clear_error();
            int result = X509_STORE_add_cert(store, cert);
            X509_free(cert);
            if (!result
                    && ERR_GET_REASON(ERR_get_error())
                                   != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
                return avs_errno(AVS_EPROTO);
            }
            return AVS_OK;
        }
        case AVS_CRYPTO_DATA_SOURCE_ARRAY: {
            avs_error_t err = AVS_OK;
            for (size_t i = 0;
                 avs_is_ok(err) && i < trusted_certs->info.array.element_count;
                 ++i) {
                err = configure_trusted_certs(
                        store, &trusted_certs->info.array.array_ptr[i]);
            }
            return err;
        }
        case AVS_CRYPTO_DATA_SOURCE_LIST: {
            avs_error_t err = AVS_OK;
            AVS_LIST(avs_crypto_security_info_union_t) entry;
            AVS_LIST_FOREACH(entry, trusted_certs->info.list.list_head) {
                if (avs_is_err((err = configure_trusted_certs(store, entry)))) {
                    break;
                }
            }
            return AVS_OK;
        }
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

Please note the following additional alterations:

* This function takes an argument of type ``avs_crypto_security_info_union_t``
  instead of the ``avs_crypto_certificate_chain_info_t`` wrapper. This has been
  done so that it can be more easily called recursively.
* There is a special case for the ``X509_R_CERT_ALREADY_IN_HASH_TABLE`` error.
  Loading the same certificate multiple times shall be permitted, in case e.g.
  an explicitly specified certificate is already present in the system trust
  store.

Loading certificate revocation lists
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CRL loading function is actually almost identical to the certificate chain
loading one:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c

    static avs_error_t configure_cert_revocation_lists(
            X509_STORE *store,
            const avs_crypto_security_info_union_t *cert_revocation_lists) {
        if (!cert_revocation_lists) {
            return avs_errno(AVS_EINVAL);
        }
        switch (cert_revocation_lists->source) {
        case AVS_CRYPTO_DATA_SOURCE_EMPTY:
            return AVS_OK;
        case AVS_CRYPTO_DATA_SOURCE_BUFFER: {
            const unsigned char *ptr =
                    (const unsigned char *)
                            cert_revocation_lists->info.buffer.buffer;
            X509_CRL *crl = d2i_X509_CRL(
                    NULL, &ptr,
                    (long) cert_revocation_lists->info.buffer.buffer_size);
            if (!crl) {
                return avs_errno(AVS_EPROTO);
            }

            ERR_clear_error();
            int result = X509_STORE_add_crl(store, crl);
            X509_CRL_free(crl);
            if (result != 1) {
                return avs_errno(AVS_EPROTO);
            }
            return AVS_OK;
        }
        case AVS_CRYPTO_DATA_SOURCE_ARRAY: {
            avs_error_t err = AVS_OK;
            for (size_t i = 0;
                 avs_is_ok(err)
                 && i < cert_revocation_lists->info.array.element_count;
                 ++i) {
                err = configure_cert_revocation_lists(
                        store, &cert_revocation_lists->info.array.array_ptr[i]);
            }
            return err;
        }
        case AVS_CRYPTO_DATA_SOURCE_LIST: {
            avs_error_t err = AVS_OK;
            AVS_LIST(avs_crypto_security_info_union_t) entry;
            AVS_LIST_FOREACH(entry, cert_revocation_lists->info.list.list_head) {
                if (avs_is_err((
                            err = configure_cert_revocation_lists(store, entry)))) {
                    break;
                }
            }
            return AVS_OK;
        }
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

Enabling hostname verification
------------------------------

Now that all the credentials are properly loaded, the only thing left is to
inform the TLS library of the hostname, so that the CN or SAN fields of the
server certificate can be properly verified. This can be done by calling
``SSL_set1_host()`` just before the handshake:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-basic/src/tls_impl.c
    :emphasize-lines: 21

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
        SSL_set1_host(sock->ssl, host);

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

.. _custom-tls-api-certificates-basic-limitations:

Limitations
-----------

The implementation above is a complete basic integration with the private key
infrastructure, however it lacks a number of features that are supported by the
``avs_net`` API:

* Lack of DANE support. **This means that the Server Public Key LwM2M resource
  is not supported, and will cause a failure if used.** This is because LwM2M
  does not use standard certificate validation logic based on a trust store,
  using a custom mechanism instead. However, that mechanism is almost identical
  to the one used by `DANE
  <https://en.wikipedia.org/wiki/DNS-based_Authentication_of_Named_Entities>`_,
  so it is implemented in terms of that mechanism in Anjay and ``avs_net``.

  This feature will be discussed in the next tutorial.

* Lack of support for loading chains of more than one certificate as the client
  certificate chain, although this is rarely used.

* Lack of support for loading credential information from other sources than
  memory buffers (e.g. files). This may be used e.g. for HTTPS downloads and
  support of Hardware Security Modules.

* Lack of support for PEM encoding. This is not generally necessary for LwM2M
  compliance, but may be important for other cases, for example loading
  credentials from files, as mentioned above.

* Lack of support for the ``rebuild_client_cert_chain`` flag in
  ``avs_net_certificate_info_t``. When that flag is supported and enabled, the
  TLS implementation shall find appropriate CA certificates in the trust store,
  to rebuild the full certification chain of the single certificate specified as
  the client certificate, and send that complete chain to the server during the
  handshake.

  This feature may be required for communication with some servers. However, it
  is complex to implement, usually requiring the use of advanced low-level APIs
  of the TLS library. For this reason it will not be discussed further in the
  tutorial.
