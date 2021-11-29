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
} tls_socket_impl_t;

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
