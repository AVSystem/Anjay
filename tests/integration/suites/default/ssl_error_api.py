# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *

import re
import os
import enum


class SSLAlertDescription(enum.IntEnum):
    CLOSE_NOTIFY = 0
    UNEXPECTED_MESSAGE = 10
    BAD_RECORD_MAC = 20
    DECRYPTION_FAILED_RESERVED = 21
    RECORD_OVERFLOW = 22
    DECOMPRESSION_FAILURE_RESERVED = 30
    expect_handshake_failURE = 40
    NO_CERTIFICATE_RESERVED = 41
    BAD_CERTIFICATE = 42
    UNSUPPORTED_CERTIFICATE = 43
    CERTIFICATE_REVOKED = 44
    CERTIFICATE_EXPIRED = 45
    CERTIFICATE_UNKNOWN = 46
    ILLEGAL_PARAMETER = 47
    UNKNOWN_CA = 48
    ACCESS_DENIED = 49
    DECODE_ERROR = 50
    DECRYPT_ERROR = 51
    EXPORT_RESTRICTION_RESERVED = 60
    PROTOCOL_VERSION = 70
    INSUFFICIENT_SECURITY = 71
    INTERNAL_ERROR = 80
    INAPPROPRIATE_FALLBACK = 86
    USER_CANCELED = 90
    NO_RENEGOTIATION_RESERVED = 100
    MISSING_EXTENSION = 109
    UNSUPPORTED_EXTENSION = 110
    CERTIFICATE_UNOBTAINABLE_RESERVED = 111
    UNRECOGNIZED_NAME = 112
    BAD_CERTIFICATE_STATUS_RESPONSE = 113
    BAD_CERTIFICATE_HASH_VALUE_RESERVED = 114
    UNKNOWN_PSK_IDENTITY = 115
    CERTIFICATE_REQUIRED = 116
    NO_APPLICATION_PROTOCOL = 120
    UNKNOWN = 255


class SSLErrorCategory(enum.IntEnum):
    SSL_ALERT = 8572
    SSL_LIB_ERROR = 8573

class SSLErrorAPITest():
    class SSLErrorAPISingleServerTest(test_suite.Lwm2mSingleServerTest):
        SSL_ERROR_REGEX = re.compile(
        rb'SSL error from server with SSID=(\d+): category=(\d+), code=(\d+)\n')

        # Keep a dedicated offset so checks in this file do not consume lines
        # expected by other read_log_until_match() users.
        log_alt_offset = 0

        def _assertSslError(self, expected_ssl_errors, timeout=5):
            self.log_alt_offset, match = self.read_log_until_match(
                self.SSL_ERROR_REGEX,
                timeout_s=timeout,
                alt_offset=self.log_alt_offset)
            self.assertIsNotNone(match)
            _actual_ssid = int(match.group(1))
            actual_category = int(match.group(2))
            if actual_category == SSLErrorCategory.SSL_ALERT:
                actual_code = int(match.group(3)) & 0xFF
            else:
                actual_code = int(match.group(3))

            self.assertIn((actual_category, actual_code), expected_ssl_errors)

            return actual_category, actual_code

        def _cert_file(self, filename):
            return os.path.join(os.path.dirname(self.config.demo_path), 'certs', filename)

        def setUp(self, server_crt_file=None, server_key_file=None,
                 client_cert_on_server_file=None, client_crt_file=None,
                 client_key_file=None, server_crt_on_client_file=None,
                 *args, **kwargs):
            self.skipIfFeatureStatus('ANJAY_WITH_SSL_ERROR_API = OFF',
                                    'SSL error API disabled')

            extra_cmdline_args = []
            extra_cmdline_args += ['--certificate-usage', '3']
            extra_cmdline_args += ['--dtls-hs-retry-wait-max', '3']


            if server_crt_file is not None:
                self.server_crt_file = self._cert_file(server_crt_file)
            if server_key_file is not None:
                self.server_key_file = self._cert_file(server_key_file)
            if client_cert_on_server_file is not None:
                self.client_cert_on_server_file = self._cert_file(client_cert_on_server_file)
            if (client_crt_file is not None):
                self.client_crt_file = self._cert_file(client_crt_file)
                extra_cmdline_args += ['-C', self.client_crt_file]
            if (client_key_file is not None):
                self.client_key_file = self._cert_file(client_key_file)
                extra_cmdline_args += ['-K', self.client_key_file]
            if (server_crt_on_client_file is not None):
                self.server_crt_on_client_file = self._cert_file(server_crt_on_client_file)
                extra_cmdline_args += ['-P', self.server_crt_on_client_file]

            super().setUp(
                servers=[Lwm2mServer(
                    coap.DtlsServer(ca_file=getattr(self, 'client_cert_on_server_file', None),
                                    crt_file=getattr(self, 'server_crt_file', None),
                                    key_file=getattr(self, 'server_key_file', None)))],
                extra_cmdline_args=extra_cmdline_args, auto_register=False, *args, **kwargs)

        def runTest(self, expected_ssl_errors, expect_handshake_fail=True):
            if expect_handshake_fail:
                with self.assertRaisesRegex(RuntimeError,
                                            'mbedtls_ssl_handshake failed:'):
                    self.assertDemoRegisters()

            self._assertSslError(expected_ssl_errors)

        def tearDown(self):
            # No need to deregister - handshake should fail and the client should not register
            super().tearDown(auto_deregister=False)


