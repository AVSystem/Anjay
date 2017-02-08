import socket

from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV

class BootstrapClientTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.bootstrap_server = Lwm2mServer()
        self.serv = Lwm2mServer()

        demo_args = (self.make_demo_args([self.bootstrap_server])
                     + ['--bootstrap',
                        '--bootstrap-holdoff', '3',
                        '--bootstrap-timeout', '3'])
        self.start_demo(demo_args)

    def tearDown(self):
        try:
            self.request_demo_shutdown()
            self.assertDemoDeregisters(server=self.serv)
        finally:
            self.bootstrap_server.close()
            self.serv.close()

            self.terminate_demo()

    def runTest(self):
        # for the first 3 seconds, the client should wait for Server Initiated Bootstrap
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=2))

        # we should get Bootstrap Request now
        pkt = self.bootstrap_server.recv(timeout_s=2)
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
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
                            self.bootstrap_server.recv(timeout_s=1))

        # Create Security object
        regular_serv_uri = 'coap://127.0.0.1:%d' % self.serv.get_listen_port()

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
                            self.bootstrap_server.recv(timeout_s=1))

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=1))

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

        # now the Bootstrap Server Account should be gone
        req = Lwm2mDiscover('/0')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

        self.assertIn(b'</0/2/', res.content)
        self.assertNotIn(b'</0/1/', res.content)

        self.serv.send(Lwm2mChanged.matching(pkt)())

        # client did not try to register to a Bootstrap server (as in T847)
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=1))


class ClientBootstrapNotSentAfterDisableWithinHoldoffTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.bootstrap_server = Lwm2mServer()
        self.serv = Lwm2mServer()

        demo_args = (self.make_demo_args([self.bootstrap_server, self.serv])
                     + ['--bootstrap',
                        '--bootstrap-holdoff', '3',
                        '--bootstrap-timeout', '3'])
        self.start_demo(demo_args)
        self.assertDemoRegisters(self.serv)

    def tearDown(self):
        try:
            self.request_demo_shutdown()
            self.assertDemoDeregisters(server=self.serv)
        finally:
            self.bootstrap_server.close()
            self.serv.close()

            self.terminate_demo()

    def runTest(self):
        # set Disable Timeout to 5
        req = Lwm2mWrite('/1/2/5', '5')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # disable the server
        req = Lwm2mExecute('/1/2/4')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertDemoDeregisters(self.serv)

        with self.assertRaises(socket.timeout, msg="the client should not send "
                               "Request Bootstrap after disabling the server"):
            self.bootstrap_server.recv(timeout_s=4)

        self.assertDemoRegisters(self.serv, timeout_s=2)


class ClientBootstrapBacksOffAfterErrorResponse(test_suite.Lwm2mTest):
    def setUp(self):
        self.bootstrap_server = Lwm2mServer()

        demo_args = (self.make_demo_args([self.bootstrap_server])
                     + ['--bootstrap'])
        self.start_demo(demo_args)

    def tearDown(self):
        try:
            self.request_demo_shutdown()
        finally:
            self.bootstrap_server.close()

            self.terminate_demo()

    def runTest(self):
        req = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            req)

        self.bootstrap_server.send(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_INTERNAL_SERVER_ERROR))

        with self.assertRaises(socket.timeout, msg="the client should not send "
                               "Request Bootstrap immediately after receiving "
                               "an error response"):
            self.bootstrap_server.recv(timeout_s=1)

class BootstrapNonwritableResources(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(num_servers=0,
                                     bootstrap_server=True)

    def runTest(self):
        pkt=self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # SSID is normally not writable, yet the Bootstrap Server can write to it
        req = Lwm2mWrite('/1/0',
                         TLV.make_resource(RID.Server.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Server.Lifetime, 86400).serialize()
                         + TLV.make_resource(RID.Server.NotificationStoring, 0).serialize()
                         + TLV.make_resource(RID.Server.Binding, b'U').serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)

        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv(timeout_s=2))


class MultipleBootstrapSecurityInstancesNotAllowed(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(num_servers=0,
                                     bootstrap_server=True)

    def runTest(self):
        pkt=self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # Bootstrap Server MUST NOT be allowed to create second Bootstrap Security Instance
        req = Lwm2mWrite('/0/42',
                TLV.make_resource(RID.Security.ServerURI, 'coap://127.0.0.1:5683').serialize()
                         + TLV.make_resource(RID.Security.Bootstrap, 1).serialize()
                         + TLV.make_resource(RID.Security.Mode, 3).serialize()
                         + TLV.make_resource(RID.Security.ShortServerID, 42).serialize()
                         + TLV.make_resource(RID.Security.PKOrIdentity, "").serialize()
                         + TLV.make_resource(RID.Security.SecretKey, "").serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_BAD_REQUEST),
                            self.bootstrap_server.recv(timeout_s=2))
