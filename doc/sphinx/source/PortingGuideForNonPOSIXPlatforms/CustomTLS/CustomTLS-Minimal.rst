..
   Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Minimal DTLS implementation
===========================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/minimal
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/minimal>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one <CustomTLS-Stub>` and adds
logic related to actually initializing the SSL context state and performing the
DTLS handshake.

Only the bare minimum functionality necessary to use DTLS in PSK mode is
implemented for now - but this is enough to register to a LwM2M server in PSK
mode.

Implementation of the DTLS socket
---------------------------------

.. _custom-tls-api-create:

Initialization
^^^^^^^^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-tls/minimal/src/tls_impl.c

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
        return AVS_OK;
    }

The flow of this function is as follows:

* First, the socket object is allocated and the virtual method table is
  assigned. This is conceptually identical to the :ref:`initialization of the
  unencrypted UDP socket <non-posix-networking-api-create>`.
* Then, the underlying UDP socket and the ``SSL_CTX`` object are created.
* Initialization related to the security credentials is delegated to a separate
  function that will be described next. We only support the PSK mode for now,
  so we check that it is indeed selected, and call ``configure_psk()``.
* Finally, the auto-retry mode is enabled in OpenSSL. This is the preferred mode
  that simplifies the implementation when the non-blocking mode is not used. See
  `SSL_CTX_set_mode() <https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_mode.html>`_
  for details.

.. _custom-tls-security-info-union-type:

The ``avs_crypto_security_info_union_t`` type
"""""""""""""""""""""""""""""""""""""""""""""

Loading of security credentials in ``avs_net`` and ``avs_crypto`` is centered
around the ``avs_crypto_security_info_union_t`` type, declared as follows:

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_common.h

    typedef enum {
        AVS_CRYPTO_SECURITY_INFO_CERTIFICATE_CHAIN,
        AVS_CRYPTO_SECURITY_INFO_PRIVATE_KEY,
        AVS_CRYPTO_SECURITY_INFO_CERT_REVOCATION_LIST,
        AVS_CRYPTO_SECURITY_INFO_PSK_IDENTITY,
        AVS_CRYPTO_SECURITY_INFO_PSK_KEY
    } avs_crypto_security_info_tag_t;

    typedef enum {
        AVS_CRYPTO_DATA_SOURCE_EMPTY,
        AVS_CRYPTO_DATA_SOURCE_FILE,
        AVS_CRYPTO_DATA_SOURCE_PATH,
        AVS_CRYPTO_DATA_SOURCE_BUFFER,
        AVS_CRYPTO_DATA_SOURCE_ARRAY,
        AVS_CRYPTO_DATA_SOURCE_LIST,
    #if defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE) \
            || defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)
        AVS_CRYPTO_DATA_SOURCE_ENGINE
    #endif /* defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE) || \
            defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE) */
        } avs_crypto_data_source_t;

    /**
     * This struct is for internal use only and should not be filled manually. One
     * should construct appropriate instances of:
     * - @ref avs_crypto_certificate_chain_info_t,
     * - @ref avs_crypto_private_key_info_t
     * - @ref avs_crypto_cert_revocation_list_info_t
     * - @ref avs_crypto_psk_identity_info_t
     * - @ref avs_crypto_psk_key_info_t
     * using methods declared in @c avs_crypto_pki.h and @c avs_crypto_psk.h.
     */
    struct avs_crypto_security_info_union_struct {
        avs_crypto_security_info_tag_t type;
        avs_crypto_data_source_t source;
        union {
            avs_crypto_security_info_union_internal_file_t file;
            avs_crypto_security_info_union_internal_path_t path;
            avs_crypto_security_info_union_internal_buffer_t buffer;
            avs_crypto_security_info_union_internal_array_t array;
            avs_crypto_security_info_union_internal_list_t list;
    #if defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE) \
            || defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)
            avs_crypto_security_info_union_internal_engine_t engine;
    #endif /* defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE) || \
            defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE) */
            } info;
        };

The ``source`` fields acts as a tag to the ``info`` union, deciding from which
source the credential shall be loaded. There are a number of "simple" sources
supported:

* ``AVS_CRYPTO_DATA_SOURCE_EMPTY`` - signifies that the object does not
  represent any valid credential information
* ``AVS_CRYPTO_DATA_SOURCE_FILE`` - the credential shall be loaded from a file,
  specified as a file path (``info.file.filename``); in case of private keys,
  an optional password for encrypted PEM keys can be specified
  (``info.file.password``)
* ``AVS_CRYPTO_DATA_SOURCE_PATH`` - the credentials shall be loaded from a
  directory, specified as a file system path (``info.path.path``); this
  generally only makes sense for certificate chains
* ``AVS_CRYPTO_DATA_SOURCE_BUFFER`` - the credentials shall be loaded from a
  memory buffer (``info.buffer.buffer`` of the size
  ``info.buffer.buffer_size``); in case of private keys, an optional password
  for encrypted PEM keys can be specified (``info.buffer.password``); **this is
  the case that is almost exclusively used in Anjay**
* ``AVS_CRYPTO_DATA_SOURCE_ENGINE`` - the object refers to a credential stored
  in a hardware cryptography source, such as a secure element; information on
  the credential is stored as a "query string" at ``info.engine.query``; the
  format of the query string is platform-specific and may be arbitrary; **this
  case is supported in the HSM Feature**

In addition to the "simple" sources listed above, two additional "compound"
sources are supported:

* ``AVS_CRYPTO_DATA_SOURCE_ARRAY`` - the object specifies multiple credentials,
  stored as an array of other ``avs_crypto_security_info_union_t`` objects -
  ``info.array.element_count`` structures stored at ``info.array.array_ptr``
* ``AVS_CRYPTO_DATA_SOURCE_LIST`` - the object specifies multiple credentials,
  stored as an ``AVS_LIST`` whose first element is ``info.list.list_head``; the
  ``AVS_LIST`` macro is not explicitly used in the declaration of the
  ``list_head`` field for dependency management reasons, but that field shall
  still be treated as such

.. note::

    "Compound" credential sources are most commonly used for trust store
    information, i.e. trusted certificates and certificate revocation lists.

    "Compound" credential sources are not used for private keys, PSK keys or PSK
    identities.

    "Compound" credential sources MAY be used for client certificates, to
    signify additional CA certificates that shall be sent to the server during
    handshake.

    "Compound" credential sources, in general, MAY contain other "compound"
    credential sources, forming a tree-like structure. Those SHOULD be loaded
    recursively. However, the credentials provided by Anjay are expected to not
    be formed in this way.

.. important::

    Anjay uses both ``AVS_CRYPTO_DATA_SOURCE_ARRAY`` and
    ``AVS_CRYPTO_DATA_SOURCE_LIST`` for different purposes, so support for both
    needs to be implemented.

The ``avs_crypto_security_info_union_t`` structure additionally contains the
``type`` field, which may be used for validating the credential type (i.e.,
whether the object represents a certificate chain, certificate revocation lists,
or a private key.

In typical usage, the type is conveyed by composing the
``avs_crypto_security_info_union_t`` object into one of the wrapper objects:

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_pki.h

    typedef struct avs_crypto_certificate_chain_info_struct {
        avs_crypto_security_info_union_t desc;
    } avs_crypto_certificate_chain_info_t;

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_pki.h

    typedef struct {
        avs_crypto_security_info_union_t desc;
    } avs_crypto_cert_revocation_list_info_t;

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_pki.h

    typedef struct {
        avs_crypto_security_info_union_t desc;
    } avs_crypto_private_key_info_t;

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_psk.h

    typedef struct {
        avs_crypto_security_info_union_t desc;
    } avs_crypto_psk_identity_info_t;

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_crypto_psk.h

    typedef struct {
        avs_crypto_security_info_union_t desc;
    } avs_crypto_psk_key_info_t;

We will only implement support for the ``AVS_CRYPTO_DATA_SOURCE_BUFFER`` mode
for the PSK mode; in later tutorials, where configuration of the certificate
mode is described, ``AVS_CRYPTO_DATA_SOURCE_ARRAY`` and
``AVS_CRYPTO_DATA_SOURCE_LIST`` will also be implemented for some cases.

Initialization of PSK credentials
"""""""""""""""""""""""""""""""""

In OpenSSL, credentials for the PSK mode are provided through a callback -
a function is set using `SSL_CTX_set_psk_client_callback()
<https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_psk_client_callback.html>`_
and it is called whenever the library needs the PSK credentials - this means
that they need to be stored for later access on demand.

The credentials are passed within the ``avs_net_ssl_configuration_t`` stucture
passed to ``_avs_net_create_dtls_socket()`` or ``_avs_net_create_ssl_socket()``.
However, the structure passed there shall be treated as ephemeral, so in case of
the OpenSSL API, the credentials need to be copied into the socket state.

This means that the first thing is to add appropriate fields to the
``tls_socket_impl_t`` structure:

.. highlight:: c
.. snippet-source:: examples/custom-tls/minimal/src/tls_impl.c
    :emphasize-lines: 7-10

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        avs_net_socket_t *backend_socket;
        SSL_CTX *ctx;
        SSL *ssl;

        char psk[256];
        size_t psk_size;
        char identity[128];
        size_t identity_size;
    } tls_socket_impl_t;

.. note::

    Different TLS libraries have different data lifetime contracts. For example,
    in contrast to the OpenSSL API, `mbedtls_ssl_conf_psk()
    <https://tls.mbed.org/api/ssl_8h.html#a1e185199e3ff613bdd1c8231a19e24fc>`_
    in Mbed TLS copies the data passed as arguments into internal structures and
    thus it is not necessary to make explicit copies.

    Please carefully check whether credentials are passed by value or by
    reference in the TLS backend you are integrating with.

