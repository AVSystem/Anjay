# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import concurrent.futures
import os
import re
import socket
import unittest

from framework_tools.lwm2m.coap.server import SecurityMode
from framework_tools.lwm2m.coap.transport import Transport
from framework.lwm2m_test import *
from suites.default import bootstrap_client

import pymbedtls

class CertificatesTest:
    class TestMixin:
        def _cert_file(self, filename):
            if os.path.isabs(filename):
                return filename
            else:
                # demo_path = 'anjay/output/bin'
                return os.path.join(os.path.dirname(self.config.demo_path), 'certs', filename)

        def setUp(self, client_ca_file=None, server_crt=None, server_key=None, client_crt_file=None,
                  client_key_file=None, server_crt_file=None, extra_cmdline_args=None,
                  *args, **kwargs):
            extra_cmdline_args = [*extra_cmdline_args] if extra_cmdline_args is not None else []
            if client_ca_file is not None:
                self.client_ca_file = self._cert_file(client_ca_file)
            if server_crt is not None:
                self.server_crt = self._cert_file(server_crt)
            if server_key is not None:
                self.server_key = self._cert_file(server_key)
            if (client_crt_file is not None):
                self.client_crt_file = self._cert_file(client_crt_file)
                extra_cmdline_args += ['-C' + self.client_crt_file]
            if (client_key_file is not None):
                self.client_key_file = self._cert_file(client_key_file)
                extra_cmdline_args += ['-K' + self.client_key_file]
            if (server_crt_file is not None):
                self.server_crt_file = self._cert_file(server_crt_file)
                extra_cmdline_args += ['-P' + self.server_crt_file]
            super().setUp(client_ca_file=getattr(self, 'client_ca_file', None),
                          server_crt_file=getattr(self, 'server_crt', None),
                          server_key_file=getattr(self, 'server_key', None),
                          extra_cmdline_args=extra_cmdline_args, *args, **kwargs)

    class TestUDPMixin(TestMixin, test_suite.Lwm2mSingleServerTest):
        pass

    class TestTCPMixin(TestMixin, test_suite.Lwm2mSingleTcpServerTest):
        def setUp(self, *args, **kwargs):

            tls_version = 'TLSv1.2'
            # pymbedtls.Context.mbedtls_version returns pymbedtls mbedtls version but we assume that
            # Anjay uses the same
            #
            # Hybrid TLS 1.2 / 1.3 is not supported in Mbed TLS on server side until v3.5.0
            if pymbedtls.Context.supports_TLS_1_3() and pymbedtls.Context.mbedtls_version() >= 0x03050000:
                tls_version = 'TLSv1.3'

            super().setUp(binding='T', tls_version=tls_version, *args, **kwargs)


    class PcapEnabledTestMixin:
        def read_certificate(self, file):
            import cryptography
            import cryptography.hazmat
            import cryptography.x509
            with open(self._cert_file(file), 'rb') as f:
                data = f.read()
            if b'-----BEGIN' in data:
                return cryptography.x509.load_pem_x509_certificate(
                    data, backend=cryptography.hazmat.backends.default_backend())
            else:
                return cryptography.x509.load_der_x509_certificate(
                    data, backend=cryptography.hazmat.backends.default_backend())

        def read_public_key(self, cert):
            import cryptography
            import cryptography.hazmat
            import cryptography.x509

            if not isinstance(cert, cryptography.x509.Certificate):
                cert = self.read_certificate(cert)

            return cert.public_key().public_bytes(
                cryptography.hazmat.primitives.serialization.Encoding.DER,
                cryptography.hazmat.primitives.serialization.PublicFormat.SubjectPublicKeyInfo)

        def get_certificate_packet(self, cert=None, common_name=None):
            import dpkt
            import cryptography
            import cryptography.hazmat
            assert cert is None or common_name is None

            if common_name is None:
                if cert is None:
                    cert = self.client_crt_file
                if isinstance(cert, str):
                    cert = self.read_certificate(cert)
                common_name = cert.subject
            if not isinstance(common_name, bytes):
                common_name = common_name.public_bytes(
                    cryptography.hazmat.backends.default_backend())

            for pkt in self.read_pcap():
                if isinstance(pkt, dpkt.ip.IP) \
                        and isinstance(pkt.data, (dpkt.udp.UDP, dpkt.tcp.TCP)) \
                        and pkt.data.dport == self.serv.get_listen_port() \
                        and common_name in pkt.data.data:
                    return pkt.data.data
            return None

    class PcapEnabledTestUDP(test_suite.PcapEnabledTest, TestUDPMixin, PcapEnabledTestMixin):
        pass

    class PcapEnabledTestTCP(test_suite.PcapEnabledTest, TestTCPMixin, PcapEnabledTestMixin):
        pass


