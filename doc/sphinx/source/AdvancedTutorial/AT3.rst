..
   Copyright 2017-2018 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

DTLS support
============

.. highlight:: c

If Anjay is compiled with support for DTLS enabled and linked with one of the
supported DTLS libraries, connection encryption is automatically handled
according to values of Resources in the Security (``/0``) Object.

This automatic configuration will be performed regardless of whether you are
using the ``security`` module that pre-implements the Security Object, or if you
perhaps decide to implement the Security Object yourself from scratch. The
library will always read the necessary DTLS configuration from the data model.

.. note:: Either **mbed TLS 2.0 or newer** or **OpenSSL 1.1 or newer** or
          **tinydtls 0.9 or newer** is required for proper, conformant support
          for the security modes defined in the LwM2M specification.

.. warning:: Anjay will likely compile successfully with older DTLS library
             versions, but this will cause some cipher suites REQUIRED by the
             LwM2M specification to not be supported, which may cause serious
             interoperability problems.

.. contents::
   :local:

Supported connection modes
--------------------------

The connection mode is determined based on the *Security Mode* Resource in a
given instance of the Security Object (``/0/*/2``). Supported values are:

* ``0`` - **Pre-Shared Key mode** - In this mode, communication is symmetrically
  encrypted using the same secret key, shared between the server and the client.

  * The TLS-PSK identity is stored in the *Public Key or Identity* Resource
    (``/0/*/3``). It is a string identifying the key being used, so that the
    server can uniquely determine which key to use for communication. This
    string shall be directly stored in the aforementioned Resource.

  * The *Secret Key* (``/0/*/5``) Resource shall contain the secret pre-shared
    key itself, directly in an opaque binary format appropriate for the
    cipher suite used by the server.

* ``2`` - **Certificate mode** - In this mode, an asymmetrical public-key
  cryptographic algorithm is used to authenticate the connection endpoints and
  initialize payload encryption.

  Appropriate Certificates need to be generated for both the LwM2M Client and
  the LwM2M Server. Public certificates of both ends are available on both
  sides, and each side also has access to its own corresponding private key.

  * In this mode, the *Public Key or Identity* (``/0/*/3``) Resource shall
    contain the Client's own public certificate in binary, DER-encoded X.509
    format.

  * The *Server Public Key* (``/0/*/4``) Resource shall contain the Server's
    public certificate, also in binary, DER-encoded X.509 format.

  * The *Secret Key* (``/0/*/5``) Resource shall contain the Client's own
    private key, corresponding to the public key contained in the *Public Key or
    Identity* Resource. It needs to be in a format defined in
    `RFC 5958 <https://tools.ietf.org/html/rfc5958>`_ (also known as PKCS#8, the
    name which was used in previous versions of the format), DER-encoded into a
    binary value.

  Note that in the Certificate mode, it is not enough if the Server's
  certificate *just matches* the one stored in the *Server Public Key* resource.
  It is also verified that the certificate is issued for the same domain name
  that is contained in the Server URI, and if it is signed by some external CA,
  that CA needs to be trusted as well.

* ``3`` - **NoSec mode** - In this mode, encryption is disabled completely and
  the CoAP messages are passed in plain text over the network. It shall not be
  used in production environments, unless end-to-end security is provided on a
  lower layer (e.g. IPsec). It is also useful for development, testing and
  debugging purposes.

The *Raw Public Key* and *Certificate with EST* modes described in the LwM2M
specification are not currently supported.

Provisioning security configuration - PSK mode
----------------------------------------------

According to the LwM2M specification, the aforementioned Resources shall be
provisioned during the Bootstrap Phase. However, if Bootstrap from Smartcard is
not used, the Client will need to contain some factory defaults for connecting
to an LwM2M Server or an LwM2M Bootstrap Server. In this section, we will learn
how to implement such factory defaults for DTLS connection.

The full code for the following examples can be found in the
``examples/tutorial/AT3-psk`` directory in Anjay sources.

Configuring DTLS version
^^^^^^^^^^^^^^^^^^^^^^^^

First of all, it is important to configure the appropriate DTLS version when
initializing Anjay:

