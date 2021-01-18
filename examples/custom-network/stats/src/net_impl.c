#include <inttypes.h>

#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <avsystem/commons/avs_socket_v_table.h>
#include <avsystem/commons/avs_utils.h>

#ifdef AVS_COMMONS_NET_WITH_POSIX_AVS_SOCKET
#    error "Custom implementation of the network layer conflicts with AVS_COMMONS_NET_WITH_POSIX_AVS_SOCKET"
#endif // AVS_COMMONS_NET_WITH_POSIX_AVS_SOCKET

avs_error_t _avs_net_initialize_global_compat_state(void);
void _avs_net_cleanup_global_compat_state(void);
avs_error_t _avs_net_create_tcp_socket(avs_net_socket_t **socket,
                                       const void *socket_configuration);
avs_error_t _avs_net_create_udp_socket(avs_net_socket_t **socket,
                                       const void *socket_configuration);

avs_error_t _avs_net_initialize_global_compat_state(void) {
    return AVS_OK;
}

void _avs_net_cleanup_global_compat_state(void) {}

typedef struct {
    const avs_net_socket_v_table_t *operations;
    int socktype;
    int fd;
    avs_time_duration_t recv_timeout;
    char remote_hostname[256];
    bool shut_down;
    size_t bytes_sent;
    size_t bytes_received;
} net_socket_impl_t;

typedef union {
    struct sockaddr addr;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
    struct sockaddr_storage storage;
} sockaddr_union_t;

static avs_error_t
net_connect(avs_net_socket_t *sock_, const char *host, const char *port) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    struct addrinfo hints = {
        .ai_socktype = sock->socktype
    };
    if (sock->fd >= 0) {
        getsockopt(sock->fd, SOL_SOCKET, SO_DOMAIN, &hints.ai_family,
                   &(socklen_t) { sizeof(hints.ai_family) });
    }
    struct addrinfo *addr = NULL;
    avs_error_t err = AVS_OK;
    if (getaddrinfo(host, port, &hints, &addr) || !addr) {
        err = avs_errno(AVS_EADDRNOTAVAIL);
    } else if (sock->fd < 0
               && (sock->fd = socket(addr->ai_family, addr->ai_socktype,
                                     addr->ai_protocol))
                          < 0) {
        err = avs_errno(AVS_UNKNOWN_ERROR);
    } else if (connect(sock->fd, addr->ai_addr, addr->ai_addrlen)) {
        err = avs_errno(AVS_ECONNREFUSED);
    }
    if (avs_is_ok(err)) {
        sock->shut_down = false;
        snprintf(sock->remote_hostname, sizeof(sock->remote_hostname), "%s",
                 host);
    }
    freeaddrinfo(addr);
    return err;
}

static avs_error_t
net_send(avs_net_socket_t *sock_, const void *buffer, size_t buffer_length) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    ssize_t written = send(sock->fd, buffer, buffer_length, MSG_NOSIGNAL);
    if (written >= 0) {
        sock->bytes_sent += (size_t) written;
        if ((size_t) written == buffer_length) {
            return AVS_OK;
        }
    }
    return avs_errno(AVS_EIO);
}

static avs_error_t net_receive(avs_net_socket_t *sock_,
                               size_t *out_bytes_received,
                               void *buffer,
                               size_t buffer_length) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    struct pollfd pfd = {
        .fd = sock->fd,
        .events = POLLIN
    };
    int64_t timeout_ms;
    if (avs_time_duration_to_scalar(&timeout_ms, AVS_TIME_MS,
                                    sock->recv_timeout)) {
        timeout_ms = -1;
    } else if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    if (poll(&pfd, 1, (int) timeout_ms) == 0) {
        return avs_errno(AVS_ETIMEDOUT);
    }
    ssize_t bytes_received = read(sock->fd, buffer, buffer_length);
    if (bytes_received < 0) {
        return avs_errno(AVS_EIO);
    }
    *out_bytes_received = (size_t) bytes_received;
    sock->bytes_received += (size_t) bytes_received;
    if (buffer_length > 0 && sock->socktype == SOCK_DGRAM
            && (size_t) bytes_received == buffer_length) {
        return avs_errno(AVS_EMSGSIZE);
    }
    return AVS_OK;
}

