..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Advanced certificate support
============================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-tls/certificates-advanced
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/certificates-advanced>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one <CustomTLS-CertificatesBasic>`
and adds:

* Ability to load multiple certificates as the client certificate chain
* Support for DANE; **this will also add proper support for the Server Public
  Key LwM2M resource**

.. note::

    In this tutorial, the ``main.c`` file is identical to the one from the
    :doc:`../../AdvancedTopics/AT-Certificates` tutorial.

    In fact, in the repository, it is a symbolic link to the file from that
    tutorial.

Support for multiple client certificates
----------------------------------------

.. note::

    Loading multiple client certificates is only possible by setting the
    ``public_cert`` field in ``anjay_security_instance_t``.


    If you don't intend to use that field there is no point in making this change.

Using multiple certificates in the client certificate chain is rarely used, but
it may be necessary to properly initiate a connection to some LwM2M servers.

To support this case, the ``AVS_CRYPTO_DATA_SOURCE_ARRAY`` and
``AVS_CRYPTO_DATA_SOURCE_LIST`` cases need to be supported in the
``configure_client_cert()`` function, much like is the case for
``configure_trusted_certs()``.

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced/src/tls_impl.c
    :emphasize-lines: 2-6, 8-9, 19-24, 31-50

    static avs_error_t
    configure_client_certs(SSL_CTX *ctx,
                           const avs_crypto_security_info_union_t *client_certs) {
        if (!client_certs) {
            return avs_errno(AVS_EINVAL);
        }
        switch (client_certs->source) {
        case AVS_CRYPTO_DATA_SOURCE_EMPTY:
            return AVS_OK;
        case AVS_CRYPTO_DATA_SOURCE_BUFFER: {
            const unsigned char *ptr =
                    (const unsigned char *) client_certs->info.buffer.buffer;
            X509 *cert = d2i_X509(NULL, &ptr,
                                  (long) client_certs->info.buffer.buffer_size);
            if (!cert) {
                return avs_errno(AVS_EPROTO);
            }

            int result;
            if (!SSL_CTX_get0_certificate(ctx)) {
                result = SSL_CTX_use_certificate(ctx, cert);
            } else {
                result = SSL_CTX_add1_chain_cert(ctx, cert);
            }
            X509_free(cert);
            if (result != 1) {
                return avs_errno(AVS_EPROTO);
            }
            return AVS_OK;
        }
        case AVS_CRYPTO_DATA_SOURCE_ARRAY: {
            avs_error_t err = AVS_OK;
            for (size_t i = 0;
                 avs_is_ok(err) && i < client_certs->info.array.element_count;
                 ++i) {
                err = configure_client_certs(
                        ctx, &client_certs->info.array.array_ptr[i]);
            }
            return err;
        }
        case AVS_CRYPTO_DATA_SOURCE_LIST: {
            avs_error_t err = AVS_OK;
            AVS_LIST(avs_crypto_security_info_union_t) entry;
            AVS_LIST_FOREACH(entry, client_certs->info.list.list_head) {
                if (avs_is_err((err = configure_client_certs(ctx, entry)))) {
                    break;
                }
            }
            return AVS_OK;
        }
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

The function has been slightly refactored to take
``avs_crypto_security_info_union_t`` as an argument to make recursive calls
easier. ``client_cert`` in the function and argument names has also been
pluralized.

Aside from these trivial changes, the ``AVS_CRYPTO_DATA_SOURCE_ARRAY`` and
``AVS_CRYPTO_DATA_SOURCE_LIST`` have been implemented in essentially the same
way as in ``configure_trusted_certs()`` and
``configure_cert_revocation_lists()``, and the ``AVS_CRYPTO_DATA_SOURCE_BUFFER``
case has been updated so that ``SSL_CTX_add1_chain_cert()`` is used for the
second and all subsequent certificate entries. This means that the first loaded
certificate is always the actual client certificate, with any subsequent ones
forming the rest of the certification path up towards the root CA certificate.

.. _custom-tls-api-certificates-advanced-dane:

DANE support
------------

Instead of the standard PKIX rules for certificate verification, from LwM2M 1.1
onwards, the server certificates are verified using a `custom mechanism
<https://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Transport-V1_1_1-20190617-A.html#5-2-8-7-0-5287-Certificate-Usage-Field>`_
that operates on concepts almost identical to those used by `DANE
<https://en.wikipedia.org/wiki/DNS-based_Authentication_of_Named_Entities>`_.

