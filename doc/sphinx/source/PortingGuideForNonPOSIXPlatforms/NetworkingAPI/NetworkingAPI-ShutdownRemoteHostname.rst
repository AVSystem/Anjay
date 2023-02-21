..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Remote hostname and shutdown operations
=======================================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-network/shutdown-remote-hostname
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-network/shutdown-remote-hostname>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one <NetworkingAPI-Bind>` and adds
support for the "get remote hostname" and "shutdown" operations.

These operations will allow suspending and resuming CoAP downloads when using
the "offline mode" functionality.

Get remote hostname operation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket_v_table.h

    typedef avs_error_t (*avs_net_socket_get_remote_hostname_t)(
            avs_net_socket_t *socket, char *out_buffer, size_t out_buffer_size);

This operation is similar in concept to the previously introduced
:ref:`non-posix-networking-api-get-remote-host`. However, "get remote host" is
intended to always return a stringified IP address, while "get remote hostname"
shall return the hostname originally passed to the
:ref:`non-posix-networking-api-connect` function.

Shutdown operation
^^^^^^^^^^^^^^^^^^

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket_v_table.h

    typedef avs_error_t (*avs_net_socket_shutdown_t)(avs_net_socket_t *socket);

This API is intended as a parallel to the POSIX ``shutdown()`` function (called
with ``SHUT_RDWR`` mode) - it shall disconnect the socket on the transport
control layer, but does not close the OS-level socket descriptor.

This shall put the socket in a state similar to closed, but with the connection
association still in place. This is mostly done to ensure that all "get
remote/local host/port" operations keep returning the same data while the
connection is unavailable from the network.

This operation has additional semantics for (D)TLS sockets - it will shut down
the underlying raw socket without gracefully closing the connection on the
(D)TLS layer. This is however implemented within the (D)TLS backend integration
and is outside the scope of this implementation.

Additional socket state
-----------------------

The string passed to the :ref:`non-posix-networking-api-connect` function is not
stored anywhere in our current logic; there is also no standard POSIX API to
determine whether the socket is in the shut down state. That's why we will need
additional fields in our socket structure to implement these operations:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c
    :emphasize-lines: 6-7

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        int socktype;
        int fd;
        avs_time_duration_t recv_timeout;
        char remote_hostname[256];
        bool shut_down;
    } net_socket_impl_t;

The ``remote_hostname`` field will contain the last known hostname to which the
connection was successful.

In our implementation, the ``shut_down`` flag is intended to only be ``true`` if
the socket is specifically in the "shut down" state - if the socket is either
bound, connected or closed, it shall be ``false``.

Updating the socket state
-------------------------

The only place where there is direct access to the hostname, is the
:ref:`non-posix-networking-api-connect` function, so we need to update it
accordingly to cache this information if the connection is successful:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c
    :emphasize-lines: 23-27

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

Note that in addition to saving the hostname, we also set the ``shut_down`` flag
to ``false``. This is because we entered the "connected" state, and - as
described above, the flag is only intended to be ``true`` when the socket is in
the "shut down" state.

For this reason, we also need to update this flag in the bind and close
operations:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c
    :emphasize-lines: 25-27, 44

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

Update to get_opt implementation
--------------------------------

We need to fix implementation of getting the ``AVS_NET_SOCKET_OPT_STATE`` option
so that the "shut down" state is properly reported:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c
    :emphasize-lines: 12-14

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
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }


New method implementations
--------------------------

Implementation of the shutdown operation method is simple and self-explanatory:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c

    static avs_error_t net_shutdown(avs_net_socket_t *sock_) {
        net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
        avs_error_t err = avs_errno(AVS_EBADF);
        if (sock->fd >= 0) {
            err = shutdown(sock->fd, SHUT_RDWR) ? avs_errno(AVS_EIO) : AVS_OK;
            sock->shut_down = true;
        }
        return err;
    }

Similarly, the "get remote hostname" method code is just a simple string copy:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c

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

Of course the newly implemented functions need to be referenced in the virtual
method table:

.. highlight:: c
.. snippet-source:: examples/custom-network/shutdown-remote-hostname/src/net_impl.c
    :emphasize-lines: 7, 11

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

.. note::

    Due to lack of support for IP address stickiness, when resuming CoAP
    downloads using this code, it might happen that the resumed download will
    connect to a different node than the original one.

    This limitation will be addressed in a subsequent tutorial.
