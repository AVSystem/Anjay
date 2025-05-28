..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Hardware Security Module
========================

.. contents:: :local:

**Hardware Security Module (HSM)** is a piece of hardware designed to increase
security of the device by keeping the vulnerable data safe (mainly private
or secret keys, but also certificates) and performing operations like:

 * key generation,
 * signing/verification,
 * encryption/decryption,

while the used private and secret keys are not leaving their secure memory.
Because such idea might have a lot of various implementations, some generic APIs
were created. Commercial feature of Anjay, HSM, includes integrations with two
of them: `PKCS11 <https://datatracker.ietf.org/doc/html/rfc7512>`_ and
`PSA <https://developer.arm.com/architectures/architecture-security-features/platform-security>`_.


To increase the safety of the IoT client even more
HSMs are often used to maintain credentials used in Enrollment over Security
Transport (EST). To make this easier, when Anjay is used with both HSM and
:doc:`CF-EST` commercial features it includes also an additional integration
between them, which allows to easily setup such a secure
client (see CF-EST-PKCS11 example).


Supported features
------------------

The following features are implemented:

* integration with *PKCS11* API - working with both *OpenSSL* (using PKI)
  and *mbed TLS* (using PKI),
* integration with *Platform Security Architecure (PSA)* API - working with
  *mbed TLS* (using PKI or PSK),
* integration with the *EST* feature (see :doc:`CF-EST`) which allows to
  keep the private keys and certificates used by EST operations.

Technical documentation
-----------------------

Enabling Hardware Security Module support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The integrations with the HSM APIs (i.e. PKCS11 and PSA) are available as
separate commercial features and the first requirement to make it work is to use
a version of Anjay containing them.

From Anjay's perspective PKCS11 and PSA engines are used in quite similar way and
are considered as backends for the APIs enabled by the corresponding macros:

* ``AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE`` - for PKI support,
* ``AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE`` - for PSK support.

To enable support for PKCS11 backend one has to enable (depending on which
cryptographic library is used):

* ``AVS_COMMONS_WITH_MBEDTLS_PKCS11_ENGINE`` - while using mbed TLS,
* ``AVS_COMMONS_WITH_OPENSSL_PKCS11_ENGINE`` - while using OpenSSL.

In the case of PSA only mbed TLS support is available, so the proper macro
is ``AVS_COMMONS_WITH_MBEDTLS_PSA_ENGINE``. Additionally, the used mbed TLS
version must be compiled with ``MBEDTLS_USE_PSA_CRYPTO`` flag. If there is
PSA Protcted Storage API available and you want Anjay to be able to use it, you
need also ``AVS_COMMONS_WITH_MBEDTLS_PSA_ENGINE_PROTECTED_STORAGE`` to be
defined.


An alternative method to enable a certain integration is enabling the proper
macro in the ``avs_commons_config.h`` file (where "proper" means that its name
consists of ``AVS_COMMONS_`` and corresponding CMake option).


There are also a few macros which can be defined for the support of the
HSM-stored credentials in Anjay:

* ``ANJAY_WITH_SECURITY_STRUCTURED`` - enable support for handling complex types
  of security credentials in the data model using structured <c>avs_crypto</c>
  types. In particular, it allows to keep credentials in the form of their HSM
  address.

* ``ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT`` - enables the automatic
  moving to HSM the credentials stored in the built-in Anjay Security object.

* ``ANJAY_WITH_EST_ENGINE_SUPPORT`` - when the EST commercial feature is
  available, it enables the support for storing on HSM the credentials used
  during EST operations.

As before, this macros might be defined directly in ``anjay_config.h`` file,
or set using CMake.


Addressing Hardware Security Module objects
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Objects in PSA and PKCS11 are addressed in a slightly different ways - in PSA
an object is called *key* and to access it just its identifier is required,
which is simply an integer. Thus, PSA query used by Anjay consists of a single
parameter: ``kid=KEY_ID``, where *KEY_ID* is the hex-encoded key identifier.
When PSA Protected Storage API is available and enabled in Anjay (it needs the
macro ``AVS_COMMONS_WITH_MBEDTLS_PSA_ENGINE_PROTECTED_STORAGE`` to be defined),
it may keep raw data addressed with some *ID*. In which case query will be quite
similar: ``uid=ID``.

Queries which are used to address the PKCS11 objects were defined in
`PKCS11 RFC <https://datatracker.ietf.org/doc/html/rfc7512>`_ as PKCS11 URI.
They may contain a lot of various fields, but usually three of them are used in
Anjay clients: *token*, *pin* and *label* (or *id* instead of label). In such
case, the PKCS11 query looks like:
``pkcs11:token=TOKEN;object=LABEL;pin-value=PIN``.


