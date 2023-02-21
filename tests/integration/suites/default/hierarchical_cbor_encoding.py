# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import cbor2
from framework.lwm2m_test import *
from framework.test_utils import *

IID = 1

SENML_CBOR_BASE_NAME_LABEL = -2
SENML_CBOR_NAME_LABEL = 0
SENML_CBOR_VALUE_LABEL = 2
SENML_CBOR_STRING_VALUE_LABEL = 3
SENML_CBOR_BOOLEAN_VALUE_LABEL = 4
SENML_CBOR_DATA_VALUE_LABEL = 8
SENML_CBOR_OBJECT_LINK_LABEL = "vlo"


class HierarchicalCborEncodingTest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=IID)


def as_cbor(pkt):
    return cbor2.loads(pkt.content)


def get_element(obj, name, label):
    base_name = ''
    for e in obj:
        base_name = e.get(SENML_CBOR_BASE_NAME_LABEL, base_name)
        sub_name = e.get(SENML_CBOR_NAME_LABEL, '')

        if base_name + sub_name == name:
            read_value = e[label]
            return read_value


def path_to_string(path):
    if isinstance(path, tuple) or isinstance(path, list):
        result = ""
        for p in path:
            result += "/" + str(p)
        return result
    else:
        return "/" + str(path)


# flatten_dict adapted from
# https://www.geeksforgeeks.org/python-convert-nested-dictionary-into-flattened-dictionary/


def flatten_dict(dd, prefix=''):
    return {prefix + k: v
            for kk, vv in dd.items()
            for k, v in flatten_dict(vv, path_to_string(kk)).items()
            } if isinstance(dd, dict) else {prefix: dd}


class SenmlCborEncoding:
    format = coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR

    def value_extractor(self, res, element_name, label):
        return get_element(res, element_name, label)


class BaseReadInteger:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1234
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInt,
                                content=str(assigned_value))

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResInt

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadUnsignedInteger:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 5678
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResUnsignedInt,
                                content=str(assigned_value))

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResUnsignedInt

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadFloat:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1.5
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                                content=str(assigned_value))

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResFloat

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadDouble:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1.1
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResDouble,
                                content=str(assigned_value))

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResDouble

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadBool:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = True
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResBool,
                                content=str(int(assigned_value)))

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResBool

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_BOOLEAN_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadObjectLink:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = '33605:1'
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResObjlnk,
                                content=assigned_value)

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResObjlnk

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_OBJECT_LINK_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadOpaque:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = b'losie, jelenie, sarny, dziki, lisy, borsuki, kuny, jenoty, wilki i rysie'
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResRawBytes,
                                content=assigned_value,
                                format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResRawBytes

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_DATA_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadString:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 'z ptakow: kuropatwy, bazanty, dzikie kaczki, gesi, lyski, bekasy i cietrzewie'
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResString,
                                content=assigned_value)

            res = as_cbor(
                self.read_object(
                    self.serv,
                    oid=OID.Test,
                    accept=self.format))
            element_name = ResPath.Test[IID].ResString

            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_STRING_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadSingleInstance:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1234
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInt,
                                content=str(assigned_value))

            res = as_cbor(self.read_instance(self.serv, oid=OID.Test, iid=IID,
                                             accept=self.format))
            element_name = ResPath.Test[IID].ResInt
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)

            element_name = ResPath.Test[IID].ResBool
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_BOOLEAN_VALUE_LABEL)
            self.assertEqual(False, read_value)


class BaseReadSingleResource:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1.5
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                                content=str(assigned_value))

            res = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                                             accept=self.format))
            element_name = ResPath.Test[IID].ResFloat
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)


class BaseReadMultipleResource:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            values = {}
            import random
            for i in range(9):
                values[i] = random.randint(0, 2**31)

            execute_args = ','.join("%d='%d'" % (k, v)
                                    for k, v in values.items())
            self.execute_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInitIntArray,
                                  content=bytes(execute_args, encoding='utf-8'))

            res = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.IntArray,
                                             accept=self.format))
            element_name = '/%d/%d/%d/%d' % (OID.Test,
                                             IID, RID.Test.IntArray, 0)
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(values[0], read_value)

            element_name = '/%d/%d/%d/%d' % (OID.Test,
                                             IID, RID.Test.IntArray, 8)
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(values[8], read_value)


class BaseReadComposite:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1.5
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                                content=str(assigned_value))

            res = as_cbor(
                self.read_composite(
                    self.serv, [
                        ResPath.Device.Manufacturer, ResPath.Test[IID].ResFloat], accept=self.format))

            element_name = ResPath.Test[IID].ResFloat
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)

            element_name = ResPath.Device.Manufacturer
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual('0023C7', read_value)


class BaseReadCompositeSameResources:
    class TestMixin(HierarchicalCborEncodingTest.Test):
        def runTest(self):
            assigned_value = 1.5
            self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                                content=str(assigned_value))

            res = as_cbor(
                self.read_composite(
                    self.serv, [
                        ResPath.Test[IID].ResFloat, ResPath.Test[IID].ResFloat], accept=self.format))

            element_name = ResPath.Test[IID].ResFloat
            read_value = self.value_extractor(
                res, element_name, SENML_CBOR_VALUE_LABEL)
            self.assertEqual(assigned_value, read_value)

# SenML CBOR


class SenmlCborEncodingReadInteger(
        BaseReadInteger.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadUnsignedInteger(
        BaseReadUnsignedInteger.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadFloat(BaseReadFloat.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadDouble(BaseReadDouble.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadBool(BaseReadBool.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadObjectLink(
        BaseReadObjectLink.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadOpaque(BaseReadOpaque.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadString(BaseReadString.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadSingleInstance(
        BaseReadSingleInstance.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadSingleResource(
        BaseReadSingleResource.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadMultipleResource(
        BaseReadMultipleResource.TestMixin, SenmlCborEncoding):
    pass


class SenmlCborEncodingReadCompositeSameResources(
        BaseReadCompositeSameResources.TestMixin, SenmlCborEncoding):
    pass

