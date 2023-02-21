..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Statistics support
==================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-network/stats
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-network/stats>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one
<NetworkingAPI-ShutdownRemoteHostname>` and adds support for statistics in the
"get options" operations.

This will allow the ``anjay_get_tx_bytes()`` and ``anjay_get_rx_bytes()`` APIs
to work properly.

Additional socket state
-----------------------

We need to store the number of bytes sent and received via the socket, so we add
appropriate fields to the socket object:

.. highlight:: c
.. snippet-source:: examples/custom-network/stats/src/net_impl.c
    :emphasize-lines: 8-9

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

Updating the socket state
-------------------------

We can now update these counters in the send and receive operations:

.. highlight:: c
.. snippet-source:: examples/custom-network/stats/src/net_impl.c
    :emphasize-lines: 6, 38

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

Update to get_opt implementation
--------------------------------

We need to add implementations of the ``AVS_NET_SOCKET_OPT_BYTES_SENT`` and
``AVS_NET_SOCKET_OPT_BYTES_RECEIVED`` options to ``net_get_opt()``:

.. highlight:: c
.. snippet-source:: examples/custom-network/stats/src/net_impl.c
    :emphasize-lines: 33-38

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
        case AVS_NET_SOCKET_HAS_BUFFERED_DATA:
            out_option_value->flag = false;
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
