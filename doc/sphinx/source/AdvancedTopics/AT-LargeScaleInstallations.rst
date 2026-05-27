..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Large scale installations
=========================

Deploying LwM2M at scale requires careful management of network traffic and server
resources. When thousands of devices attempt to communicate simultaneously - 
for instance, after a widespread power outage - it can lead to a "thundering herd"
effect, potentially overwhelming the LwM2M Server and the underlying network.

There are several mechanisms defined in LwM2M and implemented in Anjay that
help distribute load in time and optimize bandwidth.


Avoiding Traffic Bursts
-----------------------

To prevent massive spikes in traffic, it is crucial to introduce randomness into 
device communication.

**Bootstrap Holdoff**
    The :ref:`Bootstrap Holdoff <bs_holdoff>` timer prevents devices 
    from performing a Bootstrap request immediately after a failure or reboot. 
    By distributing these requests in time with a randomized initial Bootstrap
    Holdoff (e.g. in range of 0 to 10 seconds), you ensure that the Bootstrap
    Server can process them sequentially rather than all at once.

**Initial Registration Delay**
    This timer provides additional jitter to device fleets that perform the
    first registration after a Bootstrap. Setting this value to a randomized 
    value within a defined range spreads the traffic comming to the LwM2M Server
    out.
    
    It is applied only to the first Register attempt after adding a server via
    Bootstrap or API. If it fails, **Registration Retry** mechanism is applied. 

**Registration Retry**
    There is a :ref:`Registration Retries <err-abort-reg>` mechanism defined in
    LwM2M Specification and implemented in Anjay. You can define the parameters
    related to the number of retries and delays between them with ``/0`` Server
    Object Resources.

**CoAP Transmission Parameters**
    LwM2M relies on CoAP, which uses an exponential backoff mechanism. A key 
    parameter here is the :ref:`ACK_RANDOM_FACTOR <coap-retransmission-parameters>`, 
    which adds jitter to retransmission timeouts. This prevents synchronized 
    retry loops across the entire fleet of devices.


Event Loop Management
---------------------

The vast majority of Anjay's background tasks and network operations are executed
within the `Anjay event loop`.
While a basic implementation is sufficient for simple scenarios, at a larger scale,
the way an application handles this event loop - especially regarding connection
errors, flow control, and reconnect strategies - becomes critical to the overall
stability of the system.

Anjay provides built-in event loop implementations that are suitable for most use cases:

* `anjay_event_loop_run() <../api/api_generated/function_core_8h_1a95c229caf3ee8ce7de556256f4307507.html>`_ -
  The standard loop that exits when all connections fail and there are no more
  retries or fallbacks.
* `anjay_event_loop_run_with_error_handling() <../api/api_generated/function_core_8h_1ad8fb214939b8c4732d9eab048151d195.html>`_ -
  A variant that attempts a reconnect when no server could be reached automatically.

For specialized hardware or specific integration requirements, you may need 
to implement a :doc:`AT-CustomEventLoop`.

To further customize the logic related to reconnections and error handling, the application
may call several Anjay functions or set specific callbacks to fine-tune its behavior.
They are defined in the 
`core.h file <../api/api_generated/file_include_public_anjay_core.h.html>`_, 
and the most important ones are:

* `anjay_all_connections_failed <../api/api_generated/function_core_8h_1a4329b620520c565fd61b526ba760e59f.html>`_
* `anjay_transport_schedule_reconnect <../api/api_generated/function_core_8h_1ad895be5694083d015ffcd8d0b87d0b2a.html>`_
* `anjay_get_server_connection_status <../api/api_generated/function_core_8h_1a50251bc6b432adfd446a287ed77a6d9f.html>`_
* `anjay_configuration_t::server_connection_status_cb <../api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration27server_connection_status_cbE>`_


Confirmable vs Non-Confirmable Notifications
--------------------------------------------

Notifications (Information Reporting) can be sent as either **Confirmable (CON)**
or **Non-confirmable (NON)** CoAP messages. Choosing the right type is critical 
for minimizing server load and bandwidth.

* **Non-confirmable (NON):** These messages do not require an acknowledgement (ACK) 
  from the server. This significantly reduces the total number of packets 
  exchanged, lowering both the bandwidth usage and the processing overhead 
  on the LwM2M Server. It is however an unreliable method of communication,
  and is suitable for non-critical data that can be lost without significant
  consequences (e.g. periodic sensor readings).
* **Confirmable (CON):** These require an ACK. If the ACK is not received, 
  the device will retransmit the message, consuming more network resources.
  Unlike NON messages, CON notifications are suitable for critical data that
  must be delivered to the server (e.g. alarm notifications).

**Anjay's Behavior**

By default, Anjay sends almost all notifications as **NON**. To maintain 
compliance with `RFC 7641 <https://tools.ietf.org/html/rfc7641>`_, Anjay 
automatically sends a **CON** notification once every 24 hours to verify that
the server is still interested in the resource.

* **Global Configuration:** Use the `confirmable_notifications <../api/api_generated/structanjay__configuration.html#_CPPv4N19anjay_configuration25confirmable_notificationsE>`_ field 
  in the ``anjay_configuration_t`` struct to set the default behavior.
* **Fine-grained Control:** The LwM2M ``con`` attribute (Section 7.3.2 
  of the LwM2M 1.2 Core Specification) can be set per-resource to force confirmable
  notifications for critical data only. Be sure to enable ``ANJAY_WITH_CON_ATTR``
  configuration switch in compile time.
