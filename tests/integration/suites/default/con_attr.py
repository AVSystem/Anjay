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

import socket

from framework.lwm2m_test import *


class ConfirmableTest(test_suite.Lwm2mSingleServerTest,
                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        # Write Attributes for Counter
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['con=1', 'pmax=1'])

        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                                   token=random_stuff(8))
        # Observe: Timestamp
        timestamp_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Timestamp,
                                     token=get_another_token(counter_pkt.token))

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
        # We don't wait for response to the first Cancel Observe before sending
        # the other one because expected responses are small enough to not
        # trigger a BLOCK transfer. In that case, Anjay is not expected to
        # attempt to receive additional packets during request handling - they
        # should be handled sequentially without any errors.
        self.serv.send(Lwm2mObserve(
            ResPath.Test[0].Counter, token=counter_pkt.token, observe=1))
        self.serv.send(Lwm2mObserve(
            ResPath.Test[0].Timestamp, token=timestamp_pkt.token, observe=1))

        # flush any remaining notifications & Cancel Observe responses
        try:
            while True:
                pkt = self.serv.recv(timeout_s=0.1)
                self.assertIn(
                    pkt.token, {counter_pkt.token, timestamp_pkt.token})
        except socket.timeout:
            pass


class NonConfirmableTest(test_suite.Lwm2mSingleServerTest,
                         test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        # Write Attributes for Counter
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['con=0', 'pmax=1'])

        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                                   token=random_stuff(8))
        # Observe: Timestamp
        timestamp_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Timestamp,
                                     token=get_another_token(counter_pkt.token))

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
        # We don't wait for response to the first Cancel Observe before sending
        # the other one because expected responses are small enough to not
        # trigger a BLOCK transfer. In that case, Anjay is not expected to
        # attempt to receive additional packets during request handling - they
        # should be handled sequentially without any errors.
        self.serv.send(Lwm2mObserve(
            ResPath.Test[0].Counter, token=counter_pkt.token, observe=1))
        self.serv.send(Lwm2mObserve(
            ResPath.Test[0].Timestamp, token=timestamp_pkt.token, observe=1))

        # flush any remaining notifications & Cancel Observe responses
        try:
            while True:
                pkt = self.serv.recv(timeout_s=0.1)
                self.assertIn(
                    pkt.token, {counter_pkt.token, timestamp_pkt.token})
        except socket.timeout:
            pass
