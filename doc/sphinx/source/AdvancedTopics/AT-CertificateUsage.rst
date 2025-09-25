..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Certificate Usage
=================

The Certificate Usage resource in the LwM2M Security Object defines how Anjay
interprets and applies the Server Public Key resource. This setting decides whether
the certificate provided in the Security Object is used as a trust anchor, as
the exact server certificate, or in some other way.

Configuring certificate usage correctly is crucial for secure communication. Anjay
can also use an extra PKIX trust store, depending on the chosen certificate usage,
or it can ignore the trust store completely during server certificate verification.
By understanding each option, you can configure Anjay to establish safe and reliable
connections to your servers.

Certificate Usage Settings in Anjay
-----------------------------------

The `Certificate Usage <https://www.openmobilealliance.org/release/LightweightM2M/V1_2-20201110-A/HTML-Version/OMA-TS-LightweightM2M_Transport-V1_2-20201110-A.html#5-2-9-7-0-5297-Certificate-Usage-Field>`_
values in LwM2M correspond to the semantics from `RFC 6698 (DANE TLSA) <https://www.rfc-editor.org/rfc/rfc6698.html>`_.
Each value specifies how the certificate supplied in the Security Object (the
Server Public Key resource) is used when validating the server’s identity. During
the TLS/DTLS handshake, depending on this setting, Anjay behaves as follows:

- PKIX-TA (0 – CA constraint)
    Anjay performs **standard PKIX** verification of the rebuild certificate chain using the
    local trust store. Additionally, the CA named in the "Server Public Key"
    resource **must appear in the verified chain as well as in the trust store**.

    - A **trust store is required**. Without it, PKIX cannot anchor the chain and the handshake fails.
    - The TLSA value **must reference a CA** (intermediate or root), not a leaf certificate.

- PKIX-EE (1 – Service certificate constraint)
    Anjay performs standard PKIX verification and also requires that the handshake
    leaf certificate exactly matches the certificate stored in the "Server Public Key"
    resource.

    - A **trust store is required** to anchor PKIX.
    - Self-signed deployments can work if:

        - The "Server Public Key" contains the self-signed leaf.
        - The same leaf is present in the trust store to allow PKIX to succeed.

- DANE-TA (2 – Trust anchor assertion)
    Anjay treats the "Server Public Key" certificate as the **DANE trust anchor**
    for this server. It attempts to build and verify the path from the handshake
    **leaf** to that **anchor** using the certificates received in the handshake.

    - The **local trust store is not required** and is **ignored** for the accept/reject decision when a DANE anchor is present.
    - The "Server Public Key" **must be a CA/root**. A leaf value is invalid for usage 2.

    .. note::

        If the "Server Public Key" **is empty**, Anjay falls back to PKIX
        verification if a trust store is available. If neither the DM key nor a trust
        store is provided, Anjay **does not validate** the server certificate for
        Certificate Usage 2 and will connect to the server. Consider the security
        impact before relying on this mode.

- DANE-EE (3 – Domain-issued certificate)
    Anjay skips PKIX chain building and compares the handshake leaf directly to
    the certificate in "Server Public Key".

    - The value must be the leaf certificate; a CA/root will be rejected.
    - The trust store is not used for the decision when the DM key is present.

    .. note:: 

        If the "Server Public Key" **is empty**, Anjay falls back to PKIX when
        verification if a trust store is available. If neither the DM key nor a trust
        store is provided, Anjay **does not validate** the server certificate for
        Certificate Usage 3 and will connect to the server. Consider the security
        impact before relying on this mode.

.. important:: 

   Crypto backends and servers commonly omit the root certificate in the handshake.
   Validation typically proceeds as leaf → intermediates → (trusted root from store).
   Depending on what is in your trust store, the backend may anchor the chain at
   the root or stop early at an intermediate that is already trusted.


"Server Public Key" in DM is empty
----------------------------------

When the "Server Public Key" resource is empty, Anjay behaves as follows:

- Trust store present:

    - Usage 0 (PKIX-TA) and Usage 1 (PKIX-EE): Perform normal PKIX. If the chain validates to a trust anchor within the trust store, the connection succeeds.

    - Usage 2 (DANE-TA) and 3 (DANE-EE): Fall back to PKIX; if the chain validates, the connection succeeds.

- No trust store:

    - Usage 0/1: Reject the connection (PKIX cannot be performed).

    - Usage 2/3: Accept the connection without server certificate validation (as exercised in tests). Carefully assess the security risks of operating in this mode.
