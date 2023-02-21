# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
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


def get_element(res, name):
    base_name = ''
    for e in res:
        base_name = e.get('bn', base_name)
        element_name = e.get('n', '')
        if base_name + element_name == name:
            return e


class JsonEncodingSenmlReadInteger(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = 1234
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInt,
                            content=str(assigned_value))

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResInt
        read_value = get_element(res, element_name)['v']
        self.assertEqual(assigned_value, read_value)


class JsonEncodingSenmlReadUnsignedInteger(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = 5678
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResUnsignedInt,
                            content=str(assigned_value))

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResUnsignedInt
        read_value = get_element(res, element_name)['v']
        self.assertEqual(assigned_value, read_value)


class JsonEncodingSenmlReadFloat(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = 12.34
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                            content=str(assigned_value))

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResFloat
        read_value = get_element(res, element_name)['v']
        self.assertAlmostEqual(assigned_value, read_value, places=6)


class JsonEncodingSenmlReadBool(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = True
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResBool,
                            content=str(int(assigned_value)))

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResBool
        read_value = get_element(res, element_name)['vb']
        self.assertEqual(assigned_value, read_value)


class JsonEncodingSenmlReadObjectLink(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = '33605:1'
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResObjlnk,
                            content=assigned_value)

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResObjlnk
        read_value = get_element(res, element_name)['vlo']
        self.assertEqual(assigned_value, read_value)


class JsonEncodingSenmlReadOpaque(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = b'blokuje cie dzbanie'
        expected_value = base64.urlsafe_b64encode(assigned_value).rstrip(b'=').decode('UTF-8')
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResRawBytes,
                            content=assigned_value,
                            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResRawBytes
        read_value = get_element(res, element_name)['vd']
        self.assertEqual(expected_value, read_value)


class JsonEncodingSenmlReadString(JsonEncodingTest.Test):
    def runTest(self):
        assigned_value = 'kruzi'
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResString,
                            content=assigned_value)

        res = as_json(self.read_object(self.serv, oid=OID.Test,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        element_name = ResPath.Test[IID].ResString
        read_value = get_element(res, element_name)['vs']
        self.assertEqual(assigned_value, read_value)
