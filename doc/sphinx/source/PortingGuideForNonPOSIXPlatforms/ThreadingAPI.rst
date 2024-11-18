..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Threading API
=============

Overview
--------

If Anjay is compiled with thread safety enabled (``WITH_THREAD_SAFETY`` CMake
option), it requires some basic threading primitives to be implemented for the
thread safety features to work properly.

The same threading primitives are additionally used by the ``avs_net`` and
``avs_log`` modules regardless of whether thread safety is enabled or not.

The specific requirements are:

- ``avs_net`` requires ``avs_init_once()``,
- ``avs_log`` and Anjay thread safety require ``avs_mutex_create()``,
  ``avs_mutex_cleanup()``, ``avs_mutex_lock()``, ``avs_mutex_unlock()``, and
  ``avs_init_once()``.

In addition, ``avs_sched`` optionally depends on ``avs_condvar_create()``,
``avs_condvar_cleanup()``, ``avs_condvar_notify_all()`` as well as
``avs_mutex_*`` APIs. The dependency can be controlled with
``WITH_SCHEDULER_THREAD_SAFE`` CMake option. This option normally has the same
setting as ``WITH_THREAD_SAFETY``.

There are two independent implementations of the threading API for compatibility
with most platforms:

- `based on pthreads
  <https://github.com/AVSystem/avs_commons/tree/master/src/compat/threading/pthread>`_,
- `based on C11 atomic operations
  <https://github.com/AVSystem/avs_commons/tree/master/src/compat/threading/atomic_spinlock>`_.

.. note::

    You may use either of the implementations listed above as a reference for
    writing your own if necessary.

List of functions to implement
------------------------------

If, for some reason neither of the default implementations is suitable:

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
