..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Resource definitions
====================

.. contents:: :local:

Package (/33629/x/0)
^^^^^^^^^^^^^^^^^^^^

:ID: 0
:Operations: W
:Instances: Single
:Mandatory: Yes
:Type: Opaque
:Range or Enumeration: \-
:Units: \-
:Description:
  Firmware package

Package URI (/33629/x/1)
^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 1
:Operations: RW
:Instances: Single
:Mandatory: Yes
:Type: String
:Range or Enumeration: 0..255
:Units: \-
:Description:
  URI from where the device can download the firmware package by an alternative
  mechanism. As soon as the device has received the Package URI it performs the
  download at the next practical opportunity. The URI format is defined in
  ``RFC 3986``. For example, ``coaps://example.org/firmware`` is a
  syntactically valid URI. The URI scheme determines the protocol to be used.
  For CoAP this endpoint MAY be an LwM2M Server but does not necessarily need to
  be. A CoAP server implementing block-wise transfer is sufficient as a server
  hosting a firmware repository and the expectation is that this server merely
  serves as a separate file server making firmware packages available to LwM2M
  Clients.

Update (/33629/x/2)
^^^^^^^^^^^^^^^^^^^

:ID: 2
:Operations: E
:Instances: Single
:Mandatory: Yes
:Type: \-
:Range or Enumeration: \-
:Units: \-
:Description:
  Updates firmware by using the firmware package stored in Package, or, by
  using the firmware downloaded from the Package URI. This Resource is only
  executable when the value of the State Resource is Downloaded.

  If multiple instances of the Advanced Firmware Update object are in the
  Downloaded state, the device MAY update multiple components in one go. In
  this case, the Linked Instances resource MUST list all other components that
  will be updated alongside the current one.

  The server MAY override this behavior by including an argument 0 in the
  Execute operation. If the argument is present with no value, the client MUST
  attempt to update only the component handled by the current instance. If the
  argument is present with a value containing a list of Advanced Firmware
  Update object instances specified as a Core Link Format (so that the argument
  may read, for example: ``0=</33629/1>,</33629/2>``), the client MUST attempt
  to update the components handled by the current instance and the instances
  listed in the argument, and MUST NOT attempt to update any other components.
  If the client is not able to satisfy such a request, the update process shall
  fail with the Update Result resource set to 13.

  If the downloaded packages are incompatible with at least one of the packages
  installed on other components, and compatible updates for them are not
  downloaded (i.e., the State resource in an instance corresponding to the
  conflicting component is not Downloaded), the update process shall also fail
  with the Update Result resource set to 13.

  When multiple components are upgraded as part of a single Update operation,
  the device SHOULD upgrade them in a transactional fashion (i.e., all are
  updated successfully, or all are reverted in case of error), and MUST perform
  the upgrade in a way that ensures that the device will not be rendered
  unbootable due to partial errors.

State (/33629/x/3)
^^^^^^^^^^^^^^^^^^

:ID: 3
:Operations: R
:Instances: Single
:Mandatory: Yes
:Type: Integer
:Range or Enumeration: 0..3
:Units: \-
:Description:
  Indicates current state with respect to this firmware update. This value is
  set by the LwM2M Client.

  | **0:** Idle (before downloading or after successful updating)
  | **1:** Downloading (The data sequence is on the way)
  | **2:** Downloaded
  | **3:** Updating

  If writing the firmware package to Package Resource has completed, or, if the
  device has downloaded the firmware package from the Package URI the state
  changes to Downloaded. The device MAY support packages containing code for
  multiple components in a single file, in which case downloading the package in
  any instance of the Advanced Firmware Update object that is valid for it,
  MUST set the State resource to 2 in instances handling all components that
  are affected by the downloaded package; if the State of any of such instances
  was different than 0, the package MUST be rejected and the Update Result
  resource set to 12.

  Writing an empty string to Package URI Resource or setting the Package
  Resource to **NULL** (``\0``), resets the Advanced Firmware Update State
  Machine: the State Resource value is set to Idle and the Update Result
  Resource value is set to 0. The device should remove the downloaded firmware
  package when the state is reset to Idle.

  When in Downloaded state, and the executable Resource Update is triggered,
  the state changes to Updating if the update starts immediately. For devices
  that support a user interface and the deferred update functionality, the user
  may be allowed to defer the firmware update to a later time. In this case,
  the state stays in the Downloaded state and the Update Result is set to 11.
  Once a user accepts the firmware update, the state changes to Updating. When
  the user deferred the update, the device will continue operations normally
  until the user approves the firmware update or an automatic update starts. It
  will not block any operation on the device.

  If the Update Resource failed, the state may return to either Downloaded or
  Idle depending on the underlying reason of update failure, e.g. Integrity
  Check Failure results in the client moving to the Idle state. If performing
  the Update or Cancel operation was successful, the state changes to Idle. The
  Advanced Firmware Update state machine is illustrated in the respective LwM2M
  specification.

