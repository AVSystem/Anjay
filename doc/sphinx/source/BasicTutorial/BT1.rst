..
   Copyright 2017-2019 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Getting started!
================

First, let's create a minimum build environment for our example client. We
will be using a following directory structure:

.. code-block:: none

    example/
      ├── CMakeLists.txt
      └── src/
          └── main.c

Note that all code found in this tutorial is available under
``examples/tutorial/BTx`` in Anjay source directory, where ``x`` stands for
the index of tutorial chapter. Each tutorial project follows the same project
directory layout.

Build system
^^^^^^^^^^^^

We are going to use CMake as a build system. Let's create an initial minimal
``CMakeLists.txt``:

.. highlight:: cmake
.. literalinclude:: tutorial/BT1/CMakeLists.txt

.. _anjay-hello-world:

Hello World client
^^^^^^^^^^^^^^^^^^

At this moment we can begin to do actual coding, let's create ``main.c``
source file:

.. highlight:: c
.. literalinclude:: tutorial/BT1/src/main.c

.. note::

    We recommend you to look at the doxygen generated
    `API documentation <../api/>`_ if something isn't immediately
    clear to you.

Let's just build our minimal client and test that it works:

.. code-block:: sh

    $ cmake . && make && ./anjay-tutorial

.. note::

    For more information on building process see
    :doc:`/Compiling_client_applications`.

Congratulations, you now learned how to initialize the Anjay library!
