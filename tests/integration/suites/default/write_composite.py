# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import json

from framework.lwm2m_test import *
from framework.test_utils import *

IID = 1


class Test:
    class WriteComposite(test_suite.Lwm2mSingleServerTest,
                         test_suite.Lwm2mDmOperations):
        def resource_path(self, rid, riid=None):
            if riid is not None:
                return '/%d/%d/%d/%d' % (OID.Test, IID, rid, riid)
            else:
                return '/%d/%d/%d' % (OID.Test, IID, rid)

        def setUp(self, maximum_version='1.1'):
            super().setUp(maximum_version=maximum_version)
            self.create_instance(self.serv, oid=OID.Test, iid=IID)



class WriteCompositeCborTypical(Test.WriteComposite):
    def runTest(self):
        request = [
            {
                SenmlLabel.NAME: ResPath.Test[IID].ResInt,
                SenmlLabel.VALUE: 42
            },
            {
                SenmlLabel.NAME: ResPath.Test[IID].ResString,
                SenmlLabel.STRING: 'test'
            },
            {
                SenmlLabel.NAME: ResPath.Device.UTCOffset,
                SenmlLabel.STRING: 'offset'
            }
        ]

        self.write_composite(self.serv, content=CBOR.serialize(request))
        res = self.read_composite(self.serv, paths=[entry[SenmlLabel.NAME] for entry in request])
        self.assertEqual(CBOR.parse(res.content), request)


class WriteCompositeJsonTypical(Test.WriteComposite):
    def runTest(self):
        request = [
            {
                'n': ResPath.Test[IID].ResInt,
                'v': 42
            },
            {
                'n': ResPath.Test[IID].ResString,
                'vs': 'test'
            },
            {
                'n': ResPath.Device.UTCOffset,
                'vs': 'offset'
            }
        ]

        self.write_composite(self.serv, content=json.dumps(request),
                             format=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        res = self.read_composite(self.serv, paths=[entry['n'] for entry in request],
                                  accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        self.assertEqual(json.loads(res.content.decode()), request)


class WriteCompositeNonexistingResourceInstance(Test.WriteComposite):
    def runTest(self):
        request = [
            {
                'n': self.resource_path(RID.Test.IntArray, 0),
                'v': 12
            },
            {
                'n': self.resource_path(RID.Test.IntArray, 1),
                'v': 73
            }
        ]

        self.write_composite(self.serv, content=json.dumps(request),
                             format=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        res = self.read_composite(self.serv, paths=[entry['n'] for entry in request],
                                  accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        self.assertEqual(json.loads(res.content.decode()), [
            {
                'bn': self.resource_path(RID.Test.IntArray),
                'n': '/%d' % (0,),
                'v': 12
            },
            {
                'n': '/%d' % (1,),
                'v': 73
            }
        ])


class WriteCompositeUnsupportedContentFromat(Test.WriteComposite):
    def runTest(self):
        self.write_composite(self.serv, content='nothing special',
                             format=coap.ContentFormat.TEXT_PLAIN,
                             expect_error_code=coap.Code.RES_UNSUPPORTED_CONTENT_FORMAT)


