from framework.lwm2m_test import *
from framework.lwm2m.tlv import TLV
import unittest

TEST_OBJECT_OID = 1337
TEST_OBJECT_RES_BYTES = 5
TEST_OBJECT_RES_BYTES_SIZE = 6
TEST_OBJECT_RES_BYTES_BURST = 7

class BlockResponseTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self, bytes_size=9001):
        super(BlockResponseTest, self).setUp()
        self.make_test_instance()
        self.set_bytes_size(1, bytes_size)
        self.set_bytes_burst(1, 1000)

    @unittest.skip
    def runTest(self):
        pass

    def assertIdentityMatches(self, response, request):
        self.assertEqual(request.msg_id, response.msg_id)

        if response.code != coap.Code.EMPTY:
            self.assertEqual(request.token, response.token)

    def assertBlockResponse(self, response, seq_num, has_more, block_size):
        self.assertEqual(response.get_options(coap.Option.BLOCK2)[0].has_more(), has_more)
        self.assertEqual(response.get_options(coap.Option.BLOCK2)[0].seq_num(), seq_num)
        self.assertEqual(response.get_options(coap.Option.BLOCK2)[0].block_size(), block_size)

    def make_test_instance(self):
        req = Lwm2mCreate("/%d" % TEST_OBJECT_OID)
        self.serv.send(req)
        response = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), response);

    def set_bytes_size(self, iid, size):
        req = Lwm2mWrite("/%d/%d/%d" % (TEST_OBJECT_OID, iid, TEST_OBJECT_RES_BYTES_SIZE), str(size))
        self.serv.send(req)
        response = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), response);

    def set_bytes_burst(self, iid, size):
        req = Lwm2mWrite("/%d/%d" % (TEST_OBJECT_OID, iid),
                         TLV.make_resource(TEST_OBJECT_RES_BYTES_BURST, int(size)).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.serv.send(req)
        response = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), response);

    def read_bytes(self, iid, seq_num=None, block_size=None, options_modifier=None, accept=None):
        opts = [coap.Option.BLOCK2(seq_num=seq_num,has_more=0,block_size=block_size)] \
                if seq_num is not None and block_size else []
        if options_modifier is not None:
            opts = options_modifier(opts)

        req = Lwm2mRead("/%d/%d/%d" % (TEST_OBJECT_OID, iid, TEST_OBJECT_RES_BYTES),
                        options=opts, accept=accept)
        self.serv.send(req)
        res = self.serv.recv(timeout_s=5)
        self.assertIdentityMatches(res, req)
        return res

    def read_blocks(self, iid, block_size=1024, base_seq=0, accept=None):
        data = bytearray()
        while True:
            tmp = self.read_bytes(iid, base_seq, block_size, accept=accept)
            data += tmp.content
            base_seq += 1
            if not tmp.get_options(coap.Option.BLOCK2)[0].has_more():
                break
        return data

class BlockResponseFirstRequestIsNotBlock(BlockResponseTest):
    def setUp(self):
        super(BlockResponseFirstRequestIsNotBlock, self).setUp(bytes_size=9025)

    def runTest(self):
        response = self.read_bytes(iid=1)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)
        # Currently we have to read this till the end
        self.read_blocks(iid=1)

class BlockResponseFirstRequestIsBlock(BlockResponseTest):
    def runTest(self):
        response = self.read_bytes(iid=1, seq_num=1, block_size=1024)
        # Started from seq_num=1, clearly incorrect request
        self.assertEqual(response.code, coap.Code.RES_REQUEST_ENTITY_INCOMPLETE)

        # Normal request
        response = self.read_bytes(iid=1, seq_num=0, block_size=1024)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)
        self.read_blocks(iid=1)

class BlockResponseSizeNegotiation(BlockResponseTest):
    def runTest(self):
        # Forcing block_size from the very first request
        data = self.read_blocks(iid=1, block_size=16, base_seq=0)
        for i in range(len(data)):
            self.assertEqual(data[i], i % 128)

        # Forcing block_size after first message
        response = self.read_bytes(iid=1)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        second_payload = self.read_blocks(iid=1)
        self.assertEqual(data, second_payload)

        # Negotiation after first message
        response = self.read_bytes(iid=1, seq_num=0, block_size=32)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=32)

        third_payload = self.read_blocks(iid=1, block_size=32)
        self.assertEqual(data, third_payload)