Using security objects already stored in HSM
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

    The full code for the following example can be found in the
    ``examples/commercial-features/CF-PSA-PSK``,
    ``examples/commercial-features/CF-PSA-PKI`` and
    ``examples/commercial-features/CF-PKCS11`` directories in Anjay sources.
    Note that to compile and run it, you need to have access to
    a commercial version of Anjay that includes HSM feature.

When using HSM to store the Security objects, they shouldn't be stored in the
application memory, so they can't be kept in the buffers in the security object
instances, so some other kind of structures is needed for them. For this purpose
an additional set of fields, which are able to store them, was introduced in
`anjay_security_instance_t <../api/structanjay__security__instance__t.html>`_
(to make them available ``ANJAY_WITH_SECURITY_STRUCTURED`` macro must be
defined):

.. highlight:: c
.. snippet-source:: include_public/anjay/security.h

    /** Resource: Public Key Or Identity;
    * This is an alternative to the @p public_cert_or_psk_identity and
    * @p psk_identity fields that may be used only if @p security_mode is
    * either @ref ANJAY_SECURITY_CERTIFICATE or @ref ANJAY_SECURITY_EST; it is
    * also an error to specify non-empty values for more than one of these
    * fields at the same time. */
    avs_crypto_certificate_chain_info_t public_cert;
    /** Resource: Secret Key;
    * This is an alternative to the @p private_cert_or_psk_key and @ref psk_key
    * fields that may be used only if @p security_mode is either
    * @ref ANJAY_SECURITY_CERTIFICATE or @ref ANJAY_SECURITY_EST; it is also an
    * error to specify non-empty values for more than one of these fields at
    * the same time. */
    avs_crypto_private_key_info_t private_key;
    /** Resource: Public Key Or Identity;
    * This is an alternative to the @p public_cert_or_psk_identity and
    * @ref public_cert fields that may be used only if @p security_mode is
    * @ref ANJAY_SECURITY_PSK; it is also an error to specify non-empty values
    * for more than one of these fields at the same time. */
    avs_crypto_psk_identity_info_t psk_identity;
    /** Resource: Secret Key;
    * This is an alternative to the @p private_cert_or_psk_key and
    * @ref private_key fields that may be used only if @p security_mode is
    * @ref ANJAY_SECURITY_PSK; it is also an error to specify non-empty values
    * for more than one of these fields at the same time. */
    avs_crypto_psk_key_info_t psk_key;


There is also a set of functions which can turn an HSM query pointing to the
required object stored on the HSM to the struct which can be used by the
instance of the Security object:

* ``avs_crypto_certificate_chain_info_from_engine()`` - creates certificate chain
  descriptor used later on to load a certificate from the engine,

* ``avs_crypto_private_key_info_from_engine()`` - creates private key descriptor
  used later on to load private key from the engine,

* ``avs_crypto_psk_key_info_from_engine()`` - creates pre-shared key descriptor
  used later on to load pre-shared key from the engine,

* ``avs_crypto_psk_identity_info_from_engine()`` - creates pre-shared key identity
  descriptor used later on to load pre-shared key identity from the engine.


One may notice that the first two of them (as well as first two mentioned
`anjay_security_instance_t <../api/structanjay__security__instance__t.html>`_
fields) are used when the connection is secured using PKI, while the latter are
used with PSK. Let's see how they work with a Security object instance in PKI
mode in the PKCS11 example:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-PKCS11/src/main.c

    #define KEY_QUERY "pkcs11:token=MyToken;object=ClientKey;pin-value=1234"
    #define CERTIFICATE_QUERY \
        "pkcs11:token=MyToken;object=ClientCert;pin-value=1234"

    // ...

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
        .security_mode = ANJAY_SECURITY_CERTIFICATE,
        .public_cert = avs_crypto_certificate_chain_info_from_engine(
                CERTIFICATE_QUERY),
        .private_key = avs_crypto_private_key_info_from_engine(KEY_QUERY)
    };

The only thing that must be changed to use keys and certificates on HSM which
uses PSA API are the queries for the key and the certificate:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-PSA-PKI/src/main.c

    #define KEY_QUERY "kid=0x00000001"
    #define CERTIFICATE_QUERY "kid=0x00000002"

    // ...

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
        .security_mode = ANJAY_SECURITY_CERTIFICATE,
        .public_cert = avs_crypto_certificate_chain_info_from_engine(
                CERTIFICATE_QUERY),
        .private_key = avs_crypto_private_key_info_from_engine(KEY_QUERY),
    };

And in the similar way, we can use PSA for keeping credentials for the PSK mode
(CF-PSA-PSK example):

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-PSA-PSK/src/main.c

    #define IDENTITY_QUERY "kid=0x00000001"
    #define KEY_QUERY "kid=0x00000002"

    // ...

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
        .security_mode = ANJAY_SECURITY_PSK,
        .psk_identity =
                avs_crypto_psk_identity_info_from_engine(IDENTITY_QUERY),
        .psk_key = avs_crypto_psk_key_info_from_engine(KEY_QUERY),
    };


