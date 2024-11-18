..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.7
========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Since Anjay 3.8.0, confirmable notifications are not cancelled anymore in case
of a timeout.

Changed flow of cancelling observations in case of timeout
----------------------------------------------------------

If an attempt to deliver a confirmable notification times out, CoAP observation
is not cancelled by default anymore. It can be adjusted by
``WITH_AVS_COAP_OBSERVE_CANCEL_ON_TIMEOUT``.
The LwM2M Observe/Notify implementation in Anjay has been updated accordingly.