class RegisterWithCertificates(CertificatesTest.TestUDPMixin):
    def setUp(self):
        super().setUp(server_crt='server.crt', server_key='server.key')

class RegisterTCPWithCertificates(CertificatesTest.TestTCPMixin):
    def setUp(self):
        super().setUp(server_crt='server.crt', server_key='server.key')

class RegisterTCPServerCertClientPSK(CertificatesTest.TestTCPMixin):
    auto_deregister = False
    def setUp(self):
        extra_cmdline_args = ['--identity', str(binascii.hexlify(b'random'), 'ascii'),
                              '--key',      str(binascii.hexlify(b'values'), 'ascii')]
        super().setUp(server_crt='server.crt', server_key='server.key',
                    extra_cmdline_args=extra_cmdline_args, forced_client_security_mode = 'psk',
                    auto_register=False)

    def runTest(self):
        with self.assertRaises(RuntimeError):
            self.assertTcpCsm(self.serv)
            self.assertDemoRegisters(self.serv, binding='T')
            self.auto_deregister = True
        # -30592 == -0x7780 == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE
        self.read_log_until_match(b'handshake failed: -30592', 1)

    def tearDown(self):
        super().tearDown(auto_deregister=self.auto_deregister)

# TODO For now DTLSv1.3 in not supported in Mbed TLS
# class RegisterUDPServerCertClientPSK(CertificatesTest.TestUDPMixin):
#     auto_deregister = False
#     def setUp(self):
#         extra_cmdline_args = ['--identity', str(binascii.hexlify(b'random'), 'ascii'),
#                               '--key',      str(binascii.hexlify(b'values'), 'ascii')]
#         super().setUp(server_crt='server.crt', server_key='server.key',
#                     extra_cmdline_args=extra_cmdline_args, forced_client_security_mode = 'psk',
#                     tls_version='TLSv1.3', auto_register=False)

#     def runTest(self):
#         with self.assertRaises(RuntimeError):
#             self.assertDemoRegisters(self.serv)
#             self.auto_deregister = True
#         # -30592 == -0x7780 == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE
#         self.read_log_until_match(b'handshake failed: -30592', 1)

#     def tearDown(self):
#         super().tearDown(auto_deregister=self.auto_deregister)

class RegisterTCPWithPSK(CertificatesTest.TestTCPMixin):
    def setUp(self):
        super().setUp(psk_identity=b'random', psk_key=b'value')


class RegisterWithCertificateChainExternalMixin:
    def setUp(self, extra_cmdline_args=[], **kwargs):
        extra_cmdline_args += ['--use-external-security-info']
        super().setUp(client_ca_file='root.crt', server_crt='server.crt', server_key='server.key',
                      client_crt_file='client2-full-path.crt', client_key_file='client2.key.der',
                      extra_cmdline_args=extra_cmdline_args, **kwargs)

    def runTest(self):
        import time

        certificate_packet = None
        deadline = time.time() + self.DEFAULT_MSG_TIMEOUT
        while certificate_packet is None and time.time() <= deadline:
            time.sleep(0.1)
            certificate_packet = self.get_certificate_packet()

        self.assertIsNotNone(certificate_packet)
        self.assertIn(self.read_public_key('client2.crt.der'), certificate_packet)
        self.assertIn(self.read_public_key('client2_ca.crt.der'), certificate_packet)
        self.assertIn(self.read_public_key('root.crt.der'), certificate_packet)

class RegisterUDPWithCertificateChainExternal(RegisterWithCertificateChainExternalMixin,
                                              CertificatesTest.PcapEnabledTestUDP):
    pass

@unittest.skipIf(pymbedtls.Context.supports_TLS_1_3(), "TLS 1.3 encrypts its handshake messages")
class RegisterTCPWithCertificateChainExternal(RegisterWithCertificateChainExternalMixin,
                                              CertificatesTest.PcapEnabledTestTCP):
    pass

class RegisterWithCertificateChainRebuiltMixin:
    def setUp(self):
        super().setUp(client_ca_file='root.crt', server_crt='server.crt',
                      server_crt_file='server.crt.der', server_key='server.key',
                      client_crt_file='client2.crt.der', client_key_file='client2.key.der',
                      extra_cmdline_args=['--pkix-trust-store',
                                          self._cert_file('client2_ca-and-root.crt'),
                                          '--rebuild-client-cert-chain'])

    def runTest(self):
        import time

        certificate_packet = None
        deadline = time.time() + self.DEFAULT_MSG_TIMEOUT
        while certificate_packet is None and time.time() <= deadline:
            time.sleep(0.1)
            certificate_packet = self.get_certificate_packet()

        self.assertIsNotNone(certificate_packet)
        self.assertIn(self.read_public_key('client2.crt.der'), certificate_packet)
        self.assertIn(self.read_public_key('client2_ca.crt.der'), certificate_packet)
        self.assertIn(self.read_public_key('root.crt.der'), certificate_packet)

