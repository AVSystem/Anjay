# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import base64
import json

from framework.lwm2m_test import *
from framework.test_utils import *

IID = 1


class JsonRequest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):

        def _verify_values(self, entries, expected_value_map):
            """
            Verifies if the list contains all entries from expected_value_map.
            Ignores timestamps.
            """
            path_value = {}
            basename = ''
            for entry in entries:
                basename = entry.get('bn', basename)
                name = entry.get('n', '')
                for value_type in ('v', 'vs', 'vd', 'vb', 'vlo'):
                    if value_type in entry:
                        path_value[basename + name] = entry[value_type]
                        break

            for path, value in expected_value_map.items():
                self.assertIn(path, path_value)
                self.assertEqual(path_value[path], value)

        def resource_path(self, rid, riid=None):
            if riid is not None:
                return '/%d/%d/%d/%d' % (OID.Test, IID, rid, riid)
            else:
                return '/%d/%d/%d' % (OID.Test, IID, rid)

        def verify_instance(self, expected_path_value_map={}):
            res = self.read_instance(self.serv,
                                     oid=OID.Test,
                                     iid=IID,
                                     accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
            self._verify_values(entries=json.loads(res.content.decode()),
                                expected_value_map=expected_path_value_map)

        def write_resource_payload(self, rid, json_entries, expected_error=None,
                                   additional_payload=b''):
            self.write_resource(self.serv,
                                oid=OID.Test,
                                iid=IID,
                                rid=rid,
                                format=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON,
                                content=json.dumps(json_entries).encode(
                                    'utf-8') + additional_payload,
                                expect_error_code=expected_error)

        def write_instance_payload(self, json_entries, expected_error=None, additional_payload=b''):
            self.write_instance(self.serv,
                                oid=OID.Test,
                                iid=IID,
                                format=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON,
                                content=json.dumps(json_entries).encode(
                                    'utf-8') + additional_payload,
                                expect_error_code=expected_error)

        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=IID)


class JsonResourceWrite(JsonRequest.Test):
    def runTest(self):
        self.write_resource_payload(RID.Test.ResInt,
                                    [{
                                        'v': 123456,
                                        'n': self.resource_path(RID.Test.ResInt)
                                    }])
        self.write_resource_payload(RID.Test.ResDouble,
                                    [{
                                        'v': 123456.0,
                                        'n': self.resource_path(RID.Test.ResDouble)
                                    }])
        self.write_resource_payload(RID.Test.ResBool,
                                    [{
                                        'vb': True,
                                        'n': self.resource_path(RID.Test.ResBool)
                                    }])
        self.write_resource_payload(RID.Test.ResString,
                                    [{
                                        'vs': '12345',
                                        'n': self.resource_path(RID.Test.ResString)
                                    }])
        self.write_resource_payload(RID.Test.ResRawBytes,
                                    [{
                                        'vd': base64.urlsafe_b64encode(
                                            b'\xff\xfe\xfd\xfc\xfb\xfa\xf9').rstrip(b'=').decode(
                                            'utf-8'),
                                        'n': self.resource_path(RID.Test.ResRawBytes)
                                    }])
        self.write_resource_payload(RID.Test.ResObjlnk,
                                    [{
                                        'vlo': '123:456',
                                        'n': self.resource_path(RID.Test.ResObjlnk)
                                    }])
        self.write_resource_payload(RID.Test.IntArray,
                                    [{
                                        'v': 9001,
                                        'n': self.resource_path(RID.Test.IntArray, 1)
                                    }])
        self.verify_instance({
            self.resource_path(RID.Test.ResInt): 123456,
            self.resource_path(RID.Test.ResDouble): 123456.0,
            self.resource_path(RID.Test.ResBool): True,
            self.resource_path(RID.Test.ResString): "12345",
            self.resource_path(RID.Test.ResRawBytes): base64.urlsafe_b64encode(
                b'\xff\xfe\xfd\xfc\xfb\xfa\xf9').rstrip(b'=').decode('utf-8'),
            self.resource_path(RID.Test.ResObjlnk): '123:456',
            self.resource_path(RID.Test.IntArray, 1): 9001,
        })


