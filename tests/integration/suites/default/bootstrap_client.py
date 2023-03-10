# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m.coap.code import Code
from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *


class BootstrapTest:
    class TestMixin:
        def perform_bootstrap_finish(self):
            req = Lwm2mBootstrapFinish()
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(
                req)(), self.bootstrap_server.recv())

        def add_server(self,
                       server_iid,
                       security_iid,
                       server_uri,
                       lifetime=86400,
                       secure_identity=b'',
                       secure_key=b'',
                       security_mode: SecurityMode = SecurityMode.NoSec,
                       binding="U",
                       additional_security_data=b'',
                       additional_server_data=b'',
                       bootstrap_on_registration_failure=None,
                       server_communication_retry_count=1,
                       server_communication_retry_timer=0,
                       server_communication_sequence_retry_count=1,
                       server_communication_sequence_delay_timer=0):
            if bootstrap_on_registration_failure is not None:
                additional_server_data += TLV.make_resource(
                    RID.Server.BootstrapOnRegistrationFailure,
                    bootstrap_on_registration_failure).serialize()

            if server_communication_retry_count is not None:
                additional_server_data += TLV.make_resource(
                    RID.Server.ServerCommunicationRetryCount,
                    server_communication_retry_count).serialize()
                additional_server_data += TLV.make_resource(
                    RID.Server.ServerCommunicationRetryTimer,
                    server_communication_retry_timer).serialize()
                additional_server_data += TLV.make_resource(
                    RID.Server.ServerCommunicationSequenceRetryCount,
                    server_communication_sequence_retry_count).serialize()
                additional_server_data += TLV.make_resource(
                    RID.Server.ServerCommunicationSequenceDelayTimer,
                    server_communication_sequence_delay_timer).serialize()

            # Create typical Server Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Server, iid=server_iid,
                                content=TLV.make_resource(
                                    RID.Server.Lifetime, lifetime).serialize()
                                        + TLV.make_resource(RID.Server.ShortServerID,
                                                            server_iid).serialize()
                                        + TLV.make_resource(RID.Server.NotificationStoring,
                                                            True).serialize()
                                        + TLV.make_resource(RID.Server.Binding, binding).serialize()
                                        + additional_server_data)

            # Create typical (corresponding) Security Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Security, iid=security_iid,
                                content=TLV.make_resource(
                                    RID.Security.ServerURI, server_uri).serialize()
                                        + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                        + TLV.make_resource(RID.Security.Mode,
                                                            security_mode.value).serialize()
                                        + TLV.make_resource(RID.Security.ShortServerID,
                                                            server_iid).serialize()
                                        + TLV.make_resource(RID.Security.PKOrIdentity,
                                                            secure_identity).serialize()
                                        + TLV.make_resource(RID.Security.SecretKey,
                                                            secure_key).serialize()
                                        + additional_security_data)

        def perform_typical_bootstrap(self, server_iid, security_iid, server_uri, lifetime=86400,
                                      secure_identity=b'', secure_key=b'',
                                      security_mode: SecurityMode = SecurityMode.NoSec,
                                      finish=True, holdoff_s=None, binding="U",
                                      clear_everything=False,
                                      endpoint=DEMO_ENDPOINT_NAME,
                                      additional_security_data=b'',
                                      additional_server_data=b'',
                                      bootstrap_on_registration_failure=None,
                                      bootstrap_request_timeout_s=None):
            # For the first holdoff_s seconds, the client should wait for
            # 1.0-style Server Initiated Bootstrap. Note that we subtract
            # 1 second to take into account code execution delays.
            if holdoff_s is None:
                holdoff_s = self.holdoff_s or 0
            no_message_s = max(0, holdoff_s - 1)
            if no_message_s > 0:
                with self.assertRaises(socket.timeout):
                    print(self.bootstrap_server.recv(timeout_s=no_message_s))

            # We should get Bootstrap Request now
            if bootstrap_request_timeout_s is None:
                self.assertDemoRequestsBootstrap(endpoint=endpoint)
            elif bootstrap_request_timeout_s >= 0:
                self.assertDemoRequestsBootstrap(
                    endpoint=endpoint, timeout_s=bootstrap_request_timeout_s)

            if clear_everything:
                req = Lwm2mDelete('/')
                self.bootstrap_server.send(req)
                self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                                    self.bootstrap_server.recv())

            self.add_server(server_iid=server_iid,
                            security_iid=security_iid,
                            server_uri=server_uri,
                            lifetime=lifetime,
                            secure_identity=secure_identity,
                            secure_key=secure_key,
                            security_mode=security_mode,
                            binding=binding,
                            additional_security_data=additional_security_data,
                            additional_server_data=additional_server_data,
                            bootstrap_on_registration_failure=bootstrap_on_registration_failure)

            if finish:
                self.perform_bootstrap_finish()

    class Test(TestMixin, test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
        def setUp(self, servers=1, num_servers_passed=0, holdoff_s=None, timeout_s=None,
                  bootstrap_server=True,
                  extra_cmdline_args=None, **kwargs):
            assert bootstrap_server
            extra_args = extra_cmdline_args or []
            if holdoff_s is not None:
                extra_args += ['--bootstrap-holdoff', str(holdoff_s)]
            if timeout_s is not None:
                extra_args += ['--bootstrap-timeout', str(timeout_s)]

            self.holdoff_s = holdoff_s
            self.timeout_s = timeout_s
            super().setUp(servers=servers, num_servers_passed=num_servers_passed,
                          bootstrap_server=bootstrap_server,
                          extra_cmdline_args=extra_args, **kwargs)


class BootstrapClientTest(BootstrapTest.Test):
    def setUp(self):
        super().setUp(holdoff_s=3, timeout_s=3)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=60)

        # should now register with the non-bootstrap server
        self.assertDemoRegisters(self.serv, lifetime=60)

        self.assertEqual(2, self.get_socket_count())

        # no message for now
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=1))

        # no changes
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        # still no message
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=3))

        # now the Bootstrap Server Account should be purged...
        self.assertEqual(1, self.get_socket_count())

        # and we should get ICMP port unreachable on Bootstrap Finish...
        self.bootstrap_server.send(Lwm2mBootstrapFinish())
        # which raises ConnectionRefusedError on a socket.
        with self.assertRaises(ConnectionRefusedError):
            self.bootstrap_server.recv()

        # client did not try to register to a Bootstrap server (as in T847)
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=1))

        # ensure that bootstrap account was purged and client won't accept Request Bootstrap Trigger
        self.execute_resource(server=self.serv, oid=OID.Server, iid=1, rid=RID.Server.RequestBootstrapTrigger,
                              expect_error_code=Code.RES_METHOD_NOT_ALLOWED)


