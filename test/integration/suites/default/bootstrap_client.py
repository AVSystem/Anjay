# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

from framework.lwm2m_test import *
from framework.lwm2m.coap.server import SecurityMode


class BootstrapTest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, servers=1, num_servers_passed=0, holdoff_s=None, timeout_s=None, bootstrap_server=True,
                  extra_cmdline_args=None, **kwargs):
            assert bootstrap_server
            extra_args = extra_cmdline_args or []
            if holdoff_s is not None:
                extra_args += [ '--bootstrap-holdoff', str(holdoff_s) ]
            if timeout_s is not None:
                extra_args += [ '--bootstrap-timeout', str(timeout_s) ]

            self.holdoff_s = holdoff_s
            self.timeout_s = timeout_s
            self.setup_demo_with_servers(servers=servers,
                                         num_servers_passed=num_servers_passed,
                                         bootstrap_server=bootstrap_server,
                                         extra_cmdline_args=extra_args,
                                         **kwargs)

        def assertDemoRequestsBootstrap(self, uri_path='', uri_query=None, respond_with_error_code=None):
            pkt = self.bootstrap_server.recv()
            self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME,
                                                      uri_path=uri_path,
                                                      uri_query=uri_query), pkt)
            if respond_with_error_code is None:
                self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())
            else:
                self.bootstrap_server.send(Lwm2mErrorResponse.matching(pkt)(code=respond_with_error_code))

        def perform_bootstrap_finish(self):
            req = Lwm2mBootstrapFinish()
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.bootstrap_server.recv())

        def perform_typical_bootstrap(self, server_iid, security_iid, server_uri, lifetime=86400,
                                      secure_identity=b'', secure_key=b'',
                                      security_mode: SecurityMode = SecurityMode.NoSec,
                                      finish=True, holdoff_s=None):
            # For the first holdoff_s seconds, the client should wait for
            # Server Initiated Bootstrap. Note that we subtract 1 second to
            # take into account code execution delays.
            holdoff_s = holdoff_s or self.holdoff_s or 0
            timeout_s = max(0, holdoff_s - 1)
            if timeout_s > 0:
                with self.assertRaises(socket.timeout):
                    print(self.bootstrap_server.recv(timeout_s=timeout_s))

            # We should get Bootstrap Request now
            self.assertDemoRequestsBootstrap()

            # Create typical Server Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Server, iid=server_iid,
                                content=TLV.make_resource(RID.Server.Lifetime, lifetime).serialize()
                                        + TLV.make_resource(RID.Server.ShortServerID, server_iid).serialize()
                                        + TLV.make_resource(RID.Server.NotificationStoring, True).serialize()
                                        + TLV.make_resource(RID.Server.Binding, "U").serialize())


            # Create typical (corresponding) Security Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Security, iid=security_iid,
                                content=TLV.make_resource(RID.Security.ServerURI, server_uri).serialize()
                                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                         + TLV.make_resource(RID.Security.Mode, security_mode.value).serialize()
                                         + TLV.make_resource(RID.Security.ShortServerID, server_iid).serialize()
                                         + TLV.make_resource(RID.Security.PKOrIdentity, secure_identity).serialize()
                                         + TLV.make_resource(RID.Security.SecretKey, secure_key).serialize())

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


class ClientBootstrapNotSentAfterDisableWithinHoldoffTest(BootstrapTest.Test):
    def setUp(self):
        super().setUp(num_servers_passed=1, holdoff_s=3, timeout_s=3)

    def runTest(self):
        # set Disable Timeout to 5
        self.write_resource(server=self.serv, oid=OID.Server, iid=2, rid=RID.Server.DisableTimeout, content='5')
        # disable the server
        self.execute_resource(server=self.serv, oid=OID.Server, iid=2, rid=RID.Server.Disable)

        self.assertDemoDeregisters(self.serv)

        with self.assertRaises(socket.timeout, msg="the client should not send "
                                                   "Request Bootstrap after disabling the server"):
            self.bootstrap_server.recv(timeout_s=4)

        self.assertDemoRegisters(self.serv, timeout_s=2)


class ClientBootstrapBacksOffAfterErrorResponse(BootstrapTest.Test):
    def setUp(self):
        super().setUp(servers=0)

    def runTest(self):
        self.assertDemoRequestsBootstrap(respond_with_error_code=coap.Code.RES_INTERNAL_SERVER_ERROR)
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
                            content=TLV.make_resource(RID.Security.ServerURI, 'coap://127.0.0.1:5683').serialize()
                                     + TLV.make_resource(RID.Security.Bootstrap, 1).serialize()
                                     + TLV.make_resource(RID.Security.Mode, 3).serialize()
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
        self.assertDemoRequestsBootstrap(uri_path='/some/crazy/path', uri_query=['and=more', 'craziness'])

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
        self.assertMsgEqual(Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
                            req)

        # respond with 4.03
        self.serv.send(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_FORBIDDEN))

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