class RegisterUDPWithCertificateChainRebuilt(RegisterWithCertificateChainRebuiltMixin,
                                              CertificatesTest.PcapEnabledTestUDP):
    pass

@unittest.skipIf(pymbedtls.Context.supports_TLS_1_3(), "TLS 1.3 encrypts its handshake messages")
class RegisterTCPWithCertificateChainRebuilt(RegisterWithCertificateChainRebuiltMixin,
                                              CertificatesTest.PcapEnabledTestTCP):
    pass


class RegisterWithCyclicCertificateChainRebuiltMixin:
    def setUp(self):
        self._cleanup_list = test_suite.CleanupList()

        try:
            # Generate cyclic root certificates
            from cryptography.hazmat.primitives import serialization
            from suites.default import firmware_update
            TestWithTlsServer = firmware_update.FirmwareUpdate.TestWithTlsServer

            key1 = TestWithTlsServer._generate_key()
            key2 = TestWithTlsServer._generate_key()

            self.cert1 = TestWithTlsServer._generate_cert(private_key=key2,
                                                          public_key=key1.public_key(),
                                                          issuer_cn='Root2', cn='Root1', ca=True)
            self.cert2 = TestWithTlsServer._generate_cert(private_key=key1,
                                                          public_key=key2.public_key(),
                                                          issuer_cn='Root1', cn='Root2', ca=True)

            cert1_pem = self.cert1.public_bytes(encoding=serialization.Encoding.PEM)
            cert2_pem = self.cert2.public_bytes(encoding=serialization.Encoding.PEM)

            trust_store_file = tempfile.NamedTemporaryFile()
            self._cleanup_list.append(trust_store_file.close)
            trust_store_file.write(cert1_pem)
            trust_store_file.write(cert2_pem)
            trust_store_file.flush()

            client_key = TestWithTlsServer._generate_key()
            self.client_cert = TestWithTlsServer._generate_cert(private_key=key1,
                                                                public_key=client_key.public_key(),
                                                                issuer_cn='Root1', cn='127.0.0.1',
                                                                alt_ip='127.0.0.1')

            client_key_der = client_key.private_bytes(
                encoding=serialization.Encoding.DER,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption())
            self.client_cert_der = self.client_cert.public_bytes(encoding=serialization.Encoding.DER)

            client_key_file = tempfile.NamedTemporaryFile()
            self._cleanup_list.append(client_key_file.close)
            client_key_file.write(client_key_der)
            client_key_file.flush()

            client_cert_file = tempfile.NamedTemporaryFile()
            self._cleanup_list.append(client_cert_file.close)
            client_cert_file.write(self.client_cert_der)
            client_cert_file.flush()

            super().setUp(client_ca_file=trust_store_file.name,
                          server_crt='self-signed/server.crt',
                          server_crt_file='self-signed/server.crt.der',
                          server_key='self-signed/server.key',
                          client_crt_file=client_cert_file.name,
                          client_key_file=client_key_file.name,
                          extra_cmdline_args=['--pkix-trust-store', trust_store_file.name,
                                              '--rebuild-client-cert-chain'],
                          ciphersuites=[])
        except:
            self._cleanup_list()
            raise

    def runTest(self):
        import time

        certificate_packet = None
        deadline = time.time() + self.DEFAULT_MSG_TIMEOUT
        while certificate_packet is None and time.time() <= deadline:
            time.sleep(0.1)
            certificate_packet = self.get_certificate_packet()

        self.assertIsNotNone(certificate_packet)
        self.assertIn(self.read_public_key(self.client_cert), certificate_packet)
        self.assertIn(self.read_public_key(self.cert1), certificate_packet)
        self.assertIn(self.read_public_key(self.cert2), certificate_packet)

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            self._cleanup_list()

class RegisterUDPWithCyclicCertificateChainRebuilt(RegisterWithCyclicCertificateChainRebuiltMixin,
                                                    CertificatesTest.PcapEnabledTestUDP):
    pass

@unittest.skipIf(pymbedtls.Context.supports_TLS_1_3(), "TLS 1.3 encrypts its handshake messages")
class RegisterTCPWithCyclicCertificateChainRebuilt(RegisterWithCyclicCertificateChainRebuiltMixin,
                                                    CertificatesTest.PcapEnabledTestTCP):
    pass


