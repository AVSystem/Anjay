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

Event loop
==========

.. highlight:: c

The last task to complete is to write the event loop which will control the
whole application.

Anjay currently does not use any self-contained threads, so some manual handling
of incoming events is necessary. Those events may come from a number of sources:

- Network packets from LwM2M servers
- Anjay's internal task scheduler
- External events specific to the client application

On POSIX systems, a simple and portable way to handle all these events is to use
the ``poll()`` system call.

Incoming network packets
^^^^^^^^^^^^^^^^^^^^^^^^

UDP sockets that Anjay uses to communicate with LwM2M servers are accessible
via the ``anjay_get_sockets()`` call. All the used sockets are returned as an
``AVS_LIST`` (implemented and documented in the `AVSystem Commons Library
<https://github.com/AVSystem/avs_commons>`_).

.. note::

    To retrieve a low-level operating system handle (on POSIX systems it is
    a file descriptor) use the ``avs_net_socket_get_system()`` call.

When there is a new incoming packet on some socket, ``anjay_serve()`` shall
be called to handle it.

Therefore our first attempt to write an event loop could look like this:

.. _incomplete-event-loop-idea:

#. Obtain all sources of incoming packets (i.e. server sockets) from the
   library.

#. Use ``poll()`` on them to wait for some communication.

#. When the packet arrives, call ``anjay_serve()`` that will handle the
   message.

Before doing that though, you should also understand a very important concept
Anjay is using, i.e. :ref:`Task scheduler <task-scheduler>`.

.. _task-scheduler:

Task scheduler
^^^^^^^^^^^^^^

Anjay uses an internal scheduler for a number of tasks, including automated
sending of Registration Updates and notifications, among others.

During runtime, Anjay schedules jobs with specific deadlines, and
expects them to be executed when the right time comes -- it is relying on
``anjay_sched_run()`` being regularly invoked.

The ``anjay_sched_run()`` iterates over the scheduled jobs (checking if some
of them should be run at this particular moment) and executes them if
necessary.

The requirement of regularly invoking ``anjay_sched_run()`` is a direct
consequence of Anjay being a single-threaded application. It is not a problem
however, as you may call ``anjay_sched_run()`` repeatedly in the main program
loop that you would have to write anyway.

Unfortunately, a naive implementation could cause unnecessary waste of CPU
time. This problem is addressed by ``anjay_sched_calculate_wait_time_ms()``
which returns the number of milliseconds before the next scheduled job -
so, unless there is a communication going on, it would be the amount of
time the CPU can sleep. In the next example we will use this value as a
``poll()`` timeout.

.. _basic-event-loop:

Event loop finally
^^^^^^^^^^^^^^^^^^

Taking into account previous subsections we could modify the event loop
presented :ref:`before <incomplete-event-loop-idea>` as follows:

#. Obtain all sources of incoming packets (i.e. server sockets) from the library.

#. Determine what is the expected time to the next job that has to be executed.

#. Use ``poll()`` to wait for the incoming communication (but not for too long,
   to prevent missing any pending scheduler jobs).

#. Call ``anjay_serve()`` on packet arrival, which will handle the message.

#. Run the scheduler to execute any outstanding jobs.


.. topic:: Wait, why do I have to repeatedly get the sources of packets?

    There are at least two reasons for doing that:

    #. At some point network error might have happened, forcing the library
       to reconnect the socket, possibly changing its underlying descriptor.

    #. The data model might have changed (for instance due to spontaneous Bootstrap
       Write) and some Server were added / removed or their credentials were
       modified.

So, it could be written like this:

.. snippet-source:: examples/tutorial/BT3/src/main.c

    int main_loop(anjay_t *anjay) {
        while (true) {
            // Obtain all network data sources
            AVS_LIST(avs_net_socket_t *const) sockets =
                    anjay_get_sockets(anjay);

            // Prepare to poll() on them
            size_t numsocks = AVS_LIST_SIZE(sockets);
            struct pollfd pollfds[numsocks];
            size_t i = 0;
            AVS_LIST(avs_net_socket_t *const) sock;
            AVS_LIST_FOREACH(sock, sockets) {
                pollfds[i].fd = *(const int *) avs_net_socket_get_system(*sock);
                pollfds[i].events = POLLIN;
                pollfds[i].revents = 0;
                ++i;
            }

            const int max_wait_time_ms = 1000;
            // Determine the expected time to the next job in milliseconds.
            // If there is no job we will wait till something arrives for
            // at most 1 second (i.e. max_wait_time_ms).
            int wait_ms =
                    anjay_sched_calculate_wait_time_ms(anjay, max_wait_time_ms);

            // Wait for the events if necessary, and handle them.
            if (poll(pollfds, numsocks, wait_ms) > 0) {
                int socket_id = 0;
                AVS_LIST(avs_net_socket_t *const) socket = NULL;
                AVS_LIST_FOREACH(socket, sockets) {
                    if (pollfds[socket_id].revents) {
                        if (anjay_serve(anjay, *socket)) {
                            avs_log(tutorial, ERROR, "anjay_serve failed");
                        }
                    }
                    ++socket_id;
                }
            }

            // Finally run the scheduler
            anjay_sched_run(anjay);
        }
        return 0;
    }

    int main(int argc, char *argv[]) {
        // ...

        result = main_loop(anjay);

        // ...
        return result;
    }

That's it! You should now be able to connect to your LwM2M Server and exchange
messages with it interactively.

Other events
^^^^^^^^^^^^

As we've been discussing, the code above is enough to handle all events that
may happen within the Anjay library itself. Of course, the application usually
needs to handle its own activity, this is however outside of the scope of
this tutorial, but the presented code may be used as a good starting point.