class BootstrapIncorrectData(BootstrapTest.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self, **kwargs):
        super().setUp(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
                      num_servers_passed=0, **kwargs)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def test_bootstrap_backoff(self, num_attempts):
        # NOTE: mbed TLS (hence, the demo client) sends Client Key Exchange, Change Cipher Spec and Encrypted Handshake
        # Message as three separate UDP packets, but as three send() calls without any recv() attempts between them.
        # On the server side (i.e., in this test code), the fatal Alert ("Unknown PSK identity" in this case) is sent
        # after receiving Client Key Exchange. Depending on the timing of processing on both endpoints, it may be the
        # case that the Alert is sent by the server before the Change Cipher Spec and Encrypted Handshake are even
        # generated by the client. In that case, on the server we'd get "handshake failed" exception TWICE - once due to
        # the actual error, and then second time - because mbed TLS will attempt to interpret the Change Cipher Spec
        # datagram as a "malformed Client Hello". So we need to somehow discard the packets on the server side. We
        # cannot "fake-close" the socket after failed handshake, as that causes ICMP unreachable packets to be
        # generated, which in turn causes the client to restart the handshake. So we instead do this convoluted flow of
        # calling reset() just before Bootstrap Finish, so that we're absolutely sure that all leftover messages are
        # discarded just before we get the new Client Hello.

        holdoff_s = 0
        for attempt in range(num_attempts):
            # Create Security Object instance with deliberately wrong keys
            self.perform_typical_bootstrap(server_iid=1,
                                           security_iid=2,
                                           server_uri='coaps://127.0.0.1:%d' % self.serv.get_listen_port(),
                                           secure_identity=self.PSK_IDENTITY + b'hurr',
                                           secure_key=self.PSK_KEY + b'durr',
                                           security_mode=SecurityMode.PreSharedKey,
                                           finish=False,
                                           holdoff_s=holdoff_s)

            self.serv.reset()
            self.perform_bootstrap_finish()
            with self.assertRaisesRegex(RuntimeError, 'handshake failed'):
                self.serv.recv()

            holdoff_s = min(max(2 * holdoff_s, 3), 120)

    def runTest(self):
        self.test_bootstrap_backoff(3)

        # now bootstrap the right keys
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coaps://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       secure_identity=self.PSK_IDENTITY,
                                       secure_key=self.PSK_KEY,
                                       security_mode=SecurityMode.PreSharedKey,
                                       finish=False,
                                       holdoff_s=12)

        self.serv.reset()
        self.perform_bootstrap_finish()
        self.assertDemoRegisters()

        # Trigger update
        self.communicate('send-update')
        update_pkt = self.assertDemoUpdatesRegistration(self.serv, respond=False)
        # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
        self.serv.send(Lwm2mErrorResponse.matching(update_pkt)(code=coap.Code.RES_BAD_REQUEST))
        # This should cause client attempt to re-register.
        register_pkt = self.assertDemoRegisters(self.serv, respond=False)
        # To which we respond with 4.03 Forbidden, finishing off the communication.
        self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))

        # check that bootstrap backoff is restarted
        self.test_bootstrap_backoff(2)


class ClientInitiatedBootstrapOnly(BootstrapTest.Test):
    def setUp(self):
        super().setUp(server_initiated_bootstrap_allowed=False)

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
        update_pkt = self.assertDemoUpdatesRegistration(self.serv, respond=False)
        # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
        self.serv.send(Lwm2mErrorResponse.matching(update_pkt)(code=coap.Code.RES_BAD_REQUEST))
        # This should cause client attempt to re-register.
        register_pkt = self.assertDemoRegisters(self.serv, respond=False)
        # To which we respond with 4.03 Forbidden, finishing off the communication.
        self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))

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
                      server_initiated_bootstrap_allowed=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # registration has been performed by setUp()
        self.assertIsNotNone(self.serv.get_remote_addr())
        self.assertEqual(self.get_socket_count(), 1)

        # Trigger update
        self.communicate('send-update')
        update_pkt = self.assertDemoUpdatesRegistration(self.serv, respond=False)
        # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
        self.serv.send(Lwm2mErrorResponse.matching(update_pkt)(code=coap.Code.RES_BAD_REQUEST))
        # This should cause client attempt to re-register.
        register_pkt = self.assertDemoRegisters(self.serv, respond=False)
        # To which we respond with 4.03 Forbidden, finishing off the communication.
        self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))

        # The client shall only now attempt bootstrap
        self.assertIsNone(self.bootstrap_server.get_remote_addr())
        self.assertDemoRequestsBootstrap()


class ClientInitiatedBootstrapOnlyWithIncorrectData(BootstrapIncorrectData):
    def setUp(self):
        super().setUp(server_initiated_bootstrap_allowed=False)