class BootstrapOneResourceAtATimeTest(BootstrapTest.Test):
    def runTest(self):
        self.assertDemoRequestsBootstrap(endpoint=DEMO_ENDPOINT_NAME)
        # Create typical Server Object instance
        for rid, value in ((RID.Server.Lifetime, '86400'),
                           (RID.Server.ShortServerID, '1'),
                           (RID.Server.NotificationStoring, '1'),
                           (RID.Server.Binding, 'U')):
            self.write_resource(self.bootstrap_server, oid=OID.Server, iid=1, rid=rid,
                                content=value)
        # Create typical (corresponding) Security Object instance
        for rid, value in ((RID.Security.ServerURI,
                            'coap://127.0.0.1:%d' % self.serv.get_listen_port()),
                           (RID.Security.Bootstrap, '0'),
                           (RID.Security.Mode, str(SecurityMode.NoSec.value)),
                           (RID.Security.ShortServerID, '1')):
            self.write_resource(self.bootstrap_server, oid=OID.Security, iid=2, rid=rid,
                                content=value)
        self.perform_bootstrap_finish()

        # should now register with the non-bootstrap server
        self.assertDemoRegisters(self.serv)


class BootstrapOnRegistrationFailure(BootstrapTest.Test):
    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=60,
                                       additional_server_data=TLV.make_resource(
                                           RID.Server.BootstrapOnRegistrationFailure,
                                           True).serialize())

        self.assertDemoRegisters(self.serv, lifetime=60, reject=True)

        self.serv.reset()

        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=60,
                                       additional_server_data=TLV.make_resource(
                                           RID.Server.BootstrapOnRegistrationFailure,
                                           True).serialize())

        # There was a race condition in Anjay, which causes different behavior
        # if ANJAY_SERVER_NEXT_ACTION_REFRESH action was executed before getting
        # response to Register request. This sleep is added to ensure all jobs
        # scheduled for 'now' will be called first.
        time.sleep(1)
        self.assertDemoRegisters(self.serv, lifetime=60)


