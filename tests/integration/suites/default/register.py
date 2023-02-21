# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import concurrent.futures
import os
import socket
import unittest

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m.coap.transport import Transport
from framework.lwm2m_test import *
from suites.default import bootstrap_client


class CertificatesTest:
    class Test(test_suite.Lwm2mSingleServerTest):
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
                        and isinstance(pkt.data, dpkt.udp.UDP) \
                        and pkt.data.dport == self.serv.get_listen_port() \
                        and common_name in pkt.data.data:
                    return pkt.data.data
            return None

    class PcapEnabledTest(test_suite.PcapEnabledTest, Test, PcapEnabledTestMixin):
        pass



class RegisterWithCertificates(CertificatesTest.Test):
    def setUp(self):
        super().setUp(server_crt='server.crt', server_key='server.key')


class RegisterWithCertificateChainExternal(CertificatesTest.PcapEnabledTest):
    def setUp(self, extra_cmdline_args=None, **kwargs):
        if extra_cmdline_args is None:
            extra_cmdline_args = []
        extra_cmdline_args.append('--use-external-security-info')
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


class RegisterWithCertificateChainRebuilt(CertificatesTest.PcapEnabledTest):
    def setUp(self):
        super().setUp(client_ca_file='root.crt', server_crt='server.crt', server_key='server.key',
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


class RegisterWithCyclicCertificateChainRebuilt(CertificatesTest.PcapEnabledTest):
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


class RegisterWithCertificateChainExternalPersistence(RegisterWithCertificateChainExternal):
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
        self.assertDemoRegisters()

    def tearDown(self):
        try:
            super().tearDown()
        finally:
            self._dm_persistence_file.close()


class RegisterWithSelfSignedCertificatesAndServerPublicKey(CertificatesTest.Test):
    def setUp(self):
        super().setUp(server_crt='self-signed/server.crt', server_key='self-signed/server.key',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      server_crt_file='self-signed/server.crt.der')


class RegisterWithMismatchedServerPublicKey(CertificatesTest.Test):
    def setUp(self):
        super().setUp(server_crt='self-signed/server.crt', server_key='self-signed/server.key',
                      client_crt_file='self-signed/client.crt.der',
                      client_key_file='self-signed/client.key.der',
                      # Use server.crt.der generated from different server.crt (not self-signed/server.crt)
                      server_crt_file='server.crt.der', auto_register=False)

    def runTest(self):
        with self.assertRaises((RuntimeError, socket.timeout)):
            self.assertDemoRegisters(self.serv)
        # -9984 == -0x2700 == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
        self.read_log_until_match(b'handshake failed: -9984', 1)

    def tearDown(self):
        super().tearDown(auto_deregister=False)


@unittest.skip("TODO: broken due to T1083")
class RegisterWithCertificatesAndServerPublicKey(CertificatesTest.Test):
    def setUp(self):
        super().setUp(server_crt='server.crt', server_key='server.key',
                      client_crt_file=None, client_key_file=None, server_crt_file='server.crt.der')


class BlockRegister:
    class Test(unittest.TestCase):
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
                self.assertEquals(expected_content(version), register_content)

            server.send(Lwm2mCreated.matching(pkt)(location='/rd/demo', options=block1))


class Register:
    class TestCase(test_suite.Lwm2mDmOperations):
        def setUp(self):
            # skip initial registration
            super().setUp(auto_register=False)


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


def expected_content(version='1.0'):
    result = []
    for obj in ResPath.objects():
        if obj.oid == OID.Security:
            # Security (/0) instances MUST not be a part of the list
            # see LwM2M spec, Register/Update operations description
            continue

        if obj.oid == OID.Server:
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


class RegisterTest(RegisterUdp.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
            pkt)

        # should retry when no response is sent
        pkt = self.serv.recv(timeout_s=6)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
            pkt)

        # should ignore this message as Message ID does not match
        self.serv.send(Lwm2mCreated(msg_id=((pkt.msg_id + 1) % (1 << 16)),
                                    token=pkt.token,
                                    location='/rd/demo'))

        # should retry
        pkt = self.serv.recv(timeout_s=12)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
            pkt)

        # should not retry after receiving valid response
        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


class RegisterCheckOngoingRegistrations(RegisterUdp.TestCase):
    def runTest(self):
        self.assertTrue(self.ongoing_registration_exists())

        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()), pkt)

        self.assertTrue(self.ongoing_registration_exists())

        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        self.assertFalse(self.ongoing_registration_exists())


class RegisterWithLostSeparateAck(RegisterUdp.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
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
            pkt = self.serv.recv()
            path = '/rd?lwm2m=1.0&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME
            if self.serv.transport == Transport.TCP:
                path += '&b=T'
            self.assertMsgEqual(Lwm2mRegister(path, content=expected_content()), pkt)
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
            pkt = self.serv.recv()
            path = '/i/am/crazy/and/rd?lwm2m=i&ep=know&lt=it&lwm2m=1.0&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME
            if self.serv.transport == Transport.TCP:
                path += '&b=T'
            self.assertMsgEqual(Lwm2mRegister(path, content=expected_content()), pkt)
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
    def setUp(self, extra_cmdline_args=None, **kwargs):
        if extra_cmdline_args is None:
            extra_cmdline_args = []
        extra_cmdline_args.append('--use-external-security-info')
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
        # receive register packet in background to avoid race condition
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            future = executor.submit(self.tcp_serv.recv, timeout_s=10)
            self.write_resource(self.serv, OID.Server, 100, RID.Server.Binding, 'TU')
            pkt = future.result()

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
