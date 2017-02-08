from framework.lwm2m_test import *

import socket

class UriChangeUpdateTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.servers = [Lwm2mServer(), Lwm2mServer()]
        self.bootstrap_server = Lwm2mServer()

        demo_args = (self.make_demo_args([self.bootstrap_server, self.servers[0]])
                     + ['--bootstrap'])
        self.start_demo(demo_args)

    def tearDown(self):
        try:
            self.request_demo_shutdown()
            self.assertDemoDeregisters(self.servers[1])
        finally:
            self.bootstrap_server.close()
            self.servers[0].close()
            self.servers[1].close()

            self.terminate_demo()

    def runTest(self):
        regular_serv1_uri = 'coap://127.0.0.1:%d' % self.servers[0].get_listen_port()
        regular_serv2_uri = 'coap://127.0.0.1:%d' % self.servers[1].get_listen_port()

        # Register to regular_serv1
        pkt = self.servers[0].recv(timeout_s=1)
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
                pkt)
        self.servers[0].send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        req = Lwm2mDiscover('/0')
        self.servers[0].send(req)
        res = self.servers[0].recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

        self.assertIn(b'</0/1/', res.content)
        self.assertIn(b'</0/2/', res.content)

        # modify the server URI
        demo_port = int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))
        self.bootstrap_server.connect(('127.0.0.1', demo_port))

        req = Lwm2mWrite('/0/2/%d' % RID.Security.ServerURI,
                         regular_serv2_uri)
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # send Bootstrap Finish - trigger notifications
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # we should now get a Registration Update on the new URL
        self.assertDemoUpdatesRegistration(self.servers[1])
