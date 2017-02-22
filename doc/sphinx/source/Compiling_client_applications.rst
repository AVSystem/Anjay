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

Compiling client applications
=============================

Compiling the library
---------------------

Anjay uses CMake for project configuration. To compile the library with default settings:

.. code-block:: bash

    cmake . && make


Cross-compiling
~~~~~~~~~~~~~~~

First, prepare a CMake toolchain file (see `CMake documentation <https://cmake.org/cmake/help/v3.0/manual/cmake-toolchains.7.html#cross-compiling>`_), then pass :code:`CMAKE_TOOLCHAIN_FILE` when configuring Anjay:

.. code-block:: bash

    cmake -DCMAKE_TOOLCHAIN_FILE=$YOUR_TOOLCHAIN_FILE . && make

An example CMake toolchain file for an ARM Cortex-M3-powered STM3220 platform may look like so:

.. code-block:: cmake

    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_SYSTEM_VERSION 1)

    set(CMAKE_C_COMPILER arm-none-eabi-gcc)

    # CMAKE_C_FLAGS set in a toolchain file get overwritten by CMakeCInformation.cmake
    # unless they are FORCEfully set in the cache
    # See http://stackoverflow.com/a/30217088/2339636
    unset(CMAKE_C_FLAGS CACHE)
    set(CMAKE_C_FLAGS "-mcpu=cortex-m3 -mthumb -msoft-float -ffunction-sections -fdata-sections -fno-common -fmessage-length=0 -std=gnu99 --specs=nosys.specs" CACHE STRING "" FORCE)

    set(CMAKE_EXE_LINKER_FLAGS "-Wl,-gc-sections")


Installing the library
----------------------

To install Anjay headers and libraries in :code:`/usr/local`:

.. code-block:: bash

    cmake . && make && sudo make install

A custom installation prefix may be set using :code:`CMAKE_INSTALL_PREFIX`:

.. code-block:: bash

    cmake -DCMAKE_INSTALL_PREFIX=/custom/path . && make && make install


Including the library in an application
---------------------------------------

CMake projects
~~~~~~~~~~~~~~

The preferred method of using Anjay in custom projects is to use CMake :code:`find_package` command after installing the library:

.. code-block:: cmake

    find_package(anjay)
    include_directories(${ANJAY_INCLUDE_DIRS})
    target_link_libraries(my_executable ${ANJAY_LIBRARIES}) # or ANJAY_LIBRARIES_STATIC for a static library

.. note::

    If a custom installation path is used, you need to set :code:`anjay_DIR` CMake variable to :code:`$YOUR_INSTALL_PREFIX/lib/anjay`.


Alternative build systems
~~~~~~~~~~~~~~~~~~~~~~~~~

For other build systems, necessary flags can be retrieved using :code:`cmake` command:

.. code-block:: bash

    cmake --find-package -DNAME=anjay -DLANGUAGE=C -DCOMPILER_ID=Generic -DMODE=<mode>

Where :code:`<mode>` is one of:

- :code:`EXIST` - check whether the library can be found,
- :code:`COMPILE` - print compilation flags,
- :code:`LINK` - print linking arguments.

.. note::

	If a custom installation prefix is used, you need to also pass :code:`-Danjay_DIR=$YOUR_INSTALL_PREFIX/lib/anjay`.

