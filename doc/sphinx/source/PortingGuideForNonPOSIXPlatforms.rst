..
   Copyright 2017-2018 AVSystem <avsystem@avsystem.com>

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

By default, Anjay makes use of POSIX-specific interfaces for retrieving time
and handling network traffic. If no such interfaces are provided by the
toolchain, the user needs to provide custom implementations.


Time API
--------

If POSIX ``clock_gettime`` function is not available:

- Use ``WITH_POSIX_AVS_TIME=OFF`` when running CMake on Anjay,
- Provide an implementation for:

  - ``avs_time_real_now``
  - ``avs_time_monotonic_now``

.. note::
    For signatures and detailed description of listed functions, see
    `avs_commons <https://github.com/AVSystem/avs_commons/blob/master/time/include_public/avsystem/commons/time.h>`_


Networking API
--------------

.. note::

    If LwIP 2.0 is used as a network stack, you may set:

     - ``-DWITH_POSIX_AVS_SOCKET=ON``
     - ``-DWITH_IPV6=OFF``
     - ``-DPOSIX_COMPAT_HEADER=avs_commons/git/compat/lwip-posix-compat.h``
    CMake options for an out-of-the-box socket compatibility layer implementation.

If POSIX socket API is not available:

- Use ``WITH_POSIX_AVS_SOCKET=OFF`` when running CMake on Anjay,
- Provide an implementation for:

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


  - ``_avs_net_create_tcp_socket`` - only required if the ``fw_update`` module
    should support HTTP/HTTPS transfers. Otherwise, can be safely implemented as
    ``return -1;``.

    Function signature:

    .. code-block:: c

        int _avs_net_create_tcp_socket(avs_net_abstract_socket_t **socket,
                                       const void *socket_configuration);

    ``socket_configuration`` argument is a pointer to
    ``const avs_net_socket_configuration_t`` struct cast to ``void *``.

    The function should return 0 on success and a negative value on error.
    It should create a socket object, and return its pointer case to
    ``avs_net_abstract_socket_t *`` through the ``*socket`` argument.
    The socket object should be a struct, whose first field is
    ``avs_net_socket_v_table_t *`` filled with pointers to method handlers.

    Minimal set of socket methods that have to be implemented:

    - ``cleanup``
    - ``close``
    - ``connect``
    - ``create``
    - ``errno``
    - ``receive``
    - ``send``
    - ``set_opt`` able to set the ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT`` option
    - ``shutdown``


.. warning::
    Anjay may attempt to call socket methods other than listed above, even
    though they are not essential for correct operation of the application.
    Make sure that all members of ``avs_net_socket_v_table_t`` are not NULL
    - if required, provide a stub that always fails.

.. note::
    For signatures and detailed description of listed methods, see
    `avs_commons <https://github.com/AVSystem/avs_commons/blob/master/net/include_public/avsystem/commons/net.h>`_