class ClientBootstrapNotSentAfterDisableWithinHoldoffTest(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1, holdoff_s=3, timeout_s=3)

    def runTest(self):
        # set Disable Timeout to 5
        self.write_resource(server=self.serv, oid=OID.Server,
                            iid=2, rid=RID.Server.DisableTimeout, content='5')
        # disable the server
        self.execute_resource(
            server=self.serv, oid=OID.Server, iid=2, rid=RID.Server.Disable)

        self.assertDemoDeregisters(self.serv)

        with self.assertRaises(socket.timeout, msg="the client should not send "
                                                   "Request Bootstrap after disabling the server"):
            self.bootstrap_server.recv(timeout_s=4)

        self.assertDemoRegisters(self.serv, timeout_s=2)


class ClientBootstrapBacksOffAfterErrorResponse(BootstrapTest.Test):
    def setUp(self):
        super().setUp(servers=0)

    def runTest(self):
        self.assertDemoRequestsBootstrap(
            respond_with_error_code=coap.Code.RES_INTERNAL_SERVER_ERROR)
        with self.assertRaises(socket.timeout, msg="the client should not send "
                                                   "Request Bootstrap immediately after receiving "
                                                   "an error response"):
            self.bootstrap_server.recv(timeout_s=1)


class ClientBootstrapReconnect(BootstrapTest.Test):
    def setUp(self):
        super().setUp(servers=0)

    def runTest(self):
        self.assertDemoRequestsBootstrap()
        self.communicate('reconnect')
        self.assertDemoRequestsBootstrap()


class MultipleBootstrapSecurityInstancesNotAllowed(BootstrapTest.Test):
    def setUp(self):
        super().setUp(servers=0)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://unused-in-this-test',
                                       lifetime=86400,
                                       finish=False)

        # Bootstrap Server MUST NOT be allowed to create second Bootstrap Security Instance
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=42,
                            content=TLV.make_resource(
                                RID.Security.ServerURI, 'coap://127.0.0.1:5683').serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 1).serialize()
                                    + TLV.make_resource(RID.Security.Mode,
                                                        3).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                                    + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                                    + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                            expect_error_code=coap.Code.RES_BAD_REQUEST)


class BootstrapUri(BootstrapTest.Test):
    def make_demo_args(self, *args, **kwargs):
        args = super().make_demo_args(*args, **kwargs)
        for i in range(len(args)):
            if args[i].startswith('coap'):
                args[i] += '/some/crazy/path?and=more&craziness'
        return args

    def runTest(self):
        self.assertDemoRequestsBootstrap(
            uri_path='/some/crazy/path', uri_query=['and=more', 'craziness'])

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class InvalidBootstrappedServer(BootstrapTest.Test):
    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=86400)

        # demo should now try to register
        req = self.serv.recv()
        self.assertMsgEqual(Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,)),
                            req)

        # respond with 4.03
        self.serv.send(Lwm2mErrorResponse.matching(req)
                       (code=coap.Code.RES_FORBIDDEN))

        # demo should retry Bootstrap
        self.assertDemoRequestsBootstrap()

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class BootstrapFinishWithTimeoutTwice(BootstrapTest.Test):
    def setUp(self):
        super().setUp(timeout_s=3600)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=86400)

        self.assertDemoRegisters()

        # send Bootstrap Finish once again to ensure client doesn't act abnormally
        self.perform_bootstrap_finish()

        self.assertDemoRegisters()


