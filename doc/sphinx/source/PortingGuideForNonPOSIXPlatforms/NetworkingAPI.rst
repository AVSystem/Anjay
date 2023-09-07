..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Networking API
==============

.. highlight:: c

Reference implementations
-------------------------

``avs_net`` includes a full-featured, complete implementation of its networking
API that is designed to work on systems that implement BSD-style socket API
(either directly, or some close variant of it, such as lwIP or Winsock). It can
be found in the `src/net/compat/posix
<https://github.com/AVSystem/avs_commons/tree/master/src/net/compat/posix>`_
directory of its repository.

However, that implementation is very complex, as it includes a lot of
functionality that is not strictly necessary for Anjay to work (and some that is
not used by Anjay at all), alternate variants of code for compatibility with
different systems, extensive error handling etc.

For this reason, we also include tutorial code with minimal, compact
implementation of the networking API:

.. toctree::
   :glob:
   :titlesonly:

   NetworkingAPI/NetworkingAPI-Minimal
   NetworkingAPI/NetworkingAPI-RemoteHostPort
   NetworkingAPI/NetworkingAPI-Bind
   NetworkingAPI/NetworkingAPI-ShutdownRemoteHostname
   NetworkingAPI/NetworkingAPI-Stats
   NetworkingAPI/NetworkingAPI-IpStickiness
   NetworkingAPI/NetworkingAPI-EventLoopSupport
   NetworkingAPI/NetworkingAPI-OtherFeatures


List of functions to implement
------------------------------

.. note::

    If LwIP 2.0 is used as a network stack, you may set:

     - ``-DWITH_POSIX_AVS_SOCKET=ON``
     - ``-DWITH_IPV6=OFF``
     - ``-DPOSIX_COMPAT_HEADER=deps/avs_commons/compat/lwip-posix-compat.h``

    CMake options for an out-of-the-box socket compatibility layer implementation.

If POSIX socket API is not available:

- Use ``WITH_POSIX_AVS_SOCKET=OFF`` when running CMake on Anjay,
- Provide an implementation for:

  - ``_avs_net_create_udp_socket`` - a function with following signature:

    .. snippet-source:: deps/avs_commons/src/net/avs_net_impl.h

        avs_error_t _avs_net_create_udp_socket(avs_net_socket_t **socket,
                                               const void *socket_configuration);

    ``socket_configuration`` argument is a pointer to
    ``const avs_net_socket_configuration_t`` struct cast to ``void *``.

    The function should return ``AVS_OK`` on success and an error code on error.
    It should create a socket object, and return its pointer cast to
    ``avs_net_socket_t *`` through the ``*socket`` argument. The socket object
    should be a struct, whose first field is ``avs_net_socket_v_table_t *``
    filled with pointers to method handlers.

    Minimal set of socket methods that have to be implemented:

    - ``cleanup``
    - ``close``
    - ``connect``
    - ``send``
    - ``receive``
    - ``get_system_socket``
    - ``get_opt`` able to read following options:

      - ``AVS_NET_SOCKET_OPT_STATE``
      - ``AVS_NET_SOCKET_OPT_INNER_MTU``
      - ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT``
    - ``set_opt`` able to set the ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT`` option

    Additional functions that are not strictly necessary to run Anjay, but are
    used by some of the optional functionality

    - ``bind`` - allows binding to a specific statically configured port; also
      used to keep the bound port stable if possible
    - ``get_local_port`` - used to keep the bound port stable if possible
    - ``get_remote_host`` - required for CoAP message cache to work
    - ``get_remote_port`` - required for CoAP message cache to work
    - ``shutdown`` - required for ability to suspend CoAP downloads

  - ``_avs_net_create_tcp_socket`` - only required if the ``fw_update`` module
    should support HTTP/HTTPS transfers, or if support for CoAP over TCP is
    desired. Otherwise, it can be safely implemented as
    ``return avs_errno(AVS_ENOTSUP);``.

    Function signature:

    .. snippet-source:: deps/avs_commons/src/net/avs_net_impl.h

        avs_error_t _avs_net_create_tcp_socket(avs_net_socket_t **socket,
                                               const void *socket_configuration);

    ``socket_configuration`` argument is a pointer to
    ``const avs_net_socket_configuration_t`` struct cast to ``void *``.

    The function should return ``AVS_OK`` on success and an error code on error.
    It should create a socket object, and return its pointer cast to
    ``avs_net_socket_t *`` through the ``*socket`` argument. The socket object
    should be a struct, whose first field is ``avs_net_socket_v_table_t *``
    filled with pointers to method handlers.

    The same set of socket methods is required as is the case with UDP.

  - ``_avs_net_initialize_global_compat_state`` - a function with following
    signature:

    .. snippet-source:: deps/avs_commons/src/net/avs_net_global.h

        avs_error_t _avs_net_initialize_global_compat_state(void);

    The function should return ``AVS_OK`` on success and an error code on error.
    It should initialize any global state that needs to be kept by the network
    stack. If there is no such global state or it is initialized elsewhere, it
    is safe to implement this function as a no-op (``return AVS_OK;``).

  - ``_avs_net_cleanup_global_compat_state`` - a function with following
    signature:

    .. snippet-source:: deps/avs_commons/src/net/avs_net_global.h

        void _avs_net_cleanup_global_compat_state(void);

    The function should clean up any global state that is kept by the network
    stack. If there is no such global state or it is managed elsewhere, it is
    safe to implement this function as a no-op.

  - ``avs_net_resolved_endpoint_get_host_port`` - a function declared in
    `avs_addrinfo.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_addrinfo.h>`_
    with the following signature:

    .. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_addrinfo.h

        avs_error_t
        avs_net_resolved_endpoint_get_host_port(const avs_net_resolved_endpoint_t *endp,
                                                char *host,
                                                size_t hostlen,
                                                char *serv,
                                                size_t servlen);

    This function is used by the procedure that keeps the remote IP address
    stable when the connection URL uses a domain name as the host identifier.

    This functionality can be disabled at compile time by enabling the
    ``WITHOUT_IP_STICKINESS`` CMake option (``-DWITHOUT_IP_STICKINESS=OFF``),
    in which case the library will no longer depend on this function.

.. warning::
    Anjay may attempt to call socket methods other than listed above, even
    though they are not essential for correct operation of the application.
    Make sure that all members of ``avs_net_socket_v_table_t`` are not NULL
    - if required, provide a stub that always fails.

.. note::
    For signatures and detailed description of listed methods, see
    `avs_net.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_net.h>`_
