from framework.lwm2m_test import *
import unittest
import binascii
import os

msg_id_generator = SequentialMsgIdGenerator(42)

A_LOT = 10000
A_LOT_OF_STUFF = random_stuff(A_LOT)

A_LITTLE = 32
A_LITTLE_STUFF = random_stuff(A_LITTLE)

def equal_chunk_splitter(chunk_size):
    def split(data):
        return ((idx, chunk_size, chunk)
                for idx, chunk in enumerate(data[i:i+chunk_size]
                                            for i in range(0, len(data), chunk_size)))
    return split

def packets_from_chunks(chunks, process_options=None, path='/5/0/0'):
    for idx, (seq_num, chunk_size, chunk) in enumerate(chunks):
        has_more = (idx != len(chunks) - 1)

        options = (uri_path_to_options(path)
                   + [coap.Option.CONTENT_FORMAT(coap.ContentFormat.APPLICATION_OCTET_STREAM),
                   coap.Option.BLOCK1(seq_num=seq_num, has_more=has_more, block_size=chunk_size)])

        if process_options is not None:
            options = process_options(options, idx)

        yield coap.Packet(type=coap.Type.CONFIRMABLE,
                          code=coap.Code.REQ_PUT,
                          token=random_stuff(size=5),
                          msg_id=next(msg_id_generator),
                          options=options,
                          content=chunk)

class BlockTest(test_suite.Lwm2mSingleServerTest):
    def block_init_file(self):
        import tempfile

        with tempfile.NamedTemporaryFile(delete=False) as f:
            fw_file_name = f.name
        self.communicate('set-fw-package-path %s' % (os.path.abspath(fw_file_name)))
        return fw_file_name

    def block_send(self, data, splitter):
        fw_file_name = self.block_init_file()

        chunks = list(splitter(make_firmware_package(data)))

        for request in packets_from_chunks(chunks):
            self.serv.send(request)
            response = self.serv.recv()
            self.assertIsSuccessResponse(response, request)

        with open(fw_file_name, 'rb') as fw_file:
            self.assertEqual(fw_file.read(), data)

        os.unlink(fw_file_name)

    def assertIsResponse(self, response, request):
        self.assertEqual(coap.Type.ACKNOWLEDGEMENT, response.type)
        self.assertEqual(request.msg_id, response.msg_id)
        self.assertEqual(request.token, response.token)

    def assertIsSuccessResponse(self, response, request):
        self.assertIsResponse(response, request)

        if request.get_options(coap.Option.BLOCK1)[0].has_more():
            self.assertEqual(coap.Code.RES_CONTINUE, response.code)
        else:
            self.assertEqual(coap.Code.RES_CHANGED, response.code)

        self.assertEqual(request.get_options(coap.Option.BLOCK1),
                         response.options)

    @unittest.skip
    def runTest(self):
        pass

class BlockIncompleteTest(BlockTest):
    def runTest(self):
        # incomplete BLOCK should be rejected
        chunks = list(equal_chunk_splitter(1024)(A_LOT_OF_STUFF))
        self.assertGreater(len(chunks), 2)

        packets = list(packets_from_chunks([chunks[0], chunks[1], chunks[-1]]))
        self.assertEqual(len(packets), 3)

        # first packet with seq_num > 0 should be rejected
        req = packets[-1]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
                            self.serv.recv())

        # consecutive packets recieved by the anjay with such seq_nums: (0, k > 1)
        req = packets[0]
        self.serv.send(req)
        res = self.serv.recv()
        self.assertIsSuccessResponse(res, req)

        req = packets[-1]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
                            self.serv.recv())

        # consecutive packets recieved by the anjay with such seq_nums: (0, 1, 0)
        req = packets[0]
        self.serv.send(req)
        res = self.serv.recv()
        self.assertIsSuccessResponse(res, req)

        req = packets[1]
        self.serv.send(req)
        res = self.serv.recv()
        self.assertIsSuccessResponse(res, req)

        req = packets[0]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
                            self.serv.recv())

class BlockSizesTest(BlockTest):
    def runTest(self):
        # multiple chunk sizes: min/max/something in between
        for chunk_size in (16, 256, 1024):
            self.block_send(A_LOT_OF_STUFF, equal_chunk_splitter(chunk_size))

class BlockSingleChunkTest(BlockTest):
    def runTest(self):
        # single-chunk block
        self.block_send(A_LITTLE_STUFF, equal_chunk_splitter(chunk_size=len(A_LITTLE_STUFF)*2))

