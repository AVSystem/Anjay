#include <poll.h>

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
} tls_socket_impl_t;

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

avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket_ptr,
                                       const void *configuration) {
    return avs_errno(AVS_ENOTSUP);
}
