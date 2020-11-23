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

Network error handling
======================

Like any software that needs to communicate with other hosts over the network,
Anjay needs to be prepared to handle communication errors. This page documents
the library's behaviour during various error conditions.

Outgoing RPC error handling table
---------------------------------

The following table describes the behaviour of Anjay when various error
conditions happen while performing each of the client-initiated RPC methods.

+-----------------+------------------+------------------+------------------+-------------+-------------------+
|                 | Request          | Register         | Update           | De-register | Notify            |
|                 | Bootstrap        |                  |                  |             | (confirmable)     |
+=================+==================+==================+==================+=============+===================+
| **Timeout       | Retry DTLS       | Retry DTLS       | Fall back        | Ignored     | Ignored; will be  |
| (DTLS)** [#t]_  | handshake [#hs]_ | handshake [#hs]_ | to Register      |             | retried whenever  |
+-----------------+------------------+------------------+                  |             | next notification |
| **Timeout       | Abort all        | :ref:`Abort      |                  |             | (either           |
| (NoSec)** [#t]_ | communication    | registration     |                  |             | confirmable or    |
|                 | [#a]_            | <err-abort-reg>` |                  |             | not) is scheduled |
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

If using the commercial version of Anjay, with the ``ANJAY_WITH_LWM2M11``
compile-time configuration option enabled, the retry procedures as described in
that section of the 1.1 TS will be performed, with respect to settings stored in
the appropriate Server object instance, or the defaults values listen in the
`"Registration Procedures Default Values" table
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#Table-6211-1-Registration-Procedures-Default-Values>`_.
According to this configuration, further failures may result in the "abort all
communication" [#a]_ or "fall back to Client-Initiated Bootstrap" [#bs]_
condition.

In builds of Anjay that do not support LwM2M 1.1, the "abort registration"
condition is equivalent with the "fall back to Client-Initiated Bootstrap"
[#bs]_ condition.

.. note::

    In accordance with the description above, the default behavior in case of
    the "abort registration" condition is **different between the open source
    and commercial versions of Anjay**:

    * The commercial version will perform 5 retry attempts with exponential
      back-off starting with 1 minute initial delay, as mandated by the LwM2M
      1.1 defaults, and if all of them are unsuccessful, only then fall back to
      Client-Initiated Bootstrap [#bs]_
    * The open source version will fall back to Client-Initiated Bootstrap
      [#bs]_ immediately after the initial failure

Other error conditions
----------------------

* **DTLS handshakes** are performed by the DTLS backend library used. This
  includes handling non-fatal errors and retransmissions. In case of no response
  from the server, DTLS handshake retransmissions are expected to follow
  `RFC 6347, Section 4.2.4.  Timeout and Retransmission
  <https://tools.ietf.org/html/rfc6347#section-4.2.4>`_.
  The handshake timers can be customized during Anjay initialization, by setting
  `anjay_configuration_t::udp_dtls_hs_tx_params
  <../api/structanjay__configuration.html#ab8ca076537138e7d78bd1ee5d5e2031a>`_.

  In case of the ultimate timeout, network-layer error, or an internal error
  during the handshake attempt, Anjay will fall back to Client-Initiated
  Bootstrap [#bs]_ or, if the attempt was to connect to a Bootstrap Server,
  cease any attempts to communicate with it (note that unless regular Server
  accounts are available, this will mean abortion of all communication [#a]_).

* Errors while receiving an incoming request, or any unrecognized incoming
  packets, will be ignored

* Errors during `anjay_download()
  <../api/download_8h.html#a7a4d736c0a4ada68f0770e5eb45a84ce>`_ data transfers
  will be passed to the appropriate callback handler, see the `documentation to
  anjay_download_finished_handler_t
  <../api/download_8h.html#a44f0d37ec9ef8123bf88aa9ea9ee7291>`_ for details.

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
         fields in `anjay_configuration_t
         <../api/structanjay__configuration.html>`_.

.. [#hs] To prevent infinite loop of handshakes, DTLS handshake is only retried
         if the failed RPC was **not** performed immediately after the previous
         handshake; otherwise the behaviour described in "Timeout (NoSec)" is
         used.

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
