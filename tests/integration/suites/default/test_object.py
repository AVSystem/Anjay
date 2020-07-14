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
import json

from framework.lwm2m.tlv import TLVType
from framework.lwm2m_test import *


class TestObject:
    class TestCase(test_suite.Lwm2mSingleServerTest):
        def setUp(self):
            super().setUp()

            self.serv.set_timeout(timeout_s=1)

            req = Lwm2mCreate('/%d' % (OID.Test,))
            self.serv.send(req)
            self.assertMsgEqual(Lwm2mCreated.matching(req)(),
                                self.serv.recv())


class TimestampTest(TestObject.TestCase):
    def runTest(self):
        req = Lwm2mRead(ResPath.Test[0].Counter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())


class CounterTest(TestObject.TestCase):
    def runTest(self):
        # ensure the counter is zero initially
        req = Lwm2mRead(ResPath.Test[0].Counter, accept=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'0'),
                            self.serv.recv())

        # execute Increment Counter
        req = Lwm2mExecute(ResPath.Test[0].IncrementCounter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # counter should be incremented by the execute
        req = Lwm2mRead(ResPath.Test[0].Counter, accept=coap.ContentFormat.TEXT_PLAIN)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'1'),
                            self.serv.recv())


class IntegerArrayTest(TestObject.TestCase):
    def runTest(self):
        # ensure the array is empty
        req = Lwm2mRead(ResPath.Test[0].IntArray)
        self.serv.send(req)

        empty_array_tlv = TLV.make_multires(resource_id=RID.Test.IntArray, instances=[])
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=empty_array_tlv.serialize()),
                            self.serv.recv())

        # write something
        array_tlv = TLV.make_multires(
            resource_id=RID.Test.IntArray,
            instances=[
                # (1, (0).to_bytes(0, byteorder='big')),
                (2, (12).to_bytes(1, byteorder='big')),
                (4, (1234).to_bytes(2, byteorder='big')),
            ])
        req = Lwm2mWrite(ResPath.Test[0].IntArray,
                         array_tlv.serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # check updated content
        req = Lwm2mRead(ResPath.Test[0].IntArray)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mContent.matching(req)(content=array_tlv.serialize(),
                                                       format=coap.ContentFormat.APPLICATION_LWM2M_TLV),
                            self.serv.recv())


class AttemptToWriteSingleAsMultipleTest(TestObject.TestCase):
    def runTest(self):
        req = Lwm2mWrite(ResPath.Test[0].ResInt,
                         TLV.make_instance(0, [TLV.make_multires(12, [(0, 42)])]).serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(coap.Code.RES_BAD_REQUEST),
                            self.serv.recv())


class ExecArgsArrayTest(TestObject.TestCase):
    def runTest(self):
        args = [
            (7, None),
            (1, b''),
            (2, b'1'),
            (3, b'12345'),
            (9, b'0' * 512) # keep this value small, to avoid triggering blockwise transfers
        ]

        req = Lwm2mRead(ResPath.Test[0].LastExecArgsArray)
        self.serv.send(req)

        empty_array_tlv = TLV.make_multires(resource_id=RID.Test.LastExecArgsArray, instances=[])
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=empty_array_tlv.serialize()),
                            self.serv.recv())

        # perform the Execute with arguments
        exec_content = b','.join(b'%d' % k if v is None else b"%d='%s'" % (k, v)
                                 for k, v in args)
        req = Lwm2mExecute(ResPath.Test[0].IncrementCounter, content=exec_content)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Execute args should now be saved in the Execute Arguments array
        req = Lwm2mRead(ResPath.Test[0].LastExecArgsArray)
        self.serv.send(req)

        exec_args_tlv = TLV.make_multires(resource_id=RID.Test.LastExecArgsArray,
                                          instances=sorted(dict((k, v or b'') for k, v in args).items()))
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=exec_args_tlv.serialize()),
                            self.serv.recv())


class ExecArgsDuplicateTest(TestObject.TestCase):
    def runTest(self):
        args = [
            (1, None),
            (1, b''),
            (2, b'1'),
            (3, b'12345'),
            (9, b'0' * 1024)
        ]

        # perform the Execute with arguments
        exec_content = b','.join(b'%d' % k if v is None else b"%d='%s'" % (k, v)
                                 for k, v in args)
        req = Lwm2mExecute(ResPath.Test[0].IncrementCounter, content=exec_content)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # Execute Arguments array will now be unreadable due to duplicate Resource Instance IDs
        req = Lwm2mRead(ResPath.Test[0].LastExecArgsArray)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_INTERNAL_SERVER_ERROR),
                            self.serv.recv())