Update Result (/33629/x/5)
^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 5
:Operations: R
:Instances: Single
:Mandatory: Yes
:Type: Integer
:Range or Enumeration: 0..13
:Units: \-
:Description:
  Contains the result of downloading or updating the firmware.

  | **0**: Initial value. Once the updating process is initiated (Download
    /Update), this Resource MUST be reset to Initial value.
  | **1:** Firmware updated successfully.
  | **2:** Not enough flash memory for the new firmware package.
  | **3:** Out of RAM during the downloading process.
  | **4:** Connection lost during the downloading process.
  | **5:** Integrity check failure for new downloaded package.
  | **6:** Unsupported package type.
  | **7:** Invalid URI.
  | **8:** Firmware update failed.
  | **9:** Unsupported protocol. An LwM2M client indicates the failure to
    retrieve the firmware package using the URI provided in the Package URI
    resource by writing the value 9 to the ``/33629/0/5`` (Update Result
    resource) when the URI contained a URI scheme unsupported by the client.
    Consequently, the LwM2M Client is unable to retrieve the firmware package
    using the URI provided by the LwM2M Server in the Package URI when it
    refers to an unsupported protocol.
  | **10:** Firmware update cancelled. A Cancel operation has been executed
    successfully.
  | **11:** Firmware update deferred.
  | **12:** Conflicting state. Multi-component firmware package download is
    rejected before entering the Downloaded state because it conflicts with an
    already downloaded package in a different object instance.
  | **13:** Dependency error. The Update operation failed because the component
    package requires some other component to be updated first or at the same
    time.

PkgName (/33629/x/6)
^^^^^^^^^^^^^^^^^^^^

:ID: 6
:Operations: R
:Instances: Single
:Mandatory: No
:Type: String
:Range or Enumeration: \-
:Units: \-
:Description:
  Name of the Firmware Package. If this resource is supported, it shall contain
  the name of the downloaded package when the State is 2 (Downloaded) or 3
  (Updating); otherwise it MAY be empty.

PkgVersion (/33629/x/7)
^^^^^^^^^^^^^^^^^^^^^^^

:ID: 7
:Operations: R
:Instances: Single
:Mandatory: No
:Type: String
:Range or Enumeration: \-
:Units: \-
:Description:
  Version of the Firmware package. If this resource is supported, it shall
  contain the version of the downloaded package when the State is 2
  (Downloaded) or 3 (Updating); otherwise it MAY be empty.

Firmware Update Protocol Support (/33629/x/8)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 8
:Operations: R
:Instances: Multiple
:Mandatory: No
:Type: Integer
:Range or Enumeration: 0..5
:Units: \-
:Description:
  This resource indicates what protocols the LwM2M Client implements to
  retrieve firmware packages. The LwM2M server uses this information to decide
  what URI to include in the Package URI. An LwM2M Server MUST NOT include a URI
  in the Package URI object that uses a protocol that is unsupported by the
  LwM2M client. For example, if an LwM2M client indicates that it supports CoAP
  and CoAPS then an LwM2M Server must not provide an HTTP URI in the Packet URI.
  The following values are defined by this version of the specification:

  | **0:** CoAP (as defined in ``RFC 7252``) with the additional support for
    block-wise transfer. CoAP is the default setting.
  | **1:** CoAPS (as defined in ``RFC 7252``) with the additional support for
    block-wise transfer
  | **2:** HTTP 1.1 (as defined in ``RFC 7230``)
  | **3:** HTTPS 1.1 (as defined in ``RFC 7230``)
  | **4:** CoAP over TCP (as defined in ``RFC 8323``)
  | **5:** CoAP over TLS (as defined in ``RFC 8323``)

  Additional values MAY be defined in the future. Any value not understood by
  the LwM2M Server MUST be ignored.

  The value of this resource SHOULD be the same for all instances of the
  Advanced Firmware Update object.

