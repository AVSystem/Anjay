..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Network and Server error handling
=================================

Like any software that needs to communicate with other hosts over the network,
Anjay needs to be prepared to handle communication errors. This page documents
the library's behavior during various error conditions.

.. _dtls_hs:

(D)TLS Handshake
----------------

If configured to use (D)TLS, the TLS backend library performs a Handshake before
**Request Bootstrap** and **Register** operation.
This operation has a standard-defined retries schedule `RFC 6347, Section 4.2.4. 
Timeout and Retransmission <https://tools.ietf.org/html/rfc6347#section-4.2.4>`_.
The handshake timers can be customized during Anjay initialization, by setting
`anjay_configuration_t::udp_dtls_hs_tx_params <../api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration21udp_dtls_hs_tx_paramsE>`_.

A failed (D)TLS Handshake when trying to perform Bootrstrap Request operation
will lead to procedure abort.

A failed (D)TLS Handshake when trying to perform Register operation will lead to 
:ref:`Retry or abort registration <err-abort-reg>`.

Ultimate timeout, network-layer errors, and internal errors during the
handshake attempt will be treated as a failure of the "connect" operation.


Outgoing request error handling table
-------------------------------------

The following table describes the behavior of Anjay when various error
conditions happen while performing each of the client-initiated operations.

