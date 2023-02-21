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

IID = 1


class CborRequest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):

        def resource_path(self, rid, riid=None):
            if riid is not None:
                return '/%d/%d/%d/%d' % (OID.Test, IID, rid, riid)
            else:
                return '/%d/%d/%d' % (OID.Test, IID, rid)

        def verify_instance(self, expected_path_value_map={}):
            res = self.read_instance(self.serv,
                                     oid=OID.Test,
                                     iid=IID,
                                     accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
            CBOR.parse(res.content).verify_values(test=self,
                                                  expected_value_map=expected_path_value_map)

        def write_resource_payload(self, rid, cbor_entries, expected_error=None,
                                   additional_payload=b''):
            self.write_resource(self.serv,
                                oid=OID.Test,
                                iid=IID,
                                rid=rid,
                                format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
                                content=CBOR.serialize(cbor_entries) + additional_payload,
                                expect_error_code=expected_error)

        def cbor_write_resource(self, rid, value, expect_error_code=None):
            self.write_resource(self.serv,
                                oid=OID.Test,
                                iid=IID,
                                rid=rid,
                                format=coap.ContentFormat.APPLICATION_CBOR,
                                content=cbor2.dumps(value),
                                expect_error_code=expect_error_code)

        def write_instance_payload(self, cbor_entries, expected_error=None, additional_payload=b''):
            self.write_instance(self.serv,
                                oid=OID.Test,
                                iid=IID,
                                format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
                                content=CBOR.serialize(cbor_entries) + additional_payload,
                                expect_error_code=expected_error)

        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=IID)


class SenmlCborResourceWrite(CborRequest.Test):
    def runTest(self):
        self.write_resource_payload(RID.Test.ResInt,
                                    [{
                                        SenmlLabel.VALUE: 123456,
                                        SenmlLabel.NAME: self.resource_path(RID.Test.ResInt)
                                    }])
        self.write_resource_payload(RID.Test.ResDouble,
                                    [{
                                        SenmlLabel.VALUE: 123456.0,
                                        SenmlLabel.NAME: self.resource_path(RID.Test.ResDouble)
                                    }])
        self.write_resource_payload(RID.Test.ResBool,
                                    [{
                                        SenmlLabel.BOOL: True,
                                        SenmlLabel.NAME: self.resource_path(RID.Test.ResBool)
                                    }])
        self.write_resource_payload(RID.Test.ResString,
                                    [{
                                        SenmlLabel.STRING: '12345',
                                        SenmlLabel.NAME: self.resource_path(RID.Test.ResString)
                                    }])
        self.write_resource_payload(RID.Test.ResRawBytes,
                                    [{
                                        SenmlLabel.OPAQUE: b'12345',
                                        SenmlLabel.NAME: self.resource_path(RID.Test.ResRawBytes)
                                    }])
        self.write_resource_payload(RID.Test.ResObjlnk,
                                    [{
                                        SenmlLabel.OBJLNK: '123:456',
                                        SenmlLabel.NAME: self.resource_path(RID.Test.ResObjlnk)
                                    }])
        self.write_resource_payload(RID.Test.IntArray,
                                    [{
                                        SenmlLabel.VALUE: 9001,
                                        SenmlLabel.NAME: self.resource_path(RID.Test.IntArray, 1)
                                    }])
        self.verify_instance({
            self.resource_path(RID.Test.ResInt): 123456,
            self.resource_path(RID.Test.ResDouble): 123456.0,
            self.resource_path(RID.Test.ResBool): True,
            self.resource_path(RID.Test.ResString): "12345",
            self.resource_path(RID.Test.ResRawBytes): b'12345',
            self.resource_path(RID.Test.ResObjlnk): '123:456',
            self.resource_path(RID.Test.IntArray, 1): 9001,
        })


