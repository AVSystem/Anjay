# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import unittest

from collections import namedtuple
from framework.lwm2m_test import *

msg_id_generator = SequentialMsgIdGenerator(42)

A_LOT = 10000
A_LOT_OF_STUFF = random_stuff(A_LOT)

A_LITTLE = 32
A_LITTLE_STUFF = random_stuff(A_LITTLE)

Chunk = namedtuple('Chunk', ('idx', 'size', 'content'))

def equal_chunk_splitter(chunk_size):
    def split(data):
        return (Chunk(idx, chunk_size, chunk)
                for idx, chunk in enumerate(data[i:i + chunk_size]
                                            for i in range(0, len(data), chunk_size)))

    return split


def packets_from_chunks(chunks, process_options=None, path=ResPath.FirmwareUpdate.Package,
                        format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                        code=coap.Code.REQ_PUT):
    for idx, chunk in enumerate(chunks):
        has_more = (idx != len(chunks) - 1)

        options = ((uri_path_to_options(path) if path is not None else [])
                   + [coap.Option.CONTENT_FORMAT(format),
                      coap.Option.BLOCK1(seq_num=chunk.idx, has_more=has_more, block_size=chunk.size)])

        if process_options is not None:
            options = process_options(options, idx)

        yield coap.Packet(type=coap.Type.CONFIRMABLE,
                          code=code,
                          token=random_stuff(size=5),
                          msg_id=next(msg_id_generator),
                          options=options,
                          content=chunk.content)


class Block:
    class Test(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
        def block_init_file(self):
            import tempfile

            with tempfile.NamedTemporaryFile(delete=False) as f:
                fw_file_name = f.name
            self.communicate('set-fw-package-path %s' % (os.path.abspath(fw_file_name)))
            return fw_file_name

        def block_send(self, data, splitter, **make_firmware_package_args):
            fw_file_name = self.block_init_file()

            chunks = list(splitter(make_firmware_package(data, **make_firmware_package_args)))

            for request in packets_from_chunks(chunks):
                self.serv.send(request)
                response = self.serv.recv()
                self.assertIsSuccessResponse(response, request)

            with open(fw_file_name, 'rb') as fw_file:
                self.assertEqual(fw_file.read(), data)

            self.files_to_cleanup.append(fw_file_name)

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

        def setUp(self, *args, **kwargs):
            super().setUp(*args, **kwargs)
            self.files_to_cleanup = []


        def tearDown(self):
            for file in self.files_to_cleanup:
                try:
                    os.unlink(file)
                except FileNotFoundError:
                    pass

            # now reset the state machine
            self.write_resource(self.serv, OID.FirmwareUpdate, 0, RID.FirmwareUpdate.Package, b'\0',
                                format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
            super().tearDown()

        @unittest.skip
        def runTest(self):
            pass


class BlockIncompleteTest(Block.Test):
    def runTest(self):
        # incomplete BLOCK should be rejected
        chunks = list(equal_chunk_splitter(1024)(A_LOT_OF_STUFF))
        self.assertGreater(len(chunks), 4)

        packets = list(packets_from_chunks([chunks[0], chunks[1], chunks[2], chunks[3]]))
        self.assertEqual(len(packets), 4)

        # first packet with seq_num > 0 should be rejected
        req = packets[-1]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
                            self.serv.recv())

        # consecutive packets received by the anjay with such seq_nums: (0, k > 1)
        req = packets[0]
        self.serv.send(req)
        res = self.serv.recv()
        self.assertIsSuccessResponse(res, req)

        req = packets[-1]
        self.serv.send(req)
        # there is no such exchange that this packet could be matched to - the client expects consecutive
        # blocks
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_SERVICE_UNAVAILABLE),
                            self.serv.recv())

        # consecutive packets received by the anjay with such seq_nums: (1, 2, 1)
        req = packets[1]
        self.serv.send(req)
        res = self.serv.recv()
        self.assertIsSuccessResponse(res, req)

        req = packets[2]
        self.serv.send(req)
        res = self.serv.recv()
        self.assertIsSuccessResponse(res, req)

        req = packets[1]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_SERVICE_UNAVAILABLE),
                            self.serv.recv())

        # Finish blockwise transfer
        req = packets[3]
        self.serv.send(req)
        self.assertIsSuccessResponse(self.serv.recv(), req)


