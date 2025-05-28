..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

LwM2M Gateway Introduction
==========================

.. contents:: :local:

Overview
--------

Why the LwM2M gateway was created
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As the IoT continues to grow, so does the need for managing a wide range of
connected devices. Many of these devices operate in constrained network
environments or have limited processing power. Some IoT devices do not natively
support the LwM2M protocol.

To address this challenge, the Open Mobile Alliance (OMA) introduced the `(/25)
LwM2M Gateway Object <https://www.openmobilealliance.org/release/LwM2M_Gateway/
V1_1_1-20240312-A/OMA-TS-LWM2M_Gateway-V1_1_1-20240312-A.pdf>`_. This extension
to the LwM2M specification allows an LwM2M Server to manage IoT End Devices
through an intermediary **LwM2M Gateway** in a standardized and LwM2M-compliant
way.

How the LwM2M Gateway works
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The **LwM2M Gateway** acts as a bridge between IoT End Devices and the LwM2M
Server. It translates the data models of connected End Devices and presents
them to the Server, ensuring seamless interoperability. This eliminates the
need for native LwM2M support on End Devices while still allowing robust device
management, monitoring, and control.

The Gateway allows for communication with IoT End Devices over various
interfaces, for example **Bluetooth Low Energy (BLE)**, **Wi-Fi**, or **Serial
protocols** such as UART and RS-485.

Each End Device is identified by the LwM2M Server using the **Device Identifier
Resource**, which is stored in separate ``/25`` **LwM2M Gateway Object**
instances. The Server accesses these devices using a **prefix in the URI path**.

LwM2M Gateway in Anjay
^^^^^^^^^^^^^^^^^^^^^^

The **LwM2M Gateway** in Anjay provides a scalable and efficient solution for
managing End Devices that lack the hardware capabilities for a full LwM2M
communication stack. By leveraging the standardized Gateway Object (``/25``),
the Gateway enables communication between an LwM2M Server and connected
devices.

.. note::
    In accordance with the specification, each connected device must be
    **registered separately** in the Anjay client by the Gateway Device
    application.

This design simplifies the integration of cost-effective, resource-constrained
devices into IoT ecosystems while maintaining compliance with LwM2M
specifications.

Purpose and benefits
--------------------

The **LwM2M Gateway** helps overcome key challenges in IoT deployments by
providing an efficient, scalable, and secure way to manage connected devices.

**Key Benefits:**

- **Optimized Resource Use and Cost Savings**

  The Gateway offloads internet communication and protocol handling from End
  Devices, reducing their hardware requirements and enabling the use of
  simpler, cost-effective devices.

- **Seamless Communication & Interoperability**

  By leveraging the LwM2M protocol, the Gateway ensures standardized
  interaction with End Devices, simplifying integration across different IoT
  ecosystems.

- **Scalability for Diverse Applications**

  The Gateway supports various device types and data models, making it
  suitable for industries such as Smart metering, Asset tracking and
  Environmental monitoring.

- **Support for Legacy Devices**

  Devices that were not originally designed for LwM2M can still be managed and
  integrated through the Gateway. This extends their lifecycle and improves
  operational efficiency without requiring major hardware modifications.

- **Local Control & Edge Computing**

  Applications running on the Gateway can process data locally and manage
  connected devices, even during network outages, ensuring continuous
  operation.

- **Enhanced Security**

  - The connection between End Devices and the Gateway (southbound) is often
    within a trusted local zone.
  - Many constrained devices lack built-in security features. The Gateway
    ensures that all northbound communication (to the LwM2M Server over a
    public network) remains secure.

Features and limitations
------------------------

Supported LwM2M operations
^^^^^^^^^^^^^^^^^^^^^^^^^^

The LwM2M Gateway supports the following operations on End Devices:

- **Read**
- **Write**
- **Execute**
- **Discover**
- **Write-Attributes**
- **Observe/Notify**
- **Cancel Observe**
- **Send**

LwM2M Gateway limitations
^^^^^^^^^^^^^^^^^^^^^^^^^

The LwM2M Gateway simplifies the management of non-LwM2M devices but does not
perform real-time protocol translation. Instead, it abstracts data models and
provides a standardized interface for device management.

While the Gateway handles communication between End Devices and the LwM2M
Server, **developers are responsible for implementing specific drivers and
object definitions** for their End Devices.

Unsupported features
^^^^^^^^^^^^^^^^^^^^
- **Composite operations** targeting End Devices are not supported.
- ``/25`` **Gateway Object instances** and **End Device Data Models** are not
  included in the LwM2M Client Bootstrap Information.
- The **Firmware Update (FOTA) mechanism**, as described in the LwM2M
  specification and implemented in the ``/5`` **Firmware Object**, is currently
  not supported.

Enabling LwM2M Gateway support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

LwM2M Gateway functionality can be enabled at compile-time by enabling the
``ANJAY_WITH_LWM2M_GATEWAY`` macro in the ``anjay_config.h`` file or, if using
CMake, by enabling the corresponding ``WITH_LWM2M_GATEWAY`` CMake option.
