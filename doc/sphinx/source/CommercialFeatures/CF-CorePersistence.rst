..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Core Persistence
================

.. contents:: :local:

General description
-------------------

LwM2M protocol, although designed to be as effective as possible in terms of
bandwidth and energy consumption, features mechanisms which might require
sending larger amounts of data during connection initialization and client
registration processes.

Such mechanisms include:

* **(D)TLS session handshake** - both parties have to agree upon which
  ciphersuite will be used, and potentially verify each other's certificates and
  exchange encryption keys,
* **LwM2M Register** operation, which contains parameters of the registration
  such as endpoint name, used LwM2M version, list of present objects, etc.,
* **LwM2M Observe** operation, which must be issued for every previously
  configured observation, since the server assumes that the client is not aware
  of them,
* **LwM2M Discover** operation, which some servers send immediately for every
  object manifested in Register operation. It's used to discover present object
  instances and resources.

In applications where the device is deactivated most of the time and
communicates with the server infrequently, going through the registration
process before every update is a large communication overhead. One can avoid
that by attempting to resume a session, but that requires keeping the library
alive, which means it is not allowed to disable the device completely, as
application memory (RAM) vanishes when it's powered down.

By using **Core Persistence** feature it is possible to persist core library
state to non-volatile storage, allowing a device to shut down completely and
once it's up and running again resume the session using state loaded from the
memory.

Example savings summary
^^^^^^^^^^^^^^^^^^^^^^^

To demonstrate how Core Persistence feature might save bandwidth in cases
described above, a benchmark of the example application implemented further in
this article was done. Table below summarizes how much data was exchanged with
AVSystem's Coiote DM LwM2M server during initial registration, and later, during
reconnection with successful resumption. The application was configured using
default settings, with 5 resources observed, all with a single attribute.

Initial connection, no resumption:

+------------------+-----------------------------------+---------------------------+
| Step             | Packets sent (inbound + outbound) | Sum of UDP packet lengths |
+==================+===================================+===========================+
| DTLS handshake   | 6                                 | 1058                      |
+------------------+-----------------------------------+---------------------------+
| LwM2M operations | 36                                | 4104                      |
| (Observe, Read,  |                                   |                           |
| Discover)        |                                   |                           |
+------------------+-----------------------------------+---------------------------+

Reconnection, successful resumption:

+------------------+-----------------------------------+---------------------------+
| Step             | Packets sent (inbound + outbound) | Sum of UDP packet lengths |
+==================+===================================+===========================+
| DTLS handshake   | 3                                 | 639                       |
+------------------+-----------------------------------+---------------------------+
| LwM2M operations | 0                                 | 0                         |
| (Observe, Read,  |                                   |                           |
| Discover)        |                                   |                           |
+------------------+-----------------------------------+---------------------------+

The difference is **4523 bytes (87.6 % reduction)**.

Technical documentation
-----------------------

Introduced APIs
^^^^^^^^^^^^^^^

If Core Persistence feature is available in your version of Anjay, relevant APIs
can be enabled either by defining ``ANJAY_WITH_CORE_PERSISTENCE`` in
``anjay_config.h`` or, if using CMake, enabling ``WITH_CORE_PERSISTENCE``
option.

Core Persistence feature introduces two functions:

* `anjay_new_from_core_persistence()
  <../api/core_8h.html#a6fc17768db5909343831fc04a1dbd8c3>`_, which instantiates
  Anjay using previously saved client state,
* `anjay_delete_with_core_persistence()
  <../api/core_8h.html#a1ad2e0995f6ba822c300ccab819e4526>`_, which deinitializes
  Anjay and attempts to save its state to given stream.

Usage example
^^^^^^^^^^^^^

.. note::

   The full code for the following example can be found in the
   ``examples/commercial-features/CF-CorePersistence`` directory in Anjay
   sources. Note that to compile and run it, you need to have access to a
   commercial version of Anjay that includes the Core Persistence feature.

As an example, we'll modify the code from the
:doc:`../AdvancedTopics/AT-Persistence` tutorial by adding core state
persistence capability to the application.

As core library's state will be saved and loaded in different order than
Anjay's pre-implemented objects (which are persisted with ``anjay_*_persist()``
functions), it is not possible to use a single stream for all purposes. Two
separate files will be used now.

.. important::

   Anjay, as described in :doc:`../AdvancedTopics/AT-Persistence` tutorial,
   uses generic `stream` (``avs_stream_t``) interface for storage operations.
   If this feature is used on a platform which doesn't support file operations
   with standard file API, one must implement that interface on their own, e.g.
   make use of microcontroller's flash memory. This interface may be either
   implemented from ground up, or in simple use cases, ``avs_stream_simple_*``
   methods can be used.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-CorePersistence/src/main.c

    #define OBJECT_PERSISTENCE_FILENAME "cf-object-persistence.dat"
    #define CORE_PERSISTENCE_FILENAME "cf-core-persistence.dat"

Next, we'll introduce two helper functions for initialization and
deinitialization of Anjay.

In case any core persistence data is available, we'll try to use that to
instantiate Anjay and possibly resume our connection to the server. If the
persistence file is not accessible or an attempt to use it is unsuccessful, we
should fall back to normal `anjay_new()
<../api/core_8h.html#a077b9b3db59c5b4539271e190508c520>`_ call.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-CorePersistence/src/main.c

    anjay_t *
    anjay_new_try_from_core_persistence(const anjay_configuration_t *config) {
        avs_log(tutorial, INFO,
                "Attempting to initialize Anjay from core persistence");

        avs_stream_t *file_stream =
                avs_stream_file_create(CORE_PERSISTENCE_FILENAME,
                                       AVS_STREAM_FILE_READ);

        anjay_t *result;
        if (!file_stream
                || !(result = anjay_new_from_core_persistence(config,
                                                              file_stream))) {
            result = anjay_new(config);
        }

        avs_stream_cleanup(&file_stream);
        // remove persistence file to prevent client from reading
        // outdated state in case it doesn't shut down gracefully
        unlink(CORE_PERSISTENCE_FILENAME);
        return result;
    }

