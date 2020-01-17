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

from framework.lwm2m_test import *

from . import bootstrap_server as bs

def expected_enabler_version_string(version):
    return {
        '1.0': 'lwm2m="1.0",',
    }[version]


class BootstrapDiscoverBase:
    class Test(bs.BootstrapServer.Test):
        def setUp(self, version, **kwargs):
            self.lwm2m_version = version
            super().setUp(**kwargs)

        def expected_enabler_version_string(self):
            return expected_enabler_version_string(self.lwm2m_version)


class BootstrapDiscover:
    class FullNoServersTest(BootstrapDiscoverBase.Test,
                            test_suite.Lwm2mDmOperations):
        def runTest(self):
            uri = ';uri="coap://127.0.0.1:{port_bs}"'.format(port_bs=self.bootstrap_server.get_listen_port())
            if self.lwm2m_version == '1.0':
                uri = ''

            EXPECTED_PREFIX = '{prefix}</{sec}>,</{sec}/1>{uri},</{serv}>,</{ac}>,'.format(
                    prefix=self.expected_enabler_version_string(),
                    sec=OID.Security,
                    serv=OID.Server,
                    uri=uri,
                    ac=OID.AccessControl)
            self.bootstrap_server.connect_to_client(
                ('127.0.0.1', self.get_demo_port()))
            discover_result = self.discover(self.bootstrap_server).content.decode()
            self.assertLinkListValid(
                discover_result[len(self.expected_enabler_version_string()):])
            self.assertTrue(discover_result.startswith(EXPECTED_PREFIX))


    class FullMultipleServersTest(BootstrapDiscoverBase.Test,
                                  test_suite.Lwm2mDmOperations):
        def runTest(self):
            self.bootstrap_server.connect_to_client(
                ('127.0.0.1', self.get_demo_port()))
            self.write_instance(server=self.bootstrap_server, oid=OID.Server, iid=42,
                                content=TLV.make_resource(
                                    RID.Server.Lifetime, 60).serialize()
                                + TLV.make_resource(RID.Server.Binding,
                                                    "U").serialize()
                                + TLV.make_resource(RID.Server.ShortServerID, 11).serialize()
                                + TLV.make_resource(RID.Server.NotificationStoring, True).serialize())

            self.write_instance(server=self.bootstrap_server, oid=OID.Server, iid=24,
                                content=TLV.make_resource(
                                    RID.Server.Lifetime, 60).serialize()
                                + TLV.make_resource(RID.Server.Binding,
                                                    "U").serialize()
                                + TLV.make_resource(RID.Server.ShortServerID, 12).serialize()
                                + TLV.make_resource(RID.Server.NotificationStoring, True).serialize())

            uri2 = 'coap://127.0.0.1:9999'
            self.write_instance(self.bootstrap_server, oid=OID.Security, iid=2,
                                content=TLV.make_resource(
                                    RID.Security.ServerURI, uri2).serialize()
                                + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                + TLV.make_resource(RID.Security.Mode,
                                                    3).serialize()
                                + TLV.make_resource(RID.Security.ShortServerID, 11).serialize()
                                + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                                + TLV.make_resource(RID.Security.SecretKey, "").serialize())

            uri10 = 'coap://127.0.0.1:11111'
            self.write_instance(self.bootstrap_server, oid=OID.Security, iid=10,
                                content=TLV.make_resource(
                                    RID.Security.ServerURI, uri10).serialize()
                                + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                + TLV.make_resource(RID.Security.Mode,
                                                    3).serialize()
                                + TLV.make_resource(RID.Security.ShortServerID, 12).serialize()
                                + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                                + TLV.make_resource(RID.Security.SecretKey, "").serialize())

            uri = ';uri="coap://127.0.0.1:{port_bs}"'.format(port_bs=self.bootstrap_server.get_listen_port())
            uri2 = ';uri="{uri2}"'.format(uri2=uri2)
            uri10 = ';uri="{uri10}"'.format(uri10=uri10)
            if self.lwm2m_version == '1.0':
                # Bootstrap Discover does not report uri in 1.0 mode
                uri = ''
                uri2 = ''
                uri10 = ''

            EXPECTED_PREFIX = '{prefix}</{sec}>,</{sec}/1>{uri},'\
                              '</{sec}/2>;ssid=11{uri2},' \
                              '</{sec}/10>;ssid=12{uri10},' \
                              '</{serv}>,</{serv}/24>;ssid=12,' \
                              '</{serv}/42>;ssid=11,</{ac}>,'.format(prefix=self.expected_enabler_version_string(),
                                                                     sec=OID.Security,
                                                                     serv=OID.Server,
                                                                     ac=OID.AccessControl,
                                                                     uri=uri,
                                                                     uri2=uri2,
                                                                     uri10=uri10).encode()
            discover_result = self.discover(self.bootstrap_server)
            self.assertEqual([coap.Option.CONTENT_FORMAT.APPLICATION_LINK],
                             discover_result.get_options(coap.Option.CONTENT_FORMAT))
            self.assertLinkListValid(discover_result.content.decode()[
                                     len(self.expected_enabler_version_string()):])
            self.assertIn(b'</%d>;ver="1.1"' % (OID.CellularConnectivity),
                          discover_result.content[len(self.expected_enabler_version_string()):])
            # No more parameters
            self.assertEqual(
                2, len(discover_result.content[len(EXPECTED_PREFIX):].split(b';')))
            self.assertTrue(discover_result.content.startswith(EXPECTED_PREFIX))


class BootstrapDiscover10FullNoServers(BootstrapDiscover.FullNoServersTest):
    def setUp(self):
        super().setUp(version='1.0')




class BootstrapDiscover10FullMultipleServers(BootstrapDiscover.FullMultipleServersTest):
    def setUp(self):
        super().setUp(version='1.0')




class BootstrapDiscoverOnNonexistingObject(bs.BootstrapServer.Test,
                                           test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.bootstrap_server.connect_to_client(
            ('127.0.0.1', self.get_demo_port()))
        self.discover(self.bootstrap_server, oid=42,
                      expect_error_code=coap.Code.RES_NOT_FOUND)


class BootstrapDiscoverAfterEmptyInstancesWrite(bs.BootstrapServer.Test,
                                                test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.bootstrap_server.connect_to_client(
            ('127.0.0.1', self.get_demo_port()))
        self.write_object(self.bootstrap_server, oid=OID.Test,
                          content=TLV.make_instance(0).serialize()
                          + TLV.make_instance(1).serialize()
                          + TLV.make_instance(2).serialize())
        discover_result = self.discover(
            self.bootstrap_server, oid=OID.Test).content
        self.assertEqual(
            'lwm2m="1.0",</{oid}>,</{oid}/0>,</{oid}/1>,</{oid}/2>'.format(oid=OID.Test),
            str(discover_result, 'ascii'))