class SSLErrorOnRegisterWithMismatchedServerPublicKey(SSLErrorAPITest.SSLErrorAPISingleServerTest):
    def setUp(self):
        super().setUp(server_crt_file='self-signed/server.crt',
                      server_key_file='self-signed/server.key',
                      client_cert_on_server_file='self-signed/client.crt.der',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      server_crt_on_client_file='server.crt.der')

    def runTest(self):
        # MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
        super().runTest([(SSLErrorCategory.SSL_LIB_ERROR, 9984)])


class SSLErrorOnRegisterWithMismatchedClientPublicKey(SSLErrorAPITest.SSLErrorAPISingleServerTest):
    def setUp(self):
        super().setUp(server_crt_file='self-signed/server.crt',
                      server_key_file='self-signed/server.key',
                      client_cert_on_server_file='client.crt.der',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      server_crt_on_client_file='self-signed/server.crt.der')

    def runTest(self):
        super().runTest([(SSLErrorCategory.SSL_ALERT,
                          SSLAlertDescription.UNKNOWN_CA)])


class SSLErrorOnRegisterWithWrongClientKey(SSLErrorAPITest.SSLErrorAPISingleServerTest):
    def setUp(self):
        super().setUp(server_crt_file='self-signed/server.crt',
                      server_key_file='self-signed/server.key',
                      client_cert_on_server_file='self-signed/client.crt.der',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='client.key.der',
                      server_crt_on_client_file='self-signed/server.crt.der')

    def runTest(self):
        # MBEDTLS_ERR_ECP_VERIFY_FAILED or MBEDTLS_ERR_ECP_BAD_INPUT_DATA,
        # depending on verification path
        # handshake not attempted due to client key mismatch
        super().runTest([(SSLErrorCategory.SSL_LIB_ERROR, 19968),
                         (SSLErrorCategory.SSL_LIB_ERROR, 20352)],
                        expect_handshake_fail=False)


class SSLErrorOnRegisterWithWrongServerKey(SSLErrorAPITest.SSLErrorAPISingleServerTest):
    def setUp(self):
        super().setUp(server_crt_file='self-signed/server.crt',
                      server_key_file='server.key',
                      client_cert_on_server_file='self-signed/client.crt.der',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      server_crt_on_client_file='self-signed/server.crt.der')

    def runTest(self):
        # MBEDTLS_ERR_ECP_VERIFY_FAILED
        super().runTest([(SSLErrorCategory.SSL_LIB_ERROR, 19968)])


class SSLErrorOnRegisterWithWrongClientKeyAndCert(SSLErrorAPITest.SSLErrorAPISingleServerTest):
    def setUp(self):
        super().setUp(server_crt_file='self-signed/server.crt',
                      server_key_file='self-signed/server.key',
                      client_cert_on_server_file='self-signed/client.crt',
                      client_crt_file='client.crt.der',
                      client_key_file='client.key.der',
                      server_crt_on_client_file='self-signed/server.crt.der')

    def runTest(self):
        # MBEDTLS_ERR_ECP_BAD_INPUT_DATA
        super().runTest([(SSLErrorCategory.SSL_ALERT,
                          SSLAlertDescription.UNKNOWN_CA)])


class SSLErrorOnRegisterWithWrongServerKeyAndCert(SSLErrorAPITest.SSLErrorAPISingleServerTest):
    def setUp(self):
        super().setUp(server_crt_file='server.crt',
                      server_key_file='server.key',
                      client_cert_on_server_file='self-signed/client.crt',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      server_crt_on_client_file='self-signed/server.crt.der')

    def runTest(self):
        # MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
        super().runTest([(SSLErrorCategory.SSL_LIB_ERROR, 9984)])
