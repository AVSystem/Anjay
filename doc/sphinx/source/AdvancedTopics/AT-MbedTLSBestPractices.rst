..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Using Mbed TLS with Anjay: configuration and troubleshooting
============================================================

This article collects practical information and recommendations for using
Mbed TLS as the TLS/DTLS backend for Anjay. It focuses on configuration issues
commonly encountered when integrating Anjay with Mbed TLS on embedded platforms.

This article does not replace the general Anjay security tutorials. If you are
looking for a complete introduction to the Security Object, PSK mode or
certificate mode, see :doc:`/BasicClient/BC-Security`, :doc:`AT-Certificates`
and :doc:`AT-CertificateUsage` first.

For Mbed TLS-specific topics that are not directly related to Anjay, such as
footprint reduction, platform integration or low-level TLS configuration, also
refer to the official Mbed TLS knowledge base and tutorials.

Best practices
--------------

Connection ID
^^^^^^^^^^^^^

When using DTLS, consider enabling Connection ID (CID) if the server supports it.
CID allows the server to associate packets with an existing DTLS connection even
if the client's network endpoint as seen by the server changes. This is often
caused by NAT mapping expiration, which may change the source port or address
observed by the server, and does not necessarily require the device to reconnect
to a different network.

This functionality must be enabled both on the Anjay and Mbed TLS
sides. On the Anjay side, set the ``anjay_configuration_t::use_connection_id``
field to ``true``. On the Mbed TLS side, enable the ``MBEDTLS_SSL_DTLS_CONNECTION_ID``
option in the Mbed TLS configuration.

If CID enabling succeeds, Anjay will print a message like the following during the DTLS handshake:

.. code-block:: none

    DEBUG [avs_net] [../anj_mbedtls_socket.c]: negotiated CID = 0011223344556677

RNG source
^^^^^^^^^^

Use a strong entropy source for the random number generator. Mbed TLS requires
a random number generator during TLS/DTLS handshakes. In Anjay, the Mbed TLS
backend obtains the RNG callback from the PRNG context configured for the Anjay
instance. To provide a custom entropy source, create an ``avs_crypto_prng_ctx_t``
using ``avs_crypto_prng_new()`` and pass it as ``anjay_configuration_t::prng_ctx``.

.. warning::

   If you don't provide a custom PRNG context, Anjay will use a default software
   PRNG implementation which is not suitable for cryptographic purposes. Make sure
   to provide a secure PRNG context for any production deployment.

Buffer and credential size limits
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When reducing the Mbed TLS footprint, make sure that size-related configuration
options are still large enough for the credentials used by the deployment. This
is especially important for certificate-based security mode. The
``MBEDTLS_SSL_IN_CONTENT_LEN`` and ``MBEDTLS_SSL_OUT_CONTENT_LEN`` options define
the sizes of internal incoming and outgoing message buffers. They must be large
enough to receive and send the largest handshake messages expected during
connection setup, including certificate chains. If these buffers are reduced too
aggressively, certificate-based DTLS/TLS handshakes may fail.

The same applies to PSK-based deployments. Mbed TLS limits the maximum accepted
PSK size using ``MBEDTLS_PSK_MAX_LEN``. If the PSK configured in the Security
Object is longer than this limit, Mbed TLS will not be able to use it and the
connection will fail during setup.

Cipher Suites
^^^^^^^^^^^^^

The list of cipher suites used during the TLS handshake can be configured using 
``anjay_configuration_t::default_tls_ciphersuites`` field. They must be specified as an array of
16-bit integers in big-endian order. For example, ``TLS_PSK_WITH_AES_128_CCM_8``
is represented as ``0xC0A8``. If Mbed TLS configuration does not include the corresponding cipher suite,
it will be ignored. ``default_tls_ciphersuites`` is used only when the ``DTLS/TLS Ciphersuite`` Resource
(``/0/x/16``) is not available or empty.

