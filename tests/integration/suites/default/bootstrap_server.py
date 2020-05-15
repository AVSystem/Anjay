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

from framework.lwm2m.tlv import TLV
from framework.lwm2m_test import *


class BootstrapServer:
    class Test(test_suite.Lwm2mTest):
        def setUp(self, **kwargs):
            extra_args = ['--bootstrap-holdoff', '3']
            self.setup_demo_with_servers(servers=0,
                                         bootstrap_server=True,
                                         extra_cmdline_args=extra_args,
                                         **kwargs)

        def tearDown(self):
            self.teardown_demo_with_servers()

        def get_demo_port(self, server_index=None):
            # wait for sockets initialization
            # scheduler-based socket initialization might delay socket setup a bit;
            # this loop is here to ensure `communicate()` call below works as
            # expected
            for _ in range(10):
                if self.get_socket_count() > 0:
                    break
            else:
                self.fail("sockets not initialized in time")

            # send Bootstrap messages without request
            return super().get_demo_port(server_index)


class BootstrapServerTest(BootstrapServer.Test):
    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))
        req = Lwm2mWrite('/%d/42' % (OID.Server,),
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.Binding, "U").serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        regular_serv = Lwm2mServer()
        regular_serv_uri = 'coap://127.0.0.1:%d' % regular_serv.get_listen_port()

        # Create Security object
        req = Lwm2mWrite('/%d/42' % (OID.Security,),
                         TLV.make_resource(RID.Security.ServerURI, regular_serv_uri).serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # no Client Initiated bootstrap
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=4))

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        self.assertDemoRegisters(server=regular_serv, lifetime=60)

        # Bootstrap Delete / shall succeed
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
        # ...even twice
        req = Lwm2mDelete('/')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(),
                            self.bootstrap_server.recv())
        # now send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # the client will now start Client Initiated bootstrap, because it has no regular server connection
        # this might happen after a backoff, if the Bootstrap Delete was handled before the response to Register
        pkt = self.bootstrap_server.recv(timeout_s=20)
        self.assertIsInstance(pkt, Lwm2mRequestBootstrap)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        self.request_demo_shutdown()

        regular_serv.close()


class BootstrapEmptyResourcesDoesNotSegfault(BootstrapServer.Test):
    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))

        req = Lwm2mWrite('/%d/42' % (OID.Security,),
                         TLV.make_resource(RID.Security.ServerURI, 'coap://1.2.3.4:5678').serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, b'').serialize()
                         + TLV.make_resource(RID.Security.SecretKey, b'').serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        for _ in range(64):
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.bootstrap_server.recv())

        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())
        self.request_demo_shutdown()