class SenmlCborInstanceWrite(CborRequest.Test):
    def runTest(self):
        self.write_instance_payload([{
            SenmlLabel.VALUE: 123456,
            SenmlLabel.NAME: self.resource_path(RID.Test.ResInt)
        },
            {
                SenmlLabel.VALUE: 123456.0,
                SenmlLabel.NAME: self.resource_path(RID.Test.ResDouble)
            },
            {
                SenmlLabel.BOOL: True,
                SenmlLabel.NAME: self.resource_path(RID.Test.ResBool)
            },
            {
                SenmlLabel.STRING: '12345',
                SenmlLabel.NAME: self.resource_path(RID.Test.ResString)
            },
            {
                SenmlLabel.OPAQUE: b'12345',
                SenmlLabel.NAME: self.resource_path(RID.Test.ResRawBytes)
            },
            {
                SenmlLabel.OBJLNK: '123:456',
                SenmlLabel.NAME: self.resource_path(RID.Test.ResObjlnk)
            },
            {
                SenmlLabel.VALUE: 9001,
                SenmlLabel.NAME: self.resource_path(RID.Test.IntArray, 1)
            },
            {
                SenmlLabel.VALUE: 9002,
                SenmlLabel.NAME: self.resource_path(RID.Test.IntArray, 2)
            }])

        self.verify_instance({
            self.resource_path(RID.Test.ResInt): 123456,
            self.resource_path(RID.Test.ResDouble): 123456.0,
            self.resource_path(RID.Test.ResBool): True,
            self.resource_path(RID.Test.ResString): "12345",
            self.resource_path(RID.Test.ResRawBytes): b'12345',
            self.resource_path(RID.Test.ResObjlnk): '123:456',
            self.resource_path(RID.Test.IntArray, 1): 9001,
            self.resource_path(RID.Test.IntArray, 2): 9002,
        })


class SenmlCborResourceWriteWithBasenameAllDivisions(CborRequest.Test):
    def runTest(self):
        path = self.resource_path(RID.Test.ResInt)
        for i in range(len(path) + 1):
            payload = {SenmlLabel.VALUE: 42}
            basename, name = path[:i], path[i:]
            if len(basename):
                payload[SenmlLabel.BASE_NAME] = basename
            if len(name):
                payload[SenmlLabel.NAME] = name

            self.write_resource_payload(RID.Test.ResInt, [payload])
            self.verify_instance({path: 42})


class SenmlCborInstanceWriteBasenameEffectOnNextResources(CborRequest.Test):
    def runTest(self):
        payload = [
            # First resource without a basename.
            {
                SenmlLabel.STRING: "1234",
                SenmlLabel.NAME: self.resource_path(RID.Test.ResString),
            },
            # Second resource with basename included.
            {
                SenmlLabel.VALUE: 42,
                SenmlLabel.NAME: "%d" % RID.Test.ResInt,
                SenmlLabel.BASE_NAME: "/%d/%d/" % (OID.Test, IID)
            },
            # Third resource shall already be affected by the basename.
            {
                SenmlLabel.VALUE: 47.0,
                SenmlLabel.NAME: "%d" % RID.Test.ResDouble,
            },
        ]
        self.write_instance_payload(payload)
        self.verify_instance({
            self.resource_path(RID.Test.ResString): "1234",
            self.resource_path(RID.Test.ResInt): 42,
            self.resource_path(RID.Test.ResDouble): 47.0
        })


