import socket

from framework.lwm2m_test import *


class ClientIgnoresNonBootstrapTrafficDuringBootstrap(test_suite.Lwm2mSingleServerTest):
    def _get_socket_count(self):
        return int(self.communicate('socket-count', match_regex='SOCKET_COUNT==([0-9]+)\n').group(1))

    def _wait_for_socket_count(self, expected_count):
        # wait for sockets initialization
        # scheduler-based socket initialization might delay socket setup a bit;
        # this loop is here to ensure `communicate()` call below works as
        # expected
        for _ in range(10):
            if self._get_socket_count() == expected_count:
                break
        else:
            self.fail("sockets not initialized in time")

    def setUp(self):
        self.setup_demo_with_servers(bootstrap_server=True)

        self._wait_for_socket_count(2)
        demo_port = int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))
        self.bootstrap_server.connect(('127.0.0.1', demo_port))

    def runTest(self):
        req = Lwm2mCreate('/1337')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), res)
        self.assertEqual('/1337/1', res.get_location_path())

        req = Lwm2mRead('/1337/1/1')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
        self.assertEqual(b'0', res.content)

        # "spurious" Server Initiated Bootstrap
        req = Lwm2mWrite('/1337/1/1', b'42')
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.bootstrap_server.recv())

        # now regular server shall not be able to communicate with the client
        req = Lwm2mExecute('/1337/1/2')
        self.serv.send(req)
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        self.assertEqual(1, self._get_socket_count())

        # Bootstrap Finish
        self.bootstrap_server.send(Lwm2mBootstrapFinish())
        self.assertIsInstance(self.bootstrap_server.recv(), Lwm2mChanged)

        self._wait_for_socket_count(2)

        # now we shall be able to do that Execute
        req = Lwm2mExecute('/1337/1/2')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), res)

        # verify that Execute was performed just once
        req = Lwm2mRead('/1337/1/1')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)
        self.assertEqual(b'43', res.content)
