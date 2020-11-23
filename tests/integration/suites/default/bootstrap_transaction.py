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

from framework.lwm2m_test import *
from suites.default.bootstrap_client import BootstrapTest


class BootstrapTransactionTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=1,
                                     num_servers_passed=0,
                                     bootstrap_server=True,
                                     extra_cmdline_args=['--bootstrap-timeout', '-1'])

    def tearDown(self):
        self.teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME), pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # Create Server object
        req = Lwm2mWrite('/%d/1' % (OID.Server,),
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize()
                         + TLV.make_resource(RID.Server.Binding, "U").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # Create Security object
        regular_serv_uri = 'coap://127.0.0.1:%d' % self.servers[0].get_listen_port()

        req = Lwm2mWrite('/%d/2' % (OID.Security,),
                         TLV.make_resource(RID.Security.ServerURI, regular_serv_uri).serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # create incomplete Geo-Points object
        req = Lwm2mWrite('/%d/42' % (OID.GeoPoints,),
                         TLV.make_resource(RID.GeoPoints.Latitude, 42.0).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_NOT_ACCEPTABLE),
                            self.bootstrap_server.recv())

        # still bootstrapping, so nothing shall be sent to the regular server
        with self.assertRaises(socket.timeout):
            print(self.servers[0].recv(timeout_s=5))

        # check that still bootstrapping indeed
        req = Lwm2mWrite('/%d/42' % (OID.GeoPoints,),
                         TLV.make_resource(RID.GeoPoints.Longitude, 69.0).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())


class BootstrapTransactionPersistenceTest(test_suite.Lwm2mTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        self._dm_persistence_file = tempfile.NamedTemporaryFile()
        self.setup_demo_with_servers(
            servers=0, bootstrap_server=True,
            extra_cmdline_args=['--bootstrap-timeout', '-1',
                                '--dm-persistence-file', self._dm_persistence_file.name])

    def tearDown(self):
        try:
            self.teardown_demo_with_servers(auto_deregister=False)
        finally:
            self._dm_persistence_file.close()

    def runTest(self):
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME), pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # Create Server object without Binding
        req = Lwm2mWrite('/%d/1' % (OID.Server,),
                         TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                         + TLV.make_resource(RID.Server.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, True).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # Create Security object without URI
        req = Lwm2mWrite('/%d/2' % (OID.Security,),
                         TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 1).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_NOT_ACCEPTABLE),
                            self.bootstrap_server.recv())

        self.request_demo_shutdown()
        self._terminate_demo()

        self.bootstrap_server.reset()

        self._start_demo(['--dm-persistence-file', self._dm_persistence_file.name]
                         + self.make_demo_args(DEMO_ENDPOINT_NAME, [],
                                               None))

        # Demo shall launch, with the initial server configuration
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME), pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # The previously created instances shall not be present
        discover_result = self.discover(self.bootstrap_server).content.decode()
        self.assertNotIn('</%d/1' % (OID.Server,), discover_result)
        self.assertNotIn('</%d/2' % (OID.Security,), discover_result)


