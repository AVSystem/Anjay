..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

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
    `avs_time.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_time.h>`_


Networking API
--------------

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

    .. code-block:: c

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

    - ``bind``
    - ``cleanup``
    - ``close``
    - ``connect``
    - ``create``
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
    ``return avs_errno(AVS_ENOTSUP);``.

    Function signature:

    .. code-block:: c

        avs_error_t _avs_net_create_tcp_socket(avs_net_socket_t **socket,
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
    - ``create``
    - ``receive``
    - ``send``
    - ``set_opt`` able to set the ``AVS_NET_SOCKET_OPT_RECV_TIMEOUT`` option
    - ``shutdown``

  - ``_avs_net_initialize_global_compat_state`` - a function with following
    signature:

    .. code-block:: c

        avs_error_t _avs_net_initialize_global_compat_state(void);

    The function should return ``AVS_OK`` on success and an error code on error.
    It should initialize any global state that needs to be kept by the network
    stack. If there is no such global state or it is initialized elsewhere, it
    is safe to implement this function as a no-op (``return AVS_OK;``).

  - ``_avs_net_cleanup_global_compat_state`` - a function with following
    signature:

    .. code-block:: c

        void _avs_net_cleanup_global_compat_state(void);

    The function should clean up any global state that is kept by the network
    stack. If there is no such global state or it is managed elsewhere, it is
    safe to implement this function as a no-op.


.. warning::
    Anjay may attempt to call socket methods other than listed above, even
    though they are not essential for correct operation of the application.
    Make sure that all members of ``avs_net_socket_v_table_t`` are not NULL
    - if required, provide a stub that always fails.

.. note::
    For signatures and detailed description of listed methods, see
    `avs_net.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_net.h>`_


Threading API
-------------

The ``avs_net`` and ``avs_log`` modules require threading primitives
to operate reliably in multi-threaded environments, specifically:

- ``avs_net`` requires ``avs_init_once()``,
- ``avs_log`` requires ``avs_mutex_create()``, ``avs_mutex_cleanup()``,
  ``avs_mutex_lock()``, ``avs_mutex_unlock()``, and
  ``avs_init_once()``.

In addition, ``avs_sched`` optionally depends on ``avs_condvar_create()``,
``avs_condvar_cleanup()``, ``avs_condvar_notify_all()`` as well as
``avs_mutex_*`` APIs. The dependency can be controlled with
``WITH_SCHEDULER_THREAD_SAFE`` CMake option.

There are two independent implementations of the threading API for compatibility
with most platforms:

- based on `pthreads <https://en.wikipedia.org/wiki/POSIX_Threads>`_,
- based on C11 atomic operations.

If, for some reason none of the defaults is suitable:

- Use ``WITH_CUSTOM_AVS_THREADING=ON`` when running CMake on Anjay,
- Provide an implementation of:

  - ``avs_mutex_create()``,
  - ``avs_mutex_cleanup()``,
  - ``avs_init_once()``,
  - ``avs_mutex_lock()``,
  - ``avs_mutex_unlock()``.

- And if you use thread-safe scheduler, also provide implementation for:

  - ``avs_condvar_create()``,
  - ``avs_condvar_cleanup()``,
  - ``avs_condvar_notify_all()``.

.. note::
    For signatures and detailed description of listed functions, see

    - `avs_mutex.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_mutex.h>`_
    - `avs_init_once.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_init_once.h>`_
    - `avs_condvar.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_condvar.h>`_

.. note::

    If you intend to operate the library in a single-threaded fashion, you may
    provide no-op stubs (returning success) of all mentioned primitives.