The LwM2M 1.0 semantics are mirrored by the default settings used in LwM2M 1.1,
which is to use the "domain-issued certificate" mode.

For the above reasons, server certificate validation in Anjay is largely
implemented in terms of DANE, which needs to be provided in the secure socket
implementation.

.. note::

    Standard PKIX certificate validation may also be used in conjunction with
    DANE, particularly when certificate usage mode is set to "CA constraint" or
    "service certificate constraint", or when EST is used.

DANE is supported natively since OpenSSL 1.1, which makes it easy to implement
for the purpose of this tutorial.

.. important::

    DANE is not widely supported in other TLS backend libraries or hardware
    implementations.

    Please look at the
    :ref:`custom-tls-api-certificates-advanced-dane-minimum-subset` section if
    implementing proper DANE support is impossible or infeasible in your case.

Initialization
^^^^^^^^^^^^^^

It is necessary to store some additional state for DANE support, so the
``tls_socket_impl_t`` structure is extended accordingly:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced/src/tls_impl.c
    :emphasize-lines: 12-15

    typedef struct {
        const avs_net_socket_v_table_t *operations;
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

* The ``dane_enabled`` field will store the information about whether DANE shall
  be used for this connection.

* ``dane_tlsa_array`` will hold the DANE TLSA entries to be used for the
  connection; maximum of 4 entries is supported in this implementation, while
  ``dane_tlsa_array_size`` shall be the number of entries actually populated.

* ``dane_tlsa_association_data_buf`` will store the actual certificate data;
  ``dane_tlsa_array`` entries will contain pointers into this buffer.

.. note::

    In actual LwM2M use, at most 1 DANE TLSA entry is ever used.

    This tutorial provides an implementation that support multiple entries for
    the sake of completeness, but support for only a single entry is sufficient
    to cover all the cases used by Anjay.

In OpenSSL, DANE needs to be enabled both for ``SSL_CTX`` and ``SSL`` objects.
Enabling it for the ``SSL_CTX`` object needs to be done in the
``configure_certs()`` function, in accordance to the ``dane`` field in
``avs_net_certificate_info_t``:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced/src/tls_impl.c
    :emphasize-lines: 20-23

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
        sock->dane_enabled = certs->dane;
        if (sock->dane_enabled) {
            SSL_CTX_dane_enable(sock->ctx);
        }
        if (certs->client_cert.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
            avs_error_t err;
            if (avs_is_err((err = configure_client_certs(sock->ctx,
                                                         &certs->client_cert.desc)))
                    || avs_is_err(err = configure_client_key(sock->ctx,
                                                             &certs->client_key))) {
                return err;
            }
        }

        return AVS_OK;
    }

Populating the array
^^^^^^^^^^^^^^^^^^^^

DANE TLSA entries are passed into the socket object through the ``set_opt``
operation with the ``AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY`` key.

