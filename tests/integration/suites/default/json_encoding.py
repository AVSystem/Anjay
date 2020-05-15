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
import base64
from framework.lwm2m_test import *
from framework.test_utils import *

from . import plaintext_base64 as pb64

IID = 1

class JsonEncodingTest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=IID)


def as_json(pkt):
    return json.loads(pkt.content.decode('utf-8'))


class JsonEncodingBnResource(JsonEncodingTest.Test):
    def runTest(self):
        res = as_json(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.Timestamp,
                                         accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))
        self.assertEqual(ResPath.Test[1].Timestamp, res['bn'])


class JsonEncodingBnInstance(JsonEncodingTest.Test):
    def runTest(self):
        res = as_json(self.read_instance(self.serv, oid=OID.Test, iid=IID,
                                         accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))
        self.assertEqual('/%d/1' % OID.Test, res['bn'])


class JsonEncodingBnObject(JsonEncodingTest.Test):
    def runTest(self):
        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))
        self.assertEqual('/%d' % OID.Test, res['bn'])


class JsonEncodingAllNamesAreSlashPrefixed(JsonEncodingTest.Test):
    def runTest(self):
        responses = [
              as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_JSON)),
              as_json(self.read_instance(self.serv, oid=OID.Test, iid=IID,
                                         accept=coap.ContentFormat.APPLICATION_LWM2M_JSON)) ]
        for response in responses:
            self.assertTrue(len(response['e']) > 0)

            for resource in response['e']:
                self.assertEqual('/', resource['n'][0])

        resource = as_json(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.Timestamp,
                                              accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))
        # Resource path is in 'bn', therefore no 'n' parameter is specified by the client
        self.assertFalse('n' in resource['e'])


class JsonEncodingBytesInBase64(JsonEncodingTest.Test):
    def runTest(self):
        some_bytes = pb64.test_object_bytes_generator(51)
        some_bytes_b64 = base64.encodebytes(some_bytes).replace(b'\n', b'')

        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResRawBytes, content=some_bytes_b64)

        result = as_json(self.read_resource(self.serv, oid=OID.Test, iid=IID,
                                            rid=RID.Test.ResRawBytes,
                                            accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))

        self.assertEqual(some_bytes_b64, bytes(result['e'][0]['sv'], encoding='utf-8'))


class JsonEncodingArrayOfOpaqueValues(JsonEncodingTest.Test):
    def runTest(self):
        values = {}
        import random
        for i in range(9):
            values[i] = random.randint(0, 2**31)

        execute_args = ','.join("%d='%d'" % (k, v) for k, v in values.items())
        self.execute_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInitIntArray,
                              content=bytes(execute_args, encoding='utf-8'))

        result = as_json(self.read_resource(self.serv, oid=OID.Test, iid=IID,
                                            rid=RID.Test.ResOpaqueArray,
                                            accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))

        import struct
        for instance in result['e']:
            key = int(instance['n'][1:])
            expected_bytes = struct.pack('!i', values[key])
            expected_value = base64.encodebytes(expected_bytes).replace(b'\n', b'')
            self.assertEquals(expected_value, bytes(instance['sv'], encoding='utf-8'))