Similarly, if core persistence file is not accessible due to some error, we
want to resort to default `anjay_delete()
<../api/core_8h.html#a243f18f976bca57b5a7b0714bfb99095>`_ call.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-CorePersistence/src/main.c

    int anjay_delete_try_with_core_persistence(anjay_t *anjay) {
        avs_log(tutorial, INFO,
                "Attempting to shut down Anjay and persist its state");

        avs_stream_t *file_stream =
                avs_stream_file_create(CORE_PERSISTENCE_FILENAME,
                                       AVS_STREAM_FILE_WRITE);
        if (file_stream) {
            int result = anjay_delete_with_core_persistence(anjay, file_stream);
            avs_stream_cleanup(&file_stream);
            if (result) {
                unlink(CORE_PERSISTENCE_FILENAME);
            }
            return result;
        } else {
            anjay_delete(anjay);
            return -1;
        }
    }

.. important::
   It's worth noting that Core Persistence feature doesn't maintain all of the
   information about observations - observation parameters are plain LwM2M
   attributes managed by attribute storage subsystem, thus its state should be
   persisted too. The relevant code was already implemented in
   :doc:`../AdvancedTopics/AT-Persistence` tutorial.

Since registration resumption is allowed in Anjay only in case when security
context is reused, we'll convert our example to use PSK security mode.

.. note::

   Technically speaking, `LwM2M TS: Transport Bindings` allows for registration
   resumption also for NoSec mode, in case the IP address of a client doesn't
   change. Anjay always assumes that the IP address has changed, as it's
   generally not possible to reliably determine whether the address visible to
   the server is still the same; it might be affected by e.g. how routing and
   Network Address Translation is configured between the parties.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-CorePersistence/src/main.c
   :emphasize-lines: 2-3, 8-12, 17

    void initialize_objects_with_default_settings(anjay_t *anjay) {
        static const char PSK_IDENTITY[] = "identity";
        static const char PSK_KEY[] = "P4s$w0rd";

        const anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
            .security_mode = ANJAY_SECURITY_PSK,
            .public_cert_or_psk_identity = (const uint8_t *) PSK_IDENTITY,
            .public_cert_or_psk_identity_size = strlen(PSK_IDENTITY),
            .private_cert_or_psk_key = (const uint8_t *) PSK_KEY,
            .private_cert_or_psk_key_size = strlen(PSK_KEY)
        };

        const anjay_server_instance_t server_instance = {
            .ssid = 1,
            .lifetime = 60,
            .default_min_period = -1,
            .default_max_period = -1,
            .disable_timeout = -1,
            .binding = "U"
        };

        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
        anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id);
        anjay_server_object_add_instance(anjay, &server_instance,
                                         &server_instance_id);
    }

.. important::

   Please note that the example application uses IPv4 and UDP protocol, thus
   the `lifetime` parameter is set to a relatively low value to prevent NAT
   entries in routers between the parties from expiring. After shutting the
   client down, the registration will expire quickly - practical implementations
   should update their `lifetime` parameter to some larger value before
   disconnecting, or use other Layer 3/Layer 4 protocol combination which
   doesn't require frequent communication with the server.

Let's use all the functions we have implemented above.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-CorePersistence/src/main.c
   :emphasize-lines: 15, 45-50

    int main(int argc, char *argv[]) {
        if (argc != 2) {
            avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
            return -1;
        }

        signal(SIGINT, signal_handler);

        const anjay_configuration_t CONFIG = {
            .endpoint_name = argv[1],
            .in_buffer_size = 4000,
            .out_buffer_size = 4000
        };

        g_anjay = anjay_new_try_from_core_persistence(&CONFIG);
        if (!g_anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        int result = -1;

        // Setup necessary objects
        if (anjay_security_object_install(g_anjay)
                || anjay_server_object_install(g_anjay)) {
            goto cleanup;
        }

        int restore_retval = restore_objects_if_possible(g_anjay);
        if (restore_retval < 0) {
            goto cleanup;
        } else if (restore_retval > 0) {
            initialize_objects_with_default_settings(g_anjay);
        }

        result = anjay_event_loop_run(g_anjay,
                                      avs_time_duration_from_scalar(1, AVS_TIME_S));

        int persist_result = persist_objects(g_anjay);
        if (!result) {
            result = persist_result;
        }

    cleanup:
        if (result) {
            anjay_delete(g_anjay);
        } else {
            result = anjay_delete_try_with_core_persistence(g_anjay);
        }
        return result;
    }

To see how this feature affects data sent during the connection, we encourage to
not only analyze application's log output, but use some packet analyzer software
like Wireshark.

.. important::

   Because Core Persistence feature maintains some data that depends on real
   time (time at which disabled servers should be reenabled, registration expiry
   time), this feature requires a properly implemented real clock
   (``avs_time_real_now()``,
   :ref:`see porting article <timeapi_avs_time_real_now>`) to work correctly.
   Also, make sure to synchronize it first (by using e.g. RTC or NTP protocol)
   before starting the client, otherwise disabled servers will be not reenabled
   as expected and the registration will be incorrectly deemed to be up to date.
   Alternatively, if it's not possible to synchronize the clock before, make
   sure to manually enable those disabled servers back by using
   ``anjay_enable_server()`` and schedule a registration update using
   ``anjay_schedule_registration_update()``.

