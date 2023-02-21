#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
import datetime

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa

_HASHES_BY_NAME = dict((hash.name, hash) for hash in hashes.__dict__.values() if
                       isinstance(hash, type) and
                       issubclass(hash, hashes.HashAlgorithm) and
                       isinstance(hash.name, str))


def gen_cert_and_key(cert_info, key_file, cert_file):
    if 'RSAKeyLen' in cert_info:
        if 'ellipticCurve' in cert_info:
            raise KeyError('RSAKeyLen and ellipticCurve specified together')
        key = rsa.generate_private_key(65537, cert_info['RSAKeyLen'])
    else:
        curve = cert_info.get('ellipticCurve', 'secp256r1')
        try:
            curve = ec._CURVE_TYPES[curve]()
        except KeyError:
            raise ValueError('{} is not a supported elliptic curve'.format(curve))
        key = ec.generate_private_key(curve)

    try:
        hash = _HASHES_BY_NAME[cert_info.get('digest', 'sha256')]()
    except KeyError:
        raise ValueError('{} is not a supported digest algorithm'.format(cert_info['digest']))

    subject_attrs = []
    if 'countryName' in cert_info:
        subject_attrs.append(
            x509.NameAttribute(x509.oid.NameOID.COUNTRY_NAME, cert_info['countryName']))
    if 'stateOrProvinceName' in cert_info:
        subject_attrs.append(x509.NameAttribute(x509.oid.NameOID.STATE_OR_PROVINCE_NAME,
                                                cert_info['stateOrProvinceName']))
    if 'localityName' in cert_info:
        subject_attrs.append(
            x509.NameAttribute(x509.oid.NameOID.LOCALITY_NAME, cert_info['localityName']))
    if 'organizationName' in cert_info:
        subject_attrs.append(
            x509.NameAttribute(x509.oid.NameOID.ORGANIZATION_NAME, cert_info['organizationName']))
    if 'organizationUnitName' in cert_info:
        subject_attrs.append(x509.NameAttribute(x509.oid.NameOID.ORGANIZATIONAL_UNIT_NAME,
                                                cert_info['organizationUnitName']))
    if 'emailAddress' in cert_info:
        subject_attrs.append(
            x509.NameAttribute(x509.oid.NameOID.EMAIL_ADDRESS, cert_info['emailAddress']))
    if 'commonName' in cert_info:
        subject_attrs.append(
            x509.NameAttribute(x509.oid.NameOID.COMMON_NAME, cert_info['commonName']))
    else:
        raise ValueError(
            'Missing commonName information for certificate generation')

    # create a self-signed cert
    subject = x509.Name(subject_attrs)
    now = datetime.datetime.utcnow()
    cert_builder = (x509.CertificateBuilder().
                    subject_name(subject).
                    issuer_name(subject).
                    public_key(key.public_key()).
                    serial_number(cert_info.get('serialNumber', 1)).
                    not_valid_before(now).
                    not_valid_after(now + datetime.timedelta(
        seconds=cert_info.get('validityOffsetInSeconds', 7 * 365 * 24 * 60 * 60))))

    cert = cert_builder.sign(key, hash)

    # Create two copies of cert. One in PEM format and one in DER format
    # Key is need only in DER format
    # PEM format certificate can be viewed using:
    # openssl x509 -in selfsigned.crt -text -noout
    #
    # DER format certificate can be viewed using:
    # openssl x509 -inform der -in selfsigned.der -text -noout
    with open(f'{cert_file}.crt', 'wb') as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
    with open(f'{cert_file}.der', 'wb') as f:
        f.write(cert.public_bytes(serialization.Encoding.DER))
    with open(f'{key_file}.der', 'wb') as f:
        f.write(key.private_bytes(encoding=serialization.Encoding.DER,
                                  format=serialization.PrivateFormat.PKCS8,
                                  encryption_algorithm=serialization.NoEncryption()))
