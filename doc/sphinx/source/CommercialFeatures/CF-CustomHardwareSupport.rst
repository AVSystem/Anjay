..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Custom Hardware Support
=======================

Anjay can be used on several different platforms. By default, the library
targets POSIX-like operating systems and their standard interfaces for
networking, multithreading, etc., which means that Anjay can be easily compiled
to run on many OSs, including:

* Linux,
* macOS,
* FreeBSD,
* Windows (compiled using `MSYS2
  <https://github.com/AVSystem/Anjay/blob/master/README.Windows.md>`_).

For embedded platforms, AVSystem provides a couple of integration layers and
example applications for many popular SDKs and prototyping kits:

.. list-table::
   :header-rows: 1

   * - Target platform/SDK
     - Integration layer
     - Example application
   * - `Zephyr <https://zephyrproject.org/>`_ and `nRF Connect SDK
       <https://www.nordicsemi.com/Products/Development-software/nrf-connect-sdk>`_
     - `Anjay-zephyr <https://github.com/AVSystem/Anjay-zephyr>`_
     - `Anjay-zephyr-client <https://github.com/AVSystem/Anjay-zephyr-client>`_
   * - `mbedOS <https://os.mbed.com/mbed-os/>`_
     - `Anjay-mbedos <https://github.com/AVSystem/Anjay-mbedos>`_
     - `Anjay-mbedos-client <https://github.com/AVSystem/Anjay-mbedos-client/>`_
   * - `STM32Cube
       <https://www.st.com/content/st_com/en/products/ecosystems/stm32-open-development-environment/stm32cube.html>`_
       w/ `FreeRTOS <https://www.freertos.org/>`_ and `X-CUBE-CELLULAR
       <https://www.st.com/en/embedded-software/x-cube-cellular.html>`_
     - *contained in the app*
     - `Anjay-freertos-client
       <https://github.com/AVSystem/Anjay-freertos-client>`_
   * - `STM32Cube
       <https://www.st.com/content/st_com/en/products/ecosystems/stm32-open-development-environment/stm32cube.html>`_
       w/ `Azure RTOS <https://threadx.io/>`_ and `X-CUBE-CELLULAR
       <https://www.st.com/en/embedded-software/x-cube-cellular.html>`_
     - *contained in the app*
     - `Anjay-stm32-azurertos-client
       <https://github.com/AVSystem/Anjay-stm32-azurertos-client>`_
   * - `ESP-IDF <https://github.com/espressif/esp-idf>`_
     - `Anjay-esp-idf <https://github.com/AVSystem/Anjay-esp-idf>`_
     - `Anjay-esp32-client <https://github.com/AVSystem/Anjay-esp32-client>`_
   * - `Raspberry Pi Pico SDK <https://github.com/raspberrypi/pico-sdk>`_
     - *contained in the app*
     - `Anjay-pico-client <https://github.com/AVSystem/Anjay-pico-client>`_

If the desired platform isn't listed above, it means that custom implementation for
time, threading, networking and (D)TLS APIs **must be provided** as described in
:doc:`../PortingGuideForNonPOSIXPlatforms`. Integrating with networking
peripherals and APIs (e.g. cellular modems) is often **non-trivial**, thus
AVSystem is open to providing help with developing code tailored for a specific
platform. In such case, please visit our `contact page
<https://www.avsystem.com/contact/>`_.

Examples of application aspects that need custom integration with a software platform:

 * Device Object that gathers various hardware and software parameters,
 * FOTA integration with platform NVM memory (Flash, EEPROM) drivers and bootloader,
 * Connectivity-related LwM2M Objects implementations that rely on data
   exchanged with the modem (e.g. Connectivity Monitoring or APN Connection Profile),
 * GPS Location data acquired by GNSS modem,
 * Factory provisioning with PC-based tools that communicate with the device via
   serial UART port and pre-configure the device with connectivity credentials,
 * Security based on a Hardware Security Module that's not PKCS11 or PSA standards
   compliant (see: :doc:`HSM Commercial Feature <CF-HSM>`).

During the development process, AVSystem can also include your custom hardware in
CI/CD pipeline to ensure proper performance, configuration and interoperability.
Such continuous integration testing is an extension of Custom Hardware Support
included in `Anjay Support Packages <https://avsystem.com/anjay-iot-sdk/features/>`_.

We can also assist with making many **improvements and optimizations**, even if
the basic functionality works out of the box. For example, Anjay-zephyr contains
code specific to the nRF9160 SoC which is capable of encrypting network traffic
directly on the modem instead of using an additional (D)TLS library, which normally
is a part of the application. Thanks to that we were able to completely remove
mbedTLS library, saving 80 kB of flash memory, and use nRF9160's ability to
cache (D)TLS sessions over power cycles, greatly reducing the overhead of secure
connections.