class SenmlCborWriteBasenamePointsToOtherObject(CborRequest.Test):
    def runTest(self):
        payload = [{
            SenmlLabel.STRING: "1234",
            SenmlLabel.NAME: self.resource_path(RID.Test.ResString),
            SenmlLabel.BASE_NAME: "/2048",
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResString, payload, coap.Code.RES_BAD_REQUEST)


class SenmlCborWriteBasenameTooNested(CborRequest.Test):
    def runTest(self):
        # Path too nested.
        payload = [{
            SenmlLabel.VALUE: 42,
            SenmlLabel.NAME: self.resource_path(RID.Test.ResInt),
            SenmlLabel.BASE_NAME: '/%d/1' % (OID.Test,),
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)


class SenmlCborWriteBasenameItemTooLong(CborRequest.Test):
    def runTest(self):
        payload = [{
            SenmlLabel.VALUE: 42,
            SenmlLabel.NAME: self.resource_path(RID.Test.ResInt),
            SenmlLabel.BASE_NAME: '/133777',
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)


class SenmlCborWriteBasenameUnrelated(CborRequest.Test):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=IID + 1)
        # Path unrelated - request is made on /33605/IID but basename points to /33605/(IID+1)
        payload = [{
            SenmlLabel.VALUE: 42,
            SenmlLabel.NAME: '%d' % RID.Test.ResInt,
            SenmlLabel.BASE_NAME: '/%d/%d/' % (OID.Test, IID + 1),
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)


class SenmlCborWriteWithUnknownDataAtTheEnd(CborRequest.Test):
    def runTest(self):
        payload = [{
            SenmlLabel.VALUE: 42,
            SenmlLabel.NAME: self.resource_path(RID.Test.ResInt),
        }]
        self.write_instance_payload(payload,
                                    expected_error=coap.Code.RES_BAD_REQUEST,
                                    additional_payload=b'stuff')
        self.write_resource_payload(RID.Test.ResInt,
                                    payload,
                                    expected_error=coap.Code.RES_BAD_REQUEST,
                                    additional_payload=b'more stuff')


class SenmlCborAttemptWritingValueToInstance(CborRequest.Test):
    def runTest(self):
        payload = [{
            SenmlLabel.NAME: '/%d/%d' % (OID.Test, IID),
            SenmlLabel.VALUE: 42
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)


class SenmlCborAttemptWritingValueToMultipleResource(CborRequest.Test):
    def runTest(self):
        payload = [{
            SenmlLabel.NAME: '/%d/%d/%d' % (OID.Test, IID, RID.Test.IntArray),
            SenmlLabel.VALUE: 42
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)


class CborResourceWrite(CborRequest.Test):
    def runTest(self):
        self.cbor_write_resource(RID.Test.ResInt, 123456)
        self.cbor_write_resource(RID.Test.ResDouble, 123456.0)
        self.cbor_write_resource(RID.Test.ResBool, True)
        self.cbor_write_resource(RID.Test.ResString, '12345')
        self.cbor_write_resource(RID.Test.ResRawBytes, b'12345')
        self.cbor_write_resource(RID.Test.ResObjlnk, '123:456')

        self.verify_instance({
            self.resource_path(RID.Test.ResInt): 123456,
            self.resource_path(RID.Test.ResDouble): 123456.0,
            self.resource_path(RID.Test.ResBool): True,
            self.resource_path(RID.Test.ResString): "12345",
            self.resource_path(RID.Test.ResRawBytes): b'12345',
            self.resource_path(RID.Test.ResObjlnk): '123:456'
        })


class CborResourceStringWrite(CborRequest.Test):
    def runTest(self):
        too_long_string = 'xxxxxxxx10xxxxxxxx20xxxxxxxx30xxxxxxxx40xxxxxxxx50xxxxxxxx60' \
                          'xxxxxxxx70xxxxxxxx80xxxxxxxx90xxxxxxx100xxxxxxx110xxxxxxx120' \
                          'xxxx127'
        self.cbor_write_resource(RID.Test.ResString,
                                 too_long_string + 'TRUNCATED_PART_IN_TOO_LONG_STRING',
                                 coap.Code.RES_INTERNAL_SERVER_ERROR)
        self.verify_instance({
            self.resource_path(RID.Test.ResString): too_long_string
        })


class CborTryResourceInstanceWrite(CborRequest.Test):
    def runTest(self):
        self.write_resource_instance(self.serv,
                                     oid=OID.Test,
                                     iid=IID,
                                     rid=RID.Test.IntArray,
                                     riid=1,
                                     format=coap.ContentFormat.APPLICATION_CBOR,
                                     content=cbor2.dumps(1337),
                                     expect_error_code=coap.Code.RES_NOT_FOUND)


class CborResourceInstanceWrite(CborRequest.Test):
    def runTest(self):
        self.write_resource_payload(RID.Test.IntArray,
                                    [{
                                        SenmlLabel.VALUE: 0,
                                        SenmlLabel.NAME: self.resource_path(RID.Test.IntArray, 1)
                                    }])

        self.write_resource_instance(self.serv,
                                     oid=OID.Test,
                                     iid=IID,
                                     rid=RID.Test.IntArray,
                                     riid=1,
                                     format=coap.ContentFormat.APPLICATION_CBOR,
                                     content=cbor2.dumps(1337))

        self.verify_instance({
            self.resource_path(RID.Test.IntArray, 1): 1337
        })


class SenmlCborAttemptWritingNull(CborRequest.Test):
    def runTest(self):
        payload = [{
            SenmlLabel.NAME: self.resource_path(RID.Test.ResInt),
            SenmlLabel.VALUE: None
        }]
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)
