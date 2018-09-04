# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

        self.serv.send(Lwm2mWrite('/1/1/1', str(LIFETIME)))
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

        # the Update failed, wait for retransmission to check if the stream was
        # properly released, i.e. sending another message does not trigger
        # assertion failure

        # demo should still work at this point

        # receive the re-Register packet and send valid reply
        # otherwise demo will not finish until timeout

        # Note: explicit timeout is added on purpose to avoid following scenario,
        #       that happened few times during regular test runs:
        #
        # 1. Demo retrieves 'send-update' command.
        # 2. Test waits for 1s to miss that update request.
        # 3. Demo notices that it failed to deliver an update, therefore
        #    re-Register is being rescheduled after 2s.
        # 4. Time passes, demo sends Register, but Lwm2mServer hasn't started
        #    just yet.
        # 5. Demo reschedules Register again after about 4s of delay.
        # 6. Lwm2mServer has finally started.
        # 7. Test waited for 1 second for the Register (default assertDemoRegisters
        #    timeout), which is too little (see 5.), and failed.
        self.serv.reset()
        self.assertDemoRegisters(timeout_s=5)

    def tearDown(self):
        super().tearDown()
        self.assertEqual(self.count_icmp_unreachable_packets(), 1)


class ReconnectTest(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        self.communicate('reconnect')

        # server is connected, so only a packet from the same remote port will pass this assertion
        self.assertDtlsReconnect()


class ReconnectBootstrapTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=0, bootstrap_server=True)

    def runTest(self):
        self.bootstrap_server.set_timeout(timeout_s=1)
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        original_remote_addr = self.bootstrap_server.get_remote_addr()

        # reconnect
        self.communicate('reconnect')
        self.bootstrap_server.reset()
        pkt = self.bootstrap_server.recv()

        # should retain remote port after reconnecting
        self.assertEqual(original_remote_addr, self.bootstrap_server.get_remote_addr())
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)

        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        demo_port = self.get_demo_port()
        self.assertEqual(self.bootstrap_server.get_remote_addr()[1], demo_port)

        # send Bootstrap Finish
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())

        # reconnect once again
        self.communicate('reconnect')

        # now there should be no Bootstrap Request
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=3))

        # should retain remote port after reconnecting
        new_demo_port = self.get_demo_port()
        self.assertEqual(demo_port, new_demo_port)

        self.bootstrap_server.connect_to_client(('127.0.0.1', new_demo_port))

        # DELETE /1337, essentially a no-op to check connectivity
        req = Lwm2mDelete(Lwm2mPath('/1337'))
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mDeleted.matching(req)(), self.bootstrap_server.recv())


class UpdateFallbacksToRegisterAfterLifetimeExpiresTest(test_suite.Lwm2mSingleServerTest):
    LIFETIME = 4

    def setUp(self):
        super().setUp(auto_register=False,
                      lifetime=self.LIFETIME)
        self.assertDemoRegisters(lifetime=self.LIFETIME)

    def runTest(self):
        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1)

        # make subsequent Updates fail to force re-Register
        with self.serv.fake_close():
            time.sleep(self.LIFETIME + 1)

        # re-Register causes the client to change port
        self.serv.reset()
        self.assertDemoRegisters(lifetime=self.LIFETIME, timeout_s=5)


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
            check(coap.Code(4, detail))


class ReconnectFailsWithCoapErrorCodeTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mRegister('/rd?lwm2m=1.0&ep=urn:dev:os:0023C7-000001&lt=86400'), pkt)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_INTERNAL_SERVER_ERROR))

        # make sure that client retries
        self.serv.reset()
        self.assertDemoRegisters(timeout_s=10)


class ReconnectFailsWithConnectionRefusedTest(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should try to resume DTLS session
        with self.serv.fake_close():
            self.communicate('reconnect')

            # give the process some time to fail
            time.sleep(1)

        # make sure that client retries
        self.assertDtlsReconnect(timeout_s=5)
        # TODO: Update would be sufficient, but our server handling code is a pile of spaghetti for now...
        self.assertDemoRegisters(timeout_s=5)


class ConcurrentRequestWhileWaitingForResponse(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate('send-update')

        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        req = Lwm2mRead('/3/0/0')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_SERVICE_UNAVAILABLE,
                                                             options=ANY),
                            self.serv.recv())

        self.serv.send(Lwm2mChanged.matching(pkt)())


class UpdateAfterLifetimeChange(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mWrite('/1/1/1', b'5')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertDemoUpdatesRegistration(lifetime=5)
        # Next update should be there shortly
        self.assertDemoUpdatesRegistration(timeout_s=5)

        req = Lwm2mWrite('/1/1/1', b'86400')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertDemoUpdatesRegistration(lifetime=86400)
