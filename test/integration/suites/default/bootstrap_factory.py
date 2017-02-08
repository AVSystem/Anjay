import socket

from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV

class BootstrapFactoryTest(test_suite.Lwm2mTest, test_suite.SingleServerAccessor):
    def setUp(self):
        extra_args = ['--bootstrap-timeout', '5']
        self.setup_demo_with_servers(num_servers=1,
                                     bootstrap_server=True,
                                     extra_cmdline_args=extra_args)

    def tearDown(self):
        self.teardown_demo_with_servers()

    def runTest(self):
        # no message on the bootstrap socket - already bootstrapped
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=2))

        # no changes
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        # still no message
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=4))

        # now the Bootstrap Server Account should be gone
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(content=ANY)

        req = Lwm2mDiscover('/0')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

        self.assertIn(b'</0/2/', res.content)
        self.assertNotIn(b'</0/1/', res.content)