As a best practice, configure only cipher suites that are considered secure and
are required by the target LwM2M Server. Avoid offering every cipher suite
supported by the Mbed TLS build. During the handshake, the server selects one of
the cipher suites offered by the client, so offering weak or legacy cipher
suites may allow the connection to use weaker cryptography than intended.

The same rule should be applied at the Mbed TLS configuration level. Remove
unwanted cipher suites from the Mbed TLS build to reduce code size and to make
sure that they cannot be negotiated accidentally. When enabling a cipher suite,
also enable all cryptographic primitives it depends on, such as the key exchange
method, cipher algorithm, hash/MAC algorithm and required elliptic curves.

Considerations
--------------

Server Name Indication
^^^^^^^^^^^^^^^^^^^^^^

The LwM2M Security Object defines resource ``/0/x/14`` (Server Name Indication, SNI),
which may be used to explicitly configure the hostname sent during the DTLS
Client Hello message in the Server Name Indication extension field.

When SNI support is enabled in Mbed TLS (by defining ``MBEDTLS_SSL_SERVER_NAME_INDICATION``),
Anjay provides the server name indication according to the Security Object configuration.
Its value is determined as follows:

- If SNI is explicitly provided in the Security Object instance (as ``anjay_security_instance_t::server_name_indication``), it is used.
- Otherwise, the hostname part of ``anjay_security_instance_t::server_uri`` is used as SNI.

SNI is particularly important when connecting to servers that host multiple
virtual endpoints (e.g., cloud platforms), as it allows the server to select
the correct certificate and configuration. In certificate mode, the SNI value
is also relevant for hostname verification, so it needs to match the Subject
Alternative Name (SAN), or Common Name (CN) if applicable, in the server certificate.

Certificate time validation
^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the platform provides a valid real-time clock or another reliable source of
current time, enable ``MBEDTLS_HAVE_TIME_DATE`` in the Mbed TLS configuration.
This allows Mbed TLS to validate the ``notBefore`` and ``notAfter`` fields of
X.509 certificates during certificate verification.

On platforms without reliable timekeeping, certificate-based security requires
additional design decisions. Disabling ``MBEDTLS_HAVE_TIME_DATE`` may make DTLS
handshakes succeed, but it also prevents Mbed TLS from rejecting expired or
not-yet-valid certificates. Treat this as a security trade-off, not as a generic
workaround for handshake failures.

.. note::

   A working real-time clock is required when using the Enrollment over Secure
   Transport (EST) commercial feature.

Certificate verification model
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When certificate mode is used, pay attention to the Certificate Usage setting.
Its detailed behavior is described in :doc:`AT-CertificateUsage`, but from the
Mbed TLS integration perspective the most important point is whether PKIX
verification is required and where the trust anchors come from.

Certificate Usage ``0`` (``AVS_NET_SOCKET_DANE_CA_CONSTRAINT``) and
Certificate Usage ``1`` (``AVS_NET_SOCKET_DANE_SERVICE_CERTIFICATE_CONSTRAINT``)
require a trust store. In Anjay, this means providing trusted certificates
through ``anjay_configuration_t::trust_store_certs``. The
``anjay_configuration_t::use_system_trust_store`` field does not enable the
system trust store when the Mbed TLS backend is used.

LwM2M 1.0 does not define the Certificate Usage resource, so the only available
mode is ``AVS_NET_SOCKET_DANE_DOMAIN_ISSUED_CERTIFICATE``. The same mode is also
used as the default for LwM2M 1.1 and newer if no Certificate Usage value is
configured.

.. warning::

   If the Server Public Key resource is empty, Anjay may fall back to PKIX
   verification if a trust store is available. However, for Certificate Usage
   ``2`` and ``3``, if neither the Server Public Key nor a trust store is provided,
   Anjay will connect without validating the server certificate. Make sure that
   this is not the result of missing certificate provisioning.

