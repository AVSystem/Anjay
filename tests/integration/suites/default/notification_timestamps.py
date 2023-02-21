# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import json
import time

from framework.lwm2m_test import *


def as_json(pkt):
    return json.loads(pkt.content.decode('utf-8'))


class NotificationTimestampsInLegacyJsonTest(test_suite.Lwm2mSingleServerTest,
                                             test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        res = as_json(self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                                   accept=coap.ContentFormat.APPLICATION_LWM2M_JSON))
        self.assertEqual(res['bn'], ResPath.Test[0].Counter)
        self.assertIsInstance(res['e'], list)
        self.assertEqual(len(res['e']), 1)
        self.assertNotIn('n', res['e'][0])
        self.assertEqual(res['e'][0]['v'], 0)
        self.assertAlmostEqual(res['e'][0]['t'], time.time(), 0)

        self.execute_resource(
            self.serv,
            oid=OID.Test,
            iid=0,
            rid=RID.Test.IncrementCounter)
        notification = self.serv.recv()
        self.assertIsInstance(notification, Lwm2mNotify)
        res2 = as_json(notification)
        self.assertEqual(res2['e'][0]['v'], 1)
        self.assertAlmostEqual(res2['e'][0]['t'], time.time(), 0)
        self.assertGreater(res2['e'][0]['t'], res['e'][0]['t'])

        # Check if the responses have identical structure
        res2['e'][0]['v'] = 0
        res2['e'][0]['t'] = res['e'][0]['t']
        self.assertEqual(res, res2)


class NotificationTimestampsInSenmlJsonTest(test_suite.Lwm2mSingleServerTest,
                                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        res = as_json(self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                                   accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_JSON))
        self.assertIsInstance(res, list)
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0]['bn'], ResPath.Test[0].Counter)
        self.assertNotIn('n', res[0])
        self.assertEqual(res[0]['v'], 0)
        self.assertAlmostEqual(res[0]['bt'], time.time(), 0)

        self.execute_resource(
            self.serv,
            oid=OID.Test,
            iid=0,
            rid=RID.Test.IncrementCounter)
        notification = self.serv.recv()
        self.assertIsInstance(notification, Lwm2mNotify)
        res2 = as_json(notification)
        self.assertEqual(res2[0]['v'], 1)
        self.assertAlmostEqual(res2[0]['bt'], time.time(), 0)
        self.assertGreater(res2[0]['bt'], res[0]['bt'])

        # Check if the responses have identical structure
        res2[0]['v'] = 0
        res2[0]['bt'] = res[0]['bt']
        self.assertEqual(res, res2)


class NotificationTimestampsInSenmlCborTest(test_suite.Lwm2mSingleServerTest,
                                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        res = cbor2.loads(self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                                       accept=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR).content)
        self.assertIsInstance(res, list)
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0][SenmlLabel.BASE_NAME.value],
                         ResPath.Test[0].Counter)
        self.assertNotIn(SenmlLabel.NAME.value, res[0])
        self.assertEqual(res[0][SenmlLabel.VALUE.value], 0)
        self.assertAlmostEqual(
            res[0][SenmlLabel.BASE_TIME.value], time.time(), 0)

        self.execute_resource(
            self.serv,
            oid=OID.Test,
            iid=0,
            rid=RID.Test.IncrementCounter)
        notification = self.serv.recv()
        self.assertIsInstance(notification, Lwm2mNotify)
        res2 = cbor2.loads(notification.content)
        self.assertEqual(res2[0][SenmlLabel.VALUE.value], 1)
        self.assertAlmostEqual(
            res2[0][SenmlLabel.BASE_TIME.value], time.time(), 0)
        self.assertGreater(res2[0][SenmlLabel.BASE_TIME.value],
                           res[0][SenmlLabel.BASE_TIME.value])

        # Check if the responses have identical structure
        res2[0][SenmlLabel.VALUE.value] = 0
        res2[0][SenmlLabel.BASE_TIME.value] = res[0][SenmlLabel.BASE_TIME.value]
        self.assertEqual(res, res2)