class EmptyBytesTest(TestObject.TestCase, test_suite.Lwm2mDmOperations):
    def runTest(self):
        response = self.read_path(self.serv, ResPath.Test[0].ResBytes)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.content, b'')

        response = self.read_path(self.serv, ResPath.Test[0].ResBytes, accept=coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.TEXT_PLAIN)
        self.assertEqual(response.content, b'')

        response = self.read_path(self.serv, ResPath.Test[0].ResBytes, accept=coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_OCTET_STREAM)
        self.assertEqual(response.content, b'')

        response = self.read_instance(self.serv, OID.Test, 0, accept=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_LWM2M_TLV)
        tlv = TLV.parse(response.content)
        self.assertEqual(tlv[0].tlv_type, TLVType.RESOURCE)
        self.assertEqual(tlv[0].identifier, RID.Test.Timestamp)
        expected_rest = [
            TLV.make_resource(RID.Test.Counter, 0),
            TLV.make_multires(RID.Test.IntArray, {}),
            TLV.make_multires(RID.Test.LastExecArgsArray, {}),
            TLV.make_resource(RID.Test.ResBytes, b''),
            TLV.make_resource(RID.Test.ResBytesSize, 0),
            TLV.make_resource(RID.Test.ResBytesBurst, 1000),
            TLV.make_resource(RID.Test.ResRawBytes, b''),
            TLV.make_multires(RID.Test.ResOpaqueArray, {}),
            TLV.make_resource(RID.Test.ResInt, 0),
            TLV.make_resource(RID.Test.ResBool, 0),
            TLV.make_resource(RID.Test.ResFloat, 0.0),
            TLV.make_resource(RID.Test.ResString, ''),
            TLV.make_resource(RID.Test.ResObjlnk, b'\0\0\0\0'),
            TLV.make_resource(RID.Test.ResBytesZeroBegin, 1),
            TLV.make_resource(RID.Test.ResDouble, 0.0),
        ]
        self.assertEqual(len(tlv), len(expected_rest) + 1)
        for i in range(len(expected_rest)):
            self.assertEqual(tlv[i + 1], expected_rest[i])

        response = self.read_instance(self.serv, OID.Test, 0, accept=coap.ContentFormat.APPLICATION_LWM2M_JSON)
        self.assertEqual(response.get_content_format(), coap.ContentFormat.APPLICATION_LWM2M_JSON)
        js = json.loads(response.content.decode('utf-8'))
        self.assertEqual(len(js), 2)
        self.assertEqual(js['bn'], '/%d/0' % (OID.Test,))
        e = js['e']
        self.assertEqual(len(e[0]), 2)
        self.assertEqual(e[0]['n'], '/%d' % (RID.Test.Timestamp,))
        self.assertIsInstance(e[0]['v'], int)
        expected_rest = [
            {'n': '/%d' % (RID.Test.Counter,), 'v': 0},
            {'n': '/%d' % (RID.Test.ResBytes,), 'sv': ''},
            {'n': '/%d' % (RID.Test.ResBytesSize,), 'v': 0},
            {'n': '/%d' % (RID.Test.ResBytesBurst,), 'v': 1000},
            {'n': '/%d' % (RID.Test.ResRawBytes,), 'sv': ''},
            {'n': '/%d' % (RID.Test.ResInt,), 'v': 0},
            {'n': '/%d' % (RID.Test.ResBool,), 'bv': False},
            {'n': '/%d' % (RID.Test.ResFloat,), 'v': 0.0},
            {'n': '/%d' % (RID.Test.ResString,), 'sv': ''},
            {'n': '/%s' % (RID.Test.ResObjlnk,), 'ov': '0:0'},
            {'n': '/%s' % (RID.Test.ResBytesZeroBegin,), 'bv': True},
            {'n': '/%s' % (RID.Test.ResDouble,), 'v': 0.0},
        ]
        self.assertEqual(len(e), len(expected_rest) + 1)
        for i in range(len(expected_rest)):
            self.assertEqual(e[i + 1], expected_rest[i])


class UpdateResourceInstanceTest(TestObject.TestCase):
    def runTest(self):
        # ensure the array is empty
        req = Lwm2mRead(ResPath.Test[0].IntArray)
        self.serv.send(req)

        empty_array_tlv = TLV.make_multires(resource_id=RID.Test.IntArray, instances=[])
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=empty_array_tlv.serialize()),
                            self.serv.recv())

        # write something
        array_tlv = TLV.make_multires(
            resource_id=RID.Test.IntArray,
            instances=[
                (2, (12).to_bytes(1, byteorder='big')),
                (4, (97).to_bytes(1, byteorder='big')),
            ])
        req = Lwm2mWrite(ResPath.Test[0].IntArray,
                         array_tlv.serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())

        # check updated content
        req = Lwm2mRead(ResPath.Test[0].IntArray)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mContent.matching(req)(content=array_tlv.serialize(),
                                                       format=coap.ContentFormat.APPLICATION_LWM2M_TLV),
                            self.serv.recv())

        # update one of resource instances
        to_update_array_tlv = TLV.make_multires(
            resource_id=RID.Test.IntArray,
            instances=[
                (4, (65).to_bytes(1, byteorder='big'))
            ])
        req = Lwm2mWrite('/%d/0' % (OID.Test,),
                         to_update_array_tlv.serialize(),
                         format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                         update=True)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.serv.recv())
        
        # check updated content
        updated_array_tlv = TLV.make_multires(
            resource_id=RID.Test.IntArray,
            instances=[
                (2, (12).to_bytes(1, byteorder='big')),
                (4, (65).to_bytes(1, byteorder='big')),
            ])

        req = Lwm2mRead(ResPath.Test[0].IntArray)
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mContent.matching(req)(content=updated_array_tlv.serialize(),
                                                       format=coap.ContentFormat.APPLICATION_LWM2M_TLV),
                            self.serv.recv())