class ClientInitiatedBootstrapOnly(BootstrapTest.Test):
    def setUp(self):
        super().setUp(legacy_server_initiated_bootstrap_allowed=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port())
        self.assertDemoRegisters()

        self.assertEqual(self.get_socket_count(), 1)

        # Trigger update
        self.communicate('send-update')
        update_pkt = self.assertDemoUpdatesRegistration(
            self.serv, respond=False)
        # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
        self.serv.send(Lwm2mErrorResponse.matching(
            update_pkt)(code=coap.Code.RES_BAD_REQUEST))
        # This should cause client attempt to re-register.
        register_pkt = self.assertDemoRegisters(self.serv, respond=False)
        # To which we respond with 4.03 Forbidden, finishing off the communication.
        self.serv.send(Lwm2mErrorResponse.matching(
            register_pkt)(code=coap.Code.RES_FORBIDDEN))

        # The client shall attempt bootstrapping again
        self.assertDemoRequestsBootstrap()


class ClientInitiatedBootstrapFallbackOnly(BootstrapTest.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        # Using DTLS for Bootstrap Server allows us to check when does the client attempt to connect to it
        # We need to use DTLS for regular server as well, as mixing security modes is not currently possible in demo
        super().setUp(servers=[
            Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
            num_servers_passed=1,
            bootstrap_server=Lwm2mServer(
                coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)),
            extra_cmdline_args=['--identity',
                                str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')],
            legacy_server_initiated_bootstrap_allowed=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # registration has been performed by setUp()
        self.assertIsNotNone(self.serv.get_remote_addr())
        self.assertEqual(self.get_socket_count(), 1)

        # Trigger update
        self.communicate('send-update')
        update_pkt = self.assertDemoUpdatesRegistration(
            self.serv, respond=False)
        # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
        self.serv.send(Lwm2mErrorResponse.matching(
            update_pkt)(code=coap.Code.RES_BAD_REQUEST))
        # This should cause client attempt to re-register.
        register_pkt = self.assertDemoRegisters(self.serv, respond=False)
        # To which we respond with 4.03 Forbidden, finishing off the communication.
        self.serv.send(Lwm2mErrorResponse.matching(
            register_pkt)(code=coap.Code.RES_FORBIDDEN))

        # The client shall only now attempt bootstrap
        self.assertIsNone(self.bootstrap_server.get_remote_addr())
        self.assertDemoRequestsBootstrap()


# Tests below take absurd amount of time when advance_time is unavailable
class BootstrapNoInteractionFromBootstrapServer(BootstrapTest.Test):
    ACK_TIMEOUT = 1
    MAX_RETRANSMIT = 1

    def setUp(self):
        # Done to have a relatively short EXCHANGE_LIFETIME
        super().setUp(extra_cmdline_args=['--ack-random-factor', '1',
                                          '--ack-timeout', '%s' % self.ACK_TIMEOUT,
                                          '--max-retransmit', '%s' % self.MAX_RETRANSMIT])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # We should get Bootstrap Request now
        self.assertDemoRequestsBootstrap()

        self.assertEqual(1, self.get_socket_count())
        self.advance_demo_time(TxParams(ack_timeout=self.ACK_TIMEOUT,
                                        max_retransmit=self.MAX_RETRANSMIT).exchange_lifetime())
        self.wait_until_socket_count(0, timeout_s=5)


class BootstrapNoInteractionFromBootstrapServerAfterSomeExchanges(BootstrapTest.Test):
    ACK_TIMEOUT = 1
    MAX_RETRANSMIT = 1

    def setUp(self):
        # Done to have a relatively short EXCHANGE_LIFETIME
        super().setUp(extra_cmdline_args=['--ack-random-factor', '1',
                                          '--ack-timeout', '%s' % self.ACK_TIMEOUT,
                                          '--max-retransmit', '%s' % self.MAX_RETRANSMIT])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # Some random bootstrap operation, the data won't be used anyway.
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:9123',
                                       security_mode=SecurityMode.NoSec,
                                       finish=False)

        self.assertEqual(1, self.get_socket_count())
        self.advance_demo_time(TxParams(ack_timeout=self.ACK_TIMEOUT,
                                        max_retransmit=self.MAX_RETRANSMIT).exchange_lifetime())
        self.wait_until_socket_count(0, timeout_s=5)


class DtlsBootstrap:
    class Test(BootstrapTest.Test):
        PSK_IDENTITY = b'test-identity'
        PSK_KEY = b'test-key'

        # example ciphersuites, source: https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml
        SUPPORTED_CIPHER = 0xC0A8  # TLS_PSK_WITH_AES_128_CCM_8, recommended by RFC8252
        # TLS_PSK_WITH_AES_256_CCM_8, supported by mbed TLS and OpenSSL, but not by pymbedtls
        UNSUPPORTED_CIPHER = 0xC0A9

        def setUp(self, **kwargs):
            if 'servers' not in kwargs:
                kwargs = kwargs.copy()
                kwargs['servers'] = [Lwm2mServer(
                    coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))]
            super().setUp(**kwargs)


class DtlsTlsCiphersuitesSingleSupportedCipher(DtlsBootstrap.Test):
    """
    Verifies that setting DTLS/TLS Ciphersuite resource to a cipher supported
    by the server makes the client register correctly.
    """

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%s' % (
                                           self.serv.get_listen_port(),),
                                       security_mode=SecurityMode.PreSharedKey,
                                       secure_identity=self.PSK_IDENTITY,
                                       secure_key=self.PSK_KEY,
                                       additional_security_data=TLV.make_multires(
                                           RID.Security.DtlsTlsCiphersuite,
                                           enumerate([
                                               self.SUPPORTED_CIPHER])).serialize())
        self.assertDemoRegisters()