We are now ready to implement the ``configure_psk()`` function, and the
``psk_client_cb()`` callback that will be passed to
``SSL_CTX_set_psk_client_callback()``. As mentioned above, only the
``AVS_CRYPTO_DATA_SOURCE_BUFFER`` source is handled for both the key and
identity.

.. highlight:: c
.. snippet-source:: examples/custom-tls/minimal/src/tls_impl.c
    :emphasize-lines: 7, 44, 46

    static unsigned int psk_client_cb(SSL *ssl,
                                      const char *hint,
                                      char *identity,
                                      unsigned int max_identity_len,
                                      unsigned char *psk,
                                      unsigned int max_psk_len) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) SSL_get_app_data(ssl);

        (void) hint;

        if (!sock || max_psk_len < sock->psk_size
                || max_identity_len < sock->identity_size + 1) {
            return 0;
        }

        memcpy(psk, sock->psk, sock->psk_size);
        memcpy(identity, sock->identity, sock->identity_size);
        identity[sock->identity_size] = '\0';

        return (unsigned int) sock->psk_size;
    }

    static avs_error_t configure_psk(tls_socket_impl_t *sock,
                                     const avs_net_psk_info_t *psk) {
        if (!psk->key.desc.source != AVS_CRYPTO_DATA_SOURCE_BUFFER
                || psk->identity.desc.source != AVS_CRYPTO_DATA_SOURCE_BUFFER) {
            return avs_errno(AVS_EINVAL);
        }

        const void *key_ptr = psk->key.desc.info.buffer.buffer;
        size_t key_size = psk->key.desc.info.buffer.buffer_size;

        const void *identity_ptr = psk->identity.desc.info.buffer.buffer;
        size_t identity_size = psk->identity.desc.info.buffer.buffer_size;

        if (key_size > sizeof(sock->psk)
                || identity_size > sizeof(sock->identity)) {
            return avs_errno(AVS_EINVAL);
        }
        memcpy(sock->psk, key_ptr, key_size);
        sock->psk_size = key_size;
        memcpy(sock->identity, identity_ptr, identity_size);
        sock->identity_size = identity_size;
        SSL_CTX_set_cipher_list(sock->ctx, "PSK");
        SSL_CTX_set_psk_client_callback(sock->ctx, psk_client_cb);
        SSL_CTX_set_verify(sock->ctx, SSL_VERIFY_PEER, NULL);
        return AVS_OK;
    }

