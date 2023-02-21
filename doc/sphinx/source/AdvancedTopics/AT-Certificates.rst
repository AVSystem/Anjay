..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

DTLS connection using certificates
==================================

In :doc:`/BasicClient/BC-Security` section you learned how to use PSK to enable
secure connection in Anjay using DTLS. In this section, we will show how to use
certificates instead of PSK.

Preparing a LwM2M Client written using Anjay to use X.509 certificates requires
essentially the same steps as using the PSK mode. However, it is very likely
that you would like to load the certificates from files.

.. note::

   The full code for the following example can be found in the
   ``examples/tutorial/AT-Certificates`` directory in Anjay sources.

All actual parsing is performed by the TLS backend library, so it is enough to
just load contents of certificate files in DER format into memory. The code from
listing below is based on :doc:`/BasicClient/BC-MandatoryObjects` example and
highlights the modified parts.

.. highlight:: c
.. snippet-source:: examples/tutorial/AT-Certificates/src/main.c
    :emphasize-lines: 6-39,52,55-84

    #include <anjay/anjay.h>
    #include <anjay/security.h>
    #include <anjay/server.h>
    #include <avsystem/commons/avs_log.h>

    #include <string.h>

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
            .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
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
        // Setup necessary objects
        if (setup_security_object(anjay) || setup_server_object(anjay)) {
            result = -1;
        }

        if (!result) {
            result = anjay_event_loop_run(
                    anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
        }

        anjay_delete(anjay);
        return result;
    }

.. note::

    ``anjay_security_object_add_instance()`` copies the buffers present in the
    ``anjay_security_instance_t`` structure into the internal state of the
    ``security`` module, so it is safe to release the memory allocated by the
    file loading routine.