static avs_error_t
net_bind(avs_net_socket_t *sock_, const char *address, const char *port) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE,
        .ai_socktype = sock->socktype
    };
    if (sock->fd >= 0) {
        getsockopt(sock->fd, SOL_SOCKET, SO_DOMAIN, &hints.ai_family,
                   &(socklen_t) { sizeof(hints.ai_family) });
    }
    struct addrinfo *addr = NULL;
    avs_error_t err = AVS_OK;
    if (getaddrinfo(address, port, &hints, &addr) || !addr) {
        err = avs_errno(AVS_EADDRNOTAVAIL);
    } else if ((sock->fd < 0
                && (sock->fd = socket(addr->ai_family, addr->ai_socktype,
                                      addr->ai_protocol))
                           < 0)
               || setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &(int) { 1 },
                             sizeof(int))) {
        err = avs_errno(AVS_UNKNOWN_ERROR);
    } else if (bind(sock->fd, addr->ai_addr, addr->ai_addrlen)) {
        err = avs_errno(AVS_ECONNREFUSED);
    } else {
        sock->shut_down = false;
    }
    if (avs_is_err(err) && sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
    }
    freeaddrinfo(addr);
    return err;
}

static avs_error_t net_close(avs_net_socket_t *sock_) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    avs_error_t err = AVS_OK;
    if (sock->fd >= 0) {
        if (close(sock->fd)) {
            err = avs_errno(AVS_EIO);
        }
        sock->fd = -1;
        sock->shut_down = false;
    }
    return err;
}

static avs_error_t net_shutdown(avs_net_socket_t *sock_) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    avs_error_t err = avs_errno(AVS_EBADF);
    if (sock->fd >= 0) {
        err = shutdown(sock->fd, SHUT_RDWR) ? avs_errno(AVS_EIO) : AVS_OK;
        sock->shut_down = true;
    }
    return err;
}

static avs_error_t net_cleanup(avs_net_socket_t **sock_ptr) {
    avs_error_t err = AVS_OK;
    if (sock_ptr && *sock_ptr) {
        err = net_close(*sock_ptr);
        avs_free(*sock_ptr);
        *sock_ptr = NULL;
    }
    return err;
}

static const void *net_system_socket(avs_net_socket_t *sock_) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    return &sock->fd;
}

static avs_error_t stringify_sockaddr_host(const sockaddr_union_t *addr,
                                           char *out_buffer,
                                           size_t out_buffer_size) {
    if ((addr->in.sin_family == AF_INET
         && inet_ntop(AF_INET, &addr->in.sin_addr, out_buffer,
                      (socklen_t) out_buffer_size))
            || (addr->in6.sin6_family == AF_INET6
                && inet_ntop(AF_INET6, &addr->in6.sin6_addr, out_buffer,
                             (socklen_t) out_buffer_size))) {
        return AVS_OK;
    }
    return avs_errno(AVS_UNKNOWN_ERROR);
}

static avs_error_t stringify_sockaddr_port(const sockaddr_union_t *addr,
                                           char *out_buffer,
                                           size_t out_buffer_size) {
    if ((addr->in.sin_family == AF_INET
         && avs_simple_snprintf(out_buffer, out_buffer_size, "%" PRIu16,
                                ntohs(addr->in.sin_port))
                    >= 0)
            || (addr->in6.sin6_family == AF_INET6
                && avs_simple_snprintf(out_buffer, out_buffer_size, "%" PRIu16,
                                       ntohs(addr->in6.sin6_port))
                           >= 0)) {
        return AVS_OK;
    }
    return avs_errno(AVS_UNKNOWN_ERROR);
}

static avs_error_t net_remote_host(avs_net_socket_t *sock_,
                                   char *out_buffer,
                                   size_t out_buffer_size) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    sockaddr_union_t addr;
    if (getpeername(sock->fd, &addr.addr, &(socklen_t) { sizeof(addr) })) {
        return avs_errno(AVS_UNKNOWN_ERROR);
    }
    return stringify_sockaddr_host(&addr, out_buffer, out_buffer_size);
}

static avs_error_t net_remote_hostname(avs_net_socket_t *sock_,
                                       char *out_buffer,
                                       size_t out_buffer_size) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    return avs_simple_snprintf(out_buffer, out_buffer_size, "%s",
                               sock->remote_hostname)
                           < 0
                   ? avs_errno(AVS_UNKNOWN_ERROR)
                   : AVS_OK;
}

static avs_error_t net_remote_port(avs_net_socket_t *sock_,
                                   char *out_buffer,
                                   size_t out_buffer_size) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    sockaddr_union_t addr;
    if (getpeername(sock->fd, &addr.addr, &(socklen_t) { sizeof(addr) })) {
        return avs_errno(AVS_UNKNOWN_ERROR);
    }
    return stringify_sockaddr_port(&addr, out_buffer, out_buffer_size);
}