.. snippet-source:: examples/tutorial/AT3-psk/src/main.c

    static const anjay_configuration_t CONFIG = {
        .endpoint_name = "urn:dev:os:anjay-tutorial",
        .dtls_version = AVS_NET_SSL_VERSION_TLSv1_2,
        .in_buffer_size = 4000,
        .out_buffer_size = 4000
    };

    anjay_t *anjay = anjay_new(&CONFIG);

The enum values for ``dtls_version`` field are based on regular SSL/TLS versions
rather than DTLS. However, a version of DTLS based on the selected TLS version
will be used. Thus:

* to use **DTLS v1.0**, set the field to ``AVS_NET_SSL_VERSION_TLSv1`` or
  ``AVS_NET_SSL_VERSION_TLSv1_1``
* to use **DTLS v1.2**, set it to ``AVS_NET_SSL_VERSION_TLSv1_2`` or
  ``AVS_NET_SSL_VERSION_DEFAULT`` (its numeric value is ``0``, so it is the
  default after zero-initializing the ``anjay_configuration_t`` structure)

.. warning:: **The LwM2M specification mandates use of DTLS v1.2.** The option
             to use earlier versions has been provided only to aid with various
             debugging scenarios. Any version other than DTLS v1.2 shall never
             be used in production environments - failing to comply with this
             requirement is likely to cause serious interoperability problems
             and/or security vulnerabilities.

Configuring encryption keys
^^^^^^^^^^^^^^^^^^^^^^^^^^^

As mentioned above, in case of PSK mode, the security-related data that the
LwM2M Client is operating on, is raw data.

If you're using the implementation of the Security object that is provided in
Anjay's ``security`` module, you can simply fill them in the
``anjay_security_instance_t`` structure as follows:

.. snippet-source:: examples/tutorial/AT3-psk/src/main.c

    static const char PSK_IDENTITY[] = "identity";
    static const char PSK_KEY[] = "P4s$w0rd";

    anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://localhost:5684",
        .security_mode = ANJAY_UDP_SECURITY_PSK,
        .public_cert_or_psk_identity = (const uint8_t *) PSK_IDENTITY,
        .public_cert_or_psk_identity_size = strlen(PSK_IDENTITY),
        .private_cert_or_psk_key = (const uint8_t *) PSK_KEY,
        .private_cert_or_psk_key_size = strlen(PSK_KEY)
    };

Now the only thing left is to add the new Security object instance:

.. snippet-source:: examples/tutorial/AT3-psk/src/main.c

    anjay_iid_t security_instance_id = ANJAY_IID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        return -1;
    }

All remaining activities related to establishing secure communication channel
with the LwM2M Server will be performed automatically by Anjay.

Provisioning security configuration - Certificate mode
------------------------------------------------------

Preparing an LwM2M client written using Anjay to use X.509 certificates requires
essentially the same steps as using the PSK mode. However, it is very likely
that you would like to load the certificates from files.

The full code for the following examples can be found in the
``examples/tutorial/AT3-cert`` directory in Anjay sources.

Loading certificate files
^^^^^^^^^^^^^^^^^^^^^^^^^

All actual parsing is performed by the TLS backend library, so it is enough to
just load contents of certificate files in DER format into memory:

.. snippet-source:: examples/tutorial/AT3-cert/src/main.c

    static int load_buffer_from_file(uint8_t **out, size_t *out_size,
                                     const char *filename) {
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
        if (size < 0 || (unsigned long) size > SIZE_MAX
                || fseek(f, 0, SEEK_SET)) {
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

This function can then be used to fill the relevant fields in the
``anjay_security_instance_t`` structure:

.. snippet-source:: examples/tutorial/AT3-cert/src/main.c

    anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://localhost:5684",
        .security_mode = ANJAY_UDP_SECURITY_CERTIFICATE
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

Now the only thing left is to add the new Security object instance:

.. snippet-source:: examples/tutorial/AT3-cert/src/main.c

    anjay_iid_t security_instance_id = ANJAY_IID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        result = -1;
    }

``anjay_security_object_add_instance()`` copies the buffers present in the
``anjay_security_instance_t`` structure into the internal state of the
``security`` module, so it is now safe to release the memory allocated by the
file loading routine:

.. snippet-source:: examples/tutorial/AT3-cert/src/main.c

    avs_free((uint8_t *) security_instance.public_cert_or_psk_identity);
    avs_free((uint8_t *) security_instance.private_cert_or_psk_key);
    avs_free((uint8_t *) security_instance.server_public_key);
