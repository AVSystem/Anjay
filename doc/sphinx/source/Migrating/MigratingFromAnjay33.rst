..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.3
========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Since Anjay 3.4.0 and avs_commons 5.4.0, time handling in avs_sched and avs_coap
has been refactored, which slightly redefined existing APIs.

These changes should not require any changes to user code in typical usage, but
may break some edge cases, especially on platforms where the system clock has a
low resolution and you are scheduling custom jobs through the avs_sched module.

Refactor of time handling in avs_sched and avs_coap
---------------------------------------------------

It is now enforced more strictly that time-based events shall happen when the
clock reaches *at least* the expected value. Previously, the tasks scheduled via
avs_sched were executed only when the clock reached a value *later* than the
scheduled job execution time.

This change will have no impact on your code if your platform has enough clock
resolution so that two subsequent calls to ``avs_time_real_now()`` or
``avs_time_monotonic_now()`` will *always* return different values. As a rule of
thumb, this should be the case if your clock has a resolution no worse than
about 1-2 orders of magnitude smaller than the CPU clock. For example, for a
100 MHz CPU, a clock resolution of around 100-1000 ns (i.e., 1-10 MHz) should be
sufficient, depending on the specific architecture.

If your clock has a lower resolution, you may observe the following changes:

* ``anjay_sched_run()`` is now properly guaranteed to execute at least one job
  if the time reported by ``anjay_sched_time_to_next()`` passed. Previously this
  could require waiting for another change of the numerical value of the clock,
  which could cause undesirable active waiting in the event loop. This is the
  motivating factor in introducing these changes.
* Jobs scheduled using ``AVS_SCHED_NOW()`` during an execution of
  ``anjay_sched_run()`` before the numerical value of the clock changes, *will*
  be executed during the same run. The previous behavior more strictly enforced
  the policy to not execute such jobs in the same run.

If you are scheduling custom jobs through the avs_sched module, you may want or
need to modify their logic accordingly to accommodate for these changes. In most
typical use cases, no changes are expected to be necessary.

Changed flow of cancelling observations in case of errors
---------------------------------------------------------

CoAP observations are implicitly cancelled if a notification bearing a 4.xx or
5.xx error code is delivered.

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
