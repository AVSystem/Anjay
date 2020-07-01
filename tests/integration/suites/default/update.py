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
import time
import unittest

from framework.lwm2m_test import *


class UpdateTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send a correct Update
        self.communicate('send-update')
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())

        # should not send more messages after receiving a correct response
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))

        # should automatically send Updates before lifetime expires
        LIFETIME = 2

        self.serv.send(Lwm2mWrite(ResPath.Server[1].Lifetime, str(LIFETIME)))
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), pkt)
        self.assertDemoUpdatesRegistration(lifetime=LIFETIME)

        # wait for auto-scheduled Update
        self.assertDemoUpdatesRegistration(timeout_s=LIFETIME)


class UpdateServerDownReconnectTest(test_suite.PcapEnabledTest, test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # respond with Port Unreachable to the next packet
        with self.serv.fake_close():
            self.communicate('send-update')
            self.wait_until_icmp_unreachable_count(1, timeout_s=10)

        # client should abort
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)
        self.assertEqual(self.get_socket_count(), 0)

    def tearDown(self):
        super().tearDown(auto_deregister=False)
        self.assertEqual(self.count_icmp_unreachable_packets(), 1)


class ReconnectTest(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        self.communicate('reconnect')

        # server is connected, so only a packet from the same remote port will pass this assertion
        self.assertDtlsReconnect()


class UpdateFallbacksToRegisterAfterLifetimeExpiresTest(test_suite.Lwm2mSingleServerTest):
    LIFETIME = 4

    def setUp(self):
        super().setUp(auto_register=False,
                      lifetime=self.LIFETIME,
                      extra_cmdline_args=['--ack-random-factor', '1',
                                          '--ack-timeout', '1',
                                          '--max-retransmit', '1'])
        self.assertDemoRegisters(lifetime=self.LIFETIME)

    def runTest(self):
        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1)

        self.assertDemoUpdatesRegistration(
            timeout_s=self.LIFETIME / 2 + 1, respond=False)
        self.assertDemoUpdatesRegistration(
            timeout_s=self.LIFETIME / 2 + 1, respond=False)
        self.assertDemoRegisters(
            lifetime=self.LIFETIME, timeout_s=self.LIFETIME / 2 + 1)


class UpdateFallbacksToRegisterAfterCoapClientErrorResponse(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        def check(code: coap.Code):
            self.communicate('send-update')

            req = self.serv.recv()
            self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                            content=b''), req)
            self.serv.send(Lwm2mErrorResponse.matching(req)(code))

            self.assertDemoRegisters()

        # check all possible client (4.xx) errors
        for detail in range(32):
            if detail == 13:
                # TODO: do not ignore Request Entity Too Large (T2171)
                continue
            check(coap.Code(4, detail))


class ReconnectFailsWithCoapErrorCodeTest(test_suite.Lwm2mSingleServerTest):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,)),
                            pkt)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(
            code=coap.Code.RES_INTERNAL_SERVER_ERROR))

        # client should abort
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=10)
        self.assertEqual(self.get_socket_count(), 0)


class ReconnectFailsWithConnectionRefusedTest(test_suite.Lwm2mDtlsSingleServerTest,
                                              test_suite.Lwm2mDmOperations):
    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should try to resume DTLS session
        with self.serv.fake_close():
            self.communicate('reconnect')

            # give the process some time to fail
            time.sleep(1)

        # client should abort
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)
        self.assertEqual(self.get_socket_count(), 0)


class ConcurrentRequestWhileWaitingForResponse(test_suite.Lwm2mSingleServerTest,
                                               test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.communicate('send-update')

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.read_path(self.serv, ResPath.Device.Manufacturer)

        self.serv.send(Lwm2mChanged.matching(pkt)())


class UpdateAfterLifetimeChange(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mWrite(ResPath.Server[1].Lifetime, b'5')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertDemoUpdatesRegistration(lifetime=5)
        # Next update should be there shortly
        self.assertDemoUpdatesRegistration(timeout_s=5)

        req = Lwm2mWrite(ResPath.Server[1].Lifetime, b'86400')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertDemoUpdatesRegistration(lifetime=86400)


class NoUpdateDuringShutdownTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate('schedule-update-on-exit')
        # tearDown() expects a De-Register operation and will fail on
        # unexpected Update

