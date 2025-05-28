..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Support for TLS over TCP
========================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/tcp-support
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/tcp-support>`_
    in the Anjay source directory.

Introduction
------------

All the previous tutorials in this chapter were implementing support for DTLS, a
variant of TLS adapted to work over unreliable datagram-based transports such as
UDP.

While that is the most important protocol required for LwM2M communication,
implementing support for traditional TLS that works over the TCP transport might
be desirable in some cases, such as support for the CoAP+TCP binding introduced
in LwM2M 1.1, or HTTPS downloads for e.g. firmware update.

This tutorial combines code from :doc:`the previous one
<CustomTLS-CertificatesAdvanced>` and from the
:doc:`../../FirmwareUpdateTutorial/FU-SecureDownloads` tutorial from the
:doc:`../../FirmwareUpdateTutorial` chapter, and updates the (D)TLS integration
layer to support regular TLS, enabling support for HTTPS firmware downloads.

.. note::

    The code for this tutorial contains the following source files:

    * ``main.c``, ``firmware_update.h``, ``time_object.c`` and ``time_object.h``
      are symbolic links to the ones from
      :doc:`../../FirmwareUpdateTutorial/FU-SecureDownloads`. Those just
      implement a simple basic LwM2M client with support for the Time and
      Firmware Update objects. Intricate knowledge of this code is not required.

    * ``firmware_update.c`` is copied from
      :doc:`../../FirmwareUpdateTutorial/FU-SecureDownloads` with slight
      modifications so that certificate and key files are loaded into memory
      buffers, and only those are passed to the TLS layer. This is necessary
      because the example TLS integration discussed in this chapter does not
      support the ``AVS_CRYPTO_DATA_SOURCE_FILE`` data source. These changes are
      discussed in greater detail below.

    * ``net_impl.c`` is a symbolic link to the file from
      :doc:`../NetworkingAPI/NetworkingAPI-IpStickiness`, just like in all other
      tutorials in this chapter.

    * ``tls_impl.c`` is copied from :doc:`the previous tutorial
      <CustomTLS-CertificatesAdvanced>` with changes to make regular TLS work.
      These changes are the main topic of this article.

Adapting the application code
-----------------------------

The application code for this tutorial is based on the application made in the
:doc:`../../FirmwareUpdateTutorial`. The security configuration used there is
supposed to load the certificates and keys from files, and we haven't
implemented support for the ``AVS_CRYPTO_DATA_SOURCE_FILE`` data source in the
TLS integration layer.

For this reason, we need to modify the ``firmware_update.c`` file so that the
certificates and keys are loaded from files into memory, before passing them
into the security configuration. This also means that they now need to be stored
in the binary DER format.

.. highlight:: c
.. snippet-source:: examples/custom-tls/tcp-support/src/firmware_update.c
    :emphasize-lines: 1-28, 41-57, 62-67

    static int
    load_buffer_from_file(uint8_t **out, size_t *out_size, const char *filename) {
        FILE *f = fopen(filename, "rb");
        if (!f) {
            return -1;
        }
        int result = -1;
        if (fseek(f, 0, SEEK_END)) {
            goto finish;
        }
        long size = ftell(f);
        if (size < 0 || (unsigned long) size > SIZE_MAX || fseek(f, 0, SEEK_SET)) {
            goto finish;
        }
        *out_size = (size_t) size;
        if (!(*out = (uint8_t *) avs_malloc(*out_size))) {
            goto finish;
        }
        if (fread(*out, *out_size, 1, f) != 1) {
            avs_free(*out);
            *out = NULL;
            goto finish;
        }
        result = 0;
    finish:
        fclose(f);
        return result;
    }

    static int fw_get_security_config(void *user_ptr,
                                      anjay_security_config_t *out_security_info,
                                      const char *download_uri) {
        (void) user_ptr;
        if (!anjay_security_config_from_dm(FW_STATE.anjay, out_security_info,
                                           download_uri)) {
            // found a match
            return 0;
        }

        // no match found, fallback to loading certificates from given paths
        static uint8_t *ca_cert = NULL;
        static size_t ca_cert_size = 0;
        static uint8_t *client_cert = NULL;
        static size_t client_cert_size = 0;
        static uint8_t *client_key = NULL;
        static size_t client_key_size = 0;
        if ((!ca_cert
             && load_buffer_from_file(&ca_cert, &ca_cert_size,
                                      "./certs/CA.crt.der"))
                || (!client_cert
                    && load_buffer_from_file(&client_cert, &client_cert_size,
                                             "./certs/client.crt.der"))
                || (!client_key
                    && load_buffer_from_file(&client_key, &client_key_size,
                                             "./certs/client.key.der"))) {
            return -1;
        }

        memset(out_security_info, 0, sizeof(*out_security_info));
        const avs_net_certificate_info_t cert_info = {
            .server_cert_validation = true,
            .trusted_certs = avs_crypto_certificate_chain_info_from_buffer(
                    ca_cert, ca_cert_size),
            .client_cert = avs_crypto_certificate_chain_info_from_buffer(
                    client_cert, client_cert_size),
            .client_key = avs_crypto_private_key_info_from_buffer(
                    client_key, client_key_size, NULL)
        };
        out_security_info->security_info =
                avs_net_security_info_from_certificates(cert_info);
        return 0;
    }

.. note::

    The ``load_buffer_from_file()`` is identical to the one introduced in the
    :doc:`../../AdvancedTopics/AT-Certificates` tutorial (aside from removed
    logger calls). The code from that chapter has also already been used in the
    :doc:`CustomTLS-CertificatesBasic` and :doc:`CustomTLS-CertificatesAdvanced`
    tutorials.

Prerequisites
-------------

Most of the implementation of secure sockets is reused between stream-based TLS
and datagram DTLS sockets. However, the subtle differences are present across
all stages of communication, so we need to store the socket type for later
checks. This means that we need an additional field in the ``tls_socket_impl_t``
structure:

.. highlight:: c
.. snippet-source:: examples/custom-tls/tcp-support/src/tls_impl.c
    :emphasize-lines: 3

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        avs_net_socket_type_t backend_type;
        avs_net_socket_t *backend_socket;
        SSL_CTX *ctx;
        SSL *ssl;

        char psk[256];
        size_t psk_size;
        char identity[128];
        size_t identity_size;

        bool dane_enabled;
        char dane_tlsa_association_data_buf[4096];
        avs_net_socket_dane_tlsa_record_t dane_tlsa_array[4];
        size_t dane_tlsa_array_size;

        void *session_resumption_buffer;
        size_t session_resumption_buffer_size;

        char server_name_indication[256];
        unsigned int dtls_hs_timeout_min_us;
        unsigned int dtls_hs_timeout_max_us;
    } tls_socket_impl_t;

Updates to the socket creation
------------------------------

In all previous tutorials, all the socket creation code was directly implemented
in ``_avs_net_create_dtls_socket()``. To support both TLS and DTLS, the logic is
extracted to a new function called ``create_tls_socket()`` and wrapped in both
``_avs_net_create_dtls_socket()`` and ``_avs_net_create_ssl_socket()``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/tcp-support/src/tls_impl.c
    :emphasize-lines: 1-4, 15, 18-38, 78-90

    static avs_error_t
    create_tls_socket(avs_net_socket_t **socket_ptr,
                      avs_net_socket_type_t backend_type,
                      const avs_net_ssl_configuration_t *configuration) {
        assert(socket_ptr);
        assert(!*socket_ptr);
        assert(configuration);
        tls_socket_impl_t *socket =
                (tls_socket_impl_t *) avs_calloc(1, sizeof(tls_socket_impl_t));
        if (!socket) {
            return avs_errno(AVS_ENOMEM);
        }
        *socket_ptr = (avs_net_socket_t *) socket;
        socket->operations = &TLS_SOCKET_VTABLE;
        socket->backend_type = backend_type;

        avs_error_t err = AVS_OK;
        if (backend_type == AVS_NET_UDP_SOCKET) {
            if (avs_is_ok((err = avs_net_udp_socket_create(
                                   &socket->backend_socket,
                                   &configuration->backend_configuration)))
                    && !(socket->ctx = SSL_CTX_new(DTLS_method()))) {
                err = avs_errno(AVS_ENOMEM);
            }
            if (avs_is_ok(err)) {
                err = configure_dtls_version(socket, configuration->version);
            }
        } else {
            if (avs_is_ok((err = avs_net_tcp_socket_create(
                                   &socket->backend_socket,
                                   &configuration->backend_configuration)))
                    && !(socket->ctx = SSL_CTX_new(TLS_method()))) {
                err = avs_errno(AVS_ENOMEM);
            }
            if (avs_is_ok(err)) {
                err = configure_tls_version(socket, configuration->version);
            }
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

    avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket_ptr,
                                            const void *configuration) {
        return create_tls_socket(
                socket_ptr, AVS_NET_UDP_SOCKET,
                (const avs_net_ssl_configuration_t *) configuration);
    }

    avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket_ptr,
                                           const void *configuration) {
        return create_tls_socket(
                socket_ptr, AVS_NET_TCP_SOCKET,
                (const avs_net_ssl_configuration_t *) configuration);
    }

The UDP/DTLS and TCP/TLS variants of socket creation differ in the following
ways:

* Either ``avs_net_udp_socket_create()`` or ``avs_net_tcp_socket_create()`` is
  used to instantiate the backend socket

* Either ``DTLS_method()`` or ``TLS_method()`` is passed to ``SSL_CTX_new()``

* Either ``configure_dtls_version()`` or ``configure_tls_version()`` is used to
  configure the version of the protocol. ``configure_tls_version()`` itself is a
  new function, very similar to the DTLS variant, but using different constants
  for the protocol versions:

.. highlight:: c
.. snippet-source:: examples/custom-tls/tcp-support/src/tls_impl.c

    static avs_error_t configure_tls_version(tls_socket_impl_t *sock,
                                             avs_net_ssl_version_t version) {
        switch (version) {
        case AVS_NET_SSL_VERSION_DEFAULT:
        case AVS_NET_SSL_VERSION_SSLv2_OR_3:
            return AVS_OK;
        case AVS_NET_SSL_VERSION_SSLv3:
            SSL_CTX_set_min_proto_version(sock->ctx, SSL3_VERSION);
            return AVS_OK;
        case AVS_NET_SSL_VERSION_TLSv1:
            SSL_CTX_set_min_proto_version(sock->ctx, TLS1_VERSION);
            return AVS_OK;
        case AVS_NET_SSL_VERSION_TLSv1_1:
            SSL_CTX_set_min_proto_version(sock->ctx, TLS1_1_VERSION);
            return AVS_OK;
        case AVS_NET_SSL_VERSION_TLSv1_2:
            SSL_CTX_set_min_proto_version(sock->ctx, TLS1_2_VERSION);
            return AVS_OK;
        case AVS_NET_SSL_VERSION_TLSv1_3:
            SSL_CTX_set_min_proto_version(sock->ctx, TLS1_3_VERSION);
            return AVS_OK;
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

.. note::

    This implementation provides basic support for TLS 1.3. However, please note
    that proper TLS 1.3 support may require additional adjustments, such as
    :ref:`configuring ciphersuites differently <custom-tls-tls13-ciphersuites>`.
    Depending on the underlying (D)TLS library, session resumption support may
    also need to be implemented in a different way.

    (D)TLS 1.3 support is not addressed thoroughly in this tutorial due to low
    level of support for TLS 1.3 and especially DTLS 1.3 in mainstream
    implementations at the time of writing. Please refer to the reference
    implementations (`avs_mbedtls_socket.c
    <https://github.com/AVSystem/avs_commons/blob/master/src/net/mbedtls/avs_mbedtls_socket.c>`__
    and `avs_openssl.c
    <https://github.com/AVSystem/avs_commons/blob/master/src/net/openssl/avs_openssl.c>`__)
    for examples.