class DtlsTlsCiphersuitesSingleUnsupportedCipher(DtlsBootstrap.Test):
    """
    Verifies that setting DTLS/TLS Ciphersuite resource to a cipher NOT
    supported by the server makes the client fail to register, and not attempt
    to continue. A DTLS alert should be observed.
    """

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%s' % (
                                           self.serv.get_listen_port(),),
                                       security_mode=SecurityMode.PreSharedKey,
                                       secure_identity=self.PSK_IDENTITY,
                                       secure_key=self.PSK_KEY,
                                       additional_security_data=TLV.make_multires(
                                           RID.Security.DtlsTlsCiphersuite,
                                           enumerate([self.UNSUPPORTED_CIPHER])).serialize())
        with self.assertRaisesRegex(RuntimeError,
                                    r'The server has no ciphersuites in common|The handshake negotiation failed'):
            self.assertDemoRegisters()
        self.assertDemoRequestsBootstrap()


class DtlsTlsCiphersuitesUnsupportedAndSupportedCiphers(DtlsBootstrap.Test):
    """
    Verifies that setting DTLS/TLS Ciphersuite resource to a list that contains
    ciphers both supported and unsupported by server makes the client register
    correctly.
    """

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%s' % (
                                           self.serv.get_listen_port(),),
                                       security_mode=SecurityMode.PreSharedKey,
                                       secure_identity=self.PSK_IDENTITY,
                                       secure_key=self.PSK_KEY,
                                       additional_security_data=TLV.make_multires(
                                           RID.Security.DtlsTlsCiphersuite, enumerate(
                                               [self.UNSUPPORTED_CIPHER,
                                                self.SUPPORTED_CIPHER])).serialize())
        self.assertDemoRegisters()


class DtlsTlsConnectionFailedSetsAlertCode(BootstrapTest.Test):
    def runTest(self):
        dtls_server = coap.DtlsServer(psk_identity=b'foo', psk_key=b'bar')
        self.perform_typical_bootstrap(server_iid=2,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%d' % dtls_server.get_listen_port(),
                                       lifetime=86400,
                                       security_mode=SecurityMode.PreSharedKey,
                                       binding='U',
                                       secure_identity=b'foo',
                                       secure_key=b'yyy')

        try:
            dtls_server.recv_raw(4096)
        except RuntimeError as e:
            self.assertIn('mbedtls_ssl_handshake failed', str(e))

        # Bootstrap Finish succeeded, but the server was not reachable, due to bad credentials.
        self.assertDemoRequestsBootstrap(timeout_s=5)

        import json
        response = json.loads(self.read_instance(self.bootstrap_server, oid=OID.Server, iid=2,
                                                 accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON).content.decode())
        for resource in response:
            if resource['n'] == '/%d' % RID.Server.TlsDtlsAlertCode:
                # 20 is "bad_record_mac" (see https://tools.ietf.org/html/rfc5246#section-7.2 for more details)
                self.assertTrue(resource['v'] == 20)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)


