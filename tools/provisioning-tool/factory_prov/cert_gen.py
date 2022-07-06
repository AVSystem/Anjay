#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from OpenSSL import crypto, SSL


def gen_cert_and_key(cert_info, key_file, cert_file):
    key = crypto.PKey()
    key.generate_key(crypto.TYPE_RSA, cert_info.get('RSAKeyLen', 4096))

    # create a self-signed cert
    cert = crypto.X509()

    if 'countryName' in cert_info:
        cert.get_subject().C = cert_info['countryName']
    if 'stateOrProvinceName' in cert_info:
        cert.get_subject().ST = cert_info['stateOrProvinceName']
    if 'localityName' in cert_info:
        cert.get_subject().L = cert_info['localityName']
    if 'organizationName' in cert_info:
        cert.get_subject().O = cert_info['organizationName']
    if 'organizationUnitName' in cert_info:
        cert.get_subject().OU = cert_info['organizationUnitName']
    if 'emailAddress' in cert_info:
        cert.get_subject().emailAddress = cert_info['emailAddress']
    if 'commonName' in cert_info:
        cert.get_subject().CN = cert_info['commonName']
    else:
        raise ValueError(
            'Missing commonName information for certificate generation')

    cert.set_serial_number(cert_info.get('serialNumber', 0))
    cert.gmtime_adj_notBefore(0)
    cert.gmtime_adj_notAfter(cert_info.get(
        'validityOffsetInSeconds', 7*365*24*60*60))
    cert.set_issuer(cert.get_subject())
    cert.set_pubkey(key)
    cert.sign(key, cert_info.get('digest', 'sha512'))

    # Create two copies of cert. One in PEM format and one in DER format
    # Key is need only in DER format
    # PEM format certificate can be viewed using:
    # openssl x509 -in selfsigned.crt -text -noout
    #
    # DER format certificate can be viewed using:
    # openssl x509 -inform der -in selfsigned.der -text -noout
    with open(f'{cert_file}.crt', 'wb') as f:
        f.write(crypto.dump_certificate(crypto.FILETYPE_PEM, cert))
    with open(f'{cert_file}.der', 'wb') as f:
        f.write(crypto.dump_certificate(crypto.FILETYPE_ASN1, cert))
    with open(f'{key_file}.der', 'wb') as f:
        f.write(crypto.dump_privatekey(crypto.FILETYPE_ASN1, key))