Note that OpenSSL does not automatically disable ciphersuites and functionality
related to certificates when a PSK callback is provided. For this reason
additional settings are changed:

* ``SSL_CTX_set_cipher_list()`` is called to limit the set of allowed
  ciphersuites to only those that depend on the PSK mode.
* ``SSL_CTX_set_verify()`` is also set to ``SSL_VERIFY_PEER`` so that a server
  that attempts to use certificate-based authentication shall be verified -
  this verification will invariably fail, as there are no trusted certificates
  configured for this connection.

Also note that the ``tls_socket_impl_t`` structure is accessed using
``SSL_get_app_data()``. This will be set while
:ref:`custom-tls-minimal-handshake`.

Cleanup
^^^^^^^

Knowing what is happening during initialization, we can now reverse this process
in the cleanup function:

.. highlight:: c
.. snippet-source:: examples/custom-tls/minimal/src/tls_impl.c

    static avs_error_t tls_cleanup(avs_net_socket_t **sock_ptr) {
        avs_error_t err = AVS_OK;
        if (sock_ptr && *sock_ptr) {
            tls_socket_impl_t *sock = (tls_socket_impl_t *) *sock_ptr;
            tls_close(*sock_ptr);
            avs_net_socket_cleanup(&sock->backend_socket);
            if (sock->ctx) {
                SSL_CTX_free(sock->ctx);
            }
            avs_free(sock);
            *sock_ptr = NULL;
        }
        return err;
    }