class BootstrapFallback(BootstrapTest.Test):
    def setUp(self):
        super().setUp(minimum_version='1.0', maximum_version='1.1')

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap(respond_with_error_code=coap.Code.RES_BAD_REQUEST,
                                         preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        self.assertDemoRequestsBootstrap()


class BootstrapNoFallback(BootstrapTest.Test):
    def setUp(self):
        super().setUp(minimum_version='1.1', maximum_version='1.1')

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap(respond_with_error_code=coap.Code.RES_BAD_REQUEST,
                                         preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        with self.assertRaises(socket.timeout):
            self.bootstrap_server.recv(timeout_s=5)


class LastBootstrappedResource(BootstrapTest.Test):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def get_last_bootstrapped_timestamp(self, iid):
        res = self.read_instance(self.bootstrap_server, oid=OID.Server,
                                 iid=1, accept=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.assertEqual(coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         res.get_content_format())

        tlv = TLV.parse(res.content)

        for entry in tlv:
            if entry.identifier == RID.Server.LastBootstrapped:
                return entry

    def runTest(self):
        timestamp_before_bootstrap = int(time.time())
        # Some random bootstrap operation, the data won't be used anyway.
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       security_mode=SecurityMode.NoSec,
                                       finish=False)

        last_bootstrapped_resource = self.get_last_bootstrapped_timestamp(1)
        self.assertIsNotNone(last_bootstrapped_resource)
        last_bootstrapped_timestamp = int.from_bytes(
            last_bootstrapped_resource.value, byteorder='big')
        self.assertGreaterEqual(
            last_bootstrapped_timestamp, timestamp_before_bootstrap)
        timestamp_before_bootstrap = last_bootstrapped_timestamp
        time.sleep(1)

        # Modify only Security object
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=2,
                            content=TLV.make_resource(
                                RID.Security.ServerURI,
                                'coap://127.0.0.1:%d' % self.serv.get_listen_port()).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode,
                                                        SecurityMode.NoSec.value).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                                    + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                                    + TLV.make_resource(RID.Security.SecretKey, b'').serialize())

        last_bootstrapped_resource = self.get_last_bootstrapped_timestamp(1)
        self.assertIsNotNone(last_bootstrapped_resource)
        last_bootstrapped_timestamp = int.from_bytes(
            last_bootstrapped_resource.value, byteorder='big')
        self.assertGreaterEqual(
            last_bootstrapped_timestamp, timestamp_before_bootstrap)
        timestamp_before_bootstrap = last_bootstrapped_timestamp
        time.sleep(1)

        # Modify only Server object
        self.write_instance(self.bootstrap_server, oid=OID.Server, iid=1,
                            content=TLV.make_resource(
                                RID.Server.Lifetime, 86400).serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID, 42).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring,
                                                        True).serialize()
                                    + TLV.make_resource(RID.Server.Binding, 'U').serialize())

        last_bootstrapped_resource = self.get_last_bootstrapped_timestamp(1)
        self.assertIsNotNone(last_bootstrapped_resource)
        last_bootstrapped_timestamp = int.from_bytes(
            last_bootstrapped_resource.value, byteorder='big')
        self.assertGreaterEqual(
            last_bootstrapped_timestamp, timestamp_before_bootstrap)


