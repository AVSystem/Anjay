..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Bootstrap Server certificate rollover
=====================================

Certificates used to secure connections with an LwM2M Bootstrap Server have
limited validity periods and may also need to be replaced due to changes in
the PKI or a credential compromise. Performing such a rollover across a deployed
fleet requires careful coordination: depending on the ``Certificate Usage``, ``Server
Public Key``, Trust Store, and EST configuration, changing credentials in the wrong
order or using an unsuitable rollover procedure may prevent the device from
establishing subsequent bootstrap connections and leave it without a remote
recovery path. This article explains how to safely update the trust material
and credentials used by Anjay for Bootstrap Server connections while preserving
a working, authenticated connection throughout the rollover.

.. note::
    ``Server Public Key`` (``/0/x/4``) is a resource of the LwM2M Security Object.
    For certificate-based connections, it contains a CA or server certificate.
    The ``Certificate Usage`` resource (``/0/x/15``) determines how Anjay uses
    this certificate when authenticating the Bootstrap Server.

Bootstrap Server authentication
-------------------------------

During the TLS/DTLS handshake, the Bootstrap Server presents its certificate
chain. The material used by Anjay to authenticate it depends on the Bootstrap
Server account configuration:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Configuration
     - Verification and rollover implications
   * - Empty ``Server Public Key``
     - Anjay performs standard PKIX verification using the Trust Store,
       regardless of the ``Certificate Usage``. The Trust Store may contain both old
       and new trust anchors during the rollover. Without a Trust Store, Anjay
       will reject the connection.
   * - ``Certificate Usage`` 0 or 1 with a non-empty ``Server Public Key``
     - The presented chain must be accepted by the Trust Store and satisfy the
       CA or leaf certificate constraint stored in the Security Object.
   * - ``Certificate Usage`` 2 or 3 with a non-empty ``Server Public Key``
     - The trust anchor or leaf certificate from the Security Object is used
       directly, and the Trust Store is ignored. The device configuration needs
       to be updated if the relevant trust anchor or leaf certificate changes.

For a detailed description of these modes, see
:doc:`Certificate Usage <AT-CertificateUsage>`.

Authentication of the device by the Bootstrap Server uses a separate client
certificate and is discussed later in this article.

.. note::

    EST is Anjay's built-in mechanism for remotely provisioning and updating the
    PKIX Trust Store through ``/est/crts``. Without EST, updating the Trust Store
    needs to be handled by the application or platform. EST can also enroll or
    renew the device certificate through ``/est/sen`` and ``/est/sren``.

    Whether the EST-provisioned Trust Store is used to authenticate the Bootstrap
    Server depends on the ``anjay_est_cacerts_policy_t`` setting and the Bootstrap
    Server Security Mode. With the default
    ``ANJAY_EST_CACERTS_FOR_EST_SECURITY_AND_BOOTSTRAP`` policy, ``/est/crts`` is
    performed when at least one LwM2M Server uses EST, and the resulting Trust
    Store is also used for Bootstrap Server connections in Certificate or EST
    mode. Other policies may restrict its use, apply it more broadly, or disable
    ``/est/crts`` entirely.

    EST can therefore simplify rollovers that rely on the Trust Store, but it
    does not override the ``Certificate Usage`` rules described above. For details,
    see
    :doc:`Enrollment over Secure Transport <../CommercialFeatures/CF-EST>`.

Rollover strategies
-------------------

.. important::

    The rollover procedures described below assume that the existing certificate,
    private key, and trust anchors have not been compromised and may remain in use
    during the migration. If any of this material is suspected to be compromised,
    keeping it active extends the exposure. Minimize the overlap period, revoke
    the old credentials as soon as possible, and use a secure recovery procedure
    for devices that cannot be migrated in time.

Trust Store-based verification
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This case applies when the ``Server Public Key`` resource is empty. Anjay performs
standard PKIX verification using the Trust Store, regardless of the configured
``Certificate Usage``.

**Bootstrap Server certificate replacement under the same trust anchor**

If the Bootstrap Server's new certificate chain terminates at a trust anchor that is
already present in the device's Trust Store, no device-side update is required. The
Bootstrap Server can start presenting its new leaf certificate, together with any
required intermediate certificates, without performing any changes.

**Trust anchor rollover**

If the new certificate chain uses a different trust anchor, the old and new anchors
need to coexist during the migration:

#. Provision a Trust Store containing both trust anchors.
#. Confirm that the updated Trust Store has reached the devices and survives a restart.
#. Configure the Bootstrap Server to present the new certificate chain.
#. Confirm that devices can connect using the new chain.
#. Remove the old trust anchor after all target devices have migrated.

.. important::
    If some devices have not yet received the updated Trust Store, keep a
    Bootstrap Server endpoint presenting the old certificate chain available until
    they do. Otherwise, devices that were offline during the rollout may be unable
    to reconnect after the original endpoint switches to the new chain.

.. note::

    An EST-provisioned Trust Store replaces the previous EST Trust Store instead of
    extending it. During the overlap period, the certificates returned by ``/est/crts``
    must therefore include both the old and new trust anchors. The configured
    ``anjay_est_cacerts_policy_t`` must also allow this Trust Store to be used for
    Bootstrap Server connections. Without EST, the application or platform needs to
    implement the same overlap when updating and persisting the Trust Store.

PKIX verification with a Server Public Key constraint
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This case applies when the ``Server Public Key`` resource is non-empty and
``Certificate Usage`` is set to 0 or 1. The certificate chain presented by the
Bootstrap Server must pass standard PKIX verification against the Trust Store.
For ``Certificate Usage`` 0, the PKIX-verified chain must include the CA
specified in the ``Server Public Key`` resource, and that CA must also be
present in the Trust Store. For ``Certificate Usage`` 1, the presented leaf
certificate must match the certificate stored in that resource.

