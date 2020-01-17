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

import unittest

from framework.lwm2m.tlv import TLV
from framework.lwm2m_test import *


class BlockResponseTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self, bytes_size=9001, extra_cmdline_args=None):
        super(BlockResponseTest, self).setUp(extra_cmdline_args=extra_cmdline_args)
        self.make_test_instance()
        self.set_bytes_size(0, bytes_size)
        self.set_bytes_burst(0, 1000)

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
        req = Lwm2mCreate("/%d" % OID.Test)
        self.serv.send(req)
        response = self.serv.recv()
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), response);

    def set_bytes_size(self, iid, size):
        req = Lwm2mWrite(ResPath.Test[iid].ResBytesSize, str(size))
        self.serv.send(req)
        response = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), response);

    def set_bytes_burst(self, iid, size):
        req = Lwm2mWrite("/%d/%d" % (OID.Test, iid),
                         TLV.make_resource(RID.Test.ResBytesBurst, int(size)).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.serv.send(req)
        response = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), response);

    def read_bytes(self, iid, seq_num=None, block_size=None, options_modifier=None, accept=None):
        opts = [coap.Option.BLOCK2(seq_num=seq_num, has_more=0, block_size=block_size)] \
            if seq_num is not None and block_size else []
        if options_modifier is not None:
            opts = options_modifier(opts)

        req = Lwm2mRead(ResPath.Test[iid].ResBytes,
                        options=opts, accept=accept)
        self.serv.send(req)
        res = self.serv.recv()
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
        response = self.read_bytes(iid=0)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)
        # Currently we have to read this till the end
        self.read_blocks(iid=0, base_seq=1)

@unittest.skip("TODO: broken due to T2338")
class BlockResponseFirstRequestIsBlock(BlockResponseTest):
    def runTest(self):
        response = self.read_bytes(iid=0, seq_num=1, block_size=1024)
        # Started from seq_num=1, clearly incorrect request
        self.assertEqual(response.code, coap.Code.RES_REQUEST_ENTITY_INCOMPLETE)

        # Normal request
        response = self.read_bytes(iid=0, seq_num=0, block_size=1024)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)
        self.read_blocks(iid=0)