class BootstrappedSecurityInstanceBindingAndUriMismatch(BootstrapTest.Test):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap(endpoint=DEMO_ENDPOINT_NAME)

        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())

        self.write_instance(self.bootstrap_server, oid=OID.Server, iid=1,
                            content=TLV.make_resource(
                                RID.Server.Lifetime, 86400).serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID, 42).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring,
                                                        True).serialize()
                                    + TLV.make_resource(RID.Server.Binding, 'N').serialize())

        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(('127.0.0.1', 0))
        s.listen()
        s.settimeout(5)

        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=2,
                            content=TLV.make_resource(
                                RID.Security.ServerURI,
                                'coap+tcp://127.0.0.1:%d' % s.getsockname()[1]).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode,
                                                        SecurityMode.NoSec.value).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                                    + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                                    + TLV.make_resource(RID.Security.SecretKey, b'').serialize())

        self.perform_bootstrap_finish()

        with self.assertRaises(socket.timeout):
            s.accept()

        self.assertDemoRequestsBootstrap(endpoint=DEMO_ENDPOINT_NAME)


# NOTE: consecutive Bootstrap Requests are sent with exponential backoff (see schedule_request_bootstrap()),
# starting with 3s. If we were to test like k different values for the resource that causes re-bootstrapping,
# it makes more sense to do k separate tests and pay 3 seconds for each (O(k)), rather than one with cost:
# 3*(1) + 3*(2) + 3*(3) + ... + 3*(k) = O(k^2)
class BootstrapSingleServerRegistrationOnFailureNotSet(BootstrapTest.Test):
    BOOTSTRAP_ON_REGISTRATION_FAILURE = None

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.perform_typical_bootstrap(clear_everything=True,
                                       server_iid=1,
                                       security_iid=2,
                                       # Definitely an incorrect address.
                                       server_uri='coap://256.0.0.1:5683',
                                       security_mode=SecurityMode.NoSec,
                                       bootstrap_on_registration_failure=self.BOOTSTRAP_ON_REGISTRATION_FAILURE,
                                       finish=True)
        # See the comment above to understand where the timeout_s came from.
        self.assertDemoRequestsBootstrap(timeout_s=3 + 1)


class BootstrapSingleServerRegistrationOnFailureFalse(
    BootstrapSingleServerRegistrationOnFailureNotSet):
    BOOTSTRAP_ON_REGISTRATION_FAILURE = False


class BootstrapSingleServerRegistrationOnFailureTrue(
    BootstrapSingleServerRegistrationOnFailureNotSet):
    BOOTSTRAP_ON_REGISTRATION_FAILURE = True


class BootstrapMultiServerRegistrationOnFailureTrue(BootstrapTest.Test):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap()

        self.add_server(server_iid=2,
                        security_iid=2,
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                        bootstrap_on_registration_failure=False)
        self.add_server(server_iid=3,
                        security_iid=3,
                        server_uri='coap://256.0.0.1:5683',
                        bootstrap_on_registration_failure=True)
        self.perform_bootstrap_finish()
        self.assertDemoRequestsBootstrap()


class BootstrapMultiServerRegistrationOnFailureNotSet(BootstrapTest.Test):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap()

        self.add_server(server_iid=2,
                        security_iid=2,
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port())
        self.add_server(server_iid=3,
                        security_iid=3,
                        server_uri='coap://256.0.0.1:5683')
        self.perform_bootstrap_finish()
        self.assertDemoRequestsBootstrap()


class BootstrapMultiServerRegistrationOnFailureFalse(BootstrapTest.Test):
    def runTest(self):
        self.assertDemoRequestsBootstrap()

        self.add_server(server_iid=2,
                        security_iid=2,
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                        bootstrap_on_registration_failure=False)
        self.add_server(server_iid=3,
                        security_iid=3,
                        server_uri='coap://256.0.0.1:5683',
                        bootstrap_on_registration_failure=False)
        self.perform_bootstrap_finish()
        self.assertDemoRegisters(self.serv)

        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=3))


