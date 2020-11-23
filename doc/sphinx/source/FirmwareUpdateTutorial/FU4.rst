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

Secure downloads
================

Introduction
^^^^^^^^^^^^

Up until now, we developed a basic client application that's capable of
downloading firmware in **PUSH** and **PULL** modes. If the Server connection
is secure, then of course any **PUSH** transfer is automatically secured. In
case of **PULL** mode, however, there remains a question about the source
of credentials required to establishing the secure connection.

In this chapter, we will focus on methods of credentials configuration for
**PULL** mode transfers.

Two ways of security configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When a secure firmware transfer is initiated, firmware update module implemented
in Anjay gets security configuration in one of two following ways.

#. If the user implements a ``get_security_config`` callback located in
   ``anjay_fw_update_handlers_t``, it is expected to provide the library with
   security information.
#. Otherwise, the library looks through Security Object instances, matching
   the download URI host with configured URI in Security Object Instance. When
   the matching Instance is found, the security information from that instance
   is used. When no match is found, the download fails.

.. note::

    It may seem as the described methods are mutually
    exclusive. However, in ``get_security_config`` callback one can use
    ``anjay_security_config_from_dm()`` to attempt URI matching with entities
    already configured in Security Object Instances.

Supported security modes
^^^^^^^^^^^^^^^^^^^^^^^^

The download can be secured by either `PSK
<https://en.wikipedia.org/wiki/Pre-shared_key>`_, or by `Public key
certificates <https://en.wikipedia.org/wiki/Public_key_certificate>`_.

Security information is configured in Anjay through a structure:

.. highlight:: c
.. snippet-source:: include_public/anjay/core.h

    typedef struct {
        /**
         * DTLS keys or certificates.
         */
        avs_net_security_info_t security_info;

        /**
         * Single DANE TLSA record to use for certificate verification, if
         * applicable.
         */
        const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record;

        /**
         * TLS ciphersuites to use.
         *
         * A value with <c>num_ids == 0</c> (default) will cause defaults configured
         * through <c>anjay_configuration_t::default_tls_ciphersuites</c>
         * to be used.
         */
        avs_net_socket_tls_ciphersuites_t tls_ciphersuites;
    } anjay_security_config_t;

And specifically, it's the ``security_info`` field that is of interest to
us. ``avs_net_security_info_t`` can be configured by:

- ``avs_net_security_info_from_psk()``,
- ``avs_net_security_info_from_certificates()``.

We will now have a closer look at both of these methods.

Configuration of PSK
""""""""""""""""""""

This is the most straightforward. The structure representing the PSK
configuration is:

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

    /**
     * A PSK/identity pair with borrowed pointers. avs_commons will never attempt
     * to modify these values.
     */
    typedef struct {
        const void *psk;
        size_t psk_size;
        const void *identity;
        size_t identity_size;
    } avs_net_psk_info_t;

After we correctly populated it, we may use:

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

    avs_net_security_info_t avs_net_security_info_from_psk(avs_net_psk_info_t psk);

to convert ``avs_net_psk_info_t`` into ``avs_net_security_info_t``, as in
the following example:

.. code-block:: c

    const avs_net_psk_info_t psk_info = {
        .psk = "shared-key",
        .psk_size = strlen("shared-key"),
        .identity = "our-identity",
        .identity_size = strlen("our-identity")
    };
    avs_net_security_info_t psk_security =
        avs_net_security_info_from_psk(psk_info);