class RegisterWithCertificateChainExternalPersistenceMixin:
    def setUp(self):
        self._dm_persistence_file = tempfile.NamedTemporaryFile()
        super().setUp(extra_cmdline_args=['--dm-persistence-file', self._dm_persistence_file.name])

    def runTest(self):
        super().runTest()

        self.request_demo_shutdown()
        self.assertDemoDeregisters()
        self._terminate_demo()

        self._start_demo(['--dm-persistence-file', self._dm_persistence_file.name]
                         + self.make_demo_args(DEMO_ENDPOINT_NAME, [], '1.0', '1.0', None))
        if self.serv.transport == Transport.TCP:
            self.assertTcpCsm()
            self.assertDemoRegisters(binding='T')
        else:
            self.assertDemoRegisters()

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            self._dm_persistence_file.close()

class RegisterUDPWithCertificateChainExternalPersistence(RegisterWithCertificateChainExternalPersistenceMixin,
                                                         RegisterWithCertificateChainExternalMixin,
                                                         CertificatesTest.PcapEnabledTestUDP):
    pass

@unittest.skipIf(pymbedtls.Context.supports_TLS_1_3(), "TLS 1.3 encrypts its handshake messages")
class RegisterTCPWithCertificateChainExternalPersistence(RegisterWithCertificateChainExternalPersistenceMixin,
                                                         RegisterWithCertificateChainExternalMixin,
                                                         CertificatesTest.PcapEnabledTestTCP):
    pass


class RegisterWithSelfSignedCertificatesAndServerPublicKeyMixin:
    def setUp(self):
        super().setUp(server_crt='self-signed/server.crt', server_key='self-signed/server.key',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      server_crt_file='self-signed/server.crt.der')

class RegisterUDPWithSelfSignedCertificatesAndServerPublicKey(
    RegisterWithSelfSignedCertificatesAndServerPublicKeyMixin, CertificatesTest.TestUDPMixin):
    pass

class RegisterTCPWithSelfSignedCertificatesAndServerPublicKey(
    RegisterWithSelfSignedCertificatesAndServerPublicKeyMixin, CertificatesTest.TestTCPMixin):
    pass


class RegisterWithMismatchedServerPublicKeyMixin:
    auto_deregister = False
    def setUp(self):
        super().setUp(server_crt='self-signed/server.crt', server_key='self-signed/server.key',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      # Use server.crt.der generated from different server.crt (not self-signed/server.crt)
                      server_crt_file='server.crt.der', auto_register=False)

    def runTest(self):
        with self.assertRaises((RuntimeError, socket.timeout)):
            self.assertDemoRegisters(self.serv)
            self.auto_deregister = True
        # -9984 == -0x2700 == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
        self.read_log_until_match(b'handshake failed: -9984', 1)

    def tearDown(self):
        super().tearDown(auto_deregister=self.auto_deregister)

class RegisterUDPWithMismatchedServerPublicKey(
    RegisterWithMismatchedServerPublicKeyMixin, CertificatesTest.TestUDPMixin):
    pass

class RegisterTCPWithMismatchedServerPublicKey(
    RegisterWithMismatchedServerPublicKeyMixin, CertificatesTest.TestTCPMixin):
    pass


class RegisterWithCertificatesAndServerPublicKeyMixin:
    def setUp(self):
        super().setUp(server_crt='server.crt', server_key='server.key',
                      client_crt_file=None, client_key_file=None, server_crt_file='server.crt.der')

@unittest.skip("TODO: broken due to T1083")
class RegisterUDPWithCertificatesAndServerPublicKey(
    RegisterWithCertificatesAndServerPublicKeyMixin, CertificatesTest.TestUDPMixin):
    pass

@unittest.skip("TODO: broken due to T1083")
class RegisterTCPWithCertificatesAndServerPublicKey(
    RegisterWithCertificatesAndServerPublicKeyMixin, CertificatesTest.TestTCPMixin):
    pass

class RegisterMixin:
    extra_objects = []
    def expected_content(self, version='1.0'):
        result = []
        for obj in sorted(ResPath.objects() + self.extra_objects, key=lambda field: field.oid):
            if obj.oid == OID.Security:
                # Security (/0) instances MUST not be a part of the list
                # see LwM2M spec, Register/Update operations description
                continue

            if obj.oid == OID.Server:
                result.append('</%d/1>' % (obj.oid,))
            elif obj.oid == OID.SoftwareManagement:
                result.append('</%d/0>' % (obj.oid,))
                result.append('</%d/1>' % (obj.oid,))
            elif obj.oid == OID.Lwm2mGateway:
                entry = '</%d>' % (obj.oid,)
                if obj.version is not None:
                    if version == '1.0':
                        entry += ';ver="%s"' % (obj.version,)
                    else:
                        entry += ';ver=%s' % (obj.version,)
                result.append(entry)
                result.append('</%d/0>' % (obj.oid,))
                result.append('</%d/1>' % (obj.oid,))
            elif obj.is_multi_instance or obj.version is not None:
                entry = '</%d>' % (obj.oid,)
                if obj.version is not None:
                    if version == '1.0':
                        entry += ';ver="%s"' % (obj.version,)
                    else:
                        entry += ';ver=%s' % (obj.version,)
                result.append(entry)
            if not obj.is_multi_instance:
                result.append('</%d/0>' % (obj.oid,))

        return ','.join(result).encode()