**Certificate Usage 0: CA constraint**

Replacing the leaf certificate does not require a device-side update if the new
chain still contains the constrained CA and is accepted by the Trust Store. If
the constrained CA changes, both the Trust Store and the ``Server Public Key``
need to be updated.

**Certificate Usage 1: leaf certificate constraint**

Every leaf certificate replacement requires updating the ``Server Public Key``,
even if the new certificate is issued by the same CA. The Trust Store needs to
be updated only if the new chain uses a different trust anchor.

**Fleet-wide rollover**

Because the Security Object can contain only one constraint at a time, the most
generally applicable migration method is to use a separate Bootstrap Server
endpoint for the new certificate chain:

.. note::

    Steps 2 and 3 apply only when EST is used to update the Trust Store.
    Without EST, the application or platform must update and persist a Trust
    Store containing both the old and new trust anchors using another mechanism
    before continuing with step 4.

#. Prepare the new endpoint and ensure that it accepts the new device credentials.
#. Complete a Bootstrap procedure through the old endpoint without changing
   the ``Server Public Key``, ``Bootstrap Server URI``, or ``SNI``.
#. After Bootstrap Finish, allow Anjay to complete the EST procedure. The
   certificates returned by ``/est/crts`` must include both the old and new
   trust anchors.
#. Confirm that the updated Trust Store has been installed and persisted.
#. Establish another Bootstrap connection through the old endpoint.
#. During this Bootstrap procedure, use Bootstrap Write to update the
   ``Server Public Key`` and the ``Bootstrap Server URI`` or ``SNI``.
#. Confirm that the next Bootstrap Server connection is established through the new endpoint.
#. Keep the old endpoint and trust material available until all target devices have migrated.

.. warning::

    Do not update the ``Server Public Key`` in the same Bootstrap procedure
    that is intended to obtain the updated Trust Store through EST. Anjay
    validates the Security Object before performing the post-bootstrap EST
    operations. If the Trust Store update then fails, the device may be left
    with the new constraint and the old Trust Store, preventing subsequent
    Bootstrap Server connections.

Direct verification using the Server Public Key
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This case applies when the ``Server Public Key`` resource is non-empty and
``Certificate Usage`` is set to 2 or 3. Anjay uses the trust anchor or leaf
certificate stored in the Security Object to authenticate the Bootstrap Server.
The Trust Store is ignored.

**Certificate Usage 2: trust anchor**

Replacing the leaf certificate does not require a device-side update if the new
chain can still be verified using the trust anchor stored in the
``Server Public Key`` resource. If the trust anchor changes, the resource needs
to be updated.

**Certificate Usage 3: leaf certificate**

Every leaf certificate replacement requires updating the
``Server Public Key``, regardless of which CA issued the new certificate.

**Fleet-wide rollover**

Because the Security Object can contain only one trust anchor or leaf
certificate at a time, use a separate Bootstrap Server endpoint when the stored
value needs to change:

#. Prepare the new endpoint with the new certificate chain and ensure that it accepts the
   new device credentials.
#. While the device is connected to the old endpoint, use Bootstrap Write to update the
   ``Server Public Key`` and the ``Bootstrap Server URI`` or ``SNI``.
#. Confirm that the next Bootstrap Server connection is established through the new endpoint.
#. Keep the old endpoint available until all target devices have migrated.

.. warning::

    Updating only the ``Server Public Key`` while the current endpoint still
    presents the old certificate will cause the next connection to fail.
    Switching the current endpoint to the new certificate first will prevent
    devices with the old configuration from connecting.

.. note::

    An EST-provisioned Trust Store cannot provide an overlap in this
    configuration because it is ignored during Bootstrap Server authentication.
    The ``Server Public Key`` still needs to be updated through the Bootstrap
    Interface.

Device certificate rollover
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The device certificate is used by the Bootstrap Server to authenticate the
client. Its rollover is generally simpler because the new credentials can be
delivered over a connection established with the old certificate.

**Credentials stored in the Security Object**

Before updating the device, ensure that the Bootstrap Server can authenticate
both the current and replacement device certificates. If the certificates are
issued by different CAs, the server needs to trust both CAs during the migration.
Then:

#. Use Bootstrap Write to update the device certificate and the corresponding private
   key within the same Bootstrap procedure.
#. Confirm that the next Bootstrap Server connection uses the new certificate.
#. Stop accepting the old certificate only after the migration has been confirmed.

**EST-managed certificate**

If the Bootstrap Server account uses EST Security Mode, Anjay uses the
EST-provisioned device certificate for subsequent Bootstrap Server connections.
Automatic simple re-enrollment is enabled by default, so the certificate is
renewed without updating it through Bootstrap Write. The re-enrollment time is
controlled by the ``nominal_usage`` and ``max_margin`` fields of ``anjay_est_reenroll_config_t``.
Failed attempts are retried according to its ``sren_attempts_count`` field.
Simple re-enrollment renews the certificate while retaining the existing private key.

Operational checklist
^^^^^^^^^^^^^^^^^^^^^

Before retiring the old credentials:

* Test the rollover on a small group of devices before applying it to the whole fleet.
* Allow enough time for devices that remain offline for extended periods to receive the new configuration.
* Confirm that the new configuration survives a device or application restart.
* Keep the old endpoint and trust material available until successful migration has been confirmed.
* Consider how devices that do not complete the rollover can be recovered or re-provisioned.