Updates to the handshake process
--------------------------------

In OpenSSL, a different type of BIO object is used for TLS and DTLS protocols.
``perform_handshake()`` function must thus be updated accordingly, so that
``BIO_new_dgram()`` is used for DTLS and ``BIO_new_socket()`` for TLS:

.. highlight:: c
.. snippet-source:: examples/custom-tls/tcp-support/src/tls_impl.c
    :emphasize-lines: 53-64

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
        if (sock->dane_enabled) {
            // NOTE: SSL_dane_enable() calls SSL_set_tlsext_host_name() internally
            SSL_dane_enable(sock->ssl, host);
            bool have_usable_tlsa_records = false;
            for (size_t i = 0; i < sock->dane_tlsa_array_size; ++i) {
                if (SSL_CTX_get_verify_mode(sock->ctx) == SSL_VERIFY_NONE
                        && (sock->dane_tlsa_array[i].certificate_usage
                                    == AVS_NET_SOCKET_DANE_CA_CONSTRAINT
                            || sock->dane_tlsa_array[i].certificate_usage
                                       == AVS_NET_SOCKET_DANE_SERVICE_CERTIFICATE_CONSTRAINT)) {
                    // PKIX-TA and PKIX-EE constraints are unusable for
                    // opportunistic clients
                    continue;
                }
                SSL_dane_tlsa_add(
                        sock->ssl,
                        (uint8_t) sock->dane_tlsa_array[i].certificate_usage,
                        (uint8_t) sock->dane_tlsa_array[i].selector,
                        (uint8_t) sock->dane_tlsa_array[i].matching_type,
                        (unsigned const char *) sock->dane_tlsa_array[i]
                                .association_data,
                        sock->dane_tlsa_array[i].association_data_size);
                have_usable_tlsa_records = true;
            }
            if (SSL_CTX_get_verify_mode(sock->ctx) == SSL_VERIFY_NONE
                    && have_usable_tlsa_records) {
                SSL_set_verify(sock->ssl, SSL_VERIFY_PEER, NULL);
            }
        } else {
            SSL_set_tlsext_host_name(sock->ssl, host);
        }
        SSL_set1_host(sock->ssl, host);

        BIO *bio = NULL;
        if (sock->backend_type == AVS_NET_UDP_SOCKET) {
            bio = BIO_new_dgram(*(const int *) fd_ptr, 0);
            BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peername.addr);
            DTLS_set_timer_cb(sock->ssl, dtls_timer_cb);
        } else {
            bio = BIO_new_socket(*(const int *) fd_ptr, 0);
        }
        if (!bio) {
            return avs_errno(AVS_ENOMEM);
        }
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

