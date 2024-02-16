..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

OSCORE
======

.. contents:: :local:

General description
-------------------

**Object Security for Constrained RESTful Environments** (OSCORE, `RFC 8613
<https://datatracker.ietf.org/doc/html/rfc8613>`_) is a security protocol
protecting CoAP requests and responses using CBOR Object Signing and Encryption
(COSE, `RFC 8152 <https://datatracker.ietf.org/doc/html/rfc8152>`_). It provides a
high standard communication security while remaining lightweight. Unlike DTLS
and TLS, OSCORE is designed for resource-constrained devices. Encrypting only the
message payload (and parts of the header) on the application layer allows to achieve:

* **Less power consumption** due to less data being processed by time-consuming
  cryptographic algorithms and shorter messages being transmitted,

* **Smaller memory utilization** by the software protocol stack,

* **Shorter computing time** that enhances device responsiveness,

* **End-to-end** security without data being unencrypted and re-encrypted by
  the network gateways,

* **Flexibility** in transport protocols selection since OSCORE works with UDP,
  TCP, :doc:`SMS<CF-SMSBinding>` and :doc:`NIDD<CF-NIDD>`.

OSCORE encryption covers message payload and Request/Response Code. Most CoAP
header fields (i.e. the message fields in the fixed 4-byte header) are required
to be read and/or changed by CoAP proxies, so, in general, they can not be protected
end-to-end if proxy support is required. Nevertheless, there is no hop-by-hop
information encryption (which takes place in (D)TLS proxies) and only the pre-authorized
endpoint (either target device or LwM2M Server) is able to decipher the message entirely.
Even if the network becomes compromised, the data remains secure.

In solutions where the security is top-priority, OSCORE can be used together with
(D)TLS to attain double encryption realized on different protocol stack layers.
OSCORE commercial feature in Anjay comes mostly as an extension to ``avs_coap``
submodule and as LwM2M OSCORE Object implementation (OSCORE module). It gives an
alternative to the security provided by the Transport Layer Protocols (TLS/DTLS)
or enhances it by providing additional encryption on the Application Layer and by
covering additional message frame fields.

Keying material stored in the OSCORE Object can only be set in the Bootstrap phase,
that is, during a Factory Bootstrap, by an LwM2M Bootstrap-Server or by a
:doc:`CF-SmartCardBootstrap`.


Technical documentation
-----------------------

Enabling OSCORE support
^^^^^^^^^^^^^^^^^^^^^^^

If support for OSCORE is available in your version of Anjay, it can be enabled at
compile-time by enabling the ``WITH_AVS_COAP_OSCORE`` macro in the
``avs_coap_config.h`` file or, if using CMake, by enabling the corresponding
``WITH_COAP_OSCORE`` CMake option.

There is also a possibility to use CoAP as defined in `draft-ietf-core-object-security-08
<https://datatracker.ietf.org/doc/html/draft-ietf-core-object-security-08>`_ (which is
referenced in the LwM2M 1.1 Technical Specifications). There is a minor difference
between the implementation of the protocol in this draft and the final version,
so if you want to be fully compliant with the LwM2M 1.1 standard, you might want to
use it. To achieve that, enable the ``WITH_AVS_COAP_OSCORE_DRAFT_8`` macro in the
``avs_coap_config.h`` file or, if using CMake, enable the corresponding
``WITH_AVS_COAP_OSCORE_DRAFT_8`` CMake option.

Anjay provides a pre-implemented OSCORE Object module. You can enable it at compile-time
by enabling ``ANJAY_WITH_MODULE_OSCORE`` macro in the ``anjay_config.h`` file or,
if using CMake, by enabling the corresponding ``WITH_MODULE_oscore`` CMake option.
It is not mandatory to use Anjay's OSCORE Object implementation. However, this article
and example will focus on using it.

.. note::
    To provide your own object implementation, you need to prepare handler
    functions similarly to :doc:`/AdvancedTopics/AT-CustomObjects`.


Encryption and decryption backend
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

OSCORE extension in ``avs_coap`` module does not implement any encryption algorithms.
It relies on a cryptographic library used as a (D)TLS backend selected via CMake option
at the compile time.

.. important::

    | The cryptographic algorithms used in the protocol are defined by default values.
    | The AEAD Algorithm used is AES-CCM-16-64-128.
    | The HMAC Algorithm used is HKDF SHA-256.

.. important::
    If a custom (D)TLS backend library is used, make sure it supports AEAD and HMAC
    Algorithms mentioned above.

The cryptographic algorithms are used to encrypt the CoAP message payload.
CoAP message code is protected by writing the original value into an encrypted COSE object
and setting a valid but not relevant code into the CoAP header.


Installing and configuring OSCORE Object
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If OSCORE is enabled and used, OSCORE Object Instance that holds keying parameters
applied to communication with a specific server has to be linked in the corresponding
Security Object Instance. The object link is realized by a proper setting 
``anjay_security_instance_t.oscore_iid`` field during Security Object Instance creation.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-OSCORE/src/main.c
   :emphasize-lines: 1-12, 18

    anjay_oscore_instance_t oscore_instance = {
        .master_secret = "Ma$T3Rs3CR3t",
        .master_salt = "Ma$T3Rs4LT",
        .sender_id = "15",
        .recipient_id = "25"
    };

    anjay_iid_t oscore_instance_id = ANJAY_ID_INVALID;
    if (anjay_oscore_add_instance(anjay, &oscore_instance,
                                  &oscore_instance_id)) {
        return -1;
    }

    anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://eu.iot.avsystem.cloud:5683",
        .security_mode = ANJAY_SECURITY_NOSEC,
        .oscore_iid = &oscore_instance_id
    };

    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        return -1;
    }


Persisting OSCORE state
^^^^^^^^^^^^^^^^^^^^^^^

The OSCORE state can be persisted and restored similarly to other
Anjay's pre-implemented objects. Let's reuse and extend
:doc:`../AdvancedTopics/AT-Persistence` tutorial to provide an example.

.. note::
   When calling ``anjay_security_object_restore()``, a check is performed
   if a linked OSCORE Object Instance ID is a valid entry in the Data Model.
   Therefore the OSCORE Object has to be restored first.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-OSCORE/src/main.c
    :emphasize-lines: 1-4

    if (avs_is_err(anjay_oscore_object_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist OSCORE Object");
        goto finish;
    }

    if (avs_is_err(anjay_security_object_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist Security Object");
        goto finish;
    }

    if (avs_is_err(anjay_server_object_persist(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not persist Server Object");
        goto finish;
    }

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-OSCORE/src/main.c
    :emphasize-lines: 1-4

    if (avs_is_err(anjay_oscore_object_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore OSCORE Object");
        goto finish;
    }

    if (avs_is_err(anjay_security_object_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore Security Object");
        goto finish;
    }

    if (avs_is_err(anjay_server_object_restore(anjay, file_stream))) {
        avs_log(tutorial, ERROR, "Could not restore Server Object");
        goto finish;
    }

.. note::

   The full code for the following example can be found in the
   ``examples/commercial-features/CF-OSCORE`` directory in Anjay sources. Note that
   to compile and run it, you need to have access to a commercial version of
   Anjay that includes the OSCORE feature.

.. important::

    OSCORE support in Coiote DM LwM2M Server is currently a **work in progress**. 
    Provided example based on connection with EU Cloud Coiote DM instance is
    only a demonstration that **will not yet work** out of the box.

