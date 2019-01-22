# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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
import json

from framework.lwm2m.tlv import TLVType
from framework.lwm2m_test import *


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


class EmptyBytesTest(TestObject.TestCase, test_suite.Lwm2mDmOperations):
    def runTest(self):
        # By default, /1337/*/5 does NOT call anjay_ret_bytes_begin() if /1337/*/6 is zero. This results in
        # 5.00 Internal Server Error being returned, and this is actually expected behaviour - we test that it does not
        # segfault (as it used to) in a test called ObserveResourceWithEmptyHandler.
        #
        # There is now an additional resource (/1337/*/17) that makes /1337/*/5 always call anjay_ret_bytes_begin()
        # if set to true.
        self.write_resource(self.serv, OID.Test, 1, RID.Test.ResBytesZeroBegin, '1')

        response = self.read_path(self.serv, ResPath.Test[1].ResBytes)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(response.content, b'')

        response = self.read_path(self.serv, ResPath.Test[1].ResBytes, accept=coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.content, b'')

        response = self.read_path(self.serv, ResPath.Test[1].ResBytes, accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(response.content, b'')

        response = self.read_instance(self.serv, OID.Test, 1, accept=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_LWM2M_TLV)
        tlv = TLV.parse(response.content)
        self.assertGreaterEqual(len(tlv), 15)
        self.assertEqual(tlv[0].tlv_type, TLVType.RESOURCE)
        self.assertEqual(tlv[0].identifier, 0)
        self.assertEqual(tlv[1], TLV.make_resource(1, 0))
        self.assertEqual(tlv[2], TLV.make_multires(3, {}))
        self.assertEqual(tlv[3], TLV.make_multires(4, {}))
        self.assertEqual(tlv[4], TLV.make_resource(5, b''))
        self.assertEqual(tlv[5], TLV.make_resource(6, 0))
        self.assertEqual(tlv[6], TLV.make_resource(7, 1000))
        self.assertEqual(tlv[7], TLV.make_resource(10, b''))
        self.assertEqual(tlv[8], TLV.make_multires(11, {}))
        self.assertEqual(tlv[9], TLV.make_resource(12, 0))
        self.assertEqual(tlv[10], TLV.make_resource(13, 0))
        self.assertEqual(tlv[11], TLV.make_resource(14, 0.0))
        self.assertEqual(tlv[12], TLV.make_resource(15, ''))
        self.assertEqual(tlv[13], TLV.make_resource(16, b'\0\0\0\0'))
        self.assertEqual(tlv[14], TLV.make_resource(17, 1))

        response = self.read_instance(self.serv, OID.Test, 1, accept=coap.ContentFormat.APPLICATION_LWM2M_JSON)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_LWM2M_JSON)
        js = json.loads(response.content.decode('utf-8'))
        self.assertEqual(len(js), 2)
        self.assertEqual(js['bn'], '/1337/1')
        e = js['e']
        self.assertGreaterEqual(len(e), 12)
        self.assertEqual(len(e[0]), 2)
        self.assertEqual(e[0]['n'], '/0')
        self.assertIsInstance(e[0]['v'], int)
        self.assertEqual(e[1], {'n': '/1', 'v': 0})
        self.assertEqual(e[2], {'n': '/5', 'sv': ''})
        self.assertEqual(e[3], {'n': '/6', 'v': 0})
        self.assertEqual(e[4], {'n': '/7', 'v': 1000})
        self.assertEqual(e[5], {'n': '/10', 'sv': ''})
        self.assertEqual(e[6], {'n': '/12', 'v': 0})
        self.assertEqual(e[7], {'n': '/13', 'bv': False})
        self.assertEqual(e[8], {'n': '/14', 'v': 0.0})
        self.assertEqual(e[9], {'n': '/15', 'sv': ''})
        self.assertEqual(e[10], {'n': '/16', 'ov': '0:0'})
        self.assertEqual(e[11], {'n': '/17', 'bv': True})
