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

        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
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


