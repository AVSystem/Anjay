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

class CborEncodingTest:
    class Test(test_suite.Lwm2mSingleServerTest,
               test_suite.Lwm2mDmOperations):
        def resource_path(self, rid, riid=None):
            if riid is not None:
                return '/%d/%d/%d/%d' % (OID.Test, IID, rid, riid)
            else:
                return '/%d/%d/%d' % (OID.Test, IID, rid)

        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Test, iid=IID)


def as_cbor(pkt):
    return cbor2.loads(pkt.content)


class CborEncodingReadInteger(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = 1234
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInt,
                            content=str(assigned_value))

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResInt,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadUnsignedInteger(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = 5678
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResUnsignedInt,
                            content=str(assigned_value))

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResUnsignedInt,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadFloat(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = 1.5
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                            content=str(assigned_value))

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResFloat,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadDouble(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = 1.1
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResDouble,
                            content=str(assigned_value))

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResDouble,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadBool(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = True
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResBool,
                            content=str(int(assigned_value)))

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResBool,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadObjectLink(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = '33605:1'
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResObjlnk,
                            content=assigned_value)

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResObjlnk,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadOpaque(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = b'losie, jelenie, sarny, dziki, lisy, borsuki, kuny, jenoty, wilki i rysie'
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResRawBytes,
                            content=assigned_value,
                            format=coap.ContentFormat.APPLICATION_OCTET_STREAM)

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResRawBytes,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)


class CborEncodingReadString(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = 'z ptakow: kuropatwy, bazanty, dzikie kaczki, gesi, lyski, bekasy i cietrzewie'
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResString,
                            content=assigned_value)

        read_value = as_cbor(self.read_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.ResString,
                                                accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)

class CborEncodingReadResourceInstance(CborEncodingTest.Test):
    def runTest(self):
        assigned_value = 1337
        self.write_resource(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.IntArray,
                            format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR,
                            content=CBOR.serialize(
                                [{
                                    SenmlLabel.VALUE: assigned_value,
                                    SenmlLabel.NAME: self.resource_path(RID.Test.IntArray, 1)
                                }]))

        read_value = as_cbor(self.read_resource_instance(self.serv, oid=OID.Test, iid=IID, rid=RID.Test.IntArray, riid=1,
                                                         accept=coap.ContentFormat.APPLICATION_CBOR))

        self.assertEqual(assigned_value, read_value)
