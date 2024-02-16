..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
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
5.xx error code is delivered, or if an attempt to deliver a notification times
out.

In Anjay 3.4.x and earlier, this cancellation (which involves calling the
``avs_coap_observe_cancel_handler_t`` callback) was performed *before* calling
the ``avs_coap_delivery_status_handler_t`` callback for the specific
notification. Since Anjay 3.5.0, this order is reversed, so any code that relies
on this logic may break.

This change is only relevant if you are using ``avs_coap`` APIs directly (e.g.
when communicating over raw CoAP protocol) and in case of notifications intended
to be delivered as confirmable. The LwM2M Observe/Notify implementation in Anjay
has been updated accordingly.