.. _custom-tls-minimal-handshake:

Performing the handshake
^^^^^^^^^^^^^^^^^^^^^^^^

The ``perform_handshake()`` function is now relatively straightforward to
implement:

* The new ``SSL`` object is created using ``SSL_new()``
* The pointer to the socket structure is set as the application data so that it
  can be retrieved in ``psk_client_cb()``
* The hostname to which the socket is being connected is set to be used in the
  Server Name Identification TLS extension
* A new datagram ``BIO`` object is created, configured and set for use by the
  ``SSL`` object

  * OpenSSL's datagram BIO object uses ``sendto()`` instead of ``send()``
    internally, so it needs to be explicitly informed of the address of the
    peer the socket is connected to. This is performed using ``BIO_ctrl()``,
    with the raw server address queried using ``getpeername()``.
* ``SSL_connect()`` is called to perform the actual client-side (D)TLS handshake

.. highlight:: c
.. snippet-source:: examples/custom-tls/minimal/src/tls_impl.c
    :emphasize-lines: 38-40

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

        if (SSL_connect(sock->ssl) <= 0) {
            return avs_errno(AVS_EPROTO);
        }
        return AVS_OK;
    }

    static avs_error_t
    tls_connect(avs_net_socket_t *sock_, const char *host, const char *port) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        if (sock->ssl) {
            return avs_errno(AVS_EBADF);
        }
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

An additional check is also added in ``tls_connect()`` to avoid creating the
``SSL`` object multiple times.

Fixing the socket option values
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``tls_get_opt()`` function has been previously implemented by simply
forwarding the call to the underlying unencrypted socket.

This yields inaccurate results for the ``AVS_NET_SOCKET_OPT_INNER_MTU`` option.
The underlying socket will return the maximum number of bytes available on the
UDP layer, while we need to take the DTLS headers into account.

It is also desirable to overload the ``AVS_NET_SOCKET_HAS_BUFFERED_DATA``. This
option is designed to notify the Anjay library whether all data received from
the underlying system socket has been processed. This is used to make sure that
when control is returned to the event loop, the ``poll()`` call will not stall
waiting for new data, while in reality it is already available, but stuck in the
(D)TLS layer buffer.

In this example based on OpenSSL, this condition can be checked by calling the
`SSL_pending() <https://www.openssl.org/docs/man1.1.1/man3/SSL_pending.html>`_
function.

.. highlight:: c
.. snippet-source:: examples/custom-tls/minimal/src/tls_impl.c

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
        case AVS_NET_SOCKET_HAS_BUFFERED_DATA:
            out_option_value->flag = (sock->ssl && SSL_pending(sock->ssl) > 0);
            return AVS_OK;
        default:
            return avs_net_socket_get_opt(sock->backend_socket, option_key,
                                          out_option_value);
        }
    }

.. note::

    In this simplistic implementation, the DTLS overhead has been hardcoded to
    64 bytes, which is generally accepted as the upper limit for this value.

    A more complete implementation could query or calculate the precise
    overhead for the current session, based on the specific ciphersuite in use.

Limitations
-----------

This minimal implementation is enough to communicate with an LwM2M server in PSK
mode, but a number of functionalities will not work:

* Session resumption is not implemented, which may cause otherwise unnecessary
  Register requests being sent after reconnecting. Note that a Register request
  also forces the server to reinitialize all the Observe requests, so this is
  very undesirable.
* Certificate mode is not implemented.
* TLS over TCP is not implemented, which means that e.g. HTTPS will not be
  supported.
* DTLS Connection ID extension is not supported.
* Various additional configuration options are not implemented as well,
  including:

  * Configurable TLS/DTLS version
  * Configurable DTLS handshake timers
  * Configurable ciphersuite list (note that in LwM2M they can be configured
    through the data model - this will be ignored by the current implementation)
  * Overriding the hostname used for Server Name Identification - useful for
    LwM2M 1.1 only
* TLS alert codes are not forwarded to calling code, and LwM2M 1.1 exposes them
  through the data model.
* Socket file descriptor is used directly instead of wrapping ``avs_net`` APIs,
  and the ``decorate`` function is not implemented - the secure SMS mode will
  thus not work in versions that include the SMS commercial feature.

We will expand this implementation to address these limitation in subsequent
chapters.