class BlockVariableChunkSizeTest(BlockTest):
    def runTest(self):
        def shrinking_chunk_splitter(initial_chunk_size):
            def split(data):
                MIN_CHUNK_SIZE = 16

                chunk_size = initial_chunk_size
                idx = 0
                i = 0
                while i < len(data):
                    yield idx, chunk_size, data[i:i+chunk_size]

                    i += chunk_size
                    idx += 1
                    if chunk_size > MIN_CHUNK_SIZE:
                        self.assertTrue(chunk_size % 2 == 0)
                        chunk_size //= 2
                        idx *= 2

            return split

        def growing_chunk_splitter(initial_chunk_size):
            def split(data):
                MAX_CHUNK_SIZE = 1024

                chunk_size = initial_chunk_size
                idx = 0
                i = 0

                # block sequence number specifies block offset in multiples of
                # chunk_size, so the smallest chunk size must be used twice to ensure
                # that sequence numbers are integers
                yield 0, chunk_size, data[0:chunk_size]
                i += chunk_size
                idx += 1

                while i < len(data):
                    yield idx, chunk_size, data[i:i+chunk_size]

                    i += chunk_size
                    idx += 1
                    if chunk_size < MAX_CHUNK_SIZE:
                        chunk_size *= 2
                        self.assertTrue(idx % 2 == 0)
                        idx //= 2

            return split

        def alternating_size_chunk_splitter(sizes):
            max_size = max(sizes)
            self.assertTrue(all(max_size % x == 0 for x in sizes))

            def split(data):
                import itertools
                sizes_iter = itertools.cycle(sizes)

                chunk_size = next(sizes_iter)
                idx = 0
                i = 0

                while i < len(data):
                    for _ in range(max_size // chunk_size):
                        yield idx, chunk_size, data[i:i+chunk_size]

                        i += chunk_size
                        idx += 1
                        if i >= len(data):
                            return

                    new_chunk_size = next(sizes_iter)
                    if new_chunk_size < chunk_size:
                        idx *= chunk_size // new_chunk_size
                    else:
                        self.assertTrue(idx % (new_chunk_size // chunk_size) == 0)
                        idx //= new_chunk_size // chunk_size
                    chunk_size = new_chunk_size

            return split


        # variable chunk size
        self.block_send(A_LOT_OF_STUFF, shrinking_chunk_splitter(initial_chunk_size=1024))
        self.block_send(A_LOT_OF_STUFF, growing_chunk_splitter(initial_chunk_size=16))
        self.block_send(A_LOT_OF_STUFF, alternating_size_chunk_splitter(sizes=[32, 512, 256, 1024, 64]))

class BlockNonFirstTest(BlockTest):
    def runTest(self):
        import socket

        data = A_LOT_OF_STUFF
        splitter = equal_chunk_splitter(1024)

        self.block_init_file()
        chunks = list(splitter(data))

        request = list(packets_from_chunks([chunks[1]]))[0]

        self.serv.send(request)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(request)(coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
                            self.serv.recv())

class BlockBrokenStreamTest(BlockTest):
    def runTest(self):
        import socket

        data = A_LOT_OF_STUFF
        splitter = equal_chunk_splitter(1024)

        self.block_init_file()
        chunks = list(splitter(data))[:2]

        class ObjectIdIncrementer(object):
            def __init__(self):
                self.chunk_no = 0
                self.last_orig_opts = None

            def __call__(self, opts, idx):
                import copy
                self.last_orig_opts = copy.deepcopy(opts)
                for i in range(len(opts)):
                    if opts[i].matches(coap.Option.URI_PATH):
                        opts[i] = coap.Option.URI_PATH(str(int(opts[i].content) + self.chunk_no))
                        self.chunk_no += 1
                        break
                return opts

        incrementer = ObjectIdIncrementer()
        first_request, second_request = packets_from_chunks(chunks, incrementer)

        self.serv.send(first_request)
        self.assertIsSuccessResponse(self.serv.recv(), first_request)

        # broken stream
        self.serv.send(second_request)
        self.assertMsgEqual(Lwm2mReset.matching(second_request)(),
                            self.serv.recv())

        # send the valid packet so that demo can terminate cleanly
        second_request.options = incrementer.last_orig_opts
        self.serv.send(second_request)
        self.assertIsSuccessResponse(self.serv.recv(), second_request)

def block2_adder(options, idx):
    return options + [ coap.Option.BLOCK2(seq_num=0, has_more=0, block_size=16) ]

class BlockBidirectionalFailure(BlockTest):
    def runTest(self):
        splitter = equal_chunk_splitter(1024)
        chunks = list(splitter(A_LOT_OF_STUFF))
        request = list(packets_from_chunks([chunks[0]], block2_adder))[0]
        self.serv.send(request)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(request)(coap.Code.RES_BAD_OPTION),
                            self.serv.recv())

class BlockMostlyUnidirectionalWithSmallSurprise(BlockTest):
    def runTest(self):
        chunks = list(equal_chunk_splitter(1024)(A_LOT_OF_STUFF))
        self.assertGreater(len(chunks), 4)

        def helper_modifier(options, idx):
            if idx == 2:
                return block2_adder(options, idx)
            return options

        packets = list(packets_from_chunks([chunks[0], chunks[1],
                                            chunks[2], chunks[-1]], helper_modifier))

        req = packets[0]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContinue.matching(req)(), self.serv.recv())

        req = packets[1]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContinue.matching(req)(), self.serv.recv())

        req = packets[2]
        self.serv.send(req)

        # And BAD_OPTION actually stops the block transfer
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_BAD_OPTION),
                            self.serv.recv())

