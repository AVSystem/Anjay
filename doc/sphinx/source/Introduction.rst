..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Introduction
============

.. attention::

   With release of Anjay 3.10.0, the `library's license terms have changed
   <https://github.com/AVSystem/Anjay/blob/master/LICENSE>`_. Please make sure
   that you have reviewed it before updating to the new major release.

**Anjay** is a library that implements the *OMA Lightweight Machine to Machine*
protocol, including the necessary subset of CoAP.

The project has been created and is actively maintained by
`AVSystem <https://www.avsystem.com>`_.

Protocol support status
-----------------------

The basis for this implementation were the following documents:

- *Lightweight Machine to Machine Technical Specification: Core*,
  document number ``OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A``
- *Lightweight Machine to Machine Technical Specification: Transport Bindings*,
  document number ``OMA-TS-LightweightM2M_Transport-V1_1_1-20190617-A``.

In case of ambiguities, existing implementations were considered as a reference.

The following features are **supported**:

- Bootstrap - full support

  - Enrollment over Secure Transport and smart card bootstrap are
    :doc:`available commercially <CommercialFeatures>`

- Client Registration - full support
- Device Management and Service Enablement - full support
- Information Reporting - full support

- Data formats

  - Plain Text
  - Opaque
  - CBOR
  - TLV
  - SenML JSON
  - SenML CBOR
  - LwM2M JSON (output only)

- Security

  - DTLS with Certificates, if supported by backend TLS library
  - DTLS with PSK, if supported by backend TLS library
  - NoSec mode
  - Support for Hardware Security Modules (:doc:`available commercially <CommercialFeatures>`)

- Transport

  - Support for UDP Binding
  - Support for TCP Binding
  - Support for SMS Binding (:doc:`available commercially <CommercialFeatures>`)
  - Support for NIDD Binding (:doc:`available commercially <CommercialFeatures>`)

The following features are **not implemented**:

- RPK DTLS mode
- LwM2M JSON (input)

Technical information
---------------------

Anjay is written in standards-compliant C99. It partly relies on some POSIX
library extensions, although it can be easily ported to non-POSIX platforms.

Some optional features require C11's ``stdatomic.h`` header to be available
(``ANJAY_WITH_EVENT_LOOP``, ``AVS_COMMONS_COMPAT_THREADING_WITH_ATOMIC_SPINLOCK``).

Its only external dependency is the open source
`AVSystem Commons Library <https://github.com/AVSystem/avs_commons>`_. That
library in turn may additionally depend either on
`OpenSSL <https://www.openssl.org/>`_ or `Mbed TLS <https://tls.mbed.org/>`_
or `TinyDTLS <https://projects.eclipse.org/projects/iot.tinydtls>`_ for DTLS
support.

To build Anjay from source, `CMake <https://www.cmake.org/>`_ version 3.16 or
newer is necessary.

Deprecated and experimental features
------------------------------------

Anjay's Doxygen documentation contains, among others, **@deprecated** and
**@experimental** tags:

- features tagged with **@deprecated** are no longer supported and will be
  deleted in the future versions,
- features tagged with **@experimental** may contain changes (in the future
  versions) that will break their backward compatibility.
