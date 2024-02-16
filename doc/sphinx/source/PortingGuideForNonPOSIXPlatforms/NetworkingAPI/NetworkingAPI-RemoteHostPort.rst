..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Get remote host/port operations
===============================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-network/remote-host-port
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-network/remote-host-port>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one <NetworkingAPI-Minimal>` and
adds support for the "get remote host" and "get remote port" operations.

This will allow CoAP response cache to work and the
`anjay_configuration_t::msg_cache_size
<../../api/structanjay__configuration.html#a3bb16de58b283370b1ab20698dd4849a>`_
configuration option to be properly respected.

This is necessary because the response cache is shared between all the server
connections, and remote host/port pairs are used to distinguish between them in
the cache storage - and these functions are used to retrieve this information
from sockets.

.. _non-posix-networking-api-get-remote-host:

Get remote host operation
-------------------------

.. highlight:: c
.. snippet-source:: examples/custom-network/remote-host-port/src/net_impl.c

    #include <arpa/inet.h>

    // ...

    typedef union {
        struct sockaddr addr;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
        struct sockaddr_storage storage;
    } sockaddr_union_t;

    // ...

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

    // ...

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

The ``net_remote_host()`` function essentially wraps the POSIX ``getpeername()``
function. However, that function returns a structure from the ``struct
sockaddr`` family, while ``avs_commons`` operates on stringified addresses.

Because several variants of ``struct sockaddr`` may be used, the
``sockaddr_union_t`` type is declared to accommodate for all supported types.

``stringify_sockaddr_host()`` function converts the IP address stored in
``struct sockaddr_in`` or ``struct sockaddr_in6`` into stringified form by
calling POSIX ``inet_ntop()``.

.. note::

    Out of POSIX APIs, the operations in this tutorial can also be implemented
    using ``getnameinfo()`` with ``NI_NUMERICSERV`` and ``NI_NUMERICHOST`` flags
    enabled. ``inet_ntop()`` is used here because of broader compatibility.

.. _non-posix-networking-api-get-remote-port:

Get remote port operation
-------------------------

.. highlight:: c
.. snippet-source:: examples/custom-network/remote-host-port/src/net_impl.c

    #include <inttypes.h>
    // ...
    #include <avsystem/commons/avs_utils.h>

    // ...

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

    // ...

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

Similar to ``net_remote_host()``, this function also calls ``getpeername()`` -
but its companion ``stringify_sockaddr_port()``, instead of examining the IP
address stored in the ``sockaddr`` structure, retrieves the port number, and
stringifies it using ``avs_simple_snprintf()``.

Update to vtable
----------------

Of course the newly implemented function need to be referenced in the virtual
method table:

.. highlight:: c
.. snippet-source:: examples/custom-network/remote-host-port/src/net_impl.c
    :emphasize-lines: 8-9

    static const avs_net_socket_v_table_t NET_SOCKET_VTABLE = {
        .connect = net_connect,
        .send = net_send,
        .receive = net_receive,
        .close = net_close,
        .cleanup = net_cleanup,
        .get_system_socket = net_system_socket,
        .get_remote_host = net_remote_host,
        .get_remote_port = net_remote_port,
        .get_opt = net_get_opt,
        .set_opt = net_set_opt
    };
