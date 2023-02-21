..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Minimal socket implementation
=============================

.. contents:: :local:

.. note::

    Code related to this tutorial can be found under
    `examples/custom-network/minimal
    <https://github.com/AVSystem/Anjay/tree/master/examples/custom-network/minimal>`_
    in the Anjay source directory.

Introduction
------------

This tutorial builds up on the :doc:`../../BasicClient/BC-Security` tutorial
which contains an implementation of a minimal, but complete LwM2M client.

However, this tutorial is intended to be used with a version of Anjay that has
been compiled without the default network layer implementation, i.e. with these
additional CMake flags::

    -DWITH_POSIX_AVS_SOCKET=OFF
    -DWITHOUT_IP_STICKINESS=ON

.. note::

    This new custom network layer implementation will be based on the POSIX
    socket APIs. This is not very useful in the real world, as the default
    implementation works fine in such environment. However, this tutorial is
    provided as a reference implementation simpler than the actual default one,
    to make it easier to base your code on it.

Adjustments to the build system
-------------------------------

The `CMakeLists.txt <https://github.com/AVSystem/Anjay/blob/master/examples/custom-network/minimal/CMakeLists.txt>`_
file has been modified to accommodate for this custom network layer:

.. highlight:: cmake
.. snippet-source:: examples/custom-network/minimal/CMakeLists.txt
    :emphasize-lines: 4, 10

    cmake_minimum_required(VERSION 3.1)
    project(minimal-custom-network C)

    set(CMAKE_C_STANDARD 99)

    find_package(anjay REQUIRED)

    add_executable(${PROJECT_NAME}
                   src/main.c
                   src/net_impl.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay)

Two changes has been made here:

* The ``set(CMAKE_C_EXTENSIONS OFF)`` setting has been removed. This is because
  we will need to use POSIX APIs, which are considered extensions to the C
  standard and would not compile with this flag set.
* The `net_impl.c
  <https://github.com/AVSystem/Anjay/blob/master/examples/custom-network/minimal/src/net_impl.c>`_
  file has been added to the executable target. Note that the functions defined
  there will be called by Anjay or its dependent libraries, so, in a way, in
  addition to the normal dependency of the application on the library, the
  opposite is also true - parts of the library depends on the application as
  well.

.. note::

    The ``main.c`` is left completely unchanged compared to the
    :doc:`../../BasicClient/BC-Security` version. In fact, in the repository,
    it is a symbolic link to the file from that tutorial.

Global initialization
---------------------

The APIs that need to be implemented are private, so there is no public header
that can be included to provide forward declarations of them. Hence, we start
with manually including the forward declarations, as quoted in the
:doc:`previous article <../NetworkingAPI>`:

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

    avs_error_t _avs_net_initialize_global_compat_state(void);
    void _avs_net_cleanup_global_compat_state(void);
    avs_error_t _avs_net_create_tcp_socket(avs_net_socket_t **socket,
                                           const void *socket_configuration);
    avs_error_t _avs_net_create_udp_socket(avs_net_socket_t **socket,
                                           const void *socket_configuration);

We actually won't need any global state for our implementation, so implementing
the ``_avs_net_{initialize,cleanup}_global_compat_state()`` functions is
trivial:

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

    avs_error_t _avs_net_initialize_global_compat_state(void) {
        return AVS_OK;
    }

    void _avs_net_cleanup_global_compat_state(void) {}

Global state may be useful on some platforms where using the network requires
some global initialization. For example on Windows, this is the right place to
call ``WSAStartup()`` and ``WSACleanup()``.

On embedded platforms, initialization of network interfaces might also go here,
although typically this is done in the main function, before calling any of the
Anjay APIs and the network layer implementation assumes that the interface has
already been initialized.

.. _non-posix-networking-api-create:

Socket creation
---------------

