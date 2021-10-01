..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Bind operation
==============

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-network/bind
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-network/bind>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one
<NetworkingAPI-RemoteHostPort>` and adds support for the bind operation.

This will allow use of the `anjay_configuration_t::udp_listen_port
<../../api/structanjay__configuration.html#acf74549a99ca3ad5aedb227c4b0258ca>`_
setting, which might be useful e.g. for the LwM2M 1.0-style Server-Initiated
Bootstrap.

We will also add support for the "get local port" operation, which will allow
even ephemeral listening port number to be retained between subsequent
connections to the same server.

Bind operation itself
---------------------

Implementation of the bind function is very similar to the previously
implemented :ref:`non-posix-networking-api-connect` one. Important changes are
highlighted.

.. highlight:: c
.. snippet-source:: examples/custom-network/bind/src/net_impl.c
    :emphasize-lines: 5, 20-21, 23, 26-29

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
        }
        if (avs_is_err(err) && sock->fd >= 0) {
            close(sock->fd);
            sock->fd = -1;
        }
        freeaddrinfo(addr);
        return err;
    }

This time ``getaddrinfo()`` is called with ``AI_PASSIVE`` flag to allow wildcard
addresses (e.g. ``0.0.0.0``) and to prevent DNS resolution for such local
addresses.

Of course, ``bind()`` is called instead of ``connect()``. But before doing so,
``setsockopt()`` is called to enable the ``SO_REUSEADDR`` flag. This is done
because Anjay may create multiple sockets bound to the same port, one for each
remote server connection. This shall not result in a conflict, as all those
sockets will be connected to different remote servers shortly after binding.

.. note::

    More properly, ``SO_REUSEADDR`` should only be used if the ``reuse_addr``
    flag has been set in the ``avs_net_socket_configuration_t`` structure passed
    at socket creation time.

    However, Anjay always sets this flag to ``true``, so it is alright to set it
    unconditionally in such simplistic implementation.

Finally, in case of error, the underlying socket descriptor is closed. This is
to ensure that upon error, the socket will not end up in the "bound" state,
which will be evident in the modifications to ``net_get_opt()`` illustrated
below.

Changes to net_get_opt()
------------------------

Changes to this function are highlighted:

.. highlight:: c
.. snippet-source:: examples/custom-network/bind/src/net_impl.c
    :emphasize-lines: 13-22

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
        default:
            return avs_errno(AVS_ENOTSUP);
        }
    }

The original variant assumed that if the socket descriptor was present, it is
connected. Here, we need to differentiate between the "connected" and "bound"
states - hence we use the ``getpeername()`` function to check if there is a
valid remote address.

Because ``getpeername()`` might return different kind of socket addresses, the
``sockaddr_union_t`` type :ref:`declared in the previous tutorial
<non-posix-networking-api-get-remote-host>` is used.

Get local port operation
------------------------

The "get local port" operation may or may not be implemented. It is not
necessary for the bind operation to work, but if implemented, it will allow
Anjay to keep ephemeral listening port number consistent across subsequent
connections to the same server if `anjay_configuration_t::udp_listen_port
<../../api/structanjay__configuration.html#acf74549a99ca3ad5aedb227c4b0258ca>`_
is not set.

Its implementation mirrors the :ref:`non-posix-networking-api-get-remote-port`
from the previous tutorial, only with ``getsockname()`` used instead of
``getpeername()``:

.. highlight:: c
.. snippet-source:: examples/custom-network/bind/src/net_impl.c
    :emphasize-lines: 6

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

Update to vtable
----------------

Of course the newly implemented functions need to be referenced in the virtual
method table:

.. highlight:: c
.. snippet-source:: examples/custom-network/bind/src/net_impl.c
    :emphasize-lines: 5, 11

    static const avs_net_socket_v_table_t NET_SOCKET_VTABLE = {
        .connect = net_connect,
        .send = net_send,
        .receive = net_receive,
        .bind = net_bind,
        .close = net_close,
        .cleanup = net_cleanup,
        .get_system_socket = net_system_socket,
        .get_remote_host = net_remote_host,
        .get_remote_port = net_remote_port,
        .get_local_port = net_local_port,
        .get_opt = net_get_opt,
        .set_opt = net_set_opt
    };
