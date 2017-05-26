# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

        # update lifetime on the server
        self.communicate('send-update')
        pkt = self.serv.recv()

        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=['lt=' + str(LIFETIME)],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())

        # wait for auto-scheduled Update
        pkt = self.serv.recv(timeout_s=LIFETIME)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())


class UpdateServerDownReconnectTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # close the server socket, so that demo receives Port Unreachable in response
        # to the next packet
        listen_port = self.serv.get_listen_port()
        self.serv.socket.close()

        self.communicate('send-update')
        logs = self.read_logs_for(1)
        # assert that there was exactly one reconnect attempt
        self.assertEqual(sum(1 for line in logs.splitlines() if 'connected to ' in line), 1)

        # start the server again
        self.serv = Lwm2mServer(coap.Server(listen_port))

        # the Update failed, wait for retransmission to check if the stream was
        # properly released, i.e. sending another message does not trigger
        # assertion failure

        # demo should still work at this point

        # receive the update packet and send valid reply
        # otherwise demo will not finish until timeout

        # Note: explicit timeout is added on purpose to avoid following scenario,
        #       that happened few times during regular test runs:
        #
        # 1. Demo retrieves 'send-update' command.
        # 2. Test waits for 1s to miss that update request.
        # 3. Demo notices that it failed to deliver an update, therefore
        #    update is being rescheduled after 2s.
        # 4. Time passes, demo sends update again, but Lwm2mServer hasn't started
        #    just yet.
        # 5. Demo reschedules update again after about 4s of delay.
        # 6. Lwm2mServer has finally started.
        # 7. Test waited for 1 second for the update (default assertDemoUpdatesRegistration
        #    timeout), which is too little (see 5.), and failed.
        self.assertDemoUpdatesRegistration(timeout_s=5)


class ReconnectTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        original_remote_addr = self.serv.get_remote_addr()

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()
        pkt = self.serv.recv()

        # should retain remote port after reconnecting
        self.assertEqual(original_remote_addr, self.serv.get_remote_addr())
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        self.serv.send(Lwm2mChanged.matching(pkt)())


class ReconnectBootstrapTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        self.setup_demo_with_servers(num_servers=0, bootstrap_server=True)

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

        demo_port = int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))
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
        new_demo_port = int(self.communicate('get-port -1', match_regex='PORT==([0-9]+)\n').group(1))
        self.assertEqual(demo_port, new_demo_port)

        self.bootstrap_server.connect(('127.0.0.1', new_demo_port))

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
        port = self.serv.get_listen_port()

        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1)

        # make subsequent Updates fail to force re-Register
        self.serv.close()
        time.sleep(self.LIFETIME + 1)

        self.serv = Lwm2mServer(coap.Server(port))
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
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_INTERNAL_SERVER_ERROR))

        # make sure that client retries
        self.serv.reset()
        pkt = self.serv.recv(timeout_s=3)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())


class ReconnectFailsWithConnectionRefusedTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        listen_port = self.serv.get_listen_port()
        self.serv.close()

        # should send an Update with reconnect
        self.communicate('reconnect')
        self.serv.reset()

        # give the process some time to fail
        time.sleep(1)

        self.serv = Lwm2mServer(coap.Server(listen_port))
        self.serv.set_timeout(timeout_s=1)

        # make sure that client retries
        self.assertDemoUpdatesRegistration(timeout_s=5)


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
