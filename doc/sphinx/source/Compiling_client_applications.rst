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

Compiling client applications
=============================

Compiling the library
---------------------

.. important::

    If you cloned Anjay from a Git repository, ensure that you updated
    submodules by calling ``git submodule update --init`` before continuing.

Anjay uses CMake for project configuration. To compile the library with default
settings, call the following command in Anjay root directory:

.. code-block:: bash

    cmake . && make


Cross-compiling
---------------

.. note::

    Cross-compilation is necessary only if you are compiling the library to use
    on a different system, than the one used for compilation. If you, for
    example, want to use Anjay on Raspberry Pi, then you can perform
    compilation on Raspberry Pi as described above or cross-compilation on your
    PC.

ARM Cortex-M3-powered STM3220
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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


Android
~~~~~~~

Compilation on Android platform is rather straightforward. First you have to get `Android NDK
<https://developer.android.com/ndk/index.html>`_. To configure Anjay you
have to pass ``CMAKE_TOOLCHAIN_FILE`` from the NDK (we assume that
``ANDROID_NDK`` variable contains a path to the folder where Android NDK
is extracted):

.. code-block:: bash

    cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
          -DDTLS_BACKEND="" \
          -DANDROID_ALLOW_UNDEFINED_SYMBOLS=ON \
          -DANDROID_PLATFORM=android-18 \
          -DANDROID_ABI=armeabi .

After that Anjay can be compiled as usual via `make`.

.. note::

    Android platforms older than `android-18` are not supported.


.. note::

    ``ANDROID_ALLOW_UNDEFINED_SYMBOLS`` is set, so that unresolved symbols
    required by the `libanjay.so` are not reported during the linking
    stage. They shall be resolved by providing dependencies to the final
    executable as it is illustrated in the next section.

Note that we did not set any ``DTLS_BACKEND`` and therefore Anjay is compiled
without DTLS support. To enable DTLS support you have to provide a value
to ``DTLS_BACKEND`` (see `README.md <https://github.com/AVSystem/Anjay>`_
for more details) along with specific variable indicating where the required
DTLS libraries are to be found, i.e. one of:

    - ``OPENSSL_ROOT_DIR`` (as `FindOpenSSL.cmake` suggests),
    - ``MBEDTLS_ROOT_DIR``,
    - ``TINYDTLS_ROOT_DIR``

depending on the chosen backend.

.. topic:: Example compilation with mbed TLS backend

    First, we compile mbed TLS on Android:

    .. code-block:: bash

        $ git clone https://github.com/ARMmbed/mbedtls -b mbedtls-2.5.0
        $ cd mbedtls
        $ cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
                -DANDROID_PLATFORM=android-18 \
                -DANDROID_ABI=armeabi \
                -DENABLE_TESTING=OFF \
                -DCMAKE_INSTALL_PREFIX=/tmp/mbedtls/install .
        $ make
        $ make install

    We then go back to the Anjay source directory, to reconfigure Anjay to use
    mbed TLS binaries (we strongly suggest to clean all kind of CMake caches
    before proceeding, as it may not work otherwise):

    .. code-block:: bash

        cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
              -DDTLS_BACKEND="mbedtls" \
              -DMBEDTLS_ROOT_DIR=/tmp/mbedtls/install \
              -DANDROID_ALLOW_UNDEFINED_SYMBOLS=ON \
              -DANDROID_PLATFORM=android-18 \
              -DANDROID_ABI=armeabi .

    And finally, we run `make`, finishing the whole procedure.


Installing the library
----------------------

Building with CMake
~~~~~~~~~~~~~~~~~~~

The preferred way of building Anjay is to use CMake.

To install Anjay headers and libraries in :code:`/usr/local`:

.. code-block:: bash

    cmake . && make && sudo make install

A custom installation prefix may be set using :code:`CMAKE_INSTALL_PREFIX`:

.. code-block:: bash

    cmake -DCMAKE_INSTALL_PREFIX=/custom/path . && make && make install

.. _no-cmake:

Alternative build systems
~~~~~~~~~~~~~~~~~~~~~~~~~

Alternatively, you may use any other build system. You will need to:

* Prepare your ``avs_commons_config.h``, ``avs_coap_config.h`` and ``anjay_config.h`` files.

  * Comments in `avs_commons_config.h.in
    <https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_commons_config.h.in>`_,
    `avs_coap_config.h.in <https://github.com/AVSystem/Anjay/blob/master/deps/avs_coap/include_public/avsystem/coap/avs_coap_config.h.in>`_
    and `anjay_config.h.in <https://github.com/AVSystem/Anjay/blob/master/include_public/anjay/anjay_config.h.in>`_
    will guide you about the meaning of various settings.
  * You may use one of the directories from `example_configs
    <https://github.com/AVSystem/Anjay/blob/master/example_configs>`_ as a starting point. See
    `README.md inside that directory
    <https://github.com/AVSystem/Anjay/blob/master/example_configs/README.md>`_ for details. You may
    even set one of the subdirectories there are as an include path directly in your compiler if you
    do not need any customizations.
* Configure your build system so that:

  * At least all ``*.c`` and ``*.h`` files from ``src``, ``include_public``, ``deps/avs_coap/src``,
    ``deps/avs_coap/include_public``, ``deps/avs_commons/src`` and
    ``deps/avs_commons/include_public`` directories are preserved, with the directory structure
    intact.

    * It is also safe to merge contents of all ``include_public`` directories into one. Merging
      ``src`` directories should be safe, too, but is not explicitly supported.
  * All ``*.c`` files inside ``src``, ``deps/avs_coap/src``, ``deps/avs_commons/src``, or any of
    their direct or indirect subdirectories are compiled.
  * ``deps/avs_commons/src`` and ``deps/avs_commons/include_public`` directories are included in the
    header search path when compiling ``avs_commons``.
  * ``deps/avs_coap/src``, ``deps/avs_coap/include_public`` and ``deps/avs_commons/include_public``
    directories are included in the header search path when compiling ``avs_coap``.
  * ``src``, ``include_public``, ``deps/avs_coap/include_public`` and
    ``deps/avs_commons/include_public`` directories are included in the header search path when
    compiling Anjay.
  * ``include_public``, ``deps/avs_coap/include_public`` and ``deps/avs_commons/include_public``
    directories, or copies of them (possibly merged into one directory) are included in the header
    search path when compiling dependent application code.

.. rubric:: Example

Below is an example of a simplistic build process, that builds all of avs_commons, avs_coap and
Anjay from a Unix-like shell:

.. code-block:: bash

    # configuration
    cp -r example_configs/linux_lwm2m10 config
    # you may want to edit the files in the "config" directory before continuing

    # compilation
    cc -Iconfig -Iinclude_public -Ideps/avs_coap/include_public -Ideps/avs_commons/include_public -Isrc -Ideps/avs_coap/src -Ideps/avs_commons/src -c $(find src deps/avs_coap/src deps/avs_commons/src -name '*.c')
    ar rcs libanjay.a *.o

    # installation
    cp libanjay.a /usr/local/lib/
    cp -r include_public/avsystem /usr/local/include/
    cp -r deps/avs_coap/include_public/avsystem /usr/local/include/
    cp -r deps/avs_commons/include_public/avsystem /usr/local/include/
    cp -r config/* /usr/local/include/


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

If Anjay itself has been compiled using CMake, flags necessary for other build systems can be
retrieved using :code:`cmake` command:

.. code-block:: bash

    cmake --find-package -DNAME=anjay -DLANGUAGE=C -DCOMPILER_ID=Generic -DMODE=<mode>

Where :code:`<mode>` is one of:

- :code:`EXIST` - check whether the library can be found,
- :code:`COMPILE` - print compilation flags,
- :code:`LINK` - print linking arguments.

.. note::

	If a custom installation prefix is used, you need to also pass :code:`-Danjay_DIR=$YOUR_INSTALL_PREFIX/lib/anjay`.



Anjay compiled without CMake
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If Anjay has been compiled without using CMake, you will need to provide necessary flags manually.

Specific dependencies will vary according to:

* compile-time configuration, including:

  * avs_compat_threading backend
  * avs_crypto backend, if any
  * avs_net DTLS backend, if any
  * ``AVS_COMMONS_HTTP_WITH_ZLIB`` setting, if avs_http is enabled
* target platform
* build environment

.. rubric:: Example

For the following conditions:

* Anjay compiled with all optional features enabled, and:

  * mbed TLS security enabled as avs_net DTLS backend and/or avs_crypto backend
  * PThread used as avs_compat_threading backend
  * avs_http enabled with zlib support
* Target platform being a typical desktop GNU/Linux distribution
* GCC or Clang used as the compiler
* Anjay compiled and installed as shown in the example in the :ref:`no-cmake` section

the flags necessary to link client applications would be:

.. code-block:: bash

    -lanjay -lz -lmbedtls -lmbedcrypto -lmbedx509 -lm -pthread
