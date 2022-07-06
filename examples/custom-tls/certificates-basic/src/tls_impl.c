#include <poll.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <avsystem/commons/avs_socket_v_table.h>
#include <avsystem/commons/avs_utils.h>

#ifndef AVS_COMMONS_WITH_CUSTOM_TLS
#    error "Custom implementation of the TLS layer requires AVS_COMMONS_WITH_CUSTOM_TLS"
#endif // AVS_COMMONS_WITH_CUSTOM_TLS

avs_error_t _avs_net_initialize_global_ssl_state(void);
void _avs_net_cleanup_global_ssl_state(void);
avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket,
                                       const void *socket_configuration);
avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket,
                                        const void *socket_configuration);

avs_error_t _avs_net_initialize_global_ssl_state(void) {
    if (!OPENSSL_init_ssl(OPENSSL_INIT_ADD_ALL_CIPHERS
                                  | OPENSSL_INIT_ADD_ALL_DIGESTS,
                          NULL)) {
        return avs_errno(AVS_EPROTO);
    }
    return AVS_OK;
}

void _avs_net_cleanup_global_ssl_state(void) {}

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

static avs_error_t
tls_send(avs_net_socket_t *sock_, const void *buffer, size_t buffer_length) {
    tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
    int result = SSL_write(sock->ssl, buffer, (int) buffer_length);
    if (result < 0 || (size_t) result < buffer_length) {
        return avs_errno(AVS_EPROTO);
    }
    return AVS_OK;
}

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
    case AVS_NET_SOCKET_OPT_SESSION_RESUMED:
        out_option_value->flag = (sock->ssl && SSL_session_reused(sock->ssl));
        return AVS_OK;
    default:
        return avs_net_socket_get_opt(sock->backend_socket, option_key,
                                      out_option_value);
    }
}

static avs_error_t tls_set_opt(avs_net_socket_t *sock_,
                               avs_net_socket_opt_key_t option_key,
                               avs_net_socket_opt_value_t option_value) {
    tls_socket_impl_t *sock = (tls_socket_impl_t *) sock_;
    return avs_net_socket_set_opt(sock->backend_socket, option_key,
                                  option_value);
}

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
    // NOTE: Configuring the set of supported new-style ciphersuites as defined
    // for TLS 1.3 are not supported by this function.
    return AVS_OK;
}

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

avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket_ptr,
                                       const void *configuration) {
    return avs_errno(AVS_ENOTSUP);
}