static avs_error_t net_local_port(avs_net_socket_t *sock_,
                                  char *out_buffer,
                                  size_t out_buffer_size) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    sockaddr_union_t addr;
    if (getsockname(sock->fd, &addr.addr, &(socklen_t) { sizeof(addr) })) {
        return avs_errno(AVS_UNKNOWN_ERROR);
    }
    return stringify_sockaddr_port(&addr, out_buffer, out_buffer_size);
}

static avs_error_t net_get_opt(avs_net_socket_t *sock_,
                               avs_net_socket_opt_key_t option_key,
                               avs_net_socket_opt_value_t *out_option_value) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    switch (option_key) {
    case AVS_NET_SOCKET_OPT_RECV_TIMEOUT:
        out_option_value->recv_timeout = sock->recv_timeout;
        return AVS_OK;
    case AVS_NET_SOCKET_OPT_STATE:
        if (sock->fd < 0) {
            out_option_value->state = AVS_NET_SOCKET_STATE_CLOSED;
        } else if (sock->shut_down) {
            out_option_value->state = AVS_NET_SOCKET_STATE_SHUTDOWN;
        } else {
            sockaddr_union_t addr;
            if (!getpeername(sock->fd, &addr.addr,
                             &(socklen_t) { sizeof(addr) })
                    && ((addr.in.sin_family == AF_INET && addr.in.sin_port != 0)
                        || (addr.in6.sin6_family == AF_INET6
                            && addr.in6.sin6_port != 0))) {
                out_option_value->state = AVS_NET_SOCKET_STATE_CONNECTED;
            } else {
                out_option_value->state = AVS_NET_SOCKET_STATE_BOUND;
            }
        }
        return AVS_OK;
    case AVS_NET_SOCKET_OPT_INNER_MTU:
        out_option_value->mtu = 1464;
        return AVS_OK;
    case AVS_NET_SOCKET_OPT_BYTES_SENT:
        out_option_value->bytes_sent = sock->bytes_sent;
        return AVS_OK;
    case AVS_NET_SOCKET_OPT_BYTES_RECEIVED:
        out_option_value->bytes_received = sock->bytes_received;
        return AVS_OK;
    default:
        return avs_errno(AVS_ENOTSUP);
    }
}

static avs_error_t net_set_opt(avs_net_socket_t *sock_,
                               avs_net_socket_opt_key_t option_key,
                               avs_net_socket_opt_value_t option_value) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    switch (option_key) {
    case AVS_NET_SOCKET_OPT_RECV_TIMEOUT:
        sock->recv_timeout = option_value.recv_timeout;
        return AVS_OK;
    default:
        return avs_errno(AVS_ENOTSUP);
    }
}

static const avs_net_socket_v_table_t NET_SOCKET_VTABLE = {
    .connect = net_connect,
    .send = net_send,
    .receive = net_receive,
    .bind = net_bind,
    .close = net_close,
    .shutdown = net_shutdown,
    .cleanup = net_cleanup,
    .get_system_socket = net_system_socket,
    .get_remote_host = net_remote_host,
    .get_remote_hostname = net_remote_hostname,
    .get_remote_port = net_remote_port,
    .get_local_port = net_local_port,
    .get_opt = net_get_opt,
    .set_opt = net_set_opt
};

static avs_error_t
net_create_socket(avs_net_socket_t **socket_ptr,
                  const avs_net_socket_configuration_t *configuration,
                  int socktype) {
    assert(socket_ptr);
    assert(!*socket_ptr);
    (void) configuration;
    net_socket_impl_t *socket =
            (net_socket_impl_t *) avs_calloc(1, sizeof(net_socket_impl_t));
    if (!socket) {
        return avs_errno(AVS_ENOMEM);
    }
    socket->operations = &NET_SOCKET_VTABLE;
    socket->socktype = socktype;
    socket->fd = -1;
    socket->recv_timeout = avs_time_duration_from_scalar(30, AVS_TIME_S);
    *socket_ptr = (avs_net_socket_t *) socket;
    return AVS_OK;
}

avs_error_t _avs_net_create_udp_socket(avs_net_socket_t **socket_ptr,
                                       const void *configuration) {
    return net_create_socket(
            socket_ptr, (const avs_net_socket_configuration_t *) configuration,
            SOCK_DGRAM);
}

avs_error_t _avs_net_create_tcp_socket(avs_net_socket_t **socket_ptr,
                                       const void *configuration) {
    return net_create_socket(
            socket_ptr, (const avs_net_socket_configuration_t *) configuration,
            SOCK_STREAM);
}
