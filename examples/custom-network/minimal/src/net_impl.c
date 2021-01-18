#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <avsystem/commons/avs_socket_v_table.h>

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
} net_socket_impl_t;

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
    freeaddrinfo(addr);
    return err;
}

static avs_error_t
net_send(avs_net_socket_t *sock_, const void *buffer, size_t buffer_length) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    ssize_t written = send(sock->fd, buffer, buffer_length, MSG_NOSIGNAL);
    if (written >= 0 && (size_t) written == buffer_length) {
        return AVS_OK;
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
    if (buffer_length > 0 && sock->socktype == SOCK_DGRAM
            && (size_t) bytes_received == buffer_length) {
        return avs_errno(AVS_EMSGSIZE);
    }
    return AVS_OK;
}

static avs_error_t net_close(avs_net_socket_t *sock_) {
    net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
    avs_error_t err = AVS_OK;
    if (sock->fd >= 0) {
        if (close(sock->fd)) {
            err = avs_errno(AVS_EIO);
        }
        sock->fd = -1;
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
        } else {
            out_option_value->state = AVS_NET_SOCKET_STATE_CONNECTED;
        }
        return AVS_OK;
    case AVS_NET_SOCKET_OPT_INNER_MTU:
        out_option_value->mtu = 1464;
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
    .close = net_close,
    .cleanup = net_cleanup,
    .get_system_socket = net_system_socket,
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