Mbed TLS clock integration
^^^^^^^^^^^^^^^^^^^^^^^^^^

When DTLS is used, Mbed TLS needs timer callbacks for handshake retransmissions.
Anjay configures them internally through ``mbedtls_ssl_set_timer_cb()``, using
``mbedtls_timing_set_delay()`` and ``mbedtls_timing_get_delay()`` as the delay
callbacks. By default, the Mbed TLS timing module relies on POSIX
``gettimeofday()`` - if this is not available ``MBEDTLS_TIMING_ALT`` must be
enabled and the platform must provide its own implementation of these timing
functions.

Session resumption
^^^^^^^^^^^^^^^^^^

Mbed TLS supports session resumption, which allows clients to reuse previously
established TLS sessions across reconnects, including after a device reboot. This can
significantly reduce the time and energy required to establish a secure connection by
reducing the overhead of the handshake. To enable session resumption in Anjay, use
`Core Persistence API` to store the session data between connections. Check the
:doc:`/CommercialFeatures/CF-CorePersistence` documentation for details on how to use this API.

Debugging and troubleshooting
-----------------------------

Mbed TLS debugging
^^^^^^^^^^^^^^^^^^

Mbed TLS can be configured to print debug logs, which can be very helpful during development
and troubleshooting. The ``AVS_COMMONS_NET_WITH_MBEDTLS_LOGS`` option enables Mbed TLS logging at
the maximum verbosity level. On the Anjay side, the logs are printed using the ``AVS_LOG_TRACE``
log level, so make sure that ``AVS_COMMONS_WITH_INTERNAL_LOGS`` and ``AVS_COMMONS_WITH_INTERNAL_TRACE``
options are also enabled.

Another useful option is ``AVS_COMMONS_NET_WITH_MBEDTLS_SSLKEYLOG`` which enables logging of
TLS session keys to a user-specified stream. This allows tools like Wireshark to decrypt
captured TLS traffic. The user must specify the stream to which the logs will be transferred
using the ``avs_mbedtls_set_sslkeylog_stream`` function.

.. warning::

   ``AVS_COMMONS_NET_WITH_MBEDTLS_SSLKEYLOG`` is intended for development and
   troubleshooting only. The generated key log allows captured TLS traffic to be
   decrypted and should never be enabled in production environments.

Troubleshooting and verification
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When debugging Mbed TLS behavior in Anjay, start by verifying the configuration
at all relevant layers:

- whether the relevant Anjay configuration field is set, both at compile time
  and at runtime,
- whether the required Mbed TLS ``MBEDTLS_*`` option is enabled,
- whether the server supports the selected feature, such as CID, SNI or a given
  cipher suite.

If the configuration looks correct, continue with runtime diagnostics. Enable
Anjay logs and, if possible, Mbed TLS logs. For DTLS/TLS handshake issues, packet
captures are often the fastest way to confirm what is actually exchanged between
the client and the server.

Pay particular attention to certificate configuration. Many handshake failures
are caused by mismatched or incomplete certificate material, for example:

- the server certificate does not match the hostname used for verification,
- the SNI value does not match the certificate SAN/CN,
- the trust anchor is missing or does not match the selected Certificate Usage,
- the certificate is expired or not yet valid according to the device clock,
- the client certificate and private key do not match.

If debugging directly on the target device is not possible, try to reproduce the
same DTLS/TLS session from a development machine using equivalent credentials,
server URI, SNI value, cipher suites and trust anchors. This helps distinguish
server-side or credential-related issues from target-specific problems such as
network issues, memory limits or reduced Mbed TLS configuration.

.. note::

   For lossy or high-latency UDP networks, also check the retransmission timeouts configured through
   ``anjay_configuration_t::udp_dtls_hs_tx_params``. Incorrect retransmission
   timeouts may cause handshake failures due to excessive packet loss, even if the handshake would
   succeed in a more reliable network environment.
