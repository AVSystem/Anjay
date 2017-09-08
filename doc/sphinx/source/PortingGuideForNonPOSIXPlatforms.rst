..
   Copyright 2017 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Porting guide for non-POSIX platforms
=====================================

Anjay makes use of quite a few POSIX-specific interfaces. In order to compile
it for a non-POSIX platform, one needs to:

- Create a **POSIX compatibility header** containing required types, constants
  and function declarations (see list below),
- When running CMake on the Anjay library, use ``WITH_POSIX=OFF`` and set
  ``POSIX_COMPAT_HEADER`` to an absolute path to the **POSIX compatibility
  header**,
- Implement :ref:`avs_net_socket API <avs_net_socket_apis>`. If a POSIX-like
  socket API is available (e.g. when using LwIP), use
  ``-DWITH_POSIX_AVS_SOCKET=ON`` CMake option for an out-of-the-box
  implementation.

.. note::
    You can use the ``compat/posix-compat.h`` file from the Anjay repository
    as a template for your own POSIX compatibility header. **Pay close attention
    to anything marked with TODO!**


Required types
--------------

- ``ssize_t``
- ``struct timespec``
- ``struct timeval``
- ``clockid_t``
- ``socklen_t``
- ``struct addrinfo``
- if ``poll()`` is used:

  - ``nfds_t``
  - ``struct pollfd``

- if ``select()`` is used:

  - ``fd_set``


Required constants
------------------

- ``CLOCK_REALTIME``
- ``CLOCK_MONOTONIC``
- ``INET_ADDRSTRLEN`` (if IPv4 is used)
- ``INET6_ADDRSTRLEN`` (if IPv6 is used)
- ``IF_NAMESIZE``
- ``F_GETFL``
- ``F_SETFL``
- ``O_NONBLOCK``
- if ``poll()`` is used:

  - ``POLLIN``
  - ``POLLOUT``
  - ``POLLERR``
  - ``POLLHUP``


Required functions
------------------

.. note::
    These symbols may also be implemented as function-like macros instead
    of functions.

- ``clock_gettime``
- ``strcasecmp``
- ``getaddrinfo``
- ``freeaddrinfo``
- ``gai_strerror``
- ``strdup``
- ``fcntl`` (only ``F_GETFL`` and ``F_SETFL`` operations on ``O_NONBLOCK``
  are required)
- either ``poll()`` or ``select()``
- ``htons``
- ``ntohs``
- ``htonl``
- ``ntohl``


.. note::
    For more details on types, constants and functions listed above, see
    `POSIX-2008 <http://pubs.opengroup.org/onlinepubs/9699919799/>`_.


.. _avs_net_socket_apis:

``avs_net_socket`` APIs
-----------------------

- ``_avs_net_create_udp_socket`` - a function with following signature:

  .. code-block:: c

      int _avs_net_create_udp_socket(avs_net_abstract_socket_t **socket,
                                     const void *socket_configuration);

  ``socket_configuration`` argument is a pointer to
  ``const avs_net_socket_configuration_t`` struct cast to ``void *``.

  The function should return 0 on success and a negative value on error.
  It should create a socket object, and return its pointer case to
  ``avs_net_abstract_socket_t *`` through the ``*socket`` argument.
  The socket object should be a struct, whose first field is
  ``avs_net_socket_v_table_t *`` filled with pointers to method handlers.

  Minimal set of socket methods that have to be implemented:

  - ``bind``
  - ``cleanup``
  - ``close``
  - ``connect``
  - ``create``
  - ``errno``
  - ``get_local_port``
  - ``get_opt`` able to read following options:

    - ``AVS_NET_SOCKET_OPT_STATE``
    - ``AVS_NET_SOCKET_OPT_MTU``
    - ``AVS_NET_SOCKET_OPT_INNER_MTU``
    - ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT``

  - ``get_remote_host``
  - ``get_remote_hostname``
  - ``get_remote_port``
  - ``get_system``
  - ``receive``
  - ``send``
  - ``set_opt`` able to set the ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT`` option

.. warning::
    Anjay may attempt to call other socket methods, even though they are not
    essential for correct operation of the application. Make sure that all
    members of ``avs_net_socket_v_table_t`` are not NULL - if required, provide
    a stub that always fails.

.. note::
    For detailed description of listed methods, see
    `avs_commons <https://github.com/AVSystem/avs_commons/blob/master/net/include_public/avsystem/commons/net.h>`_


- ``_avs_net_create_tcp_socket`` - a function with signature identical to
  ``_avs_net_create_udp_socket``. Since TCP support is not required for LwM2M,
  it may be implemented as ``return -1;``.


Optional functions
------------------

- ``inet_pton``/``inet_ntop`` - if not specified, custom implementations
  are provided,
- ``recvmsg`` with ``MSG_TRUNC`` support - only applicable if the built-in
  implementation of ``avs_net_socket`` is used. ``recvmsg`` is used to detect
  when UDP packets get truncated due to target buffer being too small.
  If ``recvmsg`` is not available, incoming UDP packets that fully fill the
  buffer passed to ``avs_net_socket_receive`` function are considered truncated
  and Anjay will return an error in such case.
