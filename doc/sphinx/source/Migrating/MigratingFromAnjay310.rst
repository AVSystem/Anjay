..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.10
=========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

Starting from Anjay 3.11.0, the behavior regarding the maximum Hold Off Time for
bootstrap connections has changed.

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