class BlockSizesTest(Block.Test):
    def runTest(self):
        # multiple chunk sizes: min/max/something in between
        for chunk_size in (16, 256, 1024):
            self.block_send(A_LOT_OF_STUFF, equal_chunk_splitter(chunk_size))
            # now reset the state machine
            self.write_resource(self.serv, OID.FirmwareUpdate, 0, RID.FirmwareUpdate.Package, b'\0',
                                format=coap.ContentFormat.APPLICATION_OCTET_STREAM)


class BlockSingleChunkTest(Block.Test):
    def runTest(self):
        # single-chunk block
        self.block_send(A_LITTLE_STUFF, equal_chunk_splitter(chunk_size=len(A_LITTLE_STUFF) * 2))


class BlockVariableChunkSizeTest(Block.Test):
    def runTest(self):
        def shrinking_chunk_splitter(initial_chunk_size):
            def split(data):
                MIN_CHUNK_SIZE = 16

                chunk_size = initial_chunk_size
                idx = 0
                i = 0
                while i < len(data):
                    yield Chunk(idx, chunk_size, data[i:i + chunk_size])

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
                yield Chunk(0, chunk_size, data[0:chunk_size])
                i += chunk_size
                idx += 1

                while i < len(data):
                    yield Chunk(idx, chunk_size, data[i:i + chunk_size])

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
                        yield Chunk(idx, chunk_size, data[i:i + chunk_size])

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
        self.write_resource(self.serv, OID.FirmwareUpdate, 0, RID.FirmwareUpdate.Package, b'\0',
                            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.block_send(A_LOT_OF_STUFF, growing_chunk_splitter(initial_chunk_size=16))
        self.write_resource(self.serv, OID.FirmwareUpdate, 0, RID.FirmwareUpdate.Package, b'\0',
                            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.block_send(A_LOT_OF_STUFF, alternating_size_chunk_splitter(sizes=[32, 512, 256, 1024, 64]))


class BlockNonFirstTest(Block.Test):
    def runTest(self):
        data = A_LOT_OF_STUFF
        splitter = equal_chunk_splitter(1024)

        fw_file = self.block_init_file()
        try:
            chunks = list(splitter(data))

            request = list(packets_from_chunks([chunks[1]]))[0]

            self.serv.send(request)
            self.assertMsgEqual(Lwm2mErrorResponse.matching(request)(coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
                                self.serv.recv())
        finally:
            os.unlink(fw_file)


class BlockBrokenStreamTest(Block.Test):
    def runTest(self):

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
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mErrorResponse.matching(second_request)(coap.Code.RES_SERVICE_UNAVAILABLE),
                            res)
        # TODO: T2327
        # self.assertEqual(1, len(res.get_options(coap.Option.MAX_AGE)))

        # send the valid packet so that demo can terminate cleanly
        second_request.options = incrementer.last_orig_opts
        self.serv.send(second_request)
        self.assertIsSuccessResponse(self.serv.recv(), second_request)


def block2_adder(options, idx):
    return options + [coap.Option.BLOCK2(seq_num=0, has_more=0, block_size=16)]


class BlockBidirectionalSuccess(Block.Test):
    def runTest(self):
        splitter = equal_chunk_splitter(1024)
        chunks = list(splitter(A_LOT_OF_STUFF))
        request = list(packets_from_chunks([chunks[0]], block2_adder))[0]
        self.serv.send(request)
        self.assertMsgEqual(Lwm2mChanged.matching(request)(), self.serv.recv())


class BlockMostlyUnidirectionalWithRandomlyInsertedBlock2(Block.Test):
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

        # Finish blockwise transfer.
        packets = list(packets_from_chunks([chunks[0], chunks[1],
                                            chunks[2], chunks[3]]))
        req = packets[2]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContinue.matching(req)(), self.serv.recv())

        req = packets[3]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())