Updates to the data receiving procedure
---------------------------------------

Stream-oriented and datagram-oriented transport protocols are fundamentally
different.

In datagram-oriented communication, the data is transmitted in well-defined
separate packets (datagrams), which are atomic in nature. A datagram will never
be split into smaller chunks, at least not on the application layer. Datagrams
may, however, be lost or reordered, especially in the case of the most common
datagram transport protocol, UDP.

Stream-oriented communication, on the other hand, treats the connection as a
stream of bytes. The stream is guaranteed to arrive in its entirety and in
proper order, and the application is generally free to send and receive bytes at
its own pace, in arbitrarily small chunks. Boundaries between physical data
packets transmitted over the network do not need to correlate with how the send
and receive operations are called by the application.

Both OpenSSL and avs_commons use the same APIs for interacting with both
stream-oriented (TCP/TLS) and datagram-oriented (UDP/DTLS) protocols. However,
the requirements are different between the two:

* If a datagram is received only in part, due to lack of space in the buffer, it
  shall be treated as an error; for stream-oriented communication, it is a
  normal condition, and receiving may continue with the next call.

* We are using the ``poll()`` system call to wait for new data to arrive on the
  underlying unencrypted socket. This makes no sense if using a stream-oriented
  socket and there is already data decrypted and buffered by OpenSSL, but not
  passed down to application code yet (e.g. after a previous partial read
  operation); ``SSL_pending()`` function can be used to query how many bytes are
  left in the buffer.