class BlockRegister:
    class Test(RegisterMixin, unittest.TestCase):
        def __call__(self, server, timeout_s=None, verify=True, version='1.0'):
            register_content = b''
            while True:
                if timeout_s is None:
                    pkt = server.recv()
                else:
                    pkt = server.recv(timeout_s=timeout_s)

                block1 = pkt.get_options(coap.Option.BLOCK1)
                self.assertIn(len(block1), {0, 1})
                register_content += pkt.content
                if len(block1) < 1 or not block1[0].has_more():
                    break
                server.send(Lwm2mContinue.matching(pkt)(options=block1))

            if verify:
                self.assertEqual(self.expected_content(version), register_content)

            server.send(Lwm2mCreated.matching(pkt)(location='/rd/demo', options=block1))



class Register:
    class TestCase(RegisterMixin, test_suite.Lwm2mDmOperations):
        def setUp(self, *args, **kwargs):
            self.extra_objects = []
            # skip initial registration
            super().setUp(auto_register=False, *args, **kwargs)



class RegisterUdp:
    class TestCase(Register.TestCase, test_suite.Lwm2mSingleServerTest):
        pass


class RegisterTcp:
    class TestCase(Register.TestCase, test_suite.Lwm2mSingleTcpServerTest):
        pass


class RegisterTcpWithAbort(
        test_suite.Lwm2mSingleTcpServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        # skip initial registration
        super().setUp(auto_register=False)

    def runTest(self):
        self.assertTcpCsm()
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister(
                '/rd?lwm2m=1.0&ep=%s&lt=86400&b=T' %
                (DEMO_ENDPOINT_NAME)), pkt)

        abort_pkt = coap.Packet(code=coap.Code.SIGNALING_ABORT)
        self.serv.send(abort_pkt)

        pkt = self.serv.recv()
        self.assertMsgEqual(abort_pkt, pkt)

        self.serv.reset()
        # No message should arrive
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class RegisterTest(RegisterUdp.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=self.expected_content()),
            pkt)

        # should retry when no response is sent
        pkt = self.serv.recv(timeout_s=6)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=self.expected_content()),
            pkt)

        # should ignore this message as Message ID does not match
        self.serv.send(Lwm2mCreated(msg_id=((pkt.msg_id + 1) % (1 << 16)),
                                    token=pkt.token,
                                    location='/rd/demo'))

        # should retry
        pkt = self.serv.recv(timeout_s=12)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=self.expected_content()),
            pkt)

        # should not retry after receiving valid response
        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


class InitialRegistrationDelayTest(RegisterUdp.TestCase):
    INITIAL_REGISTRATION_DELAY_S = 5
    RETRY_TIMER_S = 2
    INITIAL_REGISTRATION_DELAY_LOG = \
        b'Scheduling enabling server SSID 1 in 5 seconds'

    def setUp(self):
        super().setUp(
            maximum_version='1.1',
            extra_cmdline_args=[
                '--initial-registration-delay-timer',
                str(self.INITIAL_REGISTRATION_DELAY_S),
                '--retry-count', '2',
                '--retry-timer', str(self.RETRY_TIMER_S)
            ])

    def runTest(self):
        # no communication for the Initial Registration Delay time
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=self.INITIAL_REGISTRATION_DELAY_S - 1)

        log_offset, match = self.read_log_until_match(
            self.INITIAL_REGISTRATION_DELAY_LOG, timeout_s=1, alt_offset=0)
        self.assertIsNotNone(match)

        register = self.assertDemoRegisters(
            version='1.1', timeout_s=2, respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(register)(
            coap.Code.RES_UNAUTHORIZED))

        # The timer is not applied for retries
        self.assertDemoRegisters(
            version='1.1', timeout_s=self.RETRY_TIMER_S + 1)

        self.communicate('send-update')
        update = self.assertDemoUpdatesRegistration(
            timeout_s=2, respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(update)(
            coap.Code.RES_NOT_FOUND))

        # The timer is not applied for re-registers
        self.assertDemoRegisters(version='1.1', timeout_s=2)

        _, match = self.read_log_until_match(
            self.INITIAL_REGISTRATION_DELAY_LOG, timeout_s=0,
            alt_offset=log_offset)
        self.assertIsNone(match)


