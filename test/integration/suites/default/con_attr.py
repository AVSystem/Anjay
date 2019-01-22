# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

from framework.lwm2m_test import *


class ConfirmableTest(test_suite.Lwm2mSingleServerTest,
                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)

        # Write Attributes for Counter
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['con=1', 'pmax=1'])

        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=1337, iid=0, rid=1, token=random_stuff(8))
        # Observe: Timestamp
        timestamp_pkt = self.observe(self.serv, oid=1337, iid=0, rid=0, token=get_another_token(counter_pkt.token))

        con_count = 0
        non_count = 0
        for _ in range(10):
            pkt = self.serv.recv()
            self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
            self.assertIn(pkt.token, {counter_pkt.token, timestamp_pkt.token})
            if pkt.token == counter_pkt.token:
                self.assertEqual(pkt.type, coap.Type.CONFIRMABLE)
                self.serv.send(Lwm2mEmpty.matching(pkt)())
                con_count += 1
            elif pkt.token == timestamp_pkt.token:
                self.assertEqual(pkt.type, coap.Type.NON_CONFIRMABLE)
                non_count += 1

        self.assertGreater(con_count, 0)
        self.assertGreater(non_count, 0)

        # Cancel Observations
        for _ in range(2):
            pkt = self.serv.recv()
            self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
            self.assertIn(pkt.token, {counter_pkt.token, timestamp_pkt.token})
            self.serv.send(Lwm2mReset.matching(pkt)())


class NonConfirmableTest(test_suite.Lwm2mSingleServerTest,
                         test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)

        # Write Attributes for Counter
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['con=0', 'pmax=1'])

        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=1337, iid=0, rid=1, token=random_stuff(8))
        # Observe: Timestamp
        timestamp_pkt = self.observe(self.serv, oid=1337, iid=0, rid=0, token=get_another_token(counter_pkt.token))

        con_count = 0
        non_count = 0
        for _ in range(10):
            pkt = self.serv.recv()
            self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
            self.assertIn(pkt.token, {counter_pkt.token, timestamp_pkt.token})
            if pkt.token == counter_pkt.token:
                self.assertEqual(pkt.type, coap.Type.NON_CONFIRMABLE)
                non_count += 1
            elif pkt.token == timestamp_pkt.token:
                self.assertEqual(pkt.type, coap.Type.CONFIRMABLE)
                self.serv.send(Lwm2mEmpty.matching(pkt)())
                con_count += 1

        self.assertGreater(con_count, 0)
        self.assertGreater(non_count, 0)

        # Cancel Observations
        for _ in range(2):
            pkt = self.serv.recv()
            self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
            self.assertIn(pkt.token, {counter_pkt.token, timestamp_pkt.token})
            self.serv.send(Lwm2mReset.matching(pkt)())
