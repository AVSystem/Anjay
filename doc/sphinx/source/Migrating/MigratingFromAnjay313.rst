..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.13
=========================

.. contents:: :local:

.. highlight:: c

Dropping built-in support for TinyDTLS as a crypto backend
----------------------------------------------------------

Built-in support for TinyDTLS as a (D)TLS backend has been removed.

In previous versions of Anjay, TinyDTLS could be selected as a lightweight
crypto backend, primarily intended for constrained environments. However, due
to its limited feature set, lack of ongoing maintenance, and incompatibility
with newer security requirements and LwM2M specifications, it is no longer
supported.

Users are now required to use one of the supported and actively maintained
(D)TLS backends, such as Mbed TLS or OpenSSL (via avs_commons abstraction
layer) or creating there own
:doc:`Custom (D)TLS layer <../PortingGuideForNonPOSIXPlatforms/CustomTLS>`.

Changing Server Initiated Bootstrap behavior
--------------------------------------------

Previously, Anjay did not use the Client Hold Off Time resource (``/1/x/11``)
during Server Initiated Bootstrap. This was because the description of
``/1/x/11`` states that its value should be used during Client Initiated
Bootstrap.

The LwM2M specification also states that Server Initiated Bootstrap causes
the LwM2M Client to enter Client Initiated Bootstrap. For consistency with
this behavior, Anjay now applies the Client Hold Off Time resource in this
scenario as well.

To avoid additional delay, adjust resource ``/1/x/11`` as needed.
