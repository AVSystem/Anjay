# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import socket
import time

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *


class BootstrapTest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, servers=1, num_servers_passed=0, holdoff_s=None, timeout_s=None, bootstrap_server=True,
                  extra_cmdline_args=None, **kwargs):
            assert bootstrap_server
            extra_args = extra_cmdline_args or []
            if holdoff_s is not None:
                extra_args += ['--bootstrap-holdoff', str(holdoff_s)]
            if timeout_s is not None:
                extra_args += ['--bootstrap-timeout', str(timeout_s)]

            self.holdoff_s = holdoff_s
            self.timeout_s = timeout_s
            super().setUp(servers=servers, num_servers_passed=num_servers_passed, bootstrap_server=bootstrap_server,
                          extra_cmdline_args=extra_args, **kwargs)

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
                    RID.Server.BootstrapOnRegistrationFailure, bootstrap_on_registration_failure).serialize()


            # Create typical Server Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Server, iid=server_iid,
                                content=TLV.make_resource(
                                    RID.Server.Lifetime, lifetime).serialize()
                                + TLV.make_resource(RID.Server.ShortServerID, server_iid).serialize()
                                + TLV.make_resource(RID.Server.NotificationStoring, True).serialize()
                                + TLV.make_resource(RID.Server.Binding, binding).serialize()
                                + additional_server_data)

            # Create typical (corresponding) Security Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Security, iid=security_iid,
                                content=TLV.make_resource(
                                    RID.Security.ServerURI, server_uri).serialize()
                                + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                + TLV.make_resource(RID.Security.Mode, security_mode.value).serialize()
                                + TLV.make_resource(RID.Security.ShortServerID, server_iid).serialize()
                                + TLV.make_resource(RID.Security.PKOrIdentity, secure_identity).serialize()
                                + TLV.make_resource(RID.Security.SecretKey, secure_key).serialize()
                                + additional_security_data)

        def perform_typical_bootstrap(self, server_iid, security_iid, server_uri, lifetime=86400,
                                      secure_identity=b'', secure_key=b'',
                                      security_mode: SecurityMode = SecurityMode.NoSec,
                                      finish=True, holdoff_s=None, binding="U", clear_everything=False,
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
        super().setUp(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
                      num_servers_passed=1,
                      bootstrap_server=Lwm2mServer(
                          coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)),
                      extra_cmdline_args=['--identity', str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
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




class DtlsBootstrap:
    class Test(BootstrapTest.Test):
        PSK_IDENTITY = b'test-identity'
        PSK_KEY = b'test-key'

        # example ciphersuites, source: https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml
        SUPPORTED_CIPHER = 0xC0A8  # TLS_PSK_WITH_AES_128_CCM_8, recommended by RFC8252
        # TLS_PSK_WITH_AES_256_CCM_8, supported by mbed TLS and OpenSSL, but not by pymbedtls
        UNSUPPORTED_CIPHER = 0xC0A9

        def setUp(self):
            super().setUp(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY,
                                                               psk_identity=self.PSK_IDENTITY))])




class NoBootstrapAfterCompleteFail(BootstrapTest.Test):
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
