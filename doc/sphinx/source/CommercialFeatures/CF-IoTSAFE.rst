..
   Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

IoT SAFE
========

SIM cards can be considered a great way to deliver the data needed for the
device provisioning. They may provide the bootstrap data (used, for example, by
the :doc:`CF-SmartCardBootstrap` Anjay commercial feature), but also they are a
decent component for generating, storing and using security credentials such as
keys and certificates.


**IoT SAFE** (`IoT SIM Applet For Secure End-2-End
<https://www.gsma.com/iot/iot-safe/>`_) was designed as a general
approach to use SIM card as a root of trust. In particular, it allows the user
to use a SIM card for:

* providing credentials for (D)TLS authentication using symmetric or asymmetric
  cryptography,

* generating keys and other security data, being good source of the
  pseudo-randomness,

* using the stored credentials for the security operations, like singing,
  verification, encryption and decryption.


IoT SAFE commercial feature of Anjay is one of the "on demand" commercial
features, which means that it will be developed for a particular IoT solution.
Its scope may differ, depending on IoT SAFE middleware used (if any), the
hardware used and particular customer needs.

The basic case of using IoT SAFE for credential provisioning is when the
management server, to which the device will be connected, is known during
the SIM card creation process - in such case the provisioned credentials are
just used to connect to it. If it isn't known the credentials stored
initially on the SIM card might be used only to connect to some bootstrap
server which provides the proper credentials to the management server - the
safest solution, in such case, is to use EST (see :doc:`CF-EST`) together with
on-SIM keypair generation, to create a new certificate for the device, signed
by the SIM provider's bootstrap server.

.. note::

    When the IoT SAFE integration is included in the project, it is controlled
    using the same APIs as the :doc:`CF-HSM` feature, simply with a different
    format of the query strings. In fact, some IoT SAFE middlewares may provide
    access to the card's capabilities using the PSA or PKCS#11 API, in which
    case it might be possible to use the HSM feature directly.