It is thus necessary to modify the ``tls_receive()`` function to handle these
differences appropriately:

.. highlight:: c
.. snippet-source:: examples/custom-tls/tcp-support/src/tls_impl.c
    :emphasize-lines: 6-12, 41

    static avs_error_t tls_receive(avs_net_socket_t *sock_,
                                   size_t *out_bytes_received,
                                   void *buffer,
                                   size_t buffer_length) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        int pending = 0;
        if (sock->backend_type == AVS_NET_TCP_SOCKET) {
            pending = SSL_pending(sock->ssl);
        }
        if (pending > 0) {
            buffer_length = AVS_MIN(buffer_length, (size_t) pending);
        } else {
            const void *fd_ptr = avs_net_socket_get_system(sock->backend_socket);
            avs_net_socket_opt_value_t timeout;
            if (!fd_ptr
                    || avs_is_err(avs_net_socket_get_opt(
                               sock->backend_socket,
                               AVS_NET_SOCKET_OPT_RECV_TIMEOUT, &timeout))) {
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
        }
        int bytes_received = SSL_read(sock->ssl, buffer, (int) buffer_length);
        if (bytes_received < 0) {
            return avs_errno(AVS_EPROTO);
        }
        *out_bytes_received = (size_t) bytes_received;
        if (sock->backend_type == AVS_NET_UDP_SOCKET && buffer_length > 0
                && (size_t) bytes_received == buffer_length) {
            return avs_errno(AVS_EMSGSIZE);
        }
        return AVS_OK;
    }

