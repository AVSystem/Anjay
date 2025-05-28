..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
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
