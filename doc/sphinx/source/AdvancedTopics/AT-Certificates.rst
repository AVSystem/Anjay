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

DTLS connection using certificates
==================================

In :doc:`/BasicClient/BC4` section you learned how to use PSK to enable secure
connection in Anjay using DTLS. In this section, we will show how to use
certificates instead of PSK.

Preparing a LwM2M Client written using Anjay to use X.509 certificates requires
essentially the same steps as using the PSK mode. However, it is very likely
that you would like to load the certificates from files.

.. note::

   The full code for the following example can be found in the
   ``examples/tutorial/AT-Certificates`` directory in Anjay sources.

All actual parsing is performed by the TLS backend library, so it is enough to
just load contents of certificate files in DER format into memory. The code from
listing below is based on :doc:`/BasicClient/BC3` example and highlights the
modified parts.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-Certificates/src/main.c
    :emphasize-lines: 8,54-85,98,101-130

    #include <anjay/anjay.h>
    #include <anjay/attr_storage.h>
    #include <anjay/security.h>
    #include <anjay/server.h>
    #include <avsystem/commons/avs_log.h>

    #include <poll.h>
    #include <string.h>

    static int main_loop(anjay_t *anjay) {
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

            // Finally run the scheduler
            anjay_sched_run(anjay);
        }
        return 0;
    }

    static int
    load_buffer_from_file(uint8_t **out, size_t *out_size, const char *filename) {
        FILE *f = fopen(filename, "rb");
        if (!f) {
            avs_log(tutorial, ERROR, "could not open %s", filename);
            return -1;
        }
        int result = -1;
        if (fseek(f, 0, SEEK_END)) {
            goto finish;
        }
        long size = ftell(f);
        if (size < 0 || (unsigned long) size > SIZE_MAX || fseek(f, 0, SEEK_SET)) {
            goto finish;
        }
        *out_size = (size_t) size;
        if (!(*out = (uint8_t *) avs_malloc(*out_size))) {
            goto finish;
        }
        if (fread(*out, *out_size, 1, f) != 1) {
            avs_free(*out);
            *out = NULL;
            goto finish;
        }
        result = 0;
    finish:
        fclose(f);
        if (result) {
            avs_log(tutorial, ERROR, "could not read %s", filename);
        }
        return result;
    }

    // Installs Security Object and adds and instance of it.
    // An instance of Security Object provides information needed to connect to
    // LwM2M server.
    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "coaps://try-anjay.avsystem.com:5684",
            .security_mode = ANJAY_SECURITY_CERTIFICATE
        };

        int result = 0;

        if (load_buffer_from_file(
                    (uint8_t **) &security_instance.public_cert_or_psk_identity,
                    &security_instance.public_cert_or_psk_identity_size,
                    "client_cert.der")
                || load_buffer_from_file(
                           (uint8_t **) &security_instance.private_cert_or_psk_key,
                           &security_instance.private_cert_or_psk_key_size,
                           "client_key.der")
                || load_buffer_from_file(
                           (uint8_t **) &security_instance.server_public_key,
                           &security_instance.server_public_key_size,
                           "server_cert.der")) {
            result = -1;
            goto cleanup;
        }

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                               &security_instance_id)) {
            result = -1;
        }

    cleanup:
        avs_free((uint8_t *) security_instance.public_cert_or_psk_identity);
        avs_free((uint8_t *) security_instance.private_cert_or_psk_key);
        avs_free((uint8_t *) security_instance.server_public_key);

        return result;
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

        int result = 0;
        // Install Attribute storage and setup necessary objects
        if (anjay_attr_storage_install(anjay) || setup_security_object(anjay)
                || setup_server_object(anjay)) {
            result = -1;
        }

        if (!result) {
            result = main_loop(anjay);
        }

        anjay_delete(anjay);
        return result;
    }

.. note::

    ``anjay_security_object_add_instance()`` copies the buffers present in the
    ``anjay_security_instance_t`` structure into the internal state of the
    ``security`` module, so it is safe to release the memory allocated by the
    file loading routine.
