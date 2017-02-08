from framework.lwm2m_test import *
import socket

class BufferSizeTest:
    class Base(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, inbuf_size=4096, outbuf_size=4096, auto_register=True, endpoint=None):
            extra_args = '-I %d -O %d' % (inbuf_size, outbuf_size)
            if endpoint is not None:
                extra_args += ' -e %s' % endpoint
            self.setup_demo_with_servers(num_servers=1,
                                         extra_cmdline_args=extra_args.split(),
                                         auto_register=auto_register)

class SmallInputBufferAndManyOptions(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(inbuf_size=46)

    def runTest(self):
        self.create_instance(self.serv, oid=1337, iid=1)
        def make_optionlist(size):
            return [ coap.Option.URI_QUERY('%d' % i) for i in range(size) ]

        pkt = Lwm2mWrite('/1337/1/6', options=make_optionlist(128), content=b'32')
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_REQUEST_ENTITY_TOO_LARGE),
                            self.serv.recv(timeout_s=1))

        # When options do not dominate message size everything works fine.
        pkt = Lwm2mWrite('/1337/1/6', options=make_optionlist(2), content=b'32')
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), self.serv.recv(timeout_s=1))

class OutputBufferTooSmallButDemoDoesntCrash(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(outbuf_size=8, auto_register=False)

    def tearDown(self):
        # If demo crashes valgrind will tell us.
        super().tearDown(auto_deregister=False)

    def runTest(self):
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)

class OutputBufferTooSmallToHoldEndpoint(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(outbuf_size=128, auto_register=False, endpoint="F"*125)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)

class OutputBufferCannotHoldPayloadMarker(BufferSizeTest.Base):
    def setUp(self):
        # +------+---------------------------------------------+
        # | Size |                For what?                    |
        # +------+---------------------------------------------+
        # | 1b   |  version+type+token length                  |
        # +------+---------------------------------------------+
        # | 1b   |  message code                               |
        # +------+---------------------------------------------+
        # | 2b   |  message id                                 |
        # +------+---------------------------------------------+
        # | 8b   |  token                                      |
        # +------+---------------------------------------------+
        # | 3b   |  option URI_PATH == 'rd'                    |
        # +------+---------------------------------------------+
        # | 2b   |  option CONTENT_FORMAT == APPLICATION_LINK  |
        # +------+---------------------------------------------+
        # | 133b |  option URI_QUERY == 'ep=FFFF...F'          |
        # +------+---------------------------------------------+
        # | 9b   |  option URI_QUERY == 'lt=86400'             |
        # +------+---------------------------------------------+
        # | 10b  |  option URI_QUERY == 'lwm2m=1.0'            |
        # +------+---------------------------------------------+

        #
        # In total: 1 + 1 + 2 + 8 + 3 + 2 + 133 + 9 + 10 = 169
        #
        # This was enough to:
        # 1. Write all headers to the message.
        # 2. Attempt to write a payload marker.
        # 3. Make it crash, as anjay asserted (2.) must always be possible.
        super().setUp(endpoint="F"*128, outbuf_size=169, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=True)

    def runTest(self):
        from . import register as r
        r.BlockRegister().Test()(self.serv)

class OutputBufferAbleToHoldPayloadMarkerBeginsBlockTransfer(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(endpoint="F"*128, outbuf_size=170, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=True)

    def runTest(self):
        from . import register as r
        r.BlockRegister().Test()(self.serv)

class ConfiguredInputBufferSizeDeterminesMaxIncomingPacketSize(BufferSizeTest.Base):
    def setUp(self):
        # +------+---------------------------------------------+
        # | Size |                For what?                    |
        # +------+---------------------------------------------+
        # | 1b   |  version+type+token length                  |
        # +------+---------------------------------------------+
        # | 1b   |  message code                               |
        # +------+---------------------------------------------+
        # | 2b   |  message id                                 |
        # +------+---------------------------------------------+
        # | 8b   |  token                                      |
        # +------+---------------------------------------------+
        # | 3b   |  option URI_PATH == 'rd'                    |
        # +------+---------------------------------------------+
        #
        # in total: 1 + 1 + 2 + 8 + 3 = 15
        super().setUp(inbuf_size=15, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=True, path='/rd')

    def runTest(self):
        self.assertDemoRegisters(location='/rd')

class InputBufferSizeTooSmallToHoldRegisterResponse(BufferSizeTest.Base):
    def setUp(self):
        # see calculation in ConfiguredInputBufferSizeDeterminesMaxIncomingPacketSize
        # the buffer is 1B too short to hold Register response
        super().setUp(inbuf_size=14, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        req = self.serv.recv()
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=%d' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME, 86400)),
                req)
        self.serv.send(Lwm2mCreated.matching(req)(location='/rd'))

        # client should not be able to read the whole packet, considering
        # registration unsuccessful and retrying after reconnecting
        self.serv.reset()
        req = self.serv.recv(timeout_s=5)
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=%d' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME, 86400)),
                req)
        # send a response the client is able to hold inside its buffer
        # to break synchronous wait and make demo terminate cleanly
        self.serv.send(Lwm2mReset.matching(req)())