Firmware Update Delivery Method (/33629/x/9)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 9
:Operations: R
:Instances: Single
:Mandatory: Yes
:Type: Integer
:Range or Enumeration: 0..2
:Units: \-
:Description:
  The LwM2M Client uses this resource to indicate its support for transferring
  firmware packages to the client either via the Package Resource (=push) or via
  the Package URI Resource (=pull) mechanism.

  | **0:** Pull only
  | **1:** Push only
  | **2:** Both. In this case the LwM2M Server MAY choose the preferred
    mechanism for conveying the firmware package to the LwM2M Client.

  The value of this resource SHOULD be the same for all instances of the
  Advanced Firmware Update object.

Cancel (/33629/x/10)
^^^^^^^^^^^^^^^^^^^^

:ID: 10
:Operations: E
:Instances: Single
:Mandatory: No
:Type: \-
:Range or Enumeration: \-
:Units: \-
:Description:
  Cancels firmware update. Cancel can be executed if the device has not
  initiated the Update process. If the device is in the process of installing
  the firmware or has already completed installation it MUST respond with
  Method Not Allowed error code. Upon successful Cancel operation, Update
  Result Resource is set to 10 and State is set to 0 by the device.

Severity (/33629/x/11)
^^^^^^^^^^^^^^^^^^^^^^

:ID: 11
:Operations: RW
:Instances: Single
:Mandatory: No
:Type: Integer
:Range or Enumeration: 0..2
:Units: \-
:Description:
  Severity of the firmware package.

  | **0:** Critical
  | **1:** Mandatory
  | **2:** Optional

  This information is useful when the device provides an option for the
  deferred update. Default value is 1.

Last State Change Time (/33629/x/12)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 12
:Operations: R
:Instances: Single
:Mandatory: No
:Type: Time
:Range or Enumeration: \-
:Units: \-
:Description:
  This resource stores the time when the State resource is changed. Device
  updates this resource before making any change to the State.

Maximum Defer Period (/33629/x/13)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 13
:Operations: RW
:Instances: Single
:Mandatory: No
:Type: Unsigned Integer
:Range or Enumeration: \-
:Units: s
:Description:
  The number of seconds a user can defer the software update. When this time
  period is over, the device will not prompt the user for update and install it
  automatically. If the value is 0, a deferred update is not allowed.

Component Name (/33629/x/14)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 14
:Operations: R
:Instances: Single
:Mandatory: No
:Type: String
:Range or Enumeration: \-
:Units: \-
:Description:
  Name of the component handled by this instance of the Advanced Firmware
  Update object.

  This should be a name clearly identifying the component for both humans and
  machines. The syntax of these names is implementation-specific, but might
  refer to terms such as “bootloader”, “application”, “modem firmware” etc.

Current Version (/33629/x/15)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 15
:Operations: R
:Instances: Single
:Mandatory: Yes
:Type: String
:Range or Enumeration: \-
:Units: \-
:Description:
  Version number of the package that is currently installed and running for the
  component handled by this instance of the Advanced Firmware Update object.

  For the main component (the one that contains code for the core part of the
  device's functionality), this value SHOULD be the same as the Firmware
  Version resource in the Device object (``/3/0/3``).

Linked Instances (/33629/x/16)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 16
:Operations: R
:Instances: Multiple
:Mandatory: No
:Type: Objlnk
:Range or Enumeration: \-
:Units: \-
:Description:
  When multiple instances of the Advanced Firmware Update object are in the
  Downloaded state, this resource shall list all other instances that will be
  updated in a batch if the Update resource is executed on this instance with
  no arguments. Each of the instances listed MUST be in the Downloaded state.

  The resource MUST NOT contain references to any objects other than the
  Advanced Firmware Update object.

Conflicting Instances (/33629/x/17)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:ID: 17
:Operations: R
:Instances: Multiple
:Mandatory: No
:Type: Objlnk
:Range or Enumeration: \-
:Units: \-
:Description:
  When the download or update fails and the Update Result resource is set to 12
  or 13, this resource MUST be present and contain references to the Advanced
  Firmware Update object instances that caused the conflict.

  In other states, this resource MAY be absent or empty, or it MAY contain
  references to the Advanced Firmware Update object instances which are in a
  state conflicting with the possibility of successfully updating this
  instance.

  The resource MUST NOT contain references to any objects other than the
  Advanced Firmware Update object.