In OpenSSL, this information can only be provided after specifying the hostname
for the ``SSL`` object, which in our code only happens during the ``connect``
operation. For this reason, we need to store the DANE TLSA entries in our
internal structures first.

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced/src/tls_impl.c
    :emphasize-lines: 5-44

    static avs_error_t tls_set_opt(avs_net_socket_t *sock_,
                                   avs_net_socket_opt_key_t option_key,
                                   avs_net_socket_opt_value_t option_value) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        switch (option_key) {
        case AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY: {
            if (option_value.dane_tlsa_array.array_element_count
                    > AVS_ARRAY_SIZE(sock->dane_tlsa_array)) {
                return avs_errno(AVS_EINVAL);
            }
            avs_net_socket_dane_tlsa_record_t
                    copied_array[AVS_ARRAY_SIZE(sock->dane_tlsa_array)];
            char copied_association_data[sizeof(
                    sock->dane_tlsa_association_data_buf)];
            size_t copied_association_data_offset = 0;
            memcpy(copied_array, option_value.dane_tlsa_array.array_ptr,
                   option_value.dane_tlsa_array.array_element_count
                           * sizeof(avs_net_socket_dane_tlsa_record_t));
            for (size_t i = 0; i < option_value.dane_tlsa_array.array_element_count;
                 ++i) {
                if (copied_association_data_offset
                                + option_value.dane_tlsa_array.array_ptr[i]
                                          .association_data_size
                        > sizeof(copied_association_data)) {
                    return avs_errno(AVS_EINVAL);
                }
                memcpy(copied_association_data + copied_association_data_offset,
                       option_value.dane_tlsa_array.array_ptr[i].association_data,
                       option_value.dane_tlsa_array.array_ptr[i]
                               .association_data_size);
                copied_array[i].association_data =
                        sock->dane_tlsa_association_data_buf
                        + copied_association_data_offset;
                copied_association_data_offset +=
                        option_value.dane_tlsa_array.array_ptr[i]
                                .association_data_size;
            }
            memcpy(sock->dane_tlsa_association_data_buf, copied_association_data,
                   sizeof(copied_association_data));
            memcpy(sock->dane_tlsa_array, copied_array, sizeof(copied_array));
            sock->dane_tlsa_array_size =
                    option_value.dane_tlsa_array.array_element_count;
            return AVS_OK;
        }
        default:
            return avs_net_socket_set_opt(sock->backend_socket, option_key,
                                          option_value);
        }
    }

The above code essentially makes a deep copy of the data in
``option_value.dane_tlsa_array``. The buffers pointed to by the
``association_data`` fields within array entries are copied into the
``sock->dane_tlsa_association_data_buf`` field and pointers in the copied array
updated to point into that buffer as well.

.. important::

    Any pointers passed to the ``set_opt`` function with the
    ``AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY`` options shall be treated as data that
    will be invalidated after returning from the function.

    This means that this data needs to be either immediately loaded into the
    (D)TLS context, or a deep copy otherwise made.

Configuring the connection
^^^^^^^^^^^^^^^^^^^^^^^^^^

Now with all the necessary information, we can configure the ``SSL`` object
during the ``connect`` operation.

All the DANE configuration essentially takes place of the
``SSL_set_tlsext_host_name()`` call:

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced/src/tls_impl.c
    :emphasize-lines: 20-50

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

Note that `"opportunistic DANE"
<https://datatracker.ietf.org/doc/html/rfc7671#section-4.1>`_ is mentioned and
supported in the code above. This means that even if server certificate
verification is not otherwise enabled, but DANE-TA or DANE-EE entries are
present, the client shall verify the server certificate against these entries.

.. note::

    Opportunistic DANE is not used by Anjay. An implementation is provided here
    for the sake of completeness, but it is not necessary for LwM2M
    communication.

    If only LwM2M compliance is targeted, it is safe to remove the
    ``if (SSL_CTX_get_verify_mode(sock->ctx) == SSL_VERIFY_NONE && ...)``
    clauses and the ``have_usable_tlsa_records`` variable from the code above
    altogether.

.. _custom-tls-api-certificates-advanced-dane-minimum-subset:

Minimum viable subset
^^^^^^^^^^^^^^^^^^^^^

.. warning::

    The approach described in this section is not fully compliant with DANE nor
    any version of LwM2M. It is intended **only** for use if implementing more
    complete support is not possible.

Support for DANE is, unfortunately, very limited among (D)TLS implementations.
In fact, in the default Mbed TLS integration in avs_commons, it has been
implemented from scratch in a custom certificate verification callback, see the
`verify_cert_cb() function
<https://github.com/AVSystem/avs_commons/blob/master/src/net/mbedtls/avs_mbedtls_socket.c#L535>`_
there. In many cases, this approach might still be infeasible or even
impossible, especially if (D)TLS is handled in hardware.

It is possible to emulate the most common case using standard PKIX concepts,
which will allow LwM2M 1.0 (and 1.1 with typical configuration) to work, at
least with some servers.

.. important::

    This implementation will **only work with self-signed server certificates**.

.. note::

    Code modified for this variant can be found under
    `examples/custom-tls/certificates-advanced-fake-dane
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-tls/certificates-advanced-fake-dane>`_
    in the Anjay source directory.

