# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *
from framework.test_utils import *
from . import block_write as bw


class Test:
    class ReadComposite(test_suite.Lwm2mSingleServerTest,
                        test_suite.Lwm2mDmOperations):
        def setUp(self, inbuf_size=None, outbuf_size=None, extra_cmdline_args=None, **kwargs):
            extra_args = extra_cmdline_args
            if extra_args is None:
                extra_args = []
            if inbuf_size is not None:
                extra_args += ['-I', str(inbuf_size)]
            if outbuf_size is not None:
                extra_args += ['-O', str(outbuf_size)]

            super().setUp(maximum_version='1.1', extra_cmdline_args=extra_args, **kwargs)


class ReadCompositeSupportedFormats(Test.ReadComposite):
    def runTest(self):
        SUPPORTED_FORMATS = [
            coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
            coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON,
        ]
        for fmt in SUPPORTED_FORMATS:
            self.read_composite(self.serv, [ResPath.Device.Manufacturer], accept=fmt)

        for fmt in coap.ContentFormat.iter():
            if fmt in SUPPORTED_FORMATS:
                continue

            self.read_composite(self.serv, [ResPath.Device.Manufacturer], accept=fmt,
                                expect_error_code=coap.Code.RES_NOT_ACCEPTABLE)


class ReadCompositePartialPresence(Test.ReadComposite):
    def runTest(self):
        # /33605/0 is not present, and will not be returned in the payload
        res = self.read_composite(self.serv,
                                  [ResPath.Test[0].ResBytesBurst,
                                   ResPath.Device.Manufacturer])
        self.assertEqual(
            [{
                SenmlLabel.NAME: ResPath.Device.Manufacturer,
                SenmlLabel.STRING: '0023C7'
            }],
            CBOR.parse(res.content))


# LightweightM2M-1.1-int-281 - Partially Successful Read-Composite Operation
class ReadCompositeExecutableResource(Test.ReadComposite):
    def runTest(self):
        res = self.read_composite(self.serv,
                                  [ResPath.Server[1].Lifetime,
                                   ResPath.Server[1].Binding,
                                   ResPath.Server[1].RegistrationUpdateTrigger])
        self.assertEqual(
            [{
                SenmlLabel.BASE_NAME: '/%d/%d' % (OID.Server, 1),
                SenmlLabel.NAME: '/%d' % (RID.Server.Lifetime,),
                SenmlLabel.VALUE: 86400
            }, {
                SenmlLabel.NAME: '/%d' % (RID.Server.Binding,),
                SenmlLabel.STRING: 'U'
            }],
            CBOR.parse(res.content))


BIG_LIST_OF_REQUESTED_PATHS = [
    ResPath.Test[0].Timestamp,
    ResPath.Test[0].ResInt,
    ResPath.Test[0].ResBool,
    ResPath.Test[0].ResFloat,
    ResPath.Test[0].ResString,
    ResPath.Test[0].ResDouble
]


class ReadCompositeBlockRequest(Test.ReadComposite):
    INBUF_SIZE = 64

    def setUp(self):
        super().setUp(inbuf_size=self.__class__.INBUF_SIZE)

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        encoded_paths = CBOR.serialize([
            {SenmlLabel.NAME: str(p)} for p in BIG_LIST_OF_REQUESTED_PATHS
        ])
        self.assertGreater(len(encoded_paths), self.__class__.INBUF_SIZE)

        pkts = list(bw.packets_from_chunks(chunks=list(bw.equal_chunk_splitter(16)(encoded_paths)),
                                           format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
                                           code=coap.Code.REQ_FETCH,
                                           path=None))

        for pkt in pkts[:-1]:
            self.serv.send(pkt)
            self.assertMsgEqual(Lwm2mContinue.matching(pkt)(), self.serv.recv())

        self.serv.send(pkts[-1])
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(pkts[-1])(), res)


class ReadCompositeBlockResponse(Test.ReadComposite):
    # enough to hold Register, but not Read-Composite response
    OUTPUT_SIZE = 100

    def setUp(self, **kwargs):
        super().setUp(outbuf_size=self.__class__.OUTPUT_SIZE, auto_register=False, **kwargs)

        from . import register as r
        r.BlockRegister().Test()(self.serv, version='1.1')

        self.create_instance(self.serv, oid=OID.Test, iid=0)

    def runTest(self):
        req = Lwm2mReadComposite(paths=BIG_LIST_OF_REQUESTED_PATHS)
        self.serv.send(req)
        # Read to the end using blockwise transfer.
        while True:
            pkt = self.serv.recv()
            block2 = pkt.get_options(coap.Option.BLOCK2)[0]
            if not block2.has_more():
                break

            block2 = coap.Option.BLOCK2(seq_num=block2.seq_num() + 1,
                                        has_more=False,
                                        block_size=block2.block_size())
            self.serv.send(Lwm2mReadComposite(token=req.token, paths=[], options=[block2]))


class ReadCompositeBlockResponseRetransmissions(ReadCompositeBlockResponse):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--cache-size', '4096'])

    def runTest(self):
        req = Lwm2mReadComposite(paths=BIG_LIST_OF_REQUESTED_PATHS)

        self.serv.send(req)
        resp1 = self.serv.recv()

        self.serv.send(req)
        resp2 = self.serv.recv()

        self.assertMsgEqual(resp1, resp2)

        # Read the rest of the response
        pkt = resp1
        while True:
            block2 = pkt.get_options(coap.Option.BLOCK2)[0]
            if not block2.has_more():
                break

            block2 = coap.Option.BLOCK2(seq_num=block2.seq_num() + 1,
                                        has_more=False,
                                        block_size=block2.block_size())
            self.serv.send(Lwm2mReadComposite(token=req.token, paths=[], options=[block2]))
            pkt = self.serv.recv()


class ReadCompositeRootPath(Test.ReadComposite):
    def runTest(self):
        req = Lwm2mReadComposite(paths=['/'])
        self.serv.send(req)

        # Read to the end using blockwise transfer.
        while True:
            pkt = self.serv.recv()
            block2 = pkt.get_options(coap.Option.BLOCK2)[0]
            if not block2.has_more():
                break

            block2 = coap.Option.BLOCK2(seq_num=block2.seq_num() + 1,
                                        has_more=False,
                                        block_size=block2.block_size())
            self.serv.send(Lwm2mReadComposite(token=req.token, paths=[], options=[block2]))
