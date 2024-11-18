..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Network error handling
======================

Like any software that needs to communicate with other hosts over the network,
Anjay needs to be prepared to handle communication errors. This page documents
the library's behavior during various error conditions.

Outgoing request error handling table
-------------------------------------

The following table describes the behavior of Anjay when various error
conditions happen while performing each of the client-initiated operations.

+-----------------+------------------+------------------+------------------+-------------+-------------------+
|                 | Request          | Register         | Update           | De-register | Notify            |
|                 | Bootstrap        |                  |                  |             | (confirmable)     |
+=================+==================+==================+==================+=============+===================+
| **Timeout       | Retry DTLS       | Retry DTLS       | Fall back        | Ignored     | Ignored by        |
| (DTLS)** [#t]_  | handshake [#hs]_ | handshake [#hs]_ | to Register      |             | default;          |
+-----------------+------------------+------------------+                  |             | configurable;     |
| **Timeout       | Abort all        | :ref:`Abort      |                  |             | will be retried   |
| (NoSec)** [#t]_ | communication    | registration     |                  |             | whenever          |
|                 | [#a]_            | <err-abort-reg>` |                  |             | next notification |
|                 |                  |                  |                  |             | is scheduled      |
+-----------------+                  |                  +------------------+             +-------------------+
| **Network       |                  |                  | Fall back to     |             | Fall back to      |
| (e.g. ICMP)     |                  |                  | Client-Initiated |             | Client-Initiated  |
| error**         |                  |                  | Bootstrap [#bs]_ |             | Bootstrap [#bs]_  |
+-----------------+                  |                  +------------------+             +-------------------+
| **CoAP error    |                  |                  | Fall back        |             | n/a               |
| (4.xx, 5.xx)**  |                  |                  | to Register      |             |                   |
+-----------------+                  |                  +------------------+             +-------------------+
| **CoAP Reset**  |                  |                  | Fall back to     |             | Cancel            |
|                 |                  |                  | Client-Initiated |             | observation       |
+-----------------+                  |                  | Bootstrap [#bs]_ |             +-------------------+
| **Internal      |                  |                  |                  |             | Cancel            |
| error**         |                  |                  |                  |             | observation if    |
|                 |                  |                  |                  |             | "Notification     |
|                 |                  |                  |                  |             | storing" is       |
|                 |                  |                  |                  |             | disabled          |
+-----------------+------------------+------------------+------------------+-------------+-------------------+

.. _err-abort-reg:

The "Abort registration" condition
----------------------------------

This condition corresponds to the registration failure as used in the
`Bootstrap and LwM2M Server Registration Mechanisms
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#6-2-1-1-0-6211-Bootstrap-and-LwM2M-Server-Registration-Mechanisms>`_
section of LwM2M Core TS 1.1.

If the ``ANJAY_WITH_LWM2M11`` compile-time configuration option is enabled, the
retry procedures as described in that section of the 1.1 TS will be performed,
with respect to settings stored in the appropriate Server object instance, or
the defaults values listen in the
`"Registration Procedures Default Values" table
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#Table-6211-1-Registration-Procedures-Default-Values>`_.
According to this configuration, further failures may result in the "abort all
communication" [#a]_ or "fall back to Client-Initiated Bootstrap" [#bs]_
condition.

In builds of Anjay that do not support LwM2M 1.1, the "abort registration"
condition is equivalent with the "fall back to Client-Initiated Bootstrap"
[#bs]_ condition.

Other error conditions
----------------------

* **Connect operation errors** can occur for several reasons, the most common
  being:

  * **(D)TLS handshake errors.** Handshakes are performed by the TLS backend
    library used. This includes handling non-fatal errors and retransmissions.
    In case of no response from the server, DTLS handshake retransmissions are
    expected to follow `RFC 6347, Section 4.2.4.  Timeout and Retransmission
    <https://tools.ietf.org/html/rfc6347#section-4.2.4>`_. The handshake timers
    can be customized during Anjay initialization, by setting
    `anjay_configuration_t::udp_dtls_hs_tx_params
    <../api/structanjay__configuration.html#ab8ca076537138e7d78bd1ee5d5e2031a>`_.

    Ultimate timeout, network-layer errors, and internal errors during the
    handshake attempt will be treated as a failure of the "connect" operation.

  * **Domain name resolution errors.** If the ``getaddrinfo()`` call (or
    equivalent) fails to return any usable IP address, this is also treated as
    a failure of the "connect" operation.

  * **TCP handshake errors.** While the actual socket-level "connect" operation
    does not involve any network communication for UDP and as such can almost
    never fail, it performs actual handshake in case of TCP. Failure of this
    handshake is also treated in the same way as the other cases mentioned here.

  * In some cases, **inconsistent data model state** may be treated equivalently
    to a connection error, e.g. when there is no Security object instance that
    would match a given Server object instance.

  Note that all of the operations mentioned above (domain name resolution and
  both TCP and (D)TLS handshakes) are performed synchronously and will block all
  other operations.

  If any of the above conditions happen, Anjay will, by default, fall back to
  Client-Initiated Bootstrap [#bs]_ or, if the attempt was to connect to
  a Bootstrap Server, cease any attempts to communicate with it (note that
  unless regular Server accounts are available, this will mean abortion of all
  communication [#a]_).

  This behavior can be changed by enabling the
  `connection_error_is_registration_failure
  <../api/structanjay__configuration.html#adcc95609ca645a5bd6a572f4c99a83fb>`_.
  In that case, connection errors will trigger :ref:`err-abort-reg`, and thus
  the automatic retry flow described in "Bootstrap and LwM2M Server Registration
  Mechanisms" section mentioned above will be respected.

* Errors while receiving an incoming request, or any unrecognized incoming
  packets, will be ignored

* Errors during `anjay_download()
  <../api/download_8h.html#a7a4d736c0a4ada68f0770e5eb45a84ce>`_ data transfers
  will be passed to the appropriate callback handler, see the `documentation to
  anjay_download_finished_handler_t
  <../api/download_8h.html#a44f0d37ec9ef8123bf88aa9ea9ee7291>`_ for details.
  CoAP downloads support automatic resumption of downloads after network errors,
  see the :ref:`how-can-we-ensure-higher-success-rate` for details.

.. rubric:: Footnotes

.. [#t]  Retransmissions, as specified in
         `RFC 7252, Section 4.2.  Messages Transmitted Reliably
         <https://tools.ietf.org/html/rfc7252#section-4.2>`_, are attempted
         before performing the actions described above. The `transmission
         parameters <https://tools.ietf.org/html/rfc7252#section-4.8>`_ that
         affect specific retransmission timing can be customized during Anjay
         initialization, by setting the `udp_tx_params
         <../api/structanjay__configuration.html#a9690621b087639e06dd0c747206d0679>`_
         and `sms_tx_params
         <../api/structanjay__configuration.html#ab656e5dad737416e5b66272f917df108>`_
         (in versions that include the SMS feature) fields in
         `anjay_configuration_t <../api/structanjay__configuration.html>`_.

.. [#hs] To prevent infinite loop of handshakes, DTLS handshake is only retried
         if the failed operation was **not** performed immediately after the
         previous handshake; otherwise the behavior described in "Timeout
         (NoSec)" is used.

.. [#a]  Communication with all servers will be aborted and
         `anjay_all_connections_failed()
         <../api/core_8h.html#a4329b620520c565fd61b526ba760e59f>`_ will start
         returning ``true``. Operation can be restored by calling
         `anjay_transport_schedule_reconnect()
         <../api/core_8h.html#ad895be5694083d015ffcd8d0b87d0b2a>`_ or
         `anjay_enable_server()
         <../api/core_8h.html#abc4b554e51a56da874238f3e64bff074>`_.

.. [#bs] Client-Initiated Bootstrap will be performed only if all the following
         preconditions are met:

         - a Bootstrap Server Account exists
         - no other LwM2M Server has usable connection
         - the Bootstrap Server Account has not aborted due to previous errors

         Otherwise, further communication with the server with which the
         operation failed will be aborted. This may cause
         `anjay_all_connections_failed()
         <../api/core_8h.html#a4329b620520c565fd61b526ba760e59f>`_ to start
         returning ``true`` if that was the last operational connection.
         Connection can be retried by calling `anjay_enable_server()
         <../api/core_8h.html#abc4b554e51a56da874238f3e64bff074>`_ or
         `anjay_transport_schedule_reconnect()
         <../api/core_8h.html#ad895be5694083d015ffcd8d0b87d0b2a>`_.
