import socket

from framework.lwm2m_test import *

class BootstrapTransactionTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.bootstrap_server = Lwm2mServer()
        self.servers = [Lwm2mServer()]

        demo_args = (self.make_demo_args([self.bootstrap_server])
                     + ['--bootstrap', '--bootstrap-timeout', '-1'])
        self.start_demo(demo_args)

    def tearDown(self):
        self.teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME), pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # Create Server object
        req = Lwm2mWrite('/1/1',
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

        req = Lwm2mWrite('/0/2',
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
        req = Lwm2mWrite('/12360/42',
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
        req = Lwm2mWrite('/12360/42',
                         TLV.make_resource(RID.GeoPoints.Longitude, 69.0).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())