class InitialRegistrationDelayUpdateTimeoutTest(RegisterUdp.TestCase):
    INITIAL_REGISTRATION_DELAY_S = 5
    INITIAL_REGISTRATION_DELAY_LOG = \
        b'Scheduling enabling server SSID 1 in 5 seconds'

    def setUp(self):
        super().setUp(
            maximum_version='1.1',
            extra_cmdline_args=[
                '--initial-registration-delay-timer',
                str(self.INITIAL_REGISTRATION_DELAY_S),
                '--ack-random-factor', '1',
                '--ack-timeout', '1',
                '--max-retransmit', '0'
            ])

    def runTest(self):
        # The first Register shall be delayed by Initial Registration Delay
        # Timer.
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=self.INITIAL_REGISTRATION_DELAY_S - 1)

        log_offset, match = self.read_log_until_match(
            self.INITIAL_REGISTRATION_DELAY_LOG, timeout_s=1, alt_offset=0)
        self.assertIsNotNone(match)

        self.assertDemoRegisters(version='1.1', timeout_s=2)

        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(timeout_s=2, respond=False)

        # No response to Update makes the client fall back to Register after
        # the CoAP exchange times out. Initial Registration Delay Timer shall
        # not be applied to this re-registration.
        self.assertDemoRegisters(version='1.1',
                                 timeout_s=self.INITIAL_REGISTRATION_DELAY_S - 1)

        _, match = self.read_log_until_match(
            self.INITIAL_REGISTRATION_DELAY_LOG, timeout_s=0,
            alt_offset=log_offset)
        self.assertIsNone(match)


class InitialRegistrationDelayMultipleServersTest(Register.TestCase,
                                                  test_suite.Lwm2mTest):
    INITIAL_REGISTRATION_DELAY_S = 5

    def initial_registration_delay_log(self, ssid, delay_s):
        return ('Scheduling enabling server SSID %d in %d seconds'
                % (ssid, delay_s)).encode()

    def setUp(self):
        super().setUp(
            servers=2,
            maximum_version='1.1',
            extra_cmdline_args=[
                # Options added after all --server-uri arguments are applied
                # to the last configured server. This leaves SSID 1 with the
                # default Initial Registration Delay Timer value of 0 and sets
                # a nonzero delay only for SSID 2.
                '--initial-registration-delay-timer',
                str(self.INITIAL_REGISTRATION_DELAY_S)
            ])

    def runTest(self):
        # SSID 1 is bound to self.servers[0]. It uses the default delay of
        # 0 seconds, so the client shall contact it immediately. This is checked
        # before waiting on the delayed server to make the expected order of
        # first registrations explicit.
        self.assertDemoRegisters(self.servers[0], version='1.1', timeout_s=2)

        _, match = self.read_log_until_match(
            self.initial_registration_delay_log(ssid=1, delay_s=0),
            timeout_s=1, alt_offset=0)
        self.assertIsNotNone(match)

        # SSID 2 is bound to self.servers[1]. It has an explicit Initial
        # Registration Delay Timer, so the client shall not send Register there
        # while the delay is still running.
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=self.INITIAL_REGISTRATION_DELAY_S - 2)

        _, match = self.read_log_until_match(
            self.initial_registration_delay_log(ssid=2,
                                                delay_s=self.INITIAL_REGISTRATION_DELAY_S),
            timeout_s=1, alt_offset=0)
        self.assertIsNotNone(match)

        # After the delay expires, self.servers[1] shall finally receive the
        # first Register. If the implementation accidentally used a global flag
        # or timer, this per-server distinction would be lost.
        self.assertDemoRegisters(self.servers[1], version='1.1', timeout_s=2)


class RegisterRejectLogsClientErrorResponse(RegisterUdp.TestCase):
    # regex to match Anjay log
    REGISTER_REJECTED_REGEX = re.compile(
        rb'server responded with (\d\.\d{2} [^)\n]+) \(expected 2\.01 Created\)')

    def setUp(self):
        super().setUp(extra_cmdline_args=[
            '--retry-count', '1',
            '--sequence-retry-count', '0'
        ])

    def runTest(self):
        def check(code: coap.Code, trigger_register=False):
            if trigger_register:
                self.communicate('enable-server 1')

            req = self.assertDemoRegisters(respond=False)
            self.serv.send(Lwm2mErrorResponse.matching(req)(code))

            assert_server_communication_error_logs(
                self, self.REGISTER_REJECTED_REGEX, 1, code)

            # give demo a second to process everything; the log above is printed
            # before the response is fully handled internally
            time.sleep(1)

        # check all possible client (4.xx) errors
        for detail in range(16):
            if detail == 13:
                # ignore Request Entity Too Large
                continue
            check(coap.Code(4, detail), trigger_register=(detail != 0))

        # check all possible server (5.xx) errors
        for detail in range(6):
            check(coap.Code(5, detail), trigger_register=True)

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class RegisterCheckOngoingRegistrations(RegisterUdp.TestCase):
    def runTest(self):
        self.assertTrue(self.ongoing_registration_exists())

        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=self.expected_content()), pkt)

        self.assertTrue(self.ongoing_registration_exists())

        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        self.assertFalse(self.ongoing_registration_exists())