class BlockDuplicate(Block.Test):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--cache-size', '1024'])

    def runTest(self):
        pkt1 = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'x' * 16,
                          format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                          options=[coap.Option.BLOCK1(0, 1, 16)])

        pkt2 = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'x' * 16,
                          format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                          options=[coap.Option.BLOCK1(1, 0, 16)])

        self.serv.send(pkt1)
        response1 = self.serv.recv()
        self.assertMsgEqual(Lwm2mContinue.matching(pkt1)(), response1)

        self.serv.send(pkt1)
        response2 = self.serv.recv()
        self.assertMsgEqual(response1, response2)

        self.serv.send(pkt2)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt2)(), self.serv.recv())


class BlockBadBlock1SizeInTheMiddleOfTransfer(Block.Test):
    def runTest(self):
        num_correct_blocks = 4
        seq_num = 0
        for _ in range(num_correct_blocks):
            pkt = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'x' * 16,
                             format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                             options=[coap.Option.BLOCK1(seq_num, 1, 16)])
            self.serv.send(pkt)
            self.assertMsgEqual(Lwm2mContinue.matching(pkt)(), self.serv.recv())

            seq_num += 1

        invalid_pkt = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'x' * 16,
                                 format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                                 options=[coap.Option.BLOCK1(seq_num, 1, 2048)])
        self.serv.send(invalid_pkt)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(invalid_pkt)(code=coap.Code.RES_BAD_OPTION),
                            self.serv.recv())

        # finish the request
        pkt = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'x' * 16,
                                 format=coap.ContentFormat.APPLICATION_OCTET_STREAM,
                                 options=[coap.Option.BLOCK1(seq_num, 0, 16)])
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), self.serv.recv())


class ValueSplitIntoSeparateBlocks(Block.Test):
    def assertIntValueSplit(self, chunks, value):
        chunks_content = [chunk.content for chunk in chunks]
        chunk_pairs = zip(chunks_content[:-1], chunks_content[1:])
        byte_value = value.to_bytes(4, byteorder='big')
        value_parts = [(byte_value[:i], byte_value[i:]) for i in range(1, len(byte_value))]

        for chunk_left, chunk_right in chunk_pairs:
            for part_left, part_right in value_parts:
                if chunk_left.endswith(part_left) and chunk_right.startswith(part_right):
                    return

        self.fail('Data does not split an integer')

    def runTest(self):
        self.create_instance(self.serv, OID.Test, 1)

        value = 0x123456
        array_content = enumerate([value] * 5)
        content = TLV.make_multires(RID.Test.IntArray, array_content).serialize()
        chunks = list(equal_chunk_splitter(16)(content))
        self.assertIntValueSplit(chunks, value)

        packets = packets_from_chunks(chunks, path=ResPath.Test[1].IntArray,
                                      format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        for request in packets:
            self.serv.send(request)
            response = self.serv.recv()
            self.assertIsSuccessResponse(response, request)

        client_content = self.read_resource(self.serv, OID.Test, 1, RID.Test.IntArray).content
        self.assertEqual(content, client_content)


class MessageInTheMiddleOfBlockTransfer:
    class Test(Block.Test):
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
        req = Lwm2mRead(ResPath.Device.Manufacturer)
        req.fill_placeholders()
        res = Lwm2mErrorResponse.matching(req)(coap.Code.RES_SERVICE_UNAVAILABLE)
        self.test_with_message(req, res)


class NonConfirmableRequestInTheMiddleOfBlockTransfer(MessageInTheMiddleOfBlockTransfer.Test):
    def runTest(self):
        req = Lwm2mEmpty(type=coap.Type.NON_CONFIRMABLE)
        self.test_with_message(req, expected_response=None)
