..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

IP address stickiness support
=============================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-network/ip-stickiness
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-network/ip-stickiness>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on :doc:`the previous one <NetworkingAPI-Stats>` and
adds support for IP address stickiness, i.e. makes it possible for Anjay to
guarantee that the same IP address will be used for connecting to a server
configured using a DNS hostname each time, regardless of the order of entries in
DNS response.

.. important::

    This tutorial expects Anjay to be configured differently than the previous
    ones. ``WITHOUT_IP_STICKINESS`` should be set to ``OFF`` (default) this
    time. Otherwise the added code will not be used.

For the IP stickiness feature to work, the `preferred_endpoint
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_socket.h#L157>`_
field of ``avs_net_socket_configuration_t`` must be supported. Additionally,
`avs_net_resolved_endpoint_get_host_port()
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_addrinfo.h#L172>`_
also has to be implemented.

Theory of operation
-------------------

The `avs_net_resolved_endpoint_t
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_socket.h#L56>`_
type has been introduced so that resolved addresses (e.g. the
``struct sockaddr`` family can be shared outside of ``avs_commons`` without the
need to depend on platform-specific types in public API.

Any data up to ``AVS_NET_SOCKET_RAW_RESOLVED_ENDPOINT_MAX_SIZE`` (128 bytes) in
size can be stored in that structure. There is also the ``size`` field that can
be used to preserve the size information.

In our example, we will always store an instance of the :ref:`sockaddr_union_t
<non-posix-networking-api-get-remote-host>`, and we will always store
``sizeof(sockaddr_union_t)`` in the ``size`` field. However, your implementation
is free to use these fields in whatever way you feel is appropriate.

This type is primarily used in the ``avs_net_addrinfo_resolve()`` family of
functions, which is basically a portable version of the ``getaddrinfo()`` API.
However, this API is not used by Anjay. However, the type may also be used for
storage of the preferred endpoint by the :ref:`non-posix-networking-api-connect`
function.

The ``avs_net_resolved_endpoint_get_host_port()`` function is used to convert a
resolved address into stringified form that is the primary form of passing host
addresses in ``avs_commons``. In Anjay, it is actually used only to determine
the family (IPv4 vs. IPv6) of the stored address.

Initialization
--------------

.. highlight:: c
.. snippet-source:: examples/custom-network/ip-stickiness/src/net_impl.c
    :emphasize-lines: 10, 31

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        int socktype;
        int fd;
        avs_time_duration_t recv_timeout;
        char remote_hostname[256];
        bool shut_down;
        size_t bytes_sent;
        size_t bytes_received;
        avs_net_resolved_endpoint_t *preferred_endpoint;
    } net_socket_impl_t;

    // ...

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
        socket->preferred_endpoint = configuration->preferred_endpoint;
        *socket_ptr = (avs_net_socket_t *) socket;
        return AVS_OK;
    }

The ``preferred_endpoint`` field is intended as a pointer into user-allocated
storage, so we just store that pointer at creation time.

Changes to the connect function
-------------------------------

.. note::

    In addition to the highlighted changes, the original ``addr`` variable has
    been renamed to ``addrs``. This change has not been highlighted for clarity.

.. highlight:: c
.. snippet-source:: examples/custom-network/ip-stickiness/src/net_impl.c
    :emphasize-lines: 21-38, 42-47

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
        struct addrinfo *addrs = NULL;
        avs_error_t err = AVS_OK;
        if (getaddrinfo(host, port, &hints, &addrs) || !addrs) {
            err = avs_errno(AVS_EADDRNOTAVAIL);
        } else if (sock->fd < 0
                   && (sock->fd = socket(addrs->ai_family, addrs->ai_socktype,
                                         addrs->ai_protocol))
                              < 0) {
            err = avs_errno(AVS_UNKNOWN_ERROR);
        } else {
            const struct addrinfo *addr = addrs;
            if (sock->preferred_endpoint
                    && sock->preferred_endpoint->size == sizeof(sockaddr_union_t)) {
                while (addr) {
                    if (addr->ai_addrlen <= sizeof(sockaddr_union_t)
                            && memcmp(addr->ai_addr,
                                      sock->preferred_endpoint->data.buf,
                                      addr->ai_addrlen)
                                           == 0) {
                        break;
                    }
                    addr = addr->ai_next;
                }
            }
            if (!addr) {
                // Preferred endpoint not found, use the first one
                addr = addrs;
            }
            if (connect(sock->fd, addr->ai_addr, addr->ai_addrlen)) {
                err = avs_errno(AVS_ECONNREFUSED);
            }
            if (sock->preferred_endpoint && avs_is_ok(err)) {
                assert(addr->ai_addrlen <= sizeof(sockaddr_union_t));
                memcpy(sock->preferred_endpoint->data.buf, addr->ai_addr,
                       addr->ai_addrlen);
                sock->preferred_endpoint->size = sizeof(sockaddr_union_t);
            }
        }
        if (avs_is_ok(err)) {
            sock->shut_down = false;
            snprintf(sock->remote_hostname, sizeof(sock->remote_hostname), "%s",
                     host);
        }
        freeaddrinfo(addrs);
        return err;
    }

In the code before the ``connect()`` call, if the ``preferred_endpoint`` pointer
is set and filled with valid data, we iterate over all the entries in the list
returned by ``getaddrinfo()``, and check if any of them matches. If so, that
entry will be passed to the ``connect()`` function. If not, the first entry will
be used.

After a successful ``connect()`` call, the selected address is stored into the
``preferred_endpoint`` structure.

avs_net_resolved_endpoint_get_host_port()
-----------------------------------------

.. highlight:: c
.. snippet-source:: examples/custom-network/ip-stickiness/src/net_impl.c

    avs_error_t
    avs_net_resolved_endpoint_get_host_port(const avs_net_resolved_endpoint_t *endp,
                                            char *host,
                                            size_t hostlen,
                                            char *serv,
                                            size_t servlen) {
        AVS_STATIC_ASSERT(sizeof(endp->data.buf) >= sizeof(sockaddr_union_t),
                          data_buffer_big_enough);
        if (endp->size != sizeof(sockaddr_union_t)) {
            return avs_errno(AVS_EINVAL);
        }
        const sockaddr_union_t *addr = (const sockaddr_union_t *) &endp->data.buf;
        avs_error_t err = AVS_OK;
        (void) ((host
                 && avs_is_err(
                            (err = stringify_sockaddr_host(addr, host, hostlen))))
                || (serv
                    && avs_is_err((err = stringify_sockaddr_port(addr, serv,
                                                                 servlen)))));
        return err;
    }

Since in our implementation ``avs_net_resolved_endpoint_t`` is just a wrapper
around ``sockaddr_union_t``, we can use the :doc:`previously introduced
<NetworkingAPI-RemoteHostPort>` ``stringify_sockaddr_host()`` and
``stringify_sockaddr_port()`` functions.

Please note however, that either of the ``host`` and ``serv`` arguments may be
``NULL``, in which case this function shall only fill the non-NULL arguments.
