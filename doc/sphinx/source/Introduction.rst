..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Introduction
============

**Anjay** is a library that implements the *OMA Lightweight Machine to Machine*
protocol, including the necessary subset of CoAP.

The project has been created and is actively maintained by
`AVSystem <https://www.avsystem.com>`_.

Protocol support status
-----------------------

The basis for this implementation was the *OMA Lightweight Machine to Machine
Technical Specification, Version 1.0.2 - 9 Feb 2018*, document number
``OMA-TS-LightweightM2M-V1_0_2-20180209-A``. In case of ambiguities, existing
implementations were considered as a reference.

A version that includes support for version 1.1 of the specification
(``OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A`` and
``OMA-TS-LightweightM2M_Transport-V1_1_1-20190617-A``), including Composite
operations, Send method, SenML JSON and CBOR data formats, TCP, SMS and NIDD
bindings, is :doc:`available commercially <Commercial_support>`.

The following features are **supported**:

- Bootstrap - full support
- Client Registration - full support
- Device Management and Service Enablement - full support
- Information Reporting - full support

- Data formats

  - Plain Text
  - Opaque
  - TLV
  - JSON (output only)

- Security

  - DTLS with Certificates, if supported by backend TLS library
  - DTLS with PSK, if supported by backend TLS library
  - NoSec mode

- Mechanism

  - Support for UDP Binding

The following features are **not implemented**:

- Parsing of JSON format
- RPK DTLS mode
- Smartcard support

Technical information
---------------------

Anjay is written in standards-compliant C99. It partly relies on some POSIX
library extensions, although it can be easily ported to non-POSIX platforms.

Its only external dependency is the open source
`AVSystem Commons Library <https://github.com/AVSystem/avs_commons>`_. That
library in turn may additionally depend either on
`OpenSSL <https://www.openssl.org/>`_ or `mbedTLS <https://tls.mbed.org/>`_
or `tinydtls <https://projects.eclipse.org/projects/iot.tinydtls>`_ for DTLS
support.

To build Anjay from source, `CMake <http://www.cmake.org/>`_ version 3.4.0 or
newer is necessary.
