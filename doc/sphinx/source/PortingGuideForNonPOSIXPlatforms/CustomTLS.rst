..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Custom (D)TLS layers
====================

.. highlight:: c

Introduction
------------

``avs_crypto`` and ``avs_net`` include full-featured, ready-to-use integrations
with the TLS and DTLS protocols, using either
`OpenSSL <https://www.openssl.org/>`_ or `Mbed TLS <https://tls.mbed.org/>`_,
as well as a basic implementation that supports the PSK mode only, using
`tinydtls <https://projects.eclipse.org/projects/iot.tinydtls>`_.

These integrations use the ``avs_net`` socket APIs underneath, so if the socket
layer is implemented properly (either using the default implementation or by the
user, as described in the :doc:`previous chapter <NetworkingAPI>`), all the
necessary security features will work properly.

However, in modern embedded development, it is sometimes desirable to offload
all TLS processing - for example, a cellular modem may provide integrated TLS
implementation, controlled e.g. via AT commands. For this reason, a
user-provided implementation of TLS and DTLS "sockets" may be provided instead.

This chapter is a guide for implementing all the features used by Anjay for TLS
communication. The examples recreate an integration with OpenSSL 1.1.1,
simplified compared to the default one, but they are also intended to provide
a reference for integrating with any other TLS API.

.. toctree::
   :glob:
   :titlesonly:

   CustomTLS/CustomTLS-Stub
   CustomTLS/CustomTLS-Minimal
   CustomTLS/CustomTLS-Resumption
   CustomTLS/CustomTLS-ConfigFeatures
   CustomTLS/CustomTLS-CertificatesBasic
   CustomTLS/CustomTLS-CertificatesAdvanced
   CustomTLS/CustomTLS-TCPSupport

Theory of operation
-------------------

TLS and DTLS integration in Anjay is based on the same ``avs_net`` socket APIs
as the basic unencrypted TCP and UDP sockets, with a couple of minor
adjustments:

* The configuration structure passed when creating the socket is different
  (``avs_net_ssl_configuration_t`` instead of
  ``avs_net_socket_configuration_t``), and contains the security configuration,
  including keys and certificates used for communication.

* The ``connect`` and ``accept`` (if supported) operations are expected to
  perform the TLS/DTLS handshake.

* Dedicated option keys for the ``get_opt``/``set_opt`` operations control
  additional TLS/DTLS features:

  * ``AVS_NET_SOCKET_OPT_SESSION_RESUMED`` may be queried to check whether the
    handshake resulted in a new session, or a resumption of an existing one -
    this is used by Anjay to check whether a Register operation is necessary
  * ``AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY`` may be used to specify additional
    peer certificate data for validation according to the `DANE
    <https://en.wikipedia.org/wiki/DNS-based_Authentication_of_Named_Entities>`_
    mechanism - LwM2M 1.1 specifies an almost identical flow for verifying the
    server certificate

* The ``AVS_NET_SOCKET_OPT_INNER_MTU`` option shall take the DTLS header
  overhead into account.

* Additional ``decorate`` operation may be provided to support securing
  communication over a pre-existing unencrypted socket - this is currently used
  by the SMS commercial feature of Anjay to provide security for the SMS
  transport.

List of functions to implement
------------------------------

Support for custom TLS layer needs to be enabled in the compile-time
configuration first:

* When using CMake, use ``-DDTLS_BACKEND=custom`` when configuring Anjay.

* When using another build system, enable ``AVS_COMMONS_WITH_CUSTOM_TLS`` and
  disable ``AVS_COMMONS_WITH_MBEDTLS``, ``AVS_COMMONS_WITH_OPENSSL`` and
  ``AVS_COMMONS_WITH_TINYDTLS`` in ``avs_commons_config.h``.

* Usually you should also disable
  ``AVS_COMMONS_WITH_AVS_CRYPTO_ADVANCED_FEATURES`` in ``avs_commons_config.h``.
  You will most likely want to disable features related to OSCORE and EST if you
  are using a version of Anjay that includes these commercial features.

  * If you need OSCORE or EST, you will need to implement advanced cryptographic
    functions related to AEAD, HKDF and processing various crypto-related file
    formats, that are normally provided by OpenSSL or Mbed TLS. This is not
    thoroughly supported and not covered by this documentation at the moment.

Implementations of the following functions will need to be provided:

* ``_avs_net_create_dtls_socket`` - a function with the following signature:

  .. snippet-source:: deps/avs_commons/src/net/avs_net_impl.h

      avs_error_t _avs_net_create_dtls_socket(avs_net_socket_t **socket,
                                              const void *socket_configuration);

  ``socket_configuration`` argument is a pointer to
  ``const avs_net_ssl_configuration_t`` struct cast to ``void *``.

  Otherwise the function has similar semantics and requirements to the
  ``_avs_net_create_udp_socket`` function described in :doc:`NetworkingAPI`.

* ``_avs_net_create_ssl_socket`` - only required if the ``fw_update`` module
  should support HTTPS transfers, or if support for CoAP over TCP is desired.
  Otherwise, it can be safely implemented as ``return avs_errno(AVS_ENOTSUP);``.

  .. snippet-source:: deps/avs_commons/src/net/avs_net_impl.h

      avs_error_t _avs_net_create_ssl_socket(avs_net_socket_t **socket,
                                             const void *socket_configuration);

  ``socket_configuration`` argument is a pointer to
  ``const avs_net_ssl_configuration_t`` struct cast to ``void *``.

  Otherwise the function has similar semantics and requirements to the
  ``_avs_net_create_tcp_socket`` function described in :doc:`NetworkingAPI`.

* ``_avs_net_initialize_global_ssl_state`` - a function with the following
  signature:

  .. snippet-source:: deps/avs_commons/src/net/avs_net_global.h

      avs_error_t _avs_net_initialize_global_ssl_state(void);

  The function should return ``AVS_OK`` on success and an error code on error.
  It should initialize any global state that needs to be kept by the TLS
  implementation, and initialize external libraries if necessary. If there is no
  such global state or it is initialized elsewhere, it is safe to implement this
  function as a no-op (``return AVS_OK;``).

* ``_avs_net_cleanup_global_ssl_state`` - a function with the following
  signature:

  .. snippet-source:: deps/avs_commons/src/net/avs_net_global.h

      void _avs_net_cleanup_global_ssl_state(void);

  The function should clean up any global state that is kept by the TLS
  implementation. If there is no such global state or it is managed elsewhere,
  it is safe to implement this function as a no-op.
