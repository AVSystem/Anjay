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

Time API
========

List of functions to implement
------------------------------

If POSIX ``clock_gettime`` function is not available:

- Use ``WITH_POSIX_AVS_TIME=OFF`` when running CMake on Anjay,
- Provide an implementation for:

  - ``avs_time_real_now``
  - ``avs_time_monotonic_now``

.. note::
    For signatures and detailed description of listed functions, see
    `avs_time.h <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_time.h>`_

Reference implementation
------------------------

The default
`avs_compat_time.c <https://github.com/AVSystem/avs_commons/blob/master/src/utils/compat/posix/avs_compat_time.c>`_
implementation that uses POSIX ``clock_gettime()`` API can be used as a
reference for writing your own integration layer.

avs_time_real_now()
^^^^^^^^^^^^^^^^^^^

The ``avs_time_real_now()`` function should return the current *real* time,
i.e. the amount of time that passed since January 1st, 1970, midnight UTC (the
Unix epoch).

In the reference POSIX-based implementation, it is a simple wrapper for the
``CLOCK_REALITME`` clock.

.. highlight:: c
.. snippet-source:: deps/avs_commons/src/utils/compat/posix/avs_compat_time.c

    avs_time_real_t avs_time_real_now(void) {
        struct timespec system_value;
        avs_time_real_t result;
        clock_gettime(CLOCK_REALTIME, &system_value);
        result.since_real_epoch.seconds = system_value.tv_sec;
        result.since_real_epoch.nanoseconds = (int32_t) system_value.tv_nsec;
        return result;
    }

avs_time_monotonic_now()
^^^^^^^^^^^^^^^^^^^^^^^^

The ``avs_time_monotonic_now()`` function should return the current *monotonic*
time, i.e. the amount of time that passed since *some epoch* - it might be
any point in time, but needs to be stable at least throughout the lifetime of
the process - different epochs might be used for different launches of the
application.

System boot time is often used as an epoch for the monotonic clock.

If the real-time clock is considered stable, and not reset while the application
is running, it may be also used as the monotonic clock.

This is used in the reference implementation - it is generally a wrapper for the
``CLOCK_MONOTONIC`` clock, but on some platforms it is not available -
``CLOCK_REALTIME`` is used in these cases.

.. highlight:: c
.. snippet-source:: deps/avs_commons/src/utils/compat/posix/avs_compat_time.c

    avs_time_monotonic_t avs_time_monotonic_now(void) {
        struct timespec system_value;
        avs_time_monotonic_t result;
    #    ifdef CLOCK_MONOTONIC
        if (clock_gettime(CLOCK_MONOTONIC, &system_value))
    #    endif
        {
            // CLOCK_MONOTONIC is not mandatory in POSIX;
            // fallback to REALTIME if we don't have it
            clock_gettime(CLOCK_REALTIME, &system_value);
        }
        result.since_monotonic_epoch.seconds = system_value.tv_sec;
        result.since_monotonic_epoch.nanoseconds = (int32_t) system_value.tv_nsec;
        return result;
    }
