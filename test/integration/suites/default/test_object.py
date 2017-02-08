from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV

class TestObject:
    class TestCase(test_suite.Lwm2mSingleServerTest):
        def setUp(self):
            super().setUp()

            self.serv.set_timeout(timeout_s=1)

            req = Lwm2mCreate('/1337')
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mCreated.matching(req)(),
                                self.serv.recv())


class TimestampTest(TestObject.TestCase):
    def runTest(self):
        req = Lwm2mRead('/1337/1/1')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())

class CounterTest(TestObject.TestCase):
    def runTest(self):
        # ensure the counter is zero initially
        req = Lwm2mRead('/1337/1/1', accept=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'0'),
                            self.serv.recv())

        # execute Increment Counter
        req = Lwm2mExecute('/1337/1/2')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # counter should be incremented by the execute
        req = Lwm2mRead('/1337/1/1', accept=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'1'),
                            self.serv.recv())

class IntegerArrayTest(TestObject.TestCase):
    def runTest(self):
        # ensure the array is empty
        req = Lwm2mRead('/1337/1/3')
        self.serv.send(req)

        empty_array_tlv = TLV.make_multires(resource_id=3, instances=[])
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=empty_array_tlv.serialize()),
                            self.serv.recv())

        # write something
        array_tlv = TLV.make_multires(
                resource_id=3,
                instances=[
                    # (1, (0).to_bytes(0, byteorder='big')),
                    (2, (12).to_bytes(1, byteorder='big')),
                    (4, (1234).to_bytes(2, byteorder='big')),
                ])
        req = Lwm2mWrite('/1337/1/3',
                         array_tlv.serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # check updated content
        req = Lwm2mRead('/1337/1/3')
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mContent.matching(req)(content=array_tlv.serialize(),
                                                       format=coap.ContentFormat.APPLICATION_LWM2M_TLV),
                            self.serv.recv())

class ExecArgsArrayTest(TestObject.TestCase):
    def runTest(self):
        args = [
            (1, None),
            (1, b''),
            (2, b'1'),
            (3, b'12345'),
            (9, b'0' * 1024)
        ]

        req = Lwm2mRead('/1337/1/4')
        self.serv.send(req)

        empty_array_tlv = TLV.make_multires(resource_id=4, instances=[])
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=empty_array_tlv.serialize()),
                            self.serv.recv())

        # perform the Execute with arguments
        exec_content = b','.join(b'%d' % k if v is None else b"%d='%s'" % (k, v)
                                 for k, v in args)
        req = Lwm2mExecute('/1337/1/2', content=exec_content)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Exectue args should now be saved in the Execute Arguments array
        req = Lwm2mRead('/1337/1/4')
        self.serv.send(req)

        exec_args_tlv = TLV.make_multires(resource_id=4,
                                          instances=((k, v or b'') for k, v in args))
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=exec_args_tlv.serialize()),
                            self.serv.recv())