Configuration of Certificates
"""""""""""""""""""""""""""""

That's a bit more involving. The structure representing Certificate configuration
is:

.. highlight:: c
.. snippet-source:: deps/avs_commons/include_public/avsystem/commons/avs_socket.h

    /**
     * Configuration for certificate-mode (D)TLS connection.
     */
    typedef struct {
        /**
         * Enables validation of peer certificate chain. If disabled,
         * #ignore_system_trust_store and #trusted_certs are ignored.
         */
        bool server_cert_validation;

        /**
         * Setting this flag to true disables the usage of system-wide trust store
         * (e.g. <c>/etc/ssl/certs</c> on most Unix-like systems).
         *
         * NOTE: System-wide trust store is currently supported only by the OpenSSL
         * backend. This field is ignored by the Mbed TLS backend.
         */
        bool ignore_system_trust_store;

        /**
         * Enable use of DNS-based Authentication of Named Entities (DANE) if
         * possible.
         *
         * If this field is set to true, but #server_cert_validation is disabled,
         * "opportunistic DANE" is used.
         */
        bool dane;

        /**
         * Store of trust anchor certificates. This field is optional and can be
         * left zero-initialized. If used, it shall be initialized using one of the
         * <c>avs_crypto_certificate_chain_info_from_*</c> helper functions.
         */
        avs_crypto_certificate_chain_info_t trusted_certs;

        /**
         * Store of certificate revocation lists. This field is optional and can be
         * left zero-initialized. If used, it shall be initialized using one of the
         * <c>avs_crypto_cert_revocation_list_info_from_*</c> helper functions.
         */
        avs_crypto_cert_revocation_list_info_t cert_revocation_lists;

        /**
         * Local certificate chain to use for authenticating with the peer. This
         * field is optional and can be left zero-initialized. If used, it shall be
         * initialized using one of the
         * <c>avs_crypto_certificate_chain_info_from_*</c> helper functions.
         */
        avs_crypto_certificate_chain_info_t client_cert;

        /**
         * Private key matching #client_cert to use for authenticating with the
         * peer. This field is optional and can be left zero-initialized, unless
         * #client_cert is also specified. If used, it shall be initialized using
         * one of the <c>avs_crypto_private_key_info_from_*</c> helper functions.
         */
        avs_crypto_private_key_info_t client_key;

        /**
         * Enable rebuilding of client certificate chain based on certificates in
         * the trust store.
         *
         * If this field is set to <c>true</c>, and the last certificate in the
         * #client_cert chain is not self-signed, the library will attempt to find
         * its ancestors in #trusted_certs and append them to the chain presented
         * during handshake.
         */
        bool rebuild_client_cert_chain;
    } avs_net_certificate_info_t;

To populate it properly, we're gonna need at least two pieces of information
from the following list:

- Trusted Certificates, also known as CA / Root certificates (required only
  if we intend to verify certificates presented to us by the Server; although
  it's optional it is **highly recommended**),
- Client Certificate, which is **required**,
- Client Private Key, which is also **required**.

Each of them come in variety of formats (text, binary, etc.) that need to
be loaded and parsed. In most scenarios however, the API provided by `avs_commons`
would suffice to do the necessary work.

For example, to configure Certificate based security, loading all information
from files, we could do something like this:

.. code-block:: c

    const avs_net_certificate_info_t cert_info = {
        .server_cert_validation = true,
        .trusted_certs = avs_crypto_certificate_chain_info_from_path("./CA.crt"),
        .client_cert = avs_crypto_certificate_chain_info_from_file("./client.crt"),
        // NOTE: "password" may be NULL if no password is required
        .client_key =
                avs_crypto_client_key_info_from_file("./client.key", "password")
    };
    avs_net_security_info_t cert_security =
            avs_net_security_info_from_certificates(cert_info);

Security configuration with ``get_security_config`` callback
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Firmware update module provided with Anjay, lets the user implement security
configuration per download URI. The relevant API is:

.. highlight:: c
.. snippet-source:: include_public/anjay/fw_update.h

    typedef int anjay_fw_update_get_security_config_t(
            void *user_ptr,
            anjay_security_config_t *out_security_info,
            const char *download_uri);


And the corresponding handler in ``anjay_fw_update_handlers_t`` to be implemented
by the user:

.. highlight:: c
.. snippet-source:: include_public/anjay/fw_update.h
    :emphasize-lines: 27-29

    typedef struct {
        /** Opens the stream that will be used to write the firmware package to;
         * @ref anjay_fw_update_stream_open_t */
        anjay_fw_update_stream_open_t *stream_open;
        /** Writes data to the download stream;
         * @ref anjay_fw_update_stream_write_t */
        anjay_fw_update_stream_write_t *stream_write;
        /** Closes the download stream and prepares the firmware package to be
         * flashed; @ref anjay_fw_update_stream_finish_t */
        anjay_fw_update_stream_finish_t *stream_finish;

        /** Resets the firmware update state and performs any applicable cleanup of
         * temporary storage if necessary; @ref anjay_fw_update_reset_t */
        anjay_fw_update_reset_t *reset;

        /** Returns the name of downloaded firmware package;
         * @ref anjay_fw_update_get_name_t */
        anjay_fw_update_get_name_t *get_name;
        /** Return the version of downloaded firmware package;
         * @ref anjay_fw_update_get_version_t */
        anjay_fw_update_get_version_t *get_version;

        /** Performs the actual upgrade with previously downloaded package;
         * @ref anjay_fw_update_perform_upgrade_t */
        anjay_fw_update_perform_upgrade_t *perform_upgrade;

        /** Queries security configuration that shall be used for an encrypted
         * connection; @ref anjay_fw_update_get_security_config_t */
        anjay_fw_update_get_security_config_t *get_security_config;

        /** Queries CoAP transmission parameters to be used during firmware
         * update. */
        anjay_fw_update_get_coap_tx_params_t *get_coap_tx_params;
    } anjay_fw_update_handlers_t;

Now, the ``anjay_fw_update_get_security_config_t`` job is to fill
``anjay_security_config_t`` properly. This structure consists of three fields:

.. highlight:: c
.. snippet-source:: include_public/anjay/core.h

    typedef struct {
        /**
         * DTLS keys or certificates.
         */
        avs_net_security_info_t security_info;

        /**
         * Single DANE TLSA record to use for certificate verification, if
         * applicable.
         */
        const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record;

        /**
         * TLS ciphersuites to use.
         *
         * A value with <c>num_ids == 0</c> (default) will cause defaults configured
         * through <c>anjay_configuration_t::default_tls_ciphersuites</c>
         * to be used.
         */
        avs_net_socket_tls_ciphersuites_t tls_ciphersuites;
    } anjay_security_config_t;

We've already seen in previous sections how to configure
``security_info``. Also, for now there is no need to worry about
``dane_tlsa_record`` or ``tls_ciphersuites`` - they can be reset to zero.

Implementation
^^^^^^^^^^^^^^

Our implementation will use the following strategy:

#. Try loading security info from the data model first (i.e. Security Object).
#. If that failed, attempt loading certificates from predefined paths.

.. important::

    Before we jump into implementation, there's one more important thing
    to keep in mind: the lifetime of ``anjay_security_config_t``
    fields. Failing to satisfy lifetime requirements will be met with
    undefined behavior.

    The fields of ``anjay_security_config_t`` contain references to file
    paths, binary security keys, and/or ciphersuite lists. After our
    ``get_security_config`` is called, they are not immediately stored
    anywhere, and for that reason we need to ensure their lifetime is as
    long as necessary. The documentation describes this in more detail,
    and we recommend to have a glance at it.

Our simplified implementation uses either ``anjay_security_config_from_dm()``
which caches the buffers inside the Anjay object in a way that is compatible
with the firmware update object implementation, or when the fallback to
certificates is needed, only literal c-strings are used, thus the lifetime of
security configuration in both cases is just right.

The implementation is presented below. Changes made since :doc:`last time <FU2>`
are highlighted:

.. snippet-source:: examples/tutorial/firmware-update/secure-downloads/src/firmware_update.c
    :emphasize-lines: 11-12, 106-133, 141, 155-157

    #include "./firmware_update.h"

    #include <assert.h>
    #include <errno.h>
    #include <stdio.h>
    #include <sys/stat.h>
    #include <unistd.h>

    static struct fw_state_t {
        FILE *firmware_file;
        // anjay instance this firmware update singleton is associated with
        anjay_t *anjay;
    } FW_STATE;

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

    static int fw_get_security_config(void *user_ptr,
                                      anjay_security_config_t *out_security_info,
                                      const char *download_uri) {
        (void) user_ptr;
        if (!anjay_security_config_from_dm(FW_STATE.anjay, out_security_info,
                                           download_uri)) {
            // found a match
            return 0;
        }

        // no match found, fallback to loading certificates from given paths
        memset(out_security_info, 0, sizeof(*out_security_info));
        const avs_net_certificate_info_t cert_info = {
            .server_cert_validation = true,
            .trusted_certs =
                    avs_crypto_certificate_chain_info_from_path("./certs/CA.crt"),
            .client_cert = avs_crypto_certificate_chain_info_from_path(
                    "./certs/client.crt"),
            .client_key = avs_crypto_private_key_info_from_file(
                    "./certs/client.key", NULL)
        };
        // NOTE: this assignment is safe, because cert_info contains pointers to
        // string literals only. If the configuration were to load certificate info
        // from buffers they would have to be stored somewhere - e.g. on the heap.
        out_security_info->security_info =
                avs_net_security_info_from_certificates(cert_info);
        return 0;
    }

    static const anjay_fw_update_handlers_t HANDLERS = {
        .stream_open = fw_stream_open,
        .stream_write = fw_stream_write,
        .stream_finish = fw_stream_finish,
        .reset = fw_reset,
        .perform_upgrade = fw_perform_upgrade,
        .get_security_config = fw_get_security_config
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
        // make sure this module is installed for single Anjay instance only
        assert(FW_STATE.anjay == NULL);
        FW_STATE.anjay = anjay;
        // install the module, pass handlers that we implemented and initial state
        // that we discovered upon startup
        return anjay_fw_update_install(anjay, &HANDLERS, NULL, &state);
    }
