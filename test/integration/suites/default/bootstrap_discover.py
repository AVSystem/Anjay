from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV
from . import bootstrap_server as bs

EXPECTED_ENABLER_VERSION_STRING = 'lwm2m="1.0"'

class BootstrapDiscoverFullNoServers(bs.BootstrapServer.Test,
                                     test_suite.Lwm2mDmOperations):
    def runTest(self):
        EXPECTED_PREFIX = b'lwm2m="1.0",</0>,</0/1>,</1>,</2>,'
        self.bootstrap_server.connect(('127.0.0.1', self.get_demo_port()))
        discover_result = self.discover(self.bootstrap_server).content
        self.assertLinkListValid(discover_result[len(EXPECTED_ENABLER_VERSION_STRING)+1:])
        self.assertTrue(discover_result.startswith(EXPECTED_PREFIX))

class BootstrapDiscoverFullMultipleServers(bs.BootstrapServer.Test,
                                           test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.bootstrap_server.connect(('127.0.0.1', self.get_demo_port()))
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
        discover_result = self.discover(self.bootstrap_server).content
        self.assertLinkListValid(discover_result[len(EXPECTED_ENABLER_VERSION_STRING)+1:])
        # No more parameters
        self.assertEqual(1, len(discover_result[len(EXPECTED_PREFIX):].split(b';')))
        self.assertTrue(discover_result.startswith(EXPECTED_PREFIX))