class JsonInstanceWrite(JsonRequest.Test):
    def runTest(self):
        self.write_instance_payload([{
            'v': 123456,
            'n': self.resource_path(RID.Test.ResInt)
        },
            {
                'v': 123456.0,
                'n': self.resource_path(RID.Test.ResDouble)
            },
            {
                'vb': True,
                'n': self.resource_path(RID.Test.ResBool)
            },
            {
                'vs': '12345',
                'n': self.resource_path(RID.Test.ResString)
            },
            {
                'vd': base64.urlsafe_b64encode(b'\xff\xfe\xfd\xfc\xfb\xfa\xf9').rstrip(b'=').decode(
                    'utf-8'),
                'n': self.resource_path(RID.Test.ResRawBytes)
            },
            {
                'vlo': '123:456',
                'n': self.resource_path(RID.Test.ResObjlnk)
            },
            {
                'v': 9001,
                'n': self.resource_path(RID.Test.IntArray, 1)
            },
            {
                'v': 9002,
                'n': self.resource_path(RID.Test.IntArray, 2)
            }])

        self.verify_instance({
            self.resource_path(RID.Test.ResInt): 123456,
            self.resource_path(RID.Test.ResDouble): 123456.0,
            self.resource_path(RID.Test.ResBool): True,
            self.resource_path(RID.Test.ResString): "12345",
            self.resource_path(RID.Test.ResRawBytes): base64.urlsafe_b64encode(
                b'\xff\xfe\xfd\xfc\xfb\xfa\xf9').rstrip(b'=').decode('utf-8'),
            self.resource_path(RID.Test.ResObjlnk): '123:456',
            self.resource_path(RID.Test.IntArray, 1): 9001,
            self.resource_path(RID.Test.IntArray, 2): 9002,
        })


class JsonResourceWriteWithBasenameAllDivisions(JsonRequest.Test):
    def runTest(self):
        path = self.resource_path(RID.Test.ResInt)
        for i in range(len(path) + 1):
            payload = {'v': 42}
            basename, name = path[:i], path[i:]
            if len(basename):
                payload['bn'] = basename
            if len(name):
                payload['n'] = name

            self.write_resource_payload(RID.Test.ResInt, [payload])
            self.verify_instance({path: 42})


class JsonInstanceWriteBasenameEffectOnNextResources(JsonRequest.Test):
    def runTest(self):
        payload = [
            # First resource without a basename.
            {
                'vs': "1234",
                'n': self.resource_path(RID.Test.ResString),
            },
            # Second resource with basename included.
            {
                'v': 42,
                'n': "%d" % RID.Test.ResInt,
                'bn': "/%d/%d/" % (OID.Test, IID)
            },
            # Third resource shall already be affected by the basename.
            {
                'v': 47.0,
                'n': "%d" % RID.Test.ResDouble,
            },
        ]
        self.write_instance_payload(payload)
        self.verify_instance({
            self.resource_path(RID.Test.ResString): "1234",
            self.resource_path(RID.Test.ResInt): 42,
            self.resource_path(RID.Test.ResDouble): 47.0
        })


class JsonWriteBasenamePointsToOtherObject(JsonRequest.Test):
    def runTest(self):
        payload = [{
            'vs': "1234",
            'n': self.resource_path(RID.Test.ResString),
            'bn': "/2048",
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResString, payload, coap.Code.RES_BAD_REQUEST)


class JsonWriteBasenameTooNested(JsonRequest.Test):
    def runTest(self):
        # Path too nested.
        payload = [{
            'v': 42,
            'n': self.resource_path(RID.Test.ResInt),
            'bn': '/%d/1' % (OID.Test,),
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)


class JsonWriteBasenameItemTooLong(JsonRequest.Test):
    def runTest(self):
        payload = [{
            'v': 42,
            'n': self.resource_path(RID.Test.ResInt),
            'bn': '/133777',
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)


class JsonWriteBasenameUnrelated(JsonRequest.Test):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=IID + 1)
        # Path unrelated - request is made on /33605/IID but basename points to /33605/(IID+1)
        payload = [{
            'v': 42,
            'n': '%d' % RID.Test.ResInt,
            'bn': '/%d/%d/' % (OID.Test, IID + 1),
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)


class JsonWriteWithUnknownDataAtTheEnd(JsonRequest.Test):
    def runTest(self):
        payload = [{
            'v': 42,
            'n': self.resource_path(RID.Test.ResInt),
        }]
        self.write_instance_payload(payload,
                                    expected_error=coap.Code.RES_BAD_REQUEST,
                                    additional_payload=b'stuff')
        self.write_resource_payload(RID.Test.ResInt,
                                    payload,
                                    expected_error=coap.Code.RES_BAD_REQUEST,
                                    additional_payload=b'more stuff')


class JsonAttemptWritingValueToInstance(JsonRequest.Test):
    def runTest(self):
        payload = [{
            'n': '/%d/%d' % (OID.Test, IID),
            'v': 42
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)


class JsonAttemptWritingValueToMultipleResource(JsonRequest.Test):
    def runTest(self):
        payload = [{
            'n': '/%d/%d/%d' % (OID.Test, IID, RID.Test.IntArray),
            'v': 42
        }]
        self.write_instance_payload(payload, coap.Code.RES_BAD_REQUEST)


class JsonAttemptWritingNull(JsonRequest.Test):
    def runTest(self):
        payload = [{
            'n': self.resource_path(RID.Test.ResInt),
            'v': None
        }]
        self.write_resource_payload(RID.Test.ResInt, payload, coap.Code.RES_BAD_REQUEST)