class NoBootstrapAfterCompleteFail(BootstrapTest.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self, **kwargs):
        # bootstrap server - PSK
        # management server - NoSec
        super().setUp(servers=[Lwm2mServer(coap.Server())],
                      bootstrap_server=Lwm2mServer(coap.DtlsServer(psk_identity=self.PSK_IDENTITY,
                                                                   psk_key=self.PSK_KEY)),
                      psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY,
                      legacy_server_initiated_bootstrap_allowed=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=2,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=10)
        self.assertDemoRegisters(self.serv, lifetime=10)

        with self.serv.fake_close():
            with self.bootstrap_server.fake_close():
                # let lifetime pass and everything fail
                time.sleep(10)

        self.communicate('reconnect')
        # client shall connect to the regular server...
        self.assertDemoRegisters(self.serv, lifetime=10)
        # ...but don't attempt doing anything with the Bootstrap Server, not even handshake
        self.bootstrap_server._raw_udp_socket.settimeout(2)
        with self.assertRaises(socket.timeout):
            self.bootstrap_server._raw_udp_socket.recv(4096)

        # check that another registration failure will cause a bootstrap attempt
        with self.serv.fake_close():
            self.assertDtlsReconnect(self.bootstrap_server, timeout_s=10)
            self.assertDemoRequestsBootstrap()


class BootstrapReconnectAfterCompleteFail(BootstrapTest.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self, **kwargs):
        super().setUp(bootstrap_server=Lwm2mServer(
            coap.DtlsServer(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY)),
            psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY,
            legacy_server_initiated_bootstrap_allowed=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME), pkt)

        with self.bootstrap_server.fake_close():
            # let everything fail
            time.sleep(10)

        self.communicate('reconnect')
        # client shall connect to the Bootstrap Server
        self.assertDtlsReconnect(self.bootstrap_server, timeout_s=10)
        self.assertDemoRequestsBootstrap()


class BootstrapCheckOngoingRegistrationsWithLegacyServerInitiated(BootstrapTest.Test):
    def runTest(self):
        # Client-Initiated Bootstrap
        self.assertDemoRequestsBootstrap()
        self.assertTrue(self.ongoing_registration_exists())
        self.add_server(server_iid=1,
                        security_iid=2,
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port())
        self.perform_bootstrap_finish()

        # Registration
        pkt = self.assertDemoRegisters(respond=False)
        self.assertTrue(self.ongoing_registration_exists())
        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))
        self.assertFalse(self.ongoing_registration_exists())

        # Server-Initiated Bootstrap
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
        self.assertTrue(self.ongoing_registration_exists())

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class BootstrapCheckOngoingRegistrationsWithoutLegacyServerInitiated(BootstrapTest.Test):
    def setUp(self):
        super().setUp(maximum_version='1.1', legacy_server_initiated_bootstrap_allowed=False)

    def runTest(self):
        # Client-Initiated Bootstrap
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        self.assertTrue(self.ongoing_registration_exists())
        self.add_server(server_iid=1, security_iid=2,
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port())
        self.perform_bootstrap_finish()

        # Registration
        pkt = self.assertDemoRegisters(version='1.1', respond=False)
        self.assertTrue(self.ongoing_registration_exists())
        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))
        self.assertFalse(self.ongoing_registration_exists())

        # Server-Initiated Bootstrap
        self.execute_resource(self.serv, OID.Server, 1, RID.Server.RequestBootstrapTrigger)
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        self.assertTrue(self.ongoing_registration_exists())

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class BootstrapCuriousServerDisabling(BootstrapTest.Test):
    def setUp(self):
        super().setUp(maximum_version='1.1', legacy_server_initiated_bootstrap_allowed=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap(
            preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)

        # rewrite the bootstrap instance
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=1,
                            content=TLV.make_resource(RID.Security.ServerURI,
                                                      'coap://127.0.0.1:%d/' % self.bootstrap_server.get_listen_port()).serialize() + TLV.make_resource(
                                RID.Security.ClientHoldOffTime, 1).serialize())
        self.add_server(server_iid=1, security_iid=2, binding='UQ',
                        server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                        additional_security_data=TLV.make_resource(RID.Security.ClientHoldOffTime,
                                                                   1).serialize())
        self.perform_bootstrap_finish()

        # Registration
        self.assertDemoRegisters(version='1.1', lwm2m11_queue_mode=True)
        self.wait_until_socket_count(expected=1, timeout_s=5)
