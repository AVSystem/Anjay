# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

from framework.lwm2m.tlv import TLV
from framework.lwm2m_test import *

class BootstrapTest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, servers=1, num_servers_passed=0, holdoff_s=None, timeout_s=None):
            extra_args = []
            if holdoff_s is not None:
                extra_args += [ '--bootstrap-holdoff', str(holdoff_s) ]
            if timeout_s is not None:
                extra_args += [ '--bootstrap-timeout', str(timeout_s) ]

            self.holdoff_s = holdoff_s
            self.timeout_s = timeout_s
            self.setup_demo_with_servers(servers=servers,
                                         num_servers_passed=num_servers_passed,
                                         bootstrap_server=True,
                                         extra_cmdline_args=extra_args)

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

        def perform_typical_bootstrap(self, server_iid, security_iid, server_uri, lifetime=86400, finish=True):
            if self.holdoff_s is not None:
                # For the first holdoff_s seconds, the client should wait for
                # Server Initiated Bootstrap. Note that we subtract 1 second to
                # take into account code execution delays.
                with self.assertRaises(socket.timeout):
                    print(self.bootstrap_server.recv(timeout_s=max(0, self.holdoff_s - 1)))

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
                                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                                         + TLV.make_resource(RID.Security.ShortServerID, server_iid).serialize()
                                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                                         + TLV.make_resource(RID.Security.SecretKey, "").serialize())

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
        discovered_security = self.discover(server=self.serv, oid=0).content
        self.assertIn(b'</0/2/', discovered_security)
        self.assertNotIn(b'</0/1/', discovered_security)

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
