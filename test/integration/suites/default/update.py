from framework.lwm2m_test import *

import socket
import time

class UpdateTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send a correct Update
        self.communicate('send-update')
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())

        # should not send more messages after receiving a correct response
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))

        # should automatically send Updates before lifetime expires
        LIFETIME = 2

        self.serv.send(Lwm2mWrite('/1/1/1', str(LIFETIME)))
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), pkt)

        # update lifetime on the server
        self.communicate('send-update')
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=['lt=' + str(LIFETIME)],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())

        # wait for auto-scheduled Update
        pkt = self.serv.recv(timeout_s=LIFETIME)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())


class ReconnectTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        original_remote_addr = self.serv.get_remote_addr()

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()
        pkt = self.serv.recv()

        # should retain remote port after reconnecting
        self.assertEqual(original_remote_addr, self.serv.get_remote_addr())
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())


class ReconnectBootstrapTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        self.setup_demo_with_servers(num_servers=0, bootstrap_server=True)

    def runTest(self):
        self.bootstrap_server.set_timeout(timeout_s=1)
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        original_remote_addr = self.bootstrap_server.get_remote_addr()

        # reconnect
        self.communicate('reconnect')
        self.bootstrap_server.reset()
        pkt = self.bootstrap_server.recv()

        # should retain remote port after reconnecting
        self.assertEqual(original_remote_addr, self.bootstrap_server.get_remote_addr())
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)

        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        demo_port = int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))
        self.assertEqual(self.bootstrap_server.get_remote_addr()[1], demo_port)

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # reconnect once again
        self.communicate('reconnect')

        # now there should be no Bootstrap Request
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=3))

        # should retain remote port after reconnecting
        new_demo_port = int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))
        self.assertEqual(demo_port, new_demo_port)

        self.bootstrap_server.connect(('127.0.0.1', new_demo_port))

        # DELETE /1337, essentially a no-op to check connectivity
        req = Lwm2mDelete(Lwm2mPath('/1337'))
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(), self.bootstrap_server.recv())

class UpdateFallbacksToRegisterAfterLifetimeExpiresTest(test_suite.Lwm2mSingleServerTest):
    LIFETIME = 4

    def setUp(self):
        super().setUp(auto_register=False,
                      lifetime=self.LIFETIME)
        self.assertDemoRegisters(lifetime=self.LIFETIME)

    def runTest(self):
        port = self.serv.get_listen_port()

        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME/2+1)

        # make subsequent Updates fail to force re-Register
        self.serv.close()
        time.sleep(self.LIFETIME + 1)

        self.serv = Lwm2mServer(listen_port=port)
        self.assertDemoRegisters(lifetime=self.LIFETIME, timeout_s=5)

class UpdateFallbacksToRegisterAfterCoapClientErrorResponse(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        def check(code: coap.Code):
            self.communicate('send-update')

            req = self.serv.recv()
            self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                            content=b''), req)
            self.serv.send(Lwm2mErrorResponse.matching(req)(code))

            self.assertDemoRegisters()

        # check all possible client (4.xx) errors
        for detail in range(32):
            check(coap.Code(4, detail))

class ReconnectFailsWithCoapErrorCodeTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_INTERNAL_SERVER_ERROR))

        # make sure that client retries
        self.serv.reset()
        pkt = self.serv.recv(timeout_s=3)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())

class ReconnectFailsWithConnectionRefusedTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        listen_port = self.serv.get_listen_port()
        self.serv.close()

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()

        # give the process some time to fail
        time.sleep(1)

        self.serv = Lwm2mServer(listen_port=listen_port)
        self.serv.set_timeout(timeout_s=1)

        # make sure that client retries
        self.assertDemoUpdatesRegistration(timeout_s=5)
