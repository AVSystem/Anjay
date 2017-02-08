from framework.lwm2m_test import *

from .dm.utils import DataModel

import binascii


class Test401_UDPChannelSecurity_PreSharedKeyMode(DataModel.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'


    def setUp(self):
        self.servers = [Lwm2mDtlsServer(psk_identity=self.PSK_IDENTITY,
                                        psk_key=self.PSK_KEY)]

        args = ['--security-mode', 'psk',
                '--identity', str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii'),
                '--server-uri', 'coaps://127.0.0.1:%d' % (self.serv.get_listen_port(),)]
        self.start_demo(cmdline_args=args)


    def runTest(self):
        # a. Registration message (COAP POST) is sent from client to server.
        req = self.serv.recv()
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
                req)

        # b. Client receives Success message (2.01 Created) from the server.
        self.serv.send(Lwm2mCreated.matching(req)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # c. READ (COAP GET) on e.g. ACL object resources
        req = Lwm2mRead('/2')
        self.serv.send(req)

        # d. Server receives success message (2.05 Content) and the
        # requested values (encrypted)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())
