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

Basic implementation
====================

Project structure
^^^^^^^^^^^^^^^^^

We shall start with the code from :doc:`../BasicClient/BC6` chapter. In the
end, our project structure would look as follows:

.. code::

    examples/tutorial/firmware-update/basic-implementation/
    ├── CMakeLists.txt
    └── src
        ├── firmware_update.c
        ├── firmware_update.h
        ├── main.c
        ├── time_object.c
        └── time_object.h


Note the ``firmware_update.c`` and ``firmware_update.h`` are introduced in this
chapter.

Installing the Firmware Update module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In order to install the module, we are going to use:

.. highlight:: c
.. snippet-source:: include_public/anjay/fw_update.h

    int anjay_fw_update_install(
            anjay_t *anjay,
            const anjay_fw_update_handlers_t *handlers,
            void *user_arg,
            const anjay_fw_update_initial_state_t *initial_state);

The important arguments for us at this point are ``anjay``, ``handlers``
and ``user_arg``.

We already discussed ``handlers`` structure in :ref:`firmware-update-api`, and we will
shortly provide simple implementations of required callbacks.

.. note::

    The ``user_arg`` **is a pointer passed to every callback, when Anjay
    actually calls it**. This pointer can be used by the callback implementation
    to store any kind of context to operate on. Alternatively, the implementation
    may set it to `NULL` and rely on the use of global variables.

In our code, firmware update module installation will be taken care of by
the function declared in ``firmware_update.h``:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/basic-implementation/src/firmware_update.h
    :emphasize-lines: 12-17

    #ifndef FIRMWARE_UPDATE_H
    #define FIRMWARE_UPDATE_H
    #include <anjay/anjay.h>
    #include <anjay/fw_update.h>

    /**
     * Buffer for the endpoint name that will be used when re-launching the client
     * after firmware upgrade.
     */
    extern const char *ENDPOINT_NAME;

    /**
     * Installs the firmware update module.
     *
     * @returns 0 on success, negative value otherwise.
     */
    int fw_update_install(anjay_t *anjay);

    #endif // FIRMWARE_UPDATE_H