class BlockDuplicate(BlockTest):
    def runTest(self):
        pkt1 = Lwm2mWrite('/5/0/0', b'x' * 16,
                          format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                          options=[ coap.Option.BLOCK1(0, 1, 16) ])

        pkt2 = Lwm2mWrite('/5/0/0', b'x' * 16,
                          format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                          options=[ coap.Option.BLOCK1(1, 0, 16) ])

        self.serv.send(pkt1)
        response1 = self.serv.recv()
        self.assertMsgEqual(Lwm2mContinue.matching(pkt1)(), response1)

        self.serv.send(pkt1)
        response2 = self.serv.recv()
        self.assertMsgEqual(response1, response2)

        self.serv.send(pkt2)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt2)(), self.serv.recv())


class BlockBadBlock1SizeInTheMiddleOfTransfer(BlockTest):
    def runTest(self):
        num_correct_blocks = 4
        seq_num = 0
        for _ in range(num_correct_blocks):
            pkt = Lwm2mWrite('/5/0/0', b'x' * 16,
                             format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                             options=[ coap.Option.BLOCK1(seq_num, 1, 16) ])
            self.serv.send(pkt)
            self.assertMsgEqual(Lwm2mContinue.matching(pkt)(), self.serv.recv())

            seq_num += 1

        invalid_pkt = Lwm2mWrite('/5/0/0', b'x' * 16,
                                 format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                                 options=[ coap.Option.BLOCK1(seq_num, 1, 2048) ])
        self.serv.send(invalid_pkt)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(invalid_pkt)(code=coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())

        # an invalid block aborts the block transfer; De-Register should work
        # fine here

class MessageInTheMiddleOfBlockTransfer:
    class Test(BlockTest):
        def test_with_message(self, request, expected_response):
            chunks = list(equal_chunk_splitter(1024)(A_LOT_OF_STUFF))
            self.assertGreater(len(chunks), 4)

            packets = list(packets_from_chunks(chunks))

            self.serv.send(packets[0])
            self.assertMsgEqual(Lwm2mContinue.matching(packets[0])(), self.serv.recv())

            self.serv.send(packets[1])
            self.assertMsgEqual(Lwm2mContinue.matching(packets[1])(), self.serv.recv())

            self.serv.send(request)
            if expected_response is not None:
                self.assertMsgEqual(expected_response, self.serv.recv())

            for request in packets[2:]:
                self.serv.send(request)
                response = self.serv.recv()
                self.assertIsSuccessResponse(response, request)

class CoAPPingInTheMiddleOfBlockTransfer(MessageInTheMiddleOfBlockTransfer.Test):
    def runTest(self):
        req = Lwm2mEmpty(type=coap.Type.CONFIRMABLE)
        req.fill_placeholders()
        res = Lwm2mReset.matching(req)()
        self.test_with_message(req, res)

class ConfirmableRequestInTheMiddleOfBlockTransfer(MessageInTheMiddleOfBlockTransfer.Test):
    def runTest(self):
        req = Lwm2mRead('/3/0/0')
        req.fill_placeholders()
        res = Lwm2mReset.matching(req)()
        self.test_with_message(req, res)

class NonConfirmableRequestInTheMiddleOfBlockTransfer(MessageInTheMiddleOfBlockTransfer.Test):
    def runTest(self):
        req = Lwm2mEmpty(type=coap.Type.NON_CONFIRMABLE)
        self.test_with_message(req, expected_response=None)