class RegisterWithLostSeparateAck(RegisterUdp.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=self.expected_content()),
            pkt)

        # Separate Response: Confirmable; msg_id does not match, but token does
        res = Lwm2mCreated(msg_id=((pkt.msg_id + 1) % (1 << 16)),
                           token=pkt.token,
                           location='/rd/demo')
        res.type = coap.Type.CONFIRMABLE

        # should respond with Empty ACK
        self.serv.send(res)

        self.assertMsgEqual(Lwm2mEmpty.matching(res)(),
                            self.serv.recv())


class RegisterWithBlock(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        extra_args = '-I 64 -O 128'.split()
        self.setup_demo_with_servers(servers=1,
                                     extra_cmdline_args=extra_args,
                                     auto_register=False)

    def runTest(self):
        BlockRegister().Test()(self.serv)
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


class ConcurrentRequestWhileWaitingForResponse:
    class TestMixin:
        def runTest(self):
            path = '/rd?lwm2m=1.0&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME
            if self.serv.transport == Transport.TCP:
                path += '&b=T'
                self.assertTcpCsm()
            pkt = self.serv.recv()
            self.assertMsgEqual(Lwm2mRegister(path, content=self.expected_content()), pkt)
            self.read_path(self.serv, ResPath.Device.Manufacturer)
            self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))


class ConcurrentRequestWhileWaitingForResponseUdp(
    ConcurrentRequestWhileWaitingForResponse.TestMixin, RegisterUdp.TestCase):
    pass


class ConcurrentRequestWhileWaitingForResponseTcp(
    ConcurrentRequestWhileWaitingForResponse.TestMixin, RegisterTcp.TestCase):
    pass


class RegisterUri:
    class TestMixin:
        def make_demo_args(self, *args, **kwargs):
            args = super().make_demo_args(*args, **kwargs)
            for i in range(len(args)):
                if args[i].startswith('coap'):
                    args[i] += '/i/am/crazy/and?lwm2m=i&ep=know&lt=it'
            return args

        def runTest(self):
            path = '/i/am/crazy/and/rd?lwm2m=i&ep=know&lt=it&lwm2m=1.0&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME
            if self.serv.transport == Transport.TCP:
                path += '&b=T'
                self.assertTcpCsm()
            pkt = self.serv.recv()
            self.assertMsgEqual(Lwm2mRegister(path, content=self.expected_content()), pkt)
            self.serv.send(Lwm2mCreated.matching(pkt)(location='/some/weird/rd/point'))

            # Update shall not contain the path and query from Server URI
            self.communicate('send-update')
            pkt = self.serv.recv()
            self.assertMsgEqual(Lwm2mUpdate('/some/weird/rd/point', query=[], content=b''),
                                pkt)
            self.serv.send(Lwm2mChanged.matching(pkt)())

        def tearDown(self):
            self.teardown_demo_with_servers(path='/some/weird/rd/point')


class RegisterUriUdp(RegisterUri.TestMixin, RegisterUdp.TestCase):
    pass


class RegisterUriTcp(RegisterUri.TestMixin, RegisterTcp.TestCase):
    pass


class RegisterWithPskExternal(test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self, extra_cmdline_args=[], **kwargs):
        extra_cmdline_args += ['--use-external-security-info']
        super().setUp(extra_cmdline_args=extra_cmdline_args, **kwargs)


class RegisterSni(test_suite.PcapEnabledTest,
                  test_suite.Lwm2mDtlsSingleServerTest):
    SNI = 'SomeServerHost'

    def setUp(self):
        extra_args = ['--sni', self.SNI]
        super().setUp(extra_cmdline_args=extra_args, auto_register=False)

    def runTest(self):
        pkt = self.serv._raw_udp_socket.recv(4096)
        self.assertPktIsDtlsClientHello(pkt, seq_number=0)
        self.assertIn(bytes(self.SNI, 'ascii'), pkt)
        self.assertDemoRegisters()


