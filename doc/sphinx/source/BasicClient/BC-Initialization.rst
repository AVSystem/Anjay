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

Anjay initialization
====================

First, let's create a minimum build environment for our example client. We
will be using a following directory structure:

.. code-block:: none

    example/
      ├── CMakeLists.txt
      └── src/
          └── main.c

Note that all code found in this tutorial is available under
``examples/tutorial/BC*`` in Anjay source directory. Each tutorial project
follows the same project
directory layout.

Build system
^^^^^^^^^^^^

We are going to use CMake as a build system. Let's create an initial minimal
``CMakeLists.txt``:

.. highlight:: cmake
.. snippet-source:: examples/tutorial/BC-Initialization/CMakeLists.txt

    cmake_minimum_required(VERSION 3.1)
    project(anjay-bc-initialization C)

    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS OFF)

    add_compile_options(-Wall -Wextra)

    find_package(anjay REQUIRED)

    add_executable(${PROJECT_NAME} src/main.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay)

.. _anjay-hello-world:

Hello World client code
^^^^^^^^^^^^^^^^^^^^^^^

Now, we can begin the actual client implementation. Simple program with
instantiation of Anjay object is presented below:

.. highlight:: c
.. snippet-source:: examples/tutorial/BC-Initialization/src/main.c

    #include <anjay/anjay.h>
    #include <avsystem/commons/avs_log.h>

    int main(int argc, char *argv[]) {
        if (argc != 2) {
            avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
            return -1;
        }

        const anjay_configuration_t CONFIG = {
            .endpoint_name = argv[1],
            .in_buffer_size = 4000,
            .out_buffer_size = 4000,
            .msg_cache_size = 4000
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        anjay_event_loop_run(anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));

        anjay_delete(anjay);
        return 0;
    }

.. note::

    Complete code of this example can be found in
    `examples/tutorial/BC-Initialization` subdirectory of main Anjay project
    repository.

Code analysis
^^^^^^^^^^^^^

.. note::

    We recommend you to look at the doxygen generated
    `API documentation <../api/>`_ if something isn't immediately
    clear to you.

First, we call the `anjay_new()
<../api/core_8h.html#a9d95a5005ff7c3b1d76573616c57d4cc>`_ function that
initializes the client. It needs to be passed an `anjay_configuration_t
<../api/structanjay__configuration.html>`_ structure that contains basic runtime
configuration of the client.

The example code above configures the basic values that are most essential:

* `endpoint_name
  <../api/structanjay__configuration.html#aafab5578aa377788d6208d5ea6dc2da9>`_
  sets the Endpoint Client Name - see :ref:`clients-and-servers`.
* `in_buffer_size
  <../api/structanjay__configuration.html#a0be70dc47a294104527cac8e84786f02>`_
  and `out_buffer_size
  <../api/structanjay__configuration.html#a44513f6007ea6db2c75a517dbfa77df4>`_
  control sizes of the buffers used for network communication.
* `msg_cache_size
  <../api/structanjay__configuration.html#a3bb16de58b283370b1ab20698dd4849a>`_
  sets the size of the message cache - this is not strictly necessary for the
  client to work, but it is used to internally cache responses so that
  retransmitted packets are properly handled as duplicates. The bigger this
  buffer, the older packets the library will be able to detect as
  retransmissions.

After initializing the library, `anjay_event_loop_run()
<../api/core_8h.html#a95c229caf3ee8ce7de556256f4307507>`_ is called. This
function doesn't return unless there is a fatal error, instead acting as the
main loop of the LwM2M client.

In more complicated applications, this function would typically be run in a
dedicated thread, while other threads would perform tasks not directly related
to LwM2M and communicate with the LwM2M thread when necessary.

.. important::

    If you intend to run Anjay event loop in a dedicated thread, please make
    sure that the code is properly synchronized. The ``WITH_THREAD_SAFETY`` and
    ``WITH_SCHEDULER_THREAD_SAFE`` compile-time configuration options may be
    helpful in achieving this goal.

The second argument specifies the maximum time for which the loop is allowed to
wait for incoming events in a single iteration - 1 second in this example. The
shorter the time, the more responsive the loop will be in handling asynchronous
requests (e.g. jobs scheduled from another threads), but the average CPU usage
level of the main loop may be higher.

In case the event loop finishes, `anjay_delete()
<api/core_8h.html#a243f18f976bca57b5a7b0714bfb99095>`_ - a function that cleans
up all resources used by the client - is called.

Building and running
^^^^^^^^^^^^^^^^^^^^

Let's build our minimal client:

.. code-block:: sh

    $ cmake . && make

If that succeeds, we can now run it. We need to pass an endpoint name as the
program's argument - this is not important now, but when we get to the point of
being able to communicate with a server, this will be a name that the client
uses to identify itself to the server. Please look ino the :ref:`brief
description of LwM2M <clients-and-servers>` for details on recommended formats
of the endpoint name.

A simple idea for generating an endpoint name is to use the local hostname:

.. code-block:: sh

    $ ./anjay-bc1 urn:dev:os:$(hostname)

.. important::

   Project will not be configured successfully until you install Anjay library,
   see :doc:`/Compiling_client_applications` for details how to do it.

You will see only some logs and the application will appear to freeze - that's
because without any server configuration, there are no tasks to do. However, if
you do not see "Could not create Anjay object" there, then Anjay was properly
initialized. We will connect to LwM2M Server in the next steps.