This minimum implementation reverts the changed described earlier in the
:ref:`custom-tls-api-certificates-advanced-dane` section. Instead, the following
changes are made:

* The only information about DANE that needs to be kept in the socket state is
  whether or not it is enabled.

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced-fake-dane/src/tls_impl.c
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

        bool dane_enabled;

        void *session_resumption_buffer;
        size_t session_resumption_buffer_size;

        char server_name_indication[256];
        unsigned int dtls_hs_timeout_min_us;
        unsigned int dtls_hs_timeout_max_us;
    } tls_socket_impl_t;

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced-fake-dane/src/tls_impl.c
    :emphasize-lines: 20

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
        sock->dane_enabled = certs->dane;
        if (certs->client_cert.desc.source != AVS_CRYPTO_DATA_SOURCE_EMPTY) {
            avs_error_t err;
            if (avs_is_err((err = configure_client_certs(sock->ctx,
                                                         &certs->client_cert.desc)))
                    || avs_is_err(err = configure_client_key(sock->ctx,
                                                             &certs->client_key))) {
                return err;
            }
        }

        return AVS_OK;
    }

* The ``tls_set_opt()`` function is updated to put the server certificate into
  the trust store.

.. highlight:: c
.. snippet-source:: examples/custom-tls/certificates-advanced-fake-dane/src/tls_impl.c
    :emphasize-lines: 5-37

    static avs_error_t tls_set_opt(avs_net_socket_t *sock_,
                                   avs_net_socket_opt_key_t option_key,
                                   avs_net_socket_opt_value_t option_value) {
        tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
        switch (option_key) {
        case AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY: {
            if (option_value.dane_tlsa_array.array_element_count > 1) {
                return avs_errno(AVS_EINVAL);
            }
            if (!sock->dane_enabled
                    || option_value.dane_tlsa_array.array_element_count == 0
                    || option_value.dane_tlsa_array.array_ptr[0].certificate_usage
                                   == AVS_NET_SOCKET_DANE_CA_CONSTRAINT
                    || option_value.dane_tlsa_array.array_ptr[0].certificate_usage
                                   == AVS_NET_SOCKET_DANE_SERVICE_CERTIFICATE_CONSTRAINT) {
                return AVS_OK;
            }
            X509_STORE *store = SSL_CTX_get_cert_store(sock->ctx);
            if (option_value.dane_tlsa_array.array_ptr[0].selector
                            != AVS_NET_SOCKET_DANE_CERTIFICATE
                    || option_value.dane_tlsa_array.array_ptr[0].matching_type
                                   != AVS_NET_SOCKET_DANE_MATCH_FULL
                    || sk_X509_OBJECT_num(X509_STORE_get0_objects(store)) > 0) {
                return avs_errno(AVS_ENOTSUP);
            }
            avs_crypto_certificate_chain_info_t chain =
                    avs_crypto_certificate_chain_info_from_buffer(
                            option_value.dane_tlsa_array.array_ptr[0]
                                    .association_data,
                            option_value.dane_tlsa_array.array_ptr[0]
                                    .association_data_size);
            avs_error_t err = configure_trusted_certs(store, &chain.desc);
            if (avs_is_ok(err)) {
                SSL_CTX_set_verify(sock->ctx, SSL_VERIFY_PEER, NULL);
            }
            return err;
        }
        default:
            return avs_net_socket_set_opt(sock->backend_socket, option_key,
                                          option_value);
        }
    }

* Due to ``configure_trusted_certs()`` being called in the code above, that
  function's declaration needs to be moved above ``tls_set_opt()``, with no
  other changes.

In the code above, only the DANE-TA and DANE-EE mode (Certificate Usage modes 2
and 3) entries are taken into account, only full certificate matching is
supported, and the trust store needs to be empty at the time of calling this
function. If all those conditions are met, the passed certificate is just added
to the store - ``configure_trusted_certs()`` is called for that purpose as a
wrapper to the ``d2i_X509()`` and ``X509_STORE_add_cert()`` functions.

This is enough for the logic used by Anjay for LwM2M 1.0 (and 1.1 on default
settings) to work. However, as mentioned above, only self-signed server
certificates are supported. DANE-EE mode will not function properly, as
certificate verification will fail due to inability to find the CA certificate.
