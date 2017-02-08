Introduction
============

**Anjay** is a library that implements the *OMA Lightweight Machine to Machine*
protocol, including the necessary subset of CoAP.

The project has been created and is actively maintained by
`AVSystem <https://www.avsystem.com>`_.

As the protocol itself is still in a draft stage, the current version might not
exactly reflect the final specification once it is released. However, we are
taking great care to achieve maximum possible interoperability with other
existing implementations.

Protocol support status
-----------------------

The basis for this implementation was the *OMA Lightweight Machine to Machine
Technical Specification, Draft Version 1.0 - 13 Jan 2017*, document number
``OMA-TS-LightweightM2M-V1_0-20170113-D``. In case of ambiguities, existing
implementations were considered as a reference.

The following features are **supported**:

- Bootstrap - full support
- Client Registration - full support
- Device Management and Service Enablement - full support
- Information Reporting - full support

- Data formats

  - Plain Text
  - Opaque
  - TLV

- Security

  - DTLS with Certificates, if supported by backend TLS library
  - DTLS with PSK, if supported by backend TLS library
  - NoSec mode

- Mechanism

  - Support for UDP Binding

The following features are **not implemented**:

- Support of JSON format
- RPK DTLS mode
- Smartcard support
- SMS Binding

Technical information
---------------------

Anjay is written in standards-compliant C99. It partly relies on some POSIX
library extensions, although it can be easily ported to non-POSIX platforms.

Its only external dependency is the open source
`AVSystem Commons Library <https://github.com/AVSystem/avs_commons>`_. That
library in turn may additionally depend either on
`OpenSSL <https://www.openssl.org/>`_ or `mbedTLS <https://tls.mbed.org/>`_
for DTLS support.

To build Anjay from source, `CMake <http://www.cmake.org/>`_ version 2.8.1 or
newer is necessary.
