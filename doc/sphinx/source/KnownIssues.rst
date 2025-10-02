..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Known Issues
============

.. contents:: :local:

Non valid hostname may appear in SNI extension
----------------------------------------------

The DTLS Server Name Indication (SNI) extension is designed to communicate
the expected server hostname during a TLS/DTLS handshake, particularly when
it differs from the connection URI. According to `RFC6066`, the SNI extension
must contain a valid hostname, not an IP address.

In Anjay, when the LwM2M Server URI (``/0/x/0``) is set to a raw IP address, the default
MbedTLS or OpenSSL integration layer used by Anjay automatically includes that IP address
in the SNI field. This behavior is non-compliant with `RFC6066 <https://datatracker.ietf.org/doc/html/rfc6066>`_, since both
MbedTLS and OpenSSL derive the SNI value from the hostname used for
certificate validation.

.. note::

   You can resolve this issue by configuring a valid hostname in the
   SNI Resource (``/0/x/14``) of the Security Object instance used to
   connect to the server. The value provided in this resource will be
   sent in the SNI extension and will also be used for certificate
   verification. Therefore, it must match the Common Name (CN) or
   Subject Alternative Name (SAN) in the server’s certificate.

If you prefer to verify the certificate by IP address instead, the SNI
extension can be disabled. In MbedTLS, this can be done by removing the
``#define MBEDTLS_SSL_SERVER_NAME_INDICATION`` directive from the MbedTLS
configuration header.

.. warning::

   This issue may lead to DTLS handshake failures without any explicit
   error message appearing in Anjay logs.

Compatibility issues between OpenSSL and libp11 on some Linux distributions
---------------------------------------------------------------------------

In certain Linux distributions, the versions of OpenSSL and the PKCS#11 engine
(``libp11``, providing ``libengine-pkcs11-openssl``) shipped with the system may
be incompatible. This incompatibility can result in runtime failures such as
segmentation faults when using PKCS#11-backed cryptography through OpenSSL.

For example, on Ubuntu 24.04, OpenSSL 3.0.13 combined with libp11 0.4.12 exposes
a bug in the libp11 library that leads to a crash of the application. This issue
has been fixed recent version of libp11, but the fix is not yet available in the
default Ubuntu repositories at the time of writing.

.. important::

   If you encounter such issues (e.g., segmentation faults or unexpected
   handshake failures when using PKCS#11 with OpenSSL), consider upgrading
   libp11 to a newer version than the one provided by your distribution. The
   simplest method is to manually build and install libp11 from the latest
   upstream sources.

   However, installing a self-built version with ``sudo make install`` may
   lead to incompatibilities or conflicts with system packages managed by
   apt or other package managers. It is therefore **preferable to rebuild
   and install the library in a way that remains compatible with the package
   management system** of your distribution. The example below demonstrates
   such an approach, which was successfully used on Ubuntu 24.04 to produce a
   clean ``.deb`` package at the time of writing.

Example procedure (tested on Ubuntu 24.04)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. warning::

   The procedure below is provided as an **example** of how the issue was
   resolved in our environment. The exact steps required on your system may (and
   likely will) differ.

The following steps reproduce the approach used in our internal CI environment
to rebuilt ``libengine-pkcs11-openssl`` from a newer upstream tag:

.. code-block:: bash

    # Enable fetching package sources
    sudo sed -i -e 's/Types: deb/Types: deb deb-src/g' /etc/apt/sources.list.d/ubuntu.sources
    sudo apt-get update

    # Obtain package source and generic dependencies used for building .debs
    mkdir ~/libp11-pkg-build && cd ~/libp11-pkg-build
    apt-get source libengine-pkcs11-openssl
    sudo apt-get install -y devscripts dpkg-dev fakeroot quilt

    # Download newer upstream release of libp11
    wget https://github.com/OpenSC/libp11/archive/refs/tags/libp11-0.4.16.tar.gz -O libp11_0.4.16.orig.tar.gz
    tar xf libp11_0.4.16.orig.tar.gz
    mv libp11-libp11-0.4.16 libp11-0.4.16

    # Reuse debian/ directory from previous package version
    cp -a libp11-0.4.12/debian libp11-0.4.16/
    cd libp11-0.4.16
    rm -rf debian/patches

    # Tag new version
    dch -v 0.4.16-0ubuntu1+local1 "Local rebuild of libp11 from upstream tag libp11-0.4.16"

    # Install build dependencies and build the package
    sudo apt-get build-dep -y libp11
    debuild -us -uc -b

    # Install the rebuilt package
    sudo apt-get install -y ../libengine-pkcs11-openssl_0.4.16-0ubuntu1+local1_amd64.deb

After installation, the newer libp11 should resolve the incompatibility and
enable stable operation of PKCS#11 integration with OpenSSL-based DTLS/TLS
backend.
