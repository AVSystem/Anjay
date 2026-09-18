..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Default ciphersuite list
------------------------

If neither the DTLS/TLS Ciphersuite Resource (``/0/x/16``) nor
``anjay_configuration_t::default_tls_ciphersuites`` is configured, Anjay now
uses a built-in allowlist of secure ciphersuites instead of all ciphersuites
supported by the TLS backend.

Applications that require ciphersuites outside this list must configure them
explicitly through ``anjay_configuration_t::default_tls_ciphersuites`` or ``/0/x/16``.

Changes in Anjay configuration
------------------------------

The ``ANJAY_MAX_PK_OR_IDENTITY_SIZE`` configuration option has been renamed to
``ANJAY_EST_CSR_BUFFER_SIZE`` to better reflect its actual purpose. The option
specifies the size of the buffer used when generating certificate signing
requests during EST enrollment and re-enrollment.

Users with custom Anjay configuration files should replace
``ANJAY_MAX_PK_OR_IDENTITY_SIZE`` with ``ANJAY_EST_CSR_BUFFER_SIZE`` while
preserving the previously configured value.

The ``ANJAY_MAX_SECRET_KEY_SIZE`` configuration option has been renamed to
``ANJAY_EST_SECRET_KEY_BUFFER_SIZE`` to better reflect its actual purpose. The
option specifies the size, in bytes, of the buffer used for generating and
storing a private key during EST enrollment.

Users with custom Anjay configuration files should replace
``ANJAY_MAX_SECRET_KEY_SIZE`` with ``ANJAY_EST_SECRET_KEY_BUFFER_SIZE`` while
preserving the previously configured value.

Message cache size configuration
--------------------------------

The type of ``anjay_configuration_t::msg_cache_size`` has changed from
``size_t`` to ``size_t *``. ``NULL`` pointer now indicates that the default
value of ``4000`` will be used.

Applications that previously relied on the zero/default value to disable the
message cache must now explicitly set this field to point to a ``size_t`` value
equal to ``0``, for example:

.. code-block:: c

    size_t msg_cache_size = 0;
    anjay_configuration_t config = {
        .msg_cache_size = &msg_cache_size,
        // other fields...
    };

Disable unsecure configuration
------------------------------

Anjay added a new configuration define ``ANJAY_WITH_UNSECURE_CONNECTIONS`` which
is unset by default in the configuration. Having this option unset will prevent
Anjay from using unecrypted communication. If you relay on a NOSEC communication
you must set this define.

Also Anjay added a new configuration define ``AVS_COMMONS_WITH_LEGACY_SSL_VERSIONS``,
which is unset by default in the configuration. Having this option unset will
prevent Anjay from using legacy SSL, TLS and DTLS protocol versions, including
SSLv2, SSLv3, TLS 1.0, TLS 1.1 and DTLS 1.0.

If you rely on any of these legacy protocol versions, you must enable
``AVS_COMMONS_WITH_LEGACY_SSL_VERSIONS``.

Changes in automatic reconnection in the event loop
--------------------------------------------------

Previously, ``anjay_event_loop_run_with_error_handling()`` called
``anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL)`` when all
configured LwM2M servers were unreachable. This also forced reconnection of
ongoing downloads.

The event loop now schedules reconnects for individual Server Object instances
using ``anjay_server_schedule_reconnect()``. Ongoing downloads using dedicated
downloader sockets are no longer reconnected as a side effect of LwM2M server
connection failures. Downloads that share a socket with a LwM2M server remain
dependent on that server's connection.

Applications that relied on this side effect to reconnect ongoing downloads
must now request it explicitly:

* Use ``anjay_download_reconnect()`` with the download handle for downloads
  started through ``anjay_download()``.
* Use ``anjay_fw_update_pull_reconnect()`` for PULL-mode downloads managed by the
  Firmware Update module.

If reconnecting all sockets on selected transports is intentional, call
``anjay_transport_schedule_reconnect()`` explicitly with the appropriate
transport set.

Automatic CoAP download retries configured through
``anjay_configuration_t::coap_downloader_retry_count`` and
``anjay_configuration_t::coap_downloader_retry_delay`` remain available and
operate independently of this event loop recovery mechanism.

**Applications that did not rely on the transport-wide reconnect side effect
require no changes.**