Storing and removing objects from HSM
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

    The full code for the following example can be found in the
    ``examples/commercial-features/CF-PSA-management`` directory in Anjay
    sources. Note that to compile and run it, you need to have access to
    a commercial version of Anjay that includes HSM feature.


The avs_commons provides following functions for storing PKI private keys and
certificates in the HSM:

* ``avs_crypto_pki_engine_key_store()``,

* ``avs_crypto_pki_engine_certificate_store()``

and corresponding functions for their removal:

* ``avs_crypto_pki_engine_key_rm()``,

* ``avs_crypto_pki_engine_certificate_rm()``.

An example of how they can be used to manage PKI objects is shown in
CF-PSA-management example:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-PSA-management/src/main.c

    if (!strcmp(argv[2], "pkey")) {
        if (avs_is_err(avs_crypto_pki_engine_key_rm(query))) {
            avs_log(tutorial, ERROR, "Private key removal failed");
            return -1;
        }
    } else if (!strcmp(argv[2], "certificate")) {
        if (avs_is_err(avs_crypto_pki_engine_certificate_rm(query))) {
            avs_log(tutorial, ERROR, "Certificate removal failed");
            return -1;
        }
    } else if (!strcmp(argv[2], "psk_key")) {

    // ...

    if (!strcmp(argv[2], "pkey")) {
        avs_crypto_private_key_info_t key_info =
                avs_crypto_private_key_info_from_file(argv[4], NULL);
        if (avs_is_err(avs_crypto_pki_engine_key_store(
                    query, &key_info, NULL))) {
            avs_log(tutorial, ERROR, "Storing private key failed");
            return -1;
        }
    } else if (!strcmp(argv[2], "certificate")) {
        avs_crypto_certificate_chain_info_t cert_info =
                avs_crypto_certificate_chain_info_from_file(argv[4]);
        if (avs_is_err(avs_crypto_pki_engine_certificate_store(
                    query, &cert_info))) {
            avs_log(tutorial, ERROR, "Storing certificate failed");
            return -1;
        }
    } else if (!strcmp(argv[2], "psk_key")) {

Analogous set of functions is also available for the PSK cryptography:

* ``avs_crypto_psk_engine_key_store()``,

* ``avs_crypto_psk_engine_identity_store()``,

* ``avs_crypto_psk_engine_key_store()`` and

* ``avs_crypto_psk_engine_identity_store()``.

There is also a similar example of their usage in the CF-PSA-management example.

In PKI engine API there are also functions for private key generation,
``avs_crypto_pki_engine_key_gen``, but to use the generated key, we need to
prepare a certificate for it. This is done typically during the EST enrollment.
Please see the EST feature documentation for more information on this topic.

Use the HSM in the implicit way
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. note::

    The full code for the following example can be found in the
    ``examples/commercial-features/CF-PSA-boostrap`` directory in Anjay
    sources. Note that to compile and run it, you need to have access to
    a commercial version of Anjay that includes HSM feature.

An alternative, and probably more elegant, approach to store and use credentials
on HSM is to use the function `anjay_security_object_install_with_hsm
<../api/security_8h.html#ad7cf8eb206cabb407aad57777dc0a144>`_ to install the
Security object and then use it in the same way as the standard one - it will
move the provided credentials to HSM memory. This function, comparing to default
`anjay_security_object_install
<../api/security_8h.html#a5fffaeedfc5c2933e58ac1446fd0401d>`_, needs an
additional argument - ``hsm_config`` which is basically a set of callbacks (and
their arguments) required to generate the HSM adresses for new HSM objects:


.. highlight:: c
.. snippet-source:: include_public/anjay/security.h
    :commercial:

    /**
    * Configuration of the callbacks for generating the query string addresses
    * under which different kinds of security credentials will be stored on the
    * hardware security engine.
    */
    typedef struct {
        /**
        * Callback function that will be called whenever a public client
        * certificate needs to be stored in an external security engine.
        *
        * If NULL, public client certificates will be stored in main system memory
        * unless explicitly requested via either EST or the <c>public_cert</c>
        * field in @ref anjay_security_instance_t.
        */
        anjay_security_hsm_query_cb_t *public_cert_cb;

        /**
        * Opaque argument that will be passed to the function configured in the
        * <c>public_cert_cb</c> field.
        *
        * If <c>public_cert_cb</c> is NULL, this field is ignored.
        */
        void *public_cert_cb_arg;

        /**
        * Callback function that will be called whenever a client private key needs
        * to be stored in an external security engine.
        *
        * If NULL, client private keys will be stored in main system memory unless
        * explicitly requested via either EST or the <c>private_key</c> field in
        * @ref anjay_security_instance_t.
        */
        anjay_security_hsm_query_cb_t *private_key_cb;

        /**
        * Opaque argument that will be passed to the function configured in the
        * <c>private_key_cb</c> field.
        *
        * If <c>private_key_cb</c> is NULL, this field is ignored.
        */
        void *private_key_cb_arg;

        /**
        * Callback function that will be called whenever a PSK identity for use
        * with the main connection needs to be stored in an external security
        * engine.
        *
        * If NULL, PSK identities for use with the main connection will be stored
        * in main system memory unless explicitly requested via the
        * <c>psk_identity</c> field in @ref anjay_security_instance_t.
        */
        anjay_security_hsm_query_cb_t *psk_identity_cb;

        /**
        * Opaque argument that will be passed to the function configured in the
        * <c>psk_identity_cb</c> field.
        *
        * If <c>psk_identity_cb</c> is NULL, this field is ignored.
        */
        void *psk_identity_cb_arg;

        /**
        * Callback function that will be called whenever a PSK key for use with the
        * main connection needs to be stored in an external security engine.
        *
        * If NULL, PSK keys for use with the main connection will be stored in main
        * system memory unless explicitly requested via the <c>psk_key</c> field in
        * @ref anjay_security_instance_t.
        */
        anjay_security_hsm_query_cb_t *psk_key_cb;

        /**
        * Opaque argument that will be passed to the function configured in the
        * <c>psk_key_cb</c> field.
        *
        * If <c>psk_key_cb</c> is NULL, this field is ignored.
        */
        void *psk_key_cb_arg;
    #    ifdef ANJAY_WITH_SMS
        /**
        * Callback function that will be called whenever a PSK identity for use
        * with SMS binding needs to be stored in an external security engine.
        *
        * If NULL, PSK identities for use with SMS binding will be stored in main
        * system memory unless explicitly requested via the <c>sms_psk_identity</c>
        * field in @ref anjay_security_instance_t.
        */
        anjay_security_hsm_query_cb_t *sms_psk_identity_cb;

        /**
        * Opaque argument that will be passed to the function configured in the
        * <c>sms_psk_identity_cb</c> field.
        *
        * If <c>sms_psk_identity_cb</c> is NULL, this field is ignored.
        */
        void *sms_psk_identity_cb_arg;

        /**
        * Callback function that will be called whenever a PSK key for use with SMS
        * binding needs to be stored in an external security engine.
        *
        * If NULL, PSK keys for use with SMS binding will be stored in main system
        * memory unless explicitly requested via the <c>sms_psk_key</c> field in
        * @ref anjay_security_instance_t.
        */
        anjay_security_hsm_query_cb_t *sms_psk_key_cb;

        /**
        * Opaque argument that will be passed to the function configured in the
        * <c>sms_psk_key_cb</c> field.
        *
        * If <c>sms_psk_key_cb</c> is NULL, this field is ignored.
        */
        void *sms_psk_key_cb_arg;
    #    endif // ANJAY_WITH_SMS
    } anjay_security_hsm_configuration_t;


This approach is particularly useful when using Bootstrap server - in this case
Anjay will automatically move the credentials received from the Bootstrap server
to the HSM memory. This is what we can see in action in CF-PSA-boostrap example.
As you can see, the changes we need to make are quite subtle:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-PSA-bootstrap/src/main.c

    anjay_security_hsm_configuration_t HSM_CONFIG = {
        .psk_identity_cb = generate_hsm_address,
        .psk_key_cb = generate_hsm_address
    };

    // ...

    if (anjay_security_object_install_with_hsm(anjay, &HSM_CONFIG)) {
        return -1;
    }

Where ``generate_hsm_address`` is a function for PSA address generation, in this
case pseudo-random:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-PSA-bootstrap/src/main.c

    static const char *generate_hsm_address(anjay_iid_t iid,
                                            anjay_ssid_t ssid,
                                            const void *data,
                                            size_t data_size,
                                            void *arg) {
        (void) iid;
        (void) ssid;
        (void) data;
        (void) data_size;
        (void) arg;

        static size_t offset = 0ul;
        static char buffer[1024];

        if (offset + sizeof(HSM_TEMPLATE) > sizeof(buffer)) {
            avs_log(tutorial, ERROR, "Wrong HSM address");
            return NULL;
        }

        static avs_rand_seed_t SEED;
        if (!SEED) {
            SEED = (avs_rand_seed_t) time(NULL);
        }

        char *result = buffer + offset;
        offset += sizeof(HSM_TEMPLATE);
        strcpy(result, HSM_TEMPLATE);

        for (int i = 0; result[i]; i++) {
            if (result[i] == '.') {
                result[i] = HSM_ALPHABET[(size_t) avs_rand_r(&SEED)
                                        % (sizeof(HSM_ALPHABET) - 1)];
            }
        }

        return result;
    }