Some platforms that handle TCP and UDP communication with completely different
APIs (`Mbed OS <https://www.mbed.com/en/platform/mbed-os/>`_ being one such
example), will require completely separate code to implement TCP and UDP
communication - or you might choose to implement just one of them, and
implement the other ``_avs_net_create_*_socket()`` function as a placeholder
that always returns an error code.

With BSD-style socket API, however, it is actually trivial to support both TCP
and UDP sockets, so we will do just that.

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

    typedef struct {
        const avs_net_socket_v_table_t *operations;
        int socktype;
        int fd;
        avs_time_duration_t recv_timeout;
    } net_socket_impl_t;

    // ... implementations of NET_SOCKET_VTABLE functions go here
    // ... they will be discussed separately later

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

``avs_commons`` uses an object-oriented paradigm for its socket layer. Any
socket object needs to be created on the heap - it can be any user-defined
structure, but its first member MUST be a pointer to the
``avs_net_socket_v_table_t`` structure. Functions from that structure will be
called as implementations of all the socket operations.

Aside from this ``vtable`` pointer, this minimal implementation contains the
following fields:

* ``socktype`` - either ``SOCK_DGRAM`` or ``SOCK_STREAM``. The actual
  ``socket()`` call for creating the OS-level socket descriptor will be deferred
  until the ``connect`` operation. At that point we will need to know whether we
  need to create a UDP or TCP socket. This will also slightly alter the behavior
  of the ``receive`` method. Thus, we need to store the value, determined at
  socket creation time.
* ``fd`` - the OS-level file descriptor referring to the actual socket.
* ``recv_timeout`` - timeout for the ``receive`` operation. Anjay uses timed
  ``receive`` operation extensively, to provide appropriate retransmission and
  timeout behavior on higher layers, as required by the CoAP and LwM2M
  protocols. This timeout is controlled by ``get_opt`` and ``set_opt``
  operations, so it needs to be stored between method calls.

The actual ``_avs_net_create_udp_socket()`` and ``_avs_net_create_tcp_socket()``
functions are implemented as thin wrappers to the static ``net_create_socket``
function, which allocates the socket object, initializes ``vtable`` and
``socktype`` fields, as well as sets ``fd`` to ``-1`` (signifying no OS-level
socket descriptor initialized yet) and initial ``recv_timeout`` to 30 seconds.

Implementing socket methods
---------------------------

.. _non-posix-networking-api-connect:

Connect
^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

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

In each of the vtable methods, the first ``avs_net_socket_t *`` argument is the
"self" pointer. It is intended to be cast to the actual type that has been
allocated for the socket.

To call the POSIX ``connect()`` function, we need a socket address formatted as
some structure from the ``struct sockaddr`` family. ``avs_commons`` use strings
for representing TCP/IP endpoint information - ``host`` can be either a
stringified IP address or a hostname, while ``port`` is a stringified port
number. This is designed to match the API of the POSIX ``getaddrinfo()``
function - as such, it is natural to use it in our implementation.

In the ``hints`` structure, we fill the ``ai_socktype`` with the type stored at
socket creation time - either ``SOCK_DGRAM`` or ``SOCK_STREAM``. If the socket
file descriptor has already been created, we also fill ``ai_family`` with the
socket family (most likely ``AF_INET`` or ``AF_INET6``).

If ``getaddrinfo()`` fails, we return the ``avs_errno(AVS_EADDRNOTAVAIL)`` error
code.

Then, we create the socket descriptor if needed, and ``connect()`` it -
returning the ``avs_errno(AVS_ECONNREFUSED)`` error code if necessary.

.. note::

    For more complete error handling, you can use ``avs_map_errno(errno)``
    function, declared in ``avs_errno_map.h``, to translate and forward the
    actual ``errno`` values to the caller. This tutorial uses hardcoded error
    codes for simplicity.

