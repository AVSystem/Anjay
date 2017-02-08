Using Anjay with embedded UDP/IP stacks
=======================================

LwIP
----

Enabling LwIP support
^^^^^^^^^^^^^^^^^^^^^

When implementing an Anjay-powered LwM2M client, it is possible to use LwIP as a lightweight UDP/IP stack. In order to achieve that, pass ``-DWITH_LWIP=ON`` option as ``AVS_COMMONS_ADDITIONAL_CMAKE_OPTIONS`` when configuring Anjay with CMake, like so:

.. code-block:: cmake

   cmake -DAVS_COMMONS_ADDITIONAL_CMAKE_OPTIONS="-DWITH_LWIP=ON" .


See also: :doc:`Compiling_client_applications`.


Required LwIP compile-time options
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you have Anjay libraries compiled with LwIP support, the only other thing you need to do is make sure ``lwipopts.h`` defines following macros:

.. code-block:: c

    // Enable UDP support
    #define LWIP_UDP 1

    // Provide POSIX socket API, used by Anjay
    #define LWIP_SOCKET 1

    // Disable macros redefining socket operations to "lwip_" prefixed variants
    #define LWIP_COMPAT_SOCKETS 0