We invoke it in ``main.c`` by performing two (highlighted) modifications:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/basic-implementation/src/main.c
    :emphasize-lines: 9, 146

    #include <anjay/anjay.h>
    #include <anjay/attr_storage.h>
    #include <anjay/security.h>
    #include <anjay/server.h>
    #include <avsystem/commons/avs_log.h>

    #include <poll.h>

    #include "firmware_update.h"
    #include "time_object.h"

    int main_loop(anjay_t *anjay, const anjay_dm_object_def_t **time_object) {
        while (true) {
            // Obtain all network data sources
            AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(anjay);

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

            // Notify the library about a Resource value change
            time_object_notify(anjay, time_object);

            // Finally run the scheduler
            anjay_sched_run(anjay);
        }
        return 0;
    }

    // Installs Security Object and adds and instance of it.
    // An instance of Security Object provides information needed to connect to
    // LwM2M server.
    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        static const char PSK_IDENTITY[] = "identity";
        static const char PSK_KEY[] = "P4s$w0rd";

        anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "coaps://try-anjay.avsystem.com:5684",
            .security_mode = ANJAY_SECURITY_PSK,
            .public_cert_or_psk_identity = (const uint8_t *) PSK_IDENTITY,
            .public_cert_or_psk_identity_size = strlen(PSK_IDENTITY),
            .private_cert_or_psk_key = (const uint8_t *) PSK_KEY,
            .private_cert_or_psk_key_size = strlen(PSK_KEY)
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                               &security_instance_id)) {
            return -1;
        }

        return 0;
    }

    // Installs Server Object and adds and instance of it.
    // An instance of Server Object provides the data related to a LwM2M Server.
    static int setup_server_object(anjay_t *anjay) {
        if (anjay_server_object_install(anjay)) {
            return -1;
        }

        const anjay_server_instance_t server_instance = {
            // Server Short ID
            .ssid = 1,
            // Client will send Update message often than every 60 seconds
            .lifetime = 60,
            // Disable Default Minimum Period resource
            .default_min_period = -1,
            // Disable Default Maximum Period resource
            .default_max_period = -1,
            // Disable Disable Timeout resource
            .disable_timeout = -1,
            // Sets preferred transport to UDP
            .binding = "U"
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
        if (anjay_server_object_add_instance(anjay, &server_instance,
                                             &server_instance_id)) {
            return -1;
        }

        return 0;
    }

    int main(int argc, char *argv[]) {
        if (argc != 2) {
            avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME", argv[0]);
            return -1;
        }

        ENDPOINT_NAME = argv[1];

        const anjay_configuration_t CONFIG = {
            .endpoint_name = ENDPOINT_NAME,
            .in_buffer_size = 4000,
            .out_buffer_size = 4000,
            .msg_cache_size = 4000
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        int result = 0;
        // Install Attribute storage and setup necessary objects
        if (anjay_attr_storage_install(anjay) || setup_security_object(anjay)
                || setup_server_object(anjay) || fw_update_install(anjay)) {
            result = -1;
        }

        const anjay_dm_object_def_t **time_object = NULL;
        if (!result) {
            time_object = time_object_create();
            if (time_object) {
                result = anjay_register_object(anjay, time_object);
            } else {
                result = -1;
            }
        }

        if (!result) {
            result = main_loop(anjay, time_object);
        }

        anjay_delete(anjay);
        time_object_release(time_object);
        return result;
    }

.. note::

    As you may see, there is also an additional ``ENDPOINT_NAME`` global
    variable that now stores the command line argument. As we will use the same
    kind of program binary as the update image, we will need this to properly
    launch it as part of the upgrade process.

    This is usually not necessary in production code, as the endpoint name is
    usually either hard-coded, or configured through other means.

Implementing handlers and installation routine
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

First, let's think about what would we need to implement I/O operations
required to download the firmware. The approach we could take is to
open a ``FILE`` during a call to the ``stream_open`` callback, write to
it in ``stream_write``, and close it in ``stream_finish``. The only detail
remaining is: how are we going to share ``FILE *`` pointer between all
of these?

We can use a globally allocated structure and pack entire shared state into
it. In ``firmware_update.c`` it looks like this:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/basic-implementation/src/firmware_update.c
    :emphasize-lines: 9

    #include "./firmware_update.h"

    #include <assert.h>
    #include <errno.h>
    #include <stdio.h>
    #include <sys/stat.h>
    #include <unistd.h>

    static struct fw_state_t { FILE *firmware_file; } FW_STATE;

.. note::

    The numerous headers included will be useful in further stages of the
    development.

.. _fw-download-io:

Having the global state structure, we can proceed with implementation of:
``fw_stream_open``, ``fw_stream_write`` and ``fw_stream_finish``, keeping in mind
our brief discussion at the beginning of the section:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/basic-implementation/src/firmware_update.c
    :emphasize-lines: 3-

    static struct fw_state_t { FILE *firmware_file; } FW_STATE;

    static const char *FW_IMAGE_DOWNLOAD_NAME = "/tmp/firmware_image.bin";

    static int fw_stream_open(void *user_ptr,
                              const char *package_uri,
                              const struct anjay_etag *package_etag) {
        // For a moment, we don't need to care about any of the arguments passed.
        (void) user_ptr;
        (void) package_uri;
        (void) package_etag;

        // It's worth ensuring we start with a NULL firmware_file. In the end
        // it would be our responsibility to manage this pointer, and we want
        // to make sure we never leak any memory.
        assert(FW_STATE.firmware_file == NULL);
        // We're about to create a firmware file for writing
        FW_STATE.firmware_file = fopen(FW_IMAGE_DOWNLOAD_NAME, "wb");
        if (!FW_STATE.firmware_file) {
            fprintf(stderr, "Could not open %s\n", FW_IMAGE_DOWNLOAD_NAME);
            return -1;
        }
        // We've succeeded
        return 0;
    }

    static int fw_stream_write(void *user_ptr, const void *data, size_t length) {
        (void) user_ptr;
        // We only need to write to file and check if that succeeded
        if (fwrite(data, length, 1, FW_STATE.firmware_file) != 1) {
            fprintf(stderr, "Writing to firmware image failed\n");
            return -1;
        }
        return 0;
    }

    static int fw_stream_finish(void *user_ptr) {
        (void) user_ptr;
        assert(FW_STATE.firmware_file != NULL);

        if (fclose(FW_STATE.firmware_file)) {
            fprintf(stderr, "Closing firmware image failed\n");
            FW_STATE.firmware_file = NULL;
            return -1;
        }
        FW_STATE.firmware_file = NULL;
        return 0;
    }

Next in queue is ``fw_reset``, which is called when something on the Client or
the Server side goes wrong, or if the Server decides to not perform firmware
update. We can implement it as follows:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/basic-implementation/src/firmware_update.c

    static void fw_reset(void *user_ptr) {
        // Reset can be issued even if the download never started.
        if (FW_STATE.firmware_file) {
            // We ignore the result code of fclose(), as fw_reset() can't fail.
            (void) fclose(FW_STATE.firmware_file);
            // and reset our global state to initial value.
            FW_STATE.firmware_file = NULL;
        }
        // Finally, let's remove any downloaded payload
        unlink(FW_IMAGE_DOWNLOAD_NAME);
    }

And finally, ``fw_perform_upgrade`` as well as ``fw_update_install`` are to
be implemented. However, up to this point, we did not specify what would
the format of a downloaded image be, nor how would it be applied.

In our simplified example, we can require from the image to be an executable,
and then in ``fw_perform_upgrade`` we could be using ``execl()`` to start a
new (downloaded) version of our Client.

.. note::

    In a more realistic scenario, one would be doing things such as:

        - firmware verification,
        - saving it to some persistent storage (e.g. flash), rather than to ``/tmp``,
        - other platform specific stuff.

The other important thing to consider is this: how's the newly running
client going to know it was upgraded? After all, it would be nice if the
Client could report this information to the Server for it to know the update
actually succeeded.

The simplest solution here is to use a "marker" file, indicating the client successfully
upgraded. Specifically, the idea is as follows:

    - just before performing the upgrade, a "marker" file is created,
    - the logic in the Client can check for the existence of the "marker" and conclude,
      if the upgrade was performed or not,
    - finally, the "marker" gets removed.

The code is self explanatory:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/basic-implementation/src/firmware_update.c

    // A part of a rather simple logic checking if the firmware update was
    // successfully performed.
    static const char *FW_UPDATED_MARKER = "/tmp/fw-updated-marker";

    static int fw_perform_upgrade(void *user_ptr) {
        if (chmod(FW_IMAGE_DOWNLOAD_NAME, 0700) == -1) {
            fprintf(stderr,
                    "Could not make firmware executable: %s\n",
                    strerror(errno));
            return -1;
        }
        // Create a marker file, so that the new process knows it is the "upgraded"
        // one
        FILE *marker = fopen(FW_UPDATED_MARKER, "w");
        if (!marker) {
            fprintf(stderr, "Marker file could not be created\n");
            return -1;
        }
        fclose(marker);

        assert(ENDPOINT_NAME);
        // If the call below succeeds, the firmware is considered as "upgraded",
        // and we hope the newly started client registers to the Server.
        (void) execl(FW_IMAGE_DOWNLOAD_NAME, FW_IMAGE_DOWNLOAD_NAME, ENDPOINT_NAME,
                     NULL);
        fprintf(stderr, "execl() failed: %s\n", strerror(errno));
        // If we are here, it means execl() failed. Marker file MUST now be removed,
        // as the firmware update failed.
        unlink(FW_UPDATED_MARKER);
        return -1;
    }

    static const anjay_fw_update_handlers_t HANDLERS = {
        .stream_open = fw_stream_open,
        .stream_write = fw_stream_write,
        .stream_finish = fw_stream_finish,
        .reset = fw_reset,
        .perform_upgrade = fw_perform_upgrade
    };

    const char *ENDPOINT_NAME = NULL;

    int fw_update_install(anjay_t *anjay) {
        anjay_fw_update_initial_state_t state;
        memset(&state, 0, sizeof(state));

        if (access(FW_UPDATED_MARKER, F_OK) != -1) {
            // marker file exists, it means firmware update succeded!
            state.result = ANJAY_FW_UPDATE_INITIAL_SUCCESS;
            unlink(FW_UPDATED_MARKER);
        }
        // install the module, pass handlers that we implemented and initial state
        // that we discovered upon startup
        return anjay_fw_update_install(anjay, &HANDLERS, NULL, &state);
    }
