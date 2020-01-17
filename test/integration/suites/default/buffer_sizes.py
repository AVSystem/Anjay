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

import socket
import unittest

from framework.lwm2m_test import *
from suites.default.retransmissions import RetransmissionTest


class BufferSizeTest:
    class Base(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self, inbuf_size=4096, outbuf_size=4096, endpoint=None, **kwargs):
            if 'extra_cmdline_args' not in kwargs:
                kwargs['extra_cmdline_args'] = []

            kwargs['extra_cmdline_args'] += ['-I', str(inbuf_size), '-O', str(outbuf_size)]
            if endpoint is not None:
                kwargs['extra_cmdline_args'] += ['-e', endpoint]

            super().setUp(**kwargs)


class SmallInputBufferAndLargeOptions(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(inbuf_size=48)

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        pkt = Lwm2mWrite(ResPath.Test[1].ResBytesSize,
                         options=[coap.Option.URI_QUERY('lt=0.' + '0' * 128),
                                  coap.Option.URI_QUERY('gt=9.' + '0' * 128),
                                  coap.Option.URI_QUERY('st=1.' + '0' * 128)],
                         content=b'3')
        self.serv.send(pkt)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_REQUEST_ENTITY_TOO_LARGE),
            self.serv.recv())

        # When options do not dominate message size everything works fine.
        pkt = Lwm2mWrite(ResPath.Test[1].ResBytesSize,
                         options=[coap.Option.URI_QUERY('lt=0.0'),
                                  coap.Option.URI_QUERY('gt=9.0'),
                                  coap.Option.URI_QUERY('st=1.0')],
                         content=b'3')
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), self.serv.recv())


@unittest.skip('TODO: this calculation does not apply to CoAP2')
class OutputBufferTooSmallButDemoDoesntCrash(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(outbuf_size=8, auto_register=False)

    def tearDown(self):
        # If demo crashes valgrind will tell us.
        super().tearDown(auto_deregister=False)

    def runTest(self):
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)


@unittest.skip('TODO: this calculation does not apply to CoAP2')
class OutputBufferTooSmallToHoldEndpoint(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(outbuf_size=128, auto_register=False, endpoint="F" * 125)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)


@unittest.skip('TODO: this calculation does not apply to CoAP2')
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
        super().setUp(endpoint="F" * 128, outbuf_size=169, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=True)

    def runTest(self):
        from . import register as r
        r.BlockRegister().Test()(self.serv)
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


@unittest.skip('TODO: this calculation does not apply to CoAP2')
class OutputBufferAbleToHoldPayloadMarkerBeginsBlockTransfer(BufferSizeTest.Base):
    def setUp(self):
        super().setUp(endpoint="F" * 128, outbuf_size=170, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=True)

    def runTest(self):
        from . import register as r
        r.BlockRegister().Test()(self.serv)
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


@unittest.skip('TODO: this calculation does not apply to CoAP2')
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