class BlockResponseSizeRenegotiation(BlockResponseTest):
    def runTest(self):
        # Case 0: when first request does not contain BLOCK2 option.
        response = self.read_bytes(iid=1, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        response = self.read_bytes(iid=1, seq_num=0, block_size=32)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=32)

        response = self.read_bytes(iid=1, seq_num=0, block_size=16)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=16)

        self.read_blocks(iid=1, block_size=16)

        # Case 1: when first request does contain BLOCK2 option.
        response = self.read_bytes(iid=1, seq_num=0, block_size=512)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=512)

        response = self.read_bytes(iid=1, seq_num=0, block_size=32)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=32)

        response = self.read_bytes(iid=1, seq_num=0, block_size=16)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=16)

        self.read_blocks(iid=1, block_size=16)

class BlockResponseSizeRenegotiationInTheMiddleOfTransfer(BlockResponseTest):
    def runTest(self):
        response = self.read_bytes(iid=1, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        # request new size on non-first block. the client should reject such
        # packet and wait for a valid one instead
        response = self.read_bytes(iid=1, seq_num=1, block_size=16)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(coap.Code.RES_BAD_REQUEST, response.code)

        self.read_blocks(iid=1, block_size=1024)

class BlockResponseInvalidSizeDuringRenegotation(BlockResponseTest):
    def runTest(self):
        # Case 0: when first request does not contain BLOCK2 option.
        response = self.read_bytes(iid=1, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        response = self.read_bytes(iid=1, seq_num=0, block_size=2048)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(response.code, coap.Code.RES_BAD_REQUEST)

        # the error does not abort block-wise transfer; finish it
        # before continuing
        self.read_blocks(iid=1, block_size=1024)

        # Case 1: when first request does contain BLOCK2 option.
        response = self.read_bytes(iid=1, seq_num=0, block_size=2048)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(response.code, coap.Code.RES_BAD_REQUEST)

class BlockResponseBadBlock2SizeInTheMiddleOfTransfer(BlockResponseTest):
    def runTest(self):
        first_invalid_seq_num = 4
        for seq_num in range(first_invalid_seq_num):
            response = self.read_bytes(iid=1, seq_num=seq_num, block_size=1024)
            self.assertBlockResponse(response, seq_num=seq_num, has_more=1, block_size=1024)

        response = self.read_bytes(iid=1, seq_num=first_invalid_seq_num, block_size=2048)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(response.code, coap.Code.RES_BAD_REQUEST)

        # should abort the transfer

class BlockResponseBadBlock1(BlockResponseTest):
    def runTest(self):
        def opts_modifier(opts):
            return opts + [ coap.Option.BLOCK1(0,0,16) ]

        response = self.read_bytes(iid=1, seq_num=0, block_size=512)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=512)
        response = self.read_bytes(iid=1, seq_num=1, block_size=512,
                                  options_modifier=opts_modifier)

        # should abort the transfer
        self.assertEqual(response.code, coap.Code.RES_BAD_OPTION)

class BlockResponseBiggerBlockSizeThanData(BlockResponseTest):
    def setUp(self):
        super().setUp(bytes_size=5)

    def runTest(self):
        response = self.read_bytes(iid=1, seq_num=0, block_size=1024)
        self.assertBlockResponse(response, seq_num=0, has_more=0, block_size=1024)

# Tests different rates at which anjay's stream buffers are filled
class BlockResponseDifferentBursts(BlockResponseTest):
    BYTES_AMOUNT = 9001
    def setUp(self):
        super().setUp(bytes_size=BlockResponseDifferentBursts.BYTES_AMOUNT)

    def runTest(self):
        for i in (1, 10, 50, 100, 1000, 1024, 1200, 2048, 4096, 5000, 9001):
            self.set_bytes_burst(1, i)

            response = self.read_bytes(iid=1)
            self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)
            self.read_blocks(iid=1)
