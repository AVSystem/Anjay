..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Event loop support
==================

.. highlight:: c

.. contents:: :local:

Introduction
------------

When ``WITH_POSIX_AVS_SOCKET`` option is disabled when compiling Anjay,
``WITH_EVENT_LOOP`` will normally be disabled as well. That means that the
``anjay_event_loop_run()`` and ``anjay_serve_any()`` functions will not be
available, and applications will generally need to implement a
:doc:`/AdvancedTopics/AT-CustomEventLoop` instead.

However, as long as the underlying API provides a function reasonably similar
to either ``select()`` or ``poll()``, it is possible to enable the event loop
functionality by providing a POSIX compatibility header and manually enabling
``WITH_EVENT_LOOP``.

Deciding between select() and poll()
------------------------------------

Two equivalent implementations of the event loop are provided in Anjay - one
uses the ``select()`` call, the other uses ``poll()``. ``poll()`` is generally
preferred due to known limitations of ``select()``. On Unix-like systems, when
using CMake to compile the library, one or the other implementation is chosen
automatically based on whether ``poll()`` is available in the system.

The event loop uses these APIs directly because the ``avs_net`` layer does not
provide abstraction over the concept of polling multiple sockets. It has been
decided that this is a solution simpler than significantly extending the
``avs_net`` API.

The implementation based on ``select()`` requires the following APIs, reasonably
similar to the ones defined in Unix-like systems, to be available:

* ``sockfd_t`` type to represent the socket descriptor - normally a ``typedef``
  to ``int``
* optional ``INVALID_SOCKET`` macro - automatically defined to ``-1`` if not
  explicitly provided
* ``fd_set`` type
* ``FD_ZERO()``, ``FD_SET()`` and ``FD_ISSET()`` operations, implemented as
  functions or macros
* ``FD_SETSIZE`` constant
* ``struct timeval`` with ``tv_sec`` and ``tv_usec`` fields
* ``int select(nfds_t nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)``
  function, or a macro that can be called as if it had this signature

Conversely, the implementation based on ``poll()`` requires the following:

* ``sockfd_t`` type to represent the socket descriptor - normally a ``typedef``
  to ``int``
* optional ``INVALID_SOCKET`` macro - automatically defined to ``-1`` if not
  explicitly provided
* ``struct pollfd`` with ``fd`` field of type ``sockfd_t``, as well as
  ``events`` and ``revents`` fields of scalar types
* ``POLLIN`` constant, compatible with the ``events`` field mentioned above
* ``int poll(struct pollfds *fds, size_t nfds, int timeout_ms)`` function, or a
  macro that can be called as if it had this signature

.. note::

    The ``sockfd_t`` type is not standard on Unix-like systems. It has been
    introduced to allow for socket descriptor types other than ``int``, found
    on some systems - e.g. ``SOCKET`` in Win32 is equivalent to ``uintptr_t``.

One or the other implementation is chosen based on state of the
``AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_POLL`` compile-time definition. When
using CMake for compiling, its value is detected; when manually populating the
configuration headers, it can be configured in ``avs_commons_config.h``.

You can also add ``#define`` or ``#undef`` for this macro in the POSIX
compatibility header, explained below.

Writing the POSIX compatibility header
--------------------------------------

The POSIX compatibility header mechanism has originally been conceived as a way
of allowing the use of the default implementations of the networking API (as
well as time API) on platforms that have APIs that are close to the Unix
standard but have minor incompatible differences - examples include lwIP and
Windows.

However, when the default networking layer is not in use, a variant of this
header limited in scope can be used to provide the minimal API subset required
for the event loop.

The POSIX compatibility header can be any custom header file, specified using
the ``-DPOSIX_COMPAT_HEADER`` option on CMake command line, or via the
``AVS_COMMONS_POSIX_COMPAT_HEADER`` macro in ``avs_commons_config.h``. It is
utilized as ``#include AVS_COMMONS_POSIX_COMPAT_HEADER`` (when using CMake,
quotes are added around the value provided on the command line), so please keep
the include path configuration in mind or use absolute paths if feasible.

The header shall contain the necessary ``#include`` directives and declarations
so that the requirements described above are met.

For example, a POSIX compatibility header for `Zephyr
<https://zephyrproject.org/>`_ may look like::

    #include <net/socket.h>

    typedef int sockfd_t;

    #ifndef pollfd
    #    define pollfd zsock_pollfd
    #endif // pollfd

    #ifndef poll
    #    define poll zsock_poll
    #endif // poll

    #ifndef POLLIN
    #    define POLLIN ZSOCK_POLLIN
    #endif // POLLIN

Note that neither include guards nor ``#pragma once`` is required in this file,
although it is permitted to include such guards.

.. note::

    The POSIX compatibility header is also included in the file that implements
    ``avs_time_real_now()`` and ``avs_time_monotonic_now()`` if
    ``WITH_POSIX_AVS_TIME`` is enabled, so you may need to also add lines such
    as ``#include <sys/time.h>`` or consider implementing the :doc:`../TimeAPI`
    yourself.
