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

from framework.lwm2m import messages
from framework.lwm2m_test import test_suite
from framework.lwm2m.coap import Type
from framework.test_utils import OID, RID


class StatsTest:
    class Test(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
        @staticmethod
        def get_coap_packet_size(pkt):
            return len(pkt.serialize())

        @staticmethod
        def get_coap_content(pkt):
            return pkt.content.decode()

        def lwm2m_read_int_resource(self, oid, iid, rid):
            return int(self.get_coap_content(self.read_resource(self.serv, oid, iid, rid)))


class BytesReceived(StatsTest.Test):
    PATH = (OID.ExtDevInfo, 0, RID.ExtDevInfo.RxBytes)

    def runTest(self):
        req = messages.Lwm2mRead('/%d/%d/%d' % self.PATH).fill_placeholders()
        req_packet_size = self.get_coap_packet_size(req)
        expected_res = self._make_expected_res(req, messages.Lwm2mContent, expect_error_code=None)

        old_value = int(self.get_coap_content(self._perform_action(self.serv, req, expected_res)))
        new_value = self.lwm2m_read_int_resource(*self.PATH)
        value_diff = new_value - old_value
        self.assertEqual(req_packet_size, value_diff)


class BytesSent(StatsTest.Test):
    PATH = (OID.ExtDevInfo, 0, RID.ExtDevInfo.TxBytes)

    def runTest(self):
        response = self.read_resource(self.serv, *self.PATH)
        response_packet_size = self.get_coap_packet_size(response)
        old_value = int(self.get_coap_content(response))
        new_value = self.lwm2m_read_int_resource(*self.PATH)
        value_diff = new_value - old_value
        self.assertEqual(response_packet_size, value_diff)


class IncomingRetransmissions(StatsTest.Test):
    PATH = (OID.ExtDevInfo, 0, RID.ExtDevInfo.NumIncomingRetransmissions)

    def setUp(self):
        super().setUp(extra_cmdline_args=['--cache-size', '1000'])

    def runTest(self):
        req = messages.Lwm2mRead('/%d/%d/%d' % self.PATH)
        # Send same message twice
        self.serv.send(req)
        self.assertMsgEqual(messages.Lwm2mContent.matching(req)(), self.serv.recv())
        self.serv.send(req)
        self.assertMsgEqual(messages.Lwm2mContent.matching(req)(), self.serv.recv())

        num_incoming_retransmissions_res = self.lwm2m_read_int_resource(*self.PATH)
        self.assertEqual(1, num_incoming_retransmissions_res)


class OutgoingRetransmissions(StatsTest.Test):
    PATH = (OID.ExtDevInfo, 0, RID.ExtDevInfo.NumOutgoingRetransmissions)

    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        SOME_RESOURCE_PATH = (OID.Device, 0, RID.Device.Manufacturer)
        self.write_attributes(self.serv, *SOME_RESOURCE_PATH, query=['pmax=1'])
        self.observe(self.serv, OID.Device, 0, RID.Device.Manufacturer)

        ignored_notify = self.serv.recv()
        self.assertIsInstance(ignored_notify, messages.Lwm2mNotify)

        second_notify = self.serv.recv()
        self.assertIsInstance(second_notify, messages.Lwm2mNotify)
        ack_for_second_notify = messages.Lwm2mEmpty().matching(second_notify)(type=Type.RESET)
        self.serv.send(ack_for_second_notify)

        num_outgoing_retransmissions_res = self.lwm2m_read_int_resource(*self.PATH)
        self.assertEqual(1, num_outgoing_retransmissions_res)
