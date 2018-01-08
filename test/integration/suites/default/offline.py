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

from framework.lwm2m_test import *

OFFLINE_INTERVAL = 4


class OfflineWithDtlsResumeTest(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        # Create object
        req = Lwm2mCreate('/1337', TLV.make_instance(instance_id=0).serialize())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), self.serv.recv())

        # Observe: Timestamp
        observe_req = Lwm2mObserve('/1337/0/0')
        self.serv.send(observe_req)

        timestamp_pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(observe_req)(), timestamp_pkt)

        # now enter offline mode
        self.communicate('enter-offline')

        # if we were not fast enough, one more message might have come;
        # we try to support both cases
        try:
            timestamp_pkt = self.serv.recv(timeout_s=1)
        except socket.timeout:
            pass

        # now no messages shall arrive
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=OFFLINE_INTERVAL)

        # exit offline mode
        self.communicate('exit-offline')

        # client reconnects with DTLS session resumption
        self.assertDtlsReconnect()

        notifications = 0
        while True:
            try:
                timestamp_pkt = self.serv.recv(timeout_s=0.2)
                self.assertEqual(timestamp_pkt.token, observe_req.token)
                notifications += 1
            except socket.timeout:
                break
        self.assertGreaterEqual(notifications, OFFLINE_INTERVAL - 1)
        self.assertLessEqual(notifications, OFFLINE_INTERVAL + 1)

        # Cancel Observe
        req = Lwm2mObserve('/1337/0/0', observe=1)
        self.serv.send(req)
        timestamp_pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), timestamp_pkt)

        # now no messages shall arrive
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=2)


class OfflineWithReregisterTest(test_suite.Lwm2mDtlsSingleServerTest):
    LIFETIME = OFFLINE_INTERVAL - 1

    def setUp(self):
        super().setUp(lifetime=OfflineWithReregisterTest.LIFETIME)

    def runTest(self):
        self.communicate('enter-offline')

        # now no messages shall arrive
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=OFFLINE_INTERVAL)

        self.communicate('exit-offline')

        # Register shall now come
        self.assertDtlsReconnect()
        self.assertDemoRegisters(lifetime=OfflineWithReregisterTest.LIFETIME)


class OfflineWithSecurityObjectChange(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.communicate('enter-offline')
        # Notify anjay that Security Object Resource changed
        self.communicate('notify /0/0/0')
        # This should not reload sockets
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=1)

        # Notify anjay that Security Object Instances changed
        self.communicate('notify /0')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=1)

        self.communicate('exit-offline')
        self.assertDtlsReconnect()


class OfflineWithReconnect(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.communicate('enter-offline')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=OFFLINE_INTERVAL)
        self.communicate('reconnect')
        self.assertDtlsReconnect()
        self.assertDemoUpdatesRegistration()


class OfflineWithoutDtlsTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate('enter-offline')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=OFFLINE_INTERVAL)
        self.communicate('exit-offline')
        self.assertDemoRegisters()


class OfflineWithRegistrationUpdateSchedule(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.communicate('enter-offline')
        self.communicate('send-update 0')
        with self.assertRaises(socket.timeout):
            pkt = self.serv.recv(timeout_s=1)

        self.communicate('exit-offline')
        self.assertDtlsReconnect()
        # no Update because it doesn't work while in offline moed
