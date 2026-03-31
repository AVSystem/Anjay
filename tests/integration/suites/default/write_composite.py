# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
import base64
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

    class WriteCompositeNull(WriteComposite):
        def setUp(self):
            super().setUp(maximum_version='1.2')

            array_tlv = TLV.make_multires(
                resource_id=RID.Test.IntArray,
                instances=[
                    # (1, (0).to_bytes(0, byteorder='big')),
                    (2, (12).to_bytes(1, byteorder='big')),
                    (4, (1234).to_bytes(2, byteorder='big')),
                ])
            req = Lwm2mWrite(ResPath.Test[IID].IntArray,
                             array_tlv.serialize(),
                             format=coap.ContentFormat.APPLICATION_LWM2M_TLV)
            self.serv.send(req)

            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.serv.recv())


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


class WriteCompositeBasenameOnly(Test.WriteComposite):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=IID + 1)
        self.create_instance(self.serv, oid=OID.Test, iid=IID + 2)
        request = [
            {
                'vd': base64.encodebytes(b'test').strip().rstrip(b'=').decode(),
                'bn': '/%d/%d/%d' % (OID.Test, IID, RID.Test.ResRawBytes)
            },
            {
                'vd': base64.encodebytes(b'hurrdurr').strip().rstrip(b'=').decode(),
                'bn': '/%d/%d/%d' % (OID.Test, IID + 1, RID.Test.ResRawBytes)
            },
            {
                'vd': base64.encodebytes(b'herpderp').strip().rstrip(b'=').decode(),
                'bn': '/%d/%d/%d' % (OID.Test, IID + 2, RID.Test.ResRawBytes)
            }
        ]

        self.write_composite(self.serv, content=json.dumps(request),
                             format=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        res = self.read_composite(self.serv, paths=[entry['bn'] for entry in request],
                                  accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        self.assertEqual(json.loads(res.content.decode()), [
            {
                'bn': f'/{OID.Test}',
                'n': f'/{IID}/{RID.Test.ResRawBytes}',
                'vd': request[0]['vd']
            },
            {
                'n': f'/{IID + 1}/{RID.Test.ResRawBytes}',
                'vd': request[1]['vd']
            },
            {
                'n': f'/{IID + 2}/{RID.Test.ResRawBytes}',
                'vd': request[2]['vd']
            }
        ])


class WriteCompositeUnsupportedContentFromat(Test.WriteComposite):
    def runTest(self):
        self.write_composite(self.serv, content='nothing special',
                             format=coap.ContentFormat.TEXT_PLAIN,
                             expect_error_code=coap.Code.RES_UNSUPPORTED_CONTENT_FORMAT)


class WriteCompositeCborNull(Test.WriteCompositeNull):
    def runTest(self):
        request = [
            {
                SenmlLabel.NAME: ResPath.Test[IID].ResInt,
                SenmlLabel.VALUE: 42
            },
            {
                # nonexistent instance
                SenmlLabel.NAME: ResPath.Test[IID].IntArray + '/1',
                SenmlLabel.VALUE: None
            },
            {
                SenmlLabel.NAME: ResPath.Test[IID].IntArray + '/2',
                SenmlLabel.VALUE: None
            },
            {
                SenmlLabel.NAME: ResPath.Test[IID].ResString,
                SenmlLabel.STRING: 'test'
            }
        ]

        self.write_composite(self.serv, content=CBOR.serialize(request),
                             format=coap.ContentFormat.APPLICATION_LWM2M_SENML_ETCH_CBOR)
        res = self.read_composite(self.serv,
                                  paths=[ResPath.Test[IID].ResInt, ResPath.Test[IID].IntArray,
                                         ResPath.Test[IID].ResString],
                                  accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR)
        response = CBOR.parse(res.content)

        expected_response = [
            {
                SenmlLabel.BASE_NAME: '/%d/%d' % (OID.Test, IID),
                SenmlLabel.NAME: '/%d' % (RID.Test.ResInt,),
                SenmlLabel.VALUE: 42
            },
            {
                SenmlLabel.NAME: '/%d/%d' % (RID.Test.IntArray, 4),
                SenmlLabel.VALUE: 1234
            },
            {
                SenmlLabel.NAME: '/%d' % (RID.Test.ResString,),
                SenmlLabel.STRING: 'test'
            }
        ]
        self.assertEqual(response, expected_response)


class WriteCompositeJsonNull(Test.WriteCompositeNull):
    def runTest(self):
        request = [
            {
                'n': ResPath.Test[IID].ResInt,
                'v': 42
            },
            {
                # nonexistent instance
                'n': ResPath.Test[IID].IntArray + '/1',
                'v': None
            },
            {
                'n': ResPath.Test[IID].IntArray + '/2',
                'v': None
            },
            {
                'n': ResPath.Test[IID].ResString,
                'vs': 'test'
            }
        ]

        self.write_composite(self.serv, content=json.dumps(request),
                             format=coap.ContentFormat.APPLICATION_LWM2M_SENML_ETCH_JSON)
        res = self.read_composite(self.serv,
                                  paths=[ResPath.Test[IID].ResInt, ResPath.Test[IID].IntArray,
                                         ResPath.Test[IID].ResString],
                                  accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON)
        response = json.loads(res.content.decode())

        expected_response = [
            {
                'bn': '/%d/%d' % (OID.Test, IID),
                'n': '/%d' % (RID.Test.ResInt,),
                'v': 42
            },
            {
                'n': '/%d/%d' % (RID.Test.IntArray, 4),
                'v': 1234
            },
            {
                'n': '/%d' % (RID.Test.ResString,),
                'vs': 'test'
            }
        ]
        self.assertEqual(response, expected_response)
