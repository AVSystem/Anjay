# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import socket
import unittest

from framework.lwm2m_test import *
from suites.default.retransmissions import RetransmissionTest


class BufferSizeTest:
    class Base(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, inbuf_size=4096, outbuf_size=4096, **kwargs):
            if 'extra_cmdline_args' not in kwargs:
                kwargs['extra_cmdline_args'] = []

            kwargs['extra_cmdline_args'] += ['-I', str(inbuf_size), '-O', str(outbuf_size)]

            super().setUp(**kwargs)


class SmallInputBufferAndLargeOptions(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(inbuf_size=48)

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # these will be technically interpreted as Write-Attributes because of no Content-Format
        pkt = Lwm2mWrite(ResPath.Test[1].ResBytesSize,
                         options=[coap.Option.URI_QUERY('lt=0.' + '0' * 128),
                                  coap.Option.URI_QUERY('gt=9.' + '0' * 128),
                                  coap.Option.URI_QUERY('st=1.' + '0' * 128)],
                         content=b'3',
                         format=None)
        self.serv.send(pkt)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_REQUEST_ENTITY_TOO_LARGE),
            self.serv.recv())

        # When options do not dominate message size everything works fine.
        pkt = Lwm2mWrite(ResPath.Test[1].ResBytesSize,
                         options=[coap.Option.URI_QUERY('lt=0.0'),
                                  coap.Option.URI_QUERY('gt=9.0'),
                                  coap.Option.URI_QUERY('st=1.0')],
                         content=b'3',
                         format=None)
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), self.serv.recv())


class OutputBufferTooSmallButDemoDoesntCrash(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(outbuf_size=8, auto_register=False)

    def tearDown(self):
        # If demo crashes valgrind will tell us.
        super().tearDown(auto_deregister=False)

    def runTest(self):
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)


class OutputBufferSizeIsEnoughToHandleBlockRegister(BufferSizeTest.Base):
    def setUp(self):
        # +------+---------------------------------------------+
        # | Size |                For what?                    |
        # +------+---------------------------------------------+
        # | 1B   |  version+type+token length                  |
        # +------+---------------------------------------------+
        # | 1B   |  message code                               |
        # +------+---------------------------------------------+
        # | 2B   |  message id                                 |
        # +------+---------------------------------------------+
        # | 8B   |  token                                      |
        # +------+---------------------------------------------+
        # | 3B   |  option URI_PATH == 'rd'                    |
        # +------+---------------------------------------------+
        # | 2B   |  option CONTENT_FORMAT == APPLICATION_LINK  |
        # +------+---------------------------------------------+
        # | 133B |  option URI_QUERY == 'ep=FFFF...F'          |
        # +------+---------------------------------------------+
        # | 9B   |  option URI_QUERY == 'lt=86400'             |
        # +------+---------------------------------------------+
        # | 10B  |  option URI_QUERY == 'lwm2m=1.0'            |
        # +------+---------------------------------------------+
        # | 2B   |  option BLOCK1                              |
        # +------+---------------------------------------------+
        # | 1B   |  payload marker                             |
        # +------+---------------------------------------------+
        # | 16B  |  payload                                    |
        # +------+---------------------------------------------+
        #
        # In total: 1 + 1 + 2 + 8 + 3 + 2 + 133 + 9 + 10 + 2 + 1 + 16 = 188
        #
        # However, since we use maximum BLOCK option size for calculations, the
        # actual size of buffer should be 191.
        super().setUp(endpoint_name="F" * 128, outbuf_size=191, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=True)

    def runTest(self):
        from . import register as r
        r.BlockRegister().Test()(self.serv)
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


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


class InputBufferSizeTooSmallToHoldRegisterResponse(RetransmissionTest.TestMixin,
                                                    BufferSizeTest.Base):
    def setUp(self):
        # see calculation in ConfiguredInputBufferSizeDeterminesMaxIncomingPacketSize
        # the buffer is 1B too short to hold Register response
        super().setUp(inbuf_size=14, auto_register=False, bootstrap_server=True)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        expected_req = Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=%d' % (DEMO_ENDPOINT_NAME, 86400))
        # client should not be able to read the whole packet, ignoring it and causing a backoff
        for _ in range(self.MAX_RETRANSMIT + 1):
            req = self.serv.recv(timeout_s=self.last_retransmission_timeout())
            self.assertMsgEqual(expected_req, req)
            self.serv.send(Lwm2mCreated.matching(req)(location='/rd'))

        # and after failing completely, client falls back to Client-Initiated Bootstrap
        req = self.bootstrap_server.recv(timeout_s=self.last_retransmission_timeout() + 5)
        self.assertIsInstance(req, Lwm2mRequestBootstrap)
        self.bootstrap_server.send(Lwm2mChanged.matching(req)())