Conclusion
----------

The above changes are enough to make communication using TLS over TCP to work.
The example application corresponding to this tutorial is able to both connect
to a LwM2M server using DTLS transport in PSK mode, and perform firmware
download using HTTPS (HTTP over TLS) with mode traditional certificate-based
security.

Please note that the example implementation developed in this chapter still does
not implement all the features of avs_commons' TLS integration API.
Specifically, the following topics were not covered:

* **Loading certificates and keys from other sources than memory buffers is not
  supported.** This may be desirable for e.g. firmware downloads, as evident in
  this article. Please note that when using files as the data source, it is
  generally expected to support both the PEM and DER file formats, and to
  automatically detect between the two.

* **DTLS Connection ID extension is not supported.** This is currently not
  supported in OpenSSL at all, which makes this topic infeasible to cover in
  this tutorial. Please take a look at the `avs_mbedtls_socket.c
  <https://github.com/AVSystem/avs_commons/blob/master/src/net/mbedtls/avs_mbedtls_socket.c>`__
  file in avs_commons to see how it can be implemented using Mbed TLS - the
  relevant parts of the code can be found by searching for usages of the
  ``use_connection_id`` field.

* **TLS alert codes are not forwarded to calling code.** LwM2M 1.1 expects TLS
  alert codes to be exposed through the data model. OpenSSL does not expose
  these alerts codes to the user, either, so it is also infeasible to cover this
  topic in this tutorial.

  It is expected that if an alert code is received during the handshake
  procedure, the alert code shall be wrapped into an ``avs_error_t`` object
  using `avs_net_ssl_alert()
  <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_socket.h#L358>`_
  and returned as a result from the ``connect`` operation. Alert handling may
  also be added to the ``receive`` operation as well.

  Please see the implementation and usages of the ``return_alert_if_any()``
  function in the `avs_mbedtls_socket.c
  <https://github.com/AVSystem/avs_commons/blob/master/src/net/mbedtls/avs_mbedtls_socket.c>`__
  file in avs_commons to see how it can be implemented using Mbed TLS.

* **Socket file descriptor is used directly instead of wrapping** ``avs_net``
  **APIs, and the** ``decorate`` **function is not implemented.** The secure SMS
  mode will thus not work in versions that include the SMS commercial feature.

* **The** ``rebuild_client_cert_chain`` **flag in**
  ``avs_net_certificate_info_t`` **is not supported.** The implications of that
  have been discussed in more detail in the
  :ref:`custom-tls-api-certificates-basic-limitations` sections of the
  :doc:`CustomTLS-CertificatesBasic` tutorial. Please take a look at the
  ``rebuild_client_cert_chain()`` functions in the `avs_mbedtls_socket.c
  <https://github.com/AVSystem/avs_commons/blob/master/src/net/mbedtls/avs_mbedtls_socket.c#L1380>`__
  and `avs_openssl.c
  <https://github.com/AVSystem/avs_commons/blob/master/src/net/openssl/avs_openssl.c#L1148>`__
  files in avs_commons for examples on how to implement this feature if needed.