.. flat-table:: LwM2M Operations Error Handling
   :widths: 15 20 15 20 15 30
   :header-rows: 1

   * -
     - Request Bootstrap
     - Register
     - Update
     - De-register
     - Notify (confirmable)

   * - **Timeout** [#t]_
     - :rspan:`4` Abort all communication [#a]_
     - :rspan:`4` :ref:`Retry or abort registration <err-abort-reg>`
     - Fall back to Register
     - :rspan:`4` Ignored
     - Ignored by default; configurable; will be retried whenever next notification is scheduled

   * - **Network (e.g. ICMP) error**
     - Fall back to :ref:`Client-Initiated Bootstrap <bs>`
     - Fall back to :ref:`Client-Initiated Bootstrap <bs>`

   * - **CoAP error (4.xx, 5.xx)**
     - Fall back to Register
     - n/a

   * - **CoAP Reset**
     - Fall back to :ref:`Client-Initiated Bootstrap <bs>`
     - Cancel observation

   * - **Internal error**
     - Fall back to :ref:`Client-Initiated Bootstrap <bs>`
     - Cancel observation if "Notification storing" is disabled


Send Operation Errors
^^^^^^^^^^^^^^^^^^^^^

As shown in :doc:`../BasicClient/BC-Send` example, Send is the only operation
that the application calls implicitly, and thus the only one with a dedicated
`finish handler <../api/api_generated/typedef_lwm2m__send_8h_1a60092ffcd1721e55cf3a753c2a4271f3.html>`_.
Please see it's documentation for details on how are errors during Send operation
reported, and how can they be handled (i.e. see `anjay_send_deferrable() <../api/api_generated/function_lwm2m__send_8h_1aee46920732fb6e207315004ec5cc3955.html>`_).

.. _err-abort-reg:

Registration retries and abort
------------------------------

This condition corresponds to the registration failure as used in the
`Bootstrap and LwM2M Server Registration Mechanisms
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#6-2-1-1-0-6211-Bootstrap-and-LwM2M-Server-Registration-Mechanisms>`_
section of LwM2M Core TS 1.1.

.. figure:: _images/registration-sequence-diagram.svg
   :width: 100%

   Registration Sequence Diagram


If Anjay is compiled with support for LwM2M 1.1 or LwM2M 1.2, the retry procedures
will be performed as described in that section of the LwM2M Technical Specification.
The parameters are stored in the appropriate Server Object instance. There is also
a set of default values listed in the
`"Registration Procedures Default Values" table
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#Table-6211-1-Registration-Procedures-Default-Values>`_.
According to this configuration, further failures may result in the "abort all
communication" [#a]_ or "fall back to :ref:`Client-Initiated Bootstrap <bs>`"
condition.

In builds of Anjay that do not support LwM2M 1.1 or 1.2, the "abort registration"
condition is equivalent with the "fall back to :ref:`Client-Initiated Bootstrap <bs>`"
condition.


Retry Mechanisms
^^^^^^^^^^^^^^^^

The retry mechanism consists of two levels: attempts within a sequence and 
multiple sequences: 

.. list-table:: 
   :widths: 15 35 50
   :header-rows: 1

   * - Resource ID
     - Parameter Name
     - Description
   * - 17
     - Communication Retry Count
     - The number of successive communication attempts within a single retry sequence before it's considered failed.
   * - 18
     - Communication Retry Timer
     - The delay (in seconds) between successive attempts within a sequence. This value is multiplied by 2^(Current Attempt-1) to create exponential back-off.
   * - 19
     - Communication Sequence Delay Timer
     - The delay between successive communication sequences after exhausting all retry attempts.
   * - 20
     - Communication Sequence Retry Count
     - The maximum number of retry sequences before a registration attempt is considered failed.

Error Recovery and Fallback
^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Bootstrap on Registration Failure** (Resource 16): If set to true, indicates
the client should re-bootstrap when registration is explicitly rejected or
considered failed. If false, the client continues with registration attempts as
dictated by the other resource settings. Default: true   

The retry flow follows these procedures:  

 - Single Ordered/Unordered Server Procedure: Individual server registration with retry logic
 - Within each sequence: attempts are made up to Communication Retry Count with exponential back-off
 - Between sequences: delay by Communication Sequence Delay Timer
 - After exhausting all sequences: trigger bootstrap if configured

When Anjay is compiled with support for LwM2M version 1.1 or 1.2, these retry procedures
are automatically performed according to settings in the Server object instance. 

**Configuration structure Fields**

The ``anjay_server_instance_t`` structure includes these LwM2M 1.1 fields
(as pointer fields for optional resources). To configure these resources, set
the pointer fields to point to actual values when creating a server instance:

.. code-block:: c
    :emphasize-lines: 5-9

    const anjay_server_instance_t server_instance = {
        .ssid = 1,
        .lifetime = 60,
        .binding = "U",
        .bootstrap_on_registration_failure = &(const bool){true},
        .communication_retry_count = &(const uint32_t){3},
        .communication_retry_timer = &(const uint32_t){60},
        .communication_sequence_retry_count = &(const uint32_t){2},
        .communication_sequence_delay_timer = &(const uint32_t){3600}
    };

Skipping initialization of these resources leaves them undefined,
causing Anjay to perform single sequence, with single attempt.

**Connect operation errors** can occur for several reasons, the most common
being:

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

Note that all of the operations mentioned above (domain name resolution and TCP)
as well as :ref:`DTLS Handshake <dtls_hs>` are performed synchronously and will
block all other operations.

If any of the above conditions happen, Anjay will, by default, fall back to
:ref:`Client-Initiated Bootstrap <bs>` or, if the attempt was to connect to
a Bootstrap Server, cease any attempts to communicate with it.

.. note::
   Unless regular Server accounts are available, this will mean abortion of all
   communication [#a]_.

This behavior can be changed by enabling the
`connection_error_is_registration_failure <../api/structanjay__configuration.html#adcc95609ca645a5bd6a572f4c99a83fb>`_.
In that case, connection errors will trigger :ref:`err-abort-reg`, and thus
the automatic retry flow described in "Bootstrap and LwM2M Server Registration
Mechanisms" section mentioned above will be respected.

.. note::

    **Registration Priority Order** (Resource 13) and
    **Registration Failure Block** (Resource 15) are **not** supported in Anjay.


Registration Update
^^^^^^^^^^^^^^^^^^^

To prevent the device from missing the Registration Expiration deadline
(e.g., if an Update message gets lost and requires retransmission),
Anjay schedules Update messages based on the Lifetime value itself:

 - If `Lifetime` < `2 * MAX_TRANSMIT_WAIT`, the update is scheduled at `Lifetime / 2`.
 - Otherwise, the update is scheduled at `Lifetime - MAX_TRANSMIT_WAIT`.

(Note: MAX_TRANSMIT_WAIT is a standard CoAP transmission parameter, which defaults to 93 seconds).

.. _bs:

Client-Initiated Bootstrap
--------------------------

Client-Initiated Bootstrap will be performed only if all the following
preconditions are met:

 * a Bootstrap Server Account exists
 * no other LwM2M Server has usable connection
 * the Bootstrap Server Account has not aborted due to previous errors

Otherwise, further communication with the server with which the operation failed
will be aborted. This may cause `anjay_all_connections_failed() <../api/api_generated/function_core_8h_1a4329b620520c565fd61b526ba760e59f.html>`_
to start returning ``true`` if that was the last operational connection.
Connection can be retried by calling `anjay_enable_server() <../api/api_generated/function_core_8h_1abc4b554e51a56da874238f3e64bff074.html>`_
or `anjay_transport_schedule_reconnect() <../api/api_generated/function_core_8h_1ad895be5694083d015ffcd8d0b87d0b2a.html>`_.


Other error conditions
----------------------

* Errors while receiving an incoming request, or any unrecognized incoming
  packets, will be ignored

* Errors during `anjay_download()
  <../api/api_generated/function_download_8h_1a7a4d736c0a4ada68f0770e5eb45a84ce.html>`_ data transfers
  will be passed to the appropriate callback handler, see the `documentation to
  anjay_download_finished_handler_t
  <../api/api_generated/typedef_download_8h_1a44f0d37ec9ef8123bf88aa9ea9ee7291.html>`_ for details.
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
         <../api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration13udp_tx_paramsE>`_
         and `sms_tx_params
         <../api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration13sms_tx_paramsE>`_
         (in versions that include the SMS feature) fields in
         `anjay_configuration_t <../api/api_generated/structanjay__configuration.html#_CPPv419anjay_configuration>`_.

.. [#hs] To prevent infinite loop of handshakes, DTLS handshake is only retried
         if the failed operation was **not** performed immediately after the
         previous handshake; otherwise the behavior described in "Timeout
         (NoSec)" is used.

.. [#a]  Communication with all servers will be aborted and
         `anjay_all_connections_failed()
         <../api/api_generated/function_core_8h_1a4329b620520c565fd61b526ba760e59f.html>`_ will start
         returning ``true``. Operation can be restored by calling
         `anjay_transport_schedule_reconnect()
         <../api/api_generated/function_core_8h_1ad895be5694083d015ffcd8d0b87d0b2a.html>`_ or
         `anjay_enable_server()
         <../api/api_generated/function_core_8h_1abc4b554e51a56da874238f3e64bff074.html>`_.