class BlockResponseSizeNegotiation(BlockResponseTest):
    def runTest(self):
        # Forcing block_size from the very first request
        data = self.read_blocks(iid=0, block_size=16, base_seq=0,
                                accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        for i in range(len(data)):
            self.assertEqual(data[i], i % 128)

        # Forcing block_size after first message
        response = self.read_bytes(iid=0,
                                   accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        second_payload = self.read_blocks(iid=0, base_seq=1,
                                          accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(data, response.content + second_payload)

        # Negotiation after first message
        response = self.read_bytes(iid=0, seq_num=0, block_size=32,
                                   accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=32)

        third_payload = self.read_blocks(iid=0, block_size=32, base_seq=1,
                                         accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(data, response.content + third_payload)


class BlockResponseSizeRenegotiation(BlockResponseTest):
    def runTest(self):
        # Case 0: when first request does not contain BLOCK2 option.
        response = self.read_bytes(iid=0, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        response = self.read_bytes(iid=0, seq_num=32, block_size=32)
        self.assertBlockResponse(response, seq_num=32, has_more=1, block_size=32)

        response = self.read_bytes(iid=0, seq_num=66, block_size=16)
        self.assertBlockResponse(response, seq_num=66, has_more=1, block_size=16)

        self.read_blocks(iid=0, base_seq=67, block_size=16)

        # Case 1: when first request does contain BLOCK2 option.
        response = self.read_bytes(iid=0, seq_num=0, block_size=512)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=512)

        response = self.read_bytes(iid=0, seq_num=16, block_size=32)
        self.assertBlockResponse(response, seq_num=16, has_more=1, block_size=32)

        response = self.read_bytes(iid=0, seq_num=34, block_size=16)
        self.assertBlockResponse(response, seq_num=34, has_more=1, block_size=16)

        self.read_blocks(iid=0, base_seq=35, block_size=16)

class BlockResponseInvalidSizeDuringRenegotation(BlockResponseTest):
    def runTest(self):
        # Case 0: when first request does not contain BLOCK2 option.
        response = self.read_bytes(iid=0, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        response = self.read_bytes(iid=0, seq_num=0, block_size=2048)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(response.code, coap.Code.RES_BAD_OPTION)

        # finish the transfer before continuing
        self.read_blocks(iid=0, base_seq=1, block_size=1024)

        # Case 1: when first request does contain BLOCK2 option.
        response = self.read_bytes(iid=0, seq_num=0, block_size=2048)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(response.code, coap.Code.RES_BAD_OPTION)

        self.read_blocks(iid=0, block_size=1024)


class BlockResponseBadBlock2SizeInTheMiddleOfTransfer(BlockResponseTest):
    def runTest(self):
        first_invalid_seq_num = 4
        for seq_num in range(first_invalid_seq_num):
            response = self.read_bytes(iid=0, seq_num=seq_num, block_size=1024)
            self.assertBlockResponse(response, seq_num=seq_num, has_more=1, block_size=1024)

        response = self.read_bytes(iid=0, seq_num=first_invalid_seq_num, block_size=2048)
        self.assertIsInstance(response, Lwm2mErrorResponse)
        self.assertEqual(response.code, coap.Code.RES_BAD_OPTION)

        self.read_blocks(iid=0, base_seq=first_invalid_seq_num, block_size=1024)


class BlockResponseBadBlock1(BlockResponseTest):
    def runTest(self):
        def opts_modifier(opts):
            return opts + [coap.Option.BLOCK1(0, 0, 16)]

        response = self.read_bytes(iid=0, seq_num=0, block_size=512)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=512)
        response = self.read_bytes(iid=0, seq_num=1, block_size=512,
                                   options_modifier=opts_modifier)

        # should continue the transfer
        self.assertEqual(response.code, coap.Code.RES_SERVICE_UNAVAILABLE)

        self.read_blocks(iid=0, base_seq=1, block_size=512)


class BlockResponseBiggerBlockSizeThanData(BlockResponseTest):
    def setUp(self):
        super().setUp(bytes_size=5)

    def runTest(self):
        response = self.read_bytes(iid=0, seq_num=0, block_size=1024)
        self.assertBlockResponse(response, seq_num=0, has_more=0, block_size=1024)


# Tests different rates at which anjay's stream buffers are filled
class BlockResponseDifferentBursts(BlockResponseTest):
    BYTES_AMOUNT = 9001

    def setUp(self):
        super().setUp(bytes_size=BlockResponseDifferentBursts.BYTES_AMOUNT)

    def runTest(self):
        for i in (1, 10, 50, 100, 1000, 1024, 1200, 2048, 4096, 5000, 9001):
            self.set_bytes_burst(0, i)
            self.read_blocks(iid=0)


class BlockResponseToNonBlockRequestRetransmission(BlockResponseTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--cache-size', '2048'])

    def runTest(self):
        # non-BLOCK request
        req = Lwm2mRead(ResPath.Test[0].ResBytes)
        self.serv.send(req)

        # BLOCK response received
        res = self.serv.recv()
        self.assertIdentityMatches(res, req)

        block_opts = res.get_options(coap.Option.BLOCK2)
        self.assertNotEqual([], block_opts)

        # retransmit non-BLOCK request
        self.serv.send(req)

        # should receive the same response as before
        res2 = self.serv.recv()
        self.assertMsgEqual(res, res2)

        # should be able to continue the transfer
        self.read_blocks(iid=0, block_size=block_opts[0].block_size(), base_seq=1)


@unittest.skip("TODO: broken due to T2338")
class BlockResponseOutOfOrder(BlockResponseTest):
    def runTest(self):
        # RFC 7959 does not define the expected behaviour when a CoAP client attempt to retrieve a block that is past
        # the end of the resource. Various CoAP implementations (taken from http://coap.technology/impls.html) behave
        # differently:
        #
        # - Erbium (http://people.inf.ethz.ch/mkovatsc/erbium.php) - 4.02 Bad Option
        # - Lobaro CoAP (http://www.lobaro.com/portfolio/lobaro-coap/) - 4.02 Bad Option
        # - MR-CoAP (https://github.com/MR-CoAP/CoAP) - 4.00 Bad Request
        # - Californium (http://www.eclipse.org/californium/) - success with empty content and Block2.More=false
        #
        # Anjay generates the resource contents on the fly, so it cannot even know the size of the resource in advance
        # and does not accept request for any sequence number other than either the previously retrieved one or the one
        # after it (retransmission or continuation).
        #
        # And it actually sends Reset upon receiving unexpected block number. We are not sure whether it makes any
        # sense, but here's a test for that.

        response = self.read_bytes(iid=0, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        response = self.read_bytes(iid=0, seq_num=42, block_size=1024)
        self.assertIsInstance(response, Lwm2mReset)

        # should be able to continue the transfer
        self.read_blocks(iid=0, block_size=1024, base_seq=1)


class BlockResponseUnrelatedRequestsDoNotAbortTransfer(BlockResponseTest):
    def runTest(self):
        # start BLOCK response transfer
        response = self.read_bytes(iid=0, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        # send unrelated request
        req = Lwm2mRead(ResPath.Device.SerialNumber)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_SERVICE_UNAVAILABLE),
                            self.serv.recv())

        # send another unrelated request
        req = Lwm2mWrite(ResPath.FirmwareUpdate.Package, b'A' * 16,
                         options=[coap.Option.BLOCK1(seq_num=0, block_size=16, has_more=False)],
                         format=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_SERVICE_UNAVAILABLE),
                            self.serv.recv())

        # should be able to continue the transfer
        block_opts = response.get_options(coap.Option.BLOCK2)
        self.read_blocks(iid=0, block_size=block_opts[0].block_size(), base_seq=1)


class BlockResponseUnexpectedServerRequestInTheMiddleOfTransfer(BlockResponseTest):
    def runTest(self):
        response = self.read_bytes(iid=0, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        # send an unrelated request during a block-wise transfer
        req = Lwm2mRead(ResPath.Device.Manufacturer)
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_SERVICE_UNAVAILABLE),
                            res)
        # TODO: T2327
        # self.assertEqual(1, len(res.get_options(coap.Option.MAX_AGE)))

        # continue reading block-wise response
        self.read_blocks(iid=0, base_seq=1, block_size=1024)


class BlockResponseUnexpectedBlockServerRequestInTheMiddleOfTransfer(BlockResponseTest):
    def runTest(self):
        response = self.read_bytes(iid=0, seq_num=None, block_size=None)
        self.assertBlockResponse(response, seq_num=0, has_more=1, block_size=1024)

        # send an unrelated request during a block-wise transfer
        # use BLOCK2 Option to not trigger heuristic based on it
        req = Lwm2mRead(ResPath.Device.Manufacturer,
                        options=[coap.Option.BLOCK2(seq_num=0, has_more=0, block_size=1024)])
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_SERVICE_UNAVAILABLE),
                            res)
        # TODO: T2327
        # self.assertEqual(1, len(res.get_options(coap.Option.MAX_AGE)))

        # continue reading block-wise response
        self.read_blocks(iid=0, base_seq=1, block_size=1024)
