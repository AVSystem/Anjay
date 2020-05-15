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
``examples/tutorial/BCx`` in Anjay source directory, where ``x`` stands for
the index of tutorial chapter. Each tutorial project follows the same project
directory layout.

Build system
^^^^^^^^^^^^

We are going to use CMake as a build system. Let's create an initial minimal
``CMakeLists.txt``:

.. highlight:: cmake
.. snippet-source:: examples/tutorial/BC1/CMakeLists.txt

    cmake_minimum_required(VERSION 3.1)
    project(anjay-bc1 C)

    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_EXTENSIONS OFF)

    find_package(anjay REQUIRED)

    add_executable(${PROJECT_NAME} src/main.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE anjay)

.. _anjay-hello-world:

Hello World client
^^^^^^^^^^^^^^^^^^

Now, we can begin the actual client implementation. Simple program with
instantiation of Anjay object is presented below:

.. highlight:: c
.. snippet-source:: examples/tutorial/BC1/src/main.c

    #include <anjay/anjay.h>
    #include <avsystem/commons/avs_log.h>

    int main(int argc, char *argv[]) {
        static const anjay_configuration_t CONFIG = {
            .endpoint_name = "urn:dev:os:anjay-tutorial",
            .in_buffer_size = 4000,
            .out_buffer_size = 4000,
            .msg_cache_size = 4000
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        anjay_delete(anjay);
        return 0;
    }

.. note::

    We recommend you to look at the doxygen generated
    `API documentation <../api/>`_ if something isn't immediately
    clear to you.

Let's just build our minimal client and test that it works:

.. code-block:: sh

    $ cmake . && make && ./anjay-bc1

.. important::

   Project will not be configured successfully until you install Anjay library,
   see :doc:`/Compiling_client_applications` for details how to do it.

You will see only some logs and application will immediately close, but if you
do not see "Could not create Anjay object" there, then Anjay was properly
initialized. We will connect to LwM2M Server in the next steps.