.. note::

    This simplistic code does not implement some features that might be useful:

    * You might want to try connecting to subsequent addresses from the ``addr``
      list if the first one fails - especially for TCP. Such issues may happen
      e.g. when the system has incomplete IPv6 connectivity.
    * You might want to implement connecting logic in a more sophisticated way,
      e.g. by putting the socket in non-blocking mode and using ``poll()`` after
      ``connect()``, to implement better-defined timeout handling when
      connecting - especially for TCP.

Send
^^^^

The ``send()`` implementation is self-explanatory:

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

    static avs_error_t
    net_send(avs_net_socket_t *sock_, const void *buffer, size_t buffer_length) {
        net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
        ssize_t written = send(sock->fd, buffer, buffer_length, MSG_NOSIGNAL);
        if (written >= 0 && (size_t) written == buffer_length) {
            return AVS_OK;
        }
        return avs_errno(AVS_EIO);
    }

.. important::

    This implementation may behave erroneously for TCP. The POSIX API for
    stream-oriented sockets permits so-called "short writes", i.e. the case
    where ``send()`` writes less data than passed to it is treated as success.
    The ``avs_commons`` API does not - so a proper implementation of this method
    for TCP shall call underlying ``send()`` function in a loop until either all
    data is sent, or an error occurs.

.. note::

    For more completeness, you might want to e.g. call ``poll()`` for the
    ``POLLOUT`` event, to implement better-defined timeout handling when
    sending.

.. _non-posix-networking-api-receive:

Receive
^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

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

Implementation of the receive method is a bit more complicated than that of the
send method, because proper receive timeout handling is essential for Anjay.

That's why ``poll()`` with a single socket, waiting for the ``POLLIN`` event is
called before actually calling ``read()``. To call ``poll()``, the configured
receive timeout, stored as ``avs_time_duration_t``, needs to be converted to the
unit expected by ``poll()`` - this is done using
``avs_time_duration_to_scalar()``, with additional adjustments to ensure
expected behavior.

If a timeout occurs, ``avs_errno(AVS_ETIMEDOUT)`` is returned; if either some
data is available or an error occurs, ``read()`` is called - in case of error
it will return a negative value, which in this implementation is handled by
returning ``avs_errno(AVS_EIO)``, but could be more completely handled by
actually translating the ``errno`` value.

If some data has been successfully received, ``*out_bytes_received`` shall be
filled with the number of bytes received.

For datagram sockets, it is additionally important to handle the truncated
message case - so that e.g. the CoAP layer can determine whether the received
payload is complete. Unfortunately, it is non-trivial to do so when using the
``read()`` function - that's why in this simplistic implementation we
pessimistically assume that if the buffer is fully filled, then the data might
have been truncated. Proper handling of this case can be achieved by using the
``MSG_TRUNC`` flag, which has not been used because it's Linux-specific, or by
using the ``recvmsg()`` API, which has not been done here because the more
convoluted API of that function would make this example code more difficult to
follow.

.. note::

    ``*out_bytes_received`` shall be set for both success and
    ``avs_errno(AVS_EMSGSIZE)`` cases.

Close
^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

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

This function is pretty self-explanatory - but please note that unlike the POSIX
``close()`` function, the close operation on ``avs_commons`` sockets does
**not** remove the socket object. This is why the cleanup operation exists.

Cleanup
^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

    static avs_error_t net_cleanup(avs_net_socket_t **sock_ptr) {
        avs_error_t err = AVS_OK;
        if (sock_ptr && *sock_ptr) {
            err = net_close(*sock_ptr);
            avs_free(*sock_ptr);
            *sock_ptr = NULL;
        }
        return err;
    }

The cleanup operation is also self-explanatory, although please note that there
is no requirement to call the close operation before it - that's why it is
called from inside this function here.

Get system socket
^^^^^^^^^^^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

    static const void *net_system_socket(avs_net_socket_t *sock_) {
        net_socket_impl_t *sock = (net_socket_impl_t *) sock_;
        return &sock->fd;
    }

