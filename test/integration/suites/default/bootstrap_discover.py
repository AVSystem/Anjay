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

from framework.lwm2m.tlv import TLV
from framework.lwm2m_test import *

from . import bootstrap_server as bs

EXPECTED_ENABLER_VERSION_STRING = 'lwm2m="1.0"'


class BootstrapDiscoverFullNoServers(bs.BootstrapServer.Test,
                                     test_suite.Lwm2mDmOperations):
    def runTest(self):
        EXPECTED_PREFIX = b'lwm2m="1.0",</0>,</0/1>,</1>,</2>,'
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))
        discover_result = self.discover(self.bootstrap_server).content
        self.assertLinkListValid(discover_result[len(EXPECTED_ENABLER_VERSION_STRING) + 1:])
        self.assertTrue(discover_result.startswith(EXPECTED_PREFIX))


class BootstrapDiscoverFullMultipleServers(bs.BootstrapServer.Test,
                                           test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))
        self.write_instance(server=self.bootstrap_server, oid=OID.Server, iid=42,
                            content=TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                                    + TLV.make_resource(RID.Server.Binding, "U").serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID, 11).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring, True).serialize())

        self.write_instance(server=self.bootstrap_server, oid=OID.Server, iid=24,
                            content=TLV.make_resource(RID.Server.Lifetime, 60).serialize()
                                    + TLV.make_resource(RID.Server.Binding, "U").serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID, 12).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring, True).serialize())

        uri = 'coap://127.0.0.1:9999'
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=2,
                            content=TLV.make_resource(RID.Security.ServerURI, uri).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode, 3).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID, 11).serialize()
                                    + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                                    + TLV.make_resource(RID.Security.SecretKey, "").serialize())

        uri = 'coap://127.0.0.1:11111'
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=10,
                            content=TLV.make_resource(RID.Security.ServerURI, uri).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode, 3).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID, 12).serialize()
                                    + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                                    + TLV.make_resource(RID.Security.SecretKey, "").serialize())

        EXPECTED_PREFIX = b'lwm2m="1.0",</0>,</0/1>,</0/2>;ssid=11,</0/10>;ssid=12,' \
                          b'</1>,</1/24>;ssid=12,</1/42>;ssid=11,</2>,';
        discover_result = self.discover(self.bootstrap_server)
        self.assertEqual([coap.Option.CONTENT_FORMAT.APPLICATION_LINK],
                         discover_result.get_options(coap.Option.CONTENT_FORMAT))
        self.assertLinkListValid(discover_result.content[len(EXPECTED_ENABLER_VERSION_STRING) + 1:])
        self.assertIn(b'</10>;ver="1.1"', discover_result.content[len(EXPECTED_ENABLER_VERSION_STRING) + 1:])
        # No more parameters
        self.assertEqual(2, len(discover_result.content[len(EXPECTED_PREFIX):].split(b';')))
        self.assertTrue(discover_result.content.startswith(EXPECTED_PREFIX))


class BootstrapDiscoverOnNonexistingObject(bs.BootstrapServer.Test,
                                           test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.bootstrap_server.connect_to_client(('127.0.0.1', self.get_demo_port()))
        self.discover(self.bootstrap_server, oid=42, expect_error_code=coap.Code.RES_NOT_FOUND)

