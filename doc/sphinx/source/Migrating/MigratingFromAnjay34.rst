..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.4
========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Since Anjay 3.5.0, the code flow of delivering notifications has been
refactored so that user handler callbacks may be called in a different order.
This only affects direct users of ``avs_coap`` APIs (e.g. when communicating
over raw CoAP protocol).

Changed flow of cancelling observations in case of errors
---------------------------------------------------------

CoAP observations are implicitly cancelled if a notification bearing a 4.xx or
5.xx error code is delivered. If an attempt to deliver a confirmable
notification times out, CoAP observation is not cancelled by default anymore.
It can be adjusted by ``WITH_AVS_COAP_OBSERVE_CANCEL_ON_TIMEOUT``.

In Anjay 3.4.x and earlier, this cancellation (which involves calling the
``avs_coap_observe_cancel_handler_t`` callback) was performed *before* calling
the ``avs_coap_delivery_status_handler_t`` callback for the specific
notification. Since Anjay 3.5.0, this order is reversed, so any code that relies
on this logic may break.

This change is only relevant if you are using ``avs_coap`` APIs directly (e.g.
when communicating over raw CoAP protocol) and in case of notifications intended
to be delivered as confirmable. The LwM2M Observe/Notify implementation in Anjay
has been updated accordingly.

Addition of resource_instance_remove handler
--------------------------------------------

LwM2M TS 1.2 introduced possibility to delete a Resource Instance, so
one additional data model handler had to be added.

New ``resource_instance_remove`` handler (and its associated
``anjay_dm_resource_instance_remove_t`` function type) has been introduced. It
is analogous to the ``instance_remove`` handler; its job is to remove a specific
Resource Instance from a multiple-instance Resource.

Its implementation is required in objects that include at least one writeable
multiple-instance Resource, if the client application aims for compliance with
LwM2M 1.2.

Limiting the maximum Hold Off Time for Bootstrap
------------------------------------------------

A limit has been introduced on the maximum Hold Off Time (LwM2M Security Object,
Resource ID: 11) to avoid excessive delays when initiating the Bootstrap process.
Previously, the upper bound was implicitly 120 seconds, but it is now explicitly
limited to **20 seconds by default**.

This behavior can be customized at build time using the ``MAX_HOLDOFF_TIME``
macro.

Handling of Trust Store for certain Certificate Usages settings
---------------------------------------------------------------  

avs_commons 5.5.0 that is used by Anjay 3.11.0 does not pass the configured Trust
Store (whether manually provided or acquired through the EST process) to Mbed TLS
when the certificate usage is set to DANE-TA or DANE-EE, if the Data Model already
contains a Server certificate intended for verifying the Server during a secure
connection.

If the Server certificate is missing from the Data Model, Anjay falls back to
PKIX verification, provided that a Trust Store is available, even when the
certificate usage is set to DANE-TA or DANE-EE.

For more information on how Anjay manages the Trust Store and the Certificate
Usage resource, see
:doc:`Certificate Usage <../AdvancedTopics/AT-CertificateUsage>`.

Python environment isolation
----------------------------

All Python-based tools (e.g. integration tests) must be executed within a
Python virtual environment. See :doc:`/Tools/VirtualEnvironments` for more
information.