This function is only called by Anjay from ``anjay_event_loop_run()`` and
``anjay_serve_any()`` - but these functions will generally not be available when
Anjay is configured to use custom socket implementation. However, the "system
socket" operation is necessary to implement the
:doc:`../../AdvancedTopics/AT-CustomEventLoop` as well.

On platforms that use POSIX-style file descriptor numbers, the standard practice
is to return a pointer to such file descriptor variable. However, the only
actual requirement is that the usage matches the implementation - so you can
return a pointer to any kind of object that you will be able to use to poll for
incoming events in the event loop.

Get/set socket options
^^^^^^^^^^^^^^^^^^^^^^

.. highlight:: c
.. snippet-source:: examples/custom-network/minimal/src/net_impl.c

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
        case AVS_NET_SOCKET_HAS_BUFFERED_DATA:
            out_option_value->flag = false;
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

The ``get_opt``/``set_opt`` interface is used for querying and setting various
state information about a given socket. The options that can be get or set are
listed in the `avs_net_socket_opt_key_t
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_socket.h#L502>`_
enumeration. Option values are passed or returned using the
`avs_net_socket_opt_value_t
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_socket.h#L674>`_
union. See the nearby documentation if you need clarification on which field is
used to pass values for which option.

Three of there options are essential for the operation of Anjay:

* ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT`` - used for getting and setting the current
  receive timeout, as used by the :ref:`non-posix-networking-api-receive`
  operation.
* ``AVS_NET_SOCKET_OPT_STATE`` (get-only) - used to check in which state
  (closed, shut down, bound, accepted or connected) the socket currently is.
* ``AVS_NET_SOCKET_OPT_INNER_MTU`` (get-only; only used for UDP) - used to check
  the number of bytes that can be safely sent and received in a single UDP
  datagram over the given socket.
* ``AVS_NET_SOCKET_HAS_BUFFERED_DATA`` (get-only; optional but highly
  recommended) - used to check whether all data received from the underlying
  system socket has been processed. This is used to make sure that when control
  is returned to the event loop, the ``poll()`` call will not stall waiting for
  new data that in reality has been already buffered and could be retrieved
  using the avs_commons APIs. This is usually meaningful for (D)TLS connections,
  but for almost all simple unencrypted socket implementations, this should
  always return ``false``. If this option is not supported, then the library
  will always retry receiving data until a timeout condition occurs (timeout is
  set to zero for subsequent retries), which may lead to stalling of the event
  loop.

.. note::

    The ``AVS_NET_SOCKET_OPT_INNER_MTU`` option will be used in addition to
    buffer sizes to e.g. calculate the maximum size of packets for Block-wise
    CoAP transfers. This is why it is essential to provide this value. If
    querying this information from the actual connection or network interface is
    not possible, a hardcoded estimate like the one above should be OK.

Limitations
-----------

This minimal implementation is enough to make Anjay run, but a number of
functionalities will not work:

* Attempt to set `anjay_configuration_t::udp_listen_port
  <../../api/structanjay__configuration.html#acf74549a99ca3ad5aedb227c4b0258ca>`_
  will result in no connectivity, as the bind operation is not supported.
* Local port will not be preserved between subsequent connections to the same
  server.
* CoAP message cache will not work, regardless of value of the
  `anjay_configuration_t::msg_cache_size
  <../../api/structanjay__configuration.html#a3bb16de58b283370b1ab20698dd4849a>`_
  setting.
* Suspending CoAP downloads when entering offline mode will not work; downloads
  will be aborted instead.
* ``anjay_get_tx_bytes()`` and ``anjay_get_rx_bytes()`` APIs will not work.
* ``WITHOUT_IP_STICKINESS`` compile-time flag cannot be disabled, which means
  that when connecting to a server using a domain name, it is not guaranteed
  that subsequent connections will use the same IP address.

We will discuss implementing additional methods to address these limitations in
subsequent chapters.