class Lwm2m11BindingSemantics(bootstrap_client.BootstrapTest.Test):
    @property
    def tcp_serv(self) -> Lwm2mServer:
        return self.servers[1]

    def setUp(self):
        super().setUp(servers=[Lwm2mServer(), Lwm2mServer(coap.Server(transport=Transport.TCP))],
                      maximum_version='1.1')

    def runTest(self):
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        self.bootstrap_server.send(Lwm2mDelete('/'))
        self.assertIsInstance(self.bootstrap_server.recv(), Lwm2mDeleted)

        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=101,
                            content=TLV.make_resource(RID.Security.ServerURI,
                                                      'coap://127.0.0.1:%d' % self.serv.get_listen_port()).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode,
                                                        SecurityMode.NoSec.value).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID,
                                                        100).serialize())

        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=102,
                            content=TLV.make_resource(RID.Security.ServerURI,
                                                      'coap+tcp://127.0.0.1:%d' % self.tcp_serv.get_listen_port()).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode,
                                                        SecurityMode.NoSec.value).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID,
                                                        100).serialize())

        self.write_instance(self.bootstrap_server, oid=OID.Server, iid=100,
                            content=TLV.make_resource(RID.Server.Lifetime, 86400).serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID, 100).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring,
                                                        True).serialize()
                                    + TLV.make_resource(RID.Server.Binding, 'UT').serialize())

        self.perform_bootstrap_finish()

        # register with UDP binding
        pkt = self.serv.recv()
        self.assertIsInstance(pkt, Lwm2mRegister)
        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # change binding and register with TCP binding
        # receive CSM packet in background to avoid race condition
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            future = executor.submit(self.tcp_serv.recv, timeout_s=10)
            self.write_resource(self.serv, OID.Server, 100, RID.Server.Binding, 'TU')
            pkt = future.result()

        assert pkt.code == coap.Code.SIGNALING_CSM
        self.tcp_serv.send(coap.Packet(code=coap.Code.SIGNALING_CSM, token=b''))

        pkt = self.tcp_serv.recv()
        self.assertIsInstance(pkt, Lwm2mRegister)
        self.tcp_serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        self.serv.reset()

        # set Preferred Transport
        self.write_resource(self.tcp_serv, OID.Server, 100, RID.Server.PreferredTransport, 'U')

        # register with UDP binding
        pkt = self.serv.recv()
        self.assertIsInstance(pkt, Lwm2mRegister)
        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

    def tearDown(self):
        super().tearDown(deregister_servers=[self.serv])


class RegisterVersionSemanticAfterBootstrapFallback(
        bootstrap_client.BootstrapTest.Test):
    def setUp(self):
        super().setUp(minimum_version='1.0',
                      maximum_version='1.1',
                      legacy_server_initiated_bootstrap_allowed=False)

    def _perform_bootstrap_lwm2m11(self):
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        self.add_server(
            server_iid=1,
            security_iid=2,
            server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port())
        self.perform_bootstrap_finish()

    def runTest(self):
        # Initial client-initiated bootstrap
        self._perform_bootstrap_lwm2m11()

        first_register_req = self.assertDemoRegisters(
            self.serv, version='1.1', respond=False)
        self.serv.send(Lwm2mCreated.matching(first_register_req)(
            location=self.DEFAULT_REGISTER_ENDPOINT))

        # Trigger server-initiated bootstrap
        self.execute_resource(self.serv, OID.Server, 1,
                              RID.Server.RequestBootstrapTrigger)
        self._perform_bootstrap_lwm2m11()

        second_register_req = self.assertDemoRegisters(
            self.serv, version='1.1', respond=False)
        self.serv.send(Lwm2mCreated.matching(second_register_req)(
            location=self.DEFAULT_REGISTER_ENDPOINT))

        # confirm that the payload is not build in lwm2m 1.0 semantic
        self.assertEqual(
            first_register_req.content,
            second_register_req.content)
        # ;ver="1.1" is lwm2m 1.0 semantic, ;ver=1.1 is lwm2m 1.1 semantic
        self.assertIn(b'</10>;ver=1.', second_register_req.content)

        # Trigger another server-initiated bootstrap
        self.execute_resource(self.serv, OID.Server, 1,
                              RID.Server.RequestBootstrapTrigger)
        self._perform_bootstrap_lwm2m11()

        # This time reject the registration with Precondition Failed to trigger
        # fallback to lwm2m 1.0
        third_register_req = self.assertDemoRegisters(
            self.serv, version='1.1', respond=False)
        self.assertEqual(
            first_register_req.content,
            third_register_req.content)
        self.serv.send(
            Lwm2mErrorResponse.matching(third_register_req)(
                coap.Code.RES_PRECONDITION_FAILED))

        # Next registration attempt should be with lwm2m 1.0 set
        fourth_register_req = self.assertDemoRegisters(
            self.serv, version='1.0', respond=False)
        self.serv.send(Lwm2mCreated.matching(fourth_register_req)(
            location=self.DEFAULT_REGISTER_ENDPOINT))
        # confirm that the payload is also build in lwm2m 1.0 semantic
        self.assertIn(b'</10>;ver="1.', fourth_register_req.content)
