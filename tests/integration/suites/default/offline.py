# -*- coding: utf-8 -*-
#
# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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
from . import retransmissions

OFFLINE_INTERVAL = 4


class OfflineWithDtlsResumeTest(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        # Create object
        req = Lwm2mCreate('/%d' % (OID.Test,), TLV.make_instance(instance_id=0).serialize())
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mCreated.matching(req)(), self.serv.recv())

        # Force Update so that we won't have differing data models during exit-offline
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(content=ANY)

        # Observe: Timestamp
        observe_req = Lwm2mObserve(ResPath.Test[0].Timestamp)
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
        req = Lwm2mObserve(ResPath.Test[0].Timestamp, observe=1, token=observe_req.token)
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
        self.communicate('notify %s' % (ResPath.Security[0].ServerURI,))
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
        self.assertDemoUpdatesRegistration()


class OfflineWithQueueMode(retransmissions.RetransmissionTest.TestMixin,
                           test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--binding=UQ'], binding='UQ')

    def runTest(self):
        self.wait_until_socket_count(0, timeout_s=self.max_transmit_wait() + 2)
        self.communicate('enter-offline')
        time.sleep(2)

        # After exiting offline mode, the client shall not reconnect
        self.communicate('exit-offline')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Something should happen only when there is something to send
        self.communicate('send-update')
        self.assertDtlsReconnect()
        self.assertDemoUpdatesRegistration()


class OfflineWithQueueModeScheduledUpdate(retransmissions.RetransmissionTest.TestMixin,
                                          test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--binding=UQ'], lifetime=45, auto_register=False)

    def runTest(self):
        self.assertDemoRegisters(lifetime=45, binding='UQ')
        next_planned_update = time.time() + 45.0 - self.max_transmit_wait()

        self.wait_until_socket_count(0, timeout_s=self.max_transmit_wait() + 2.0)
        self.communicate('enter-offline')
        time.sleep(2)

        # After exiting offline mode, the client shall not reconnect
        self.communicate('exit-offline')
        timeout_s = next_planned_update - time.time() - 2.0
        self.assertGreater(timeout_s, 0.0)
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=timeout_s)

        # The Update message shall be delivered on time
        self.assertDtlsReconnect(timeout_s=5)
        self.assertDemoUpdatesRegistration()


class OfflineWithQueueModeNotify(retransmissions.RetransmissionTest.TestMixin,
                                 test_suite.Lwm2mDtlsSingleServerTest,
                                 test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--binding=UQ'], binding='UQ')

    def runTest(self):
        token = self.observe(self.serv, OID.EventLog, 0, RID.EventLog.LogData).token
        self.wait_until_socket_count(0, timeout_s=self.max_transmit_wait() + 2)
        self.communicate('enter-offline')
        time.sleep(2)

        # After exiting offline mode, the client shall not reconnect
        self.communicate('exit-offline')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Something should happen only when there is something to send
        self.communicate('set-event-log-data Papaya')
        self.assertDtlsReconnect()
        self.assertMsgEqual(Lwm2mNotify(token=token), self.serv.recv())


class OfflineWithQueueModeScheduledNotify(retransmissions.RetransmissionTest.TestMixin,
                                          test_suite.Lwm2mDtlsSingleServerTest,
                                          test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--binding=UQ'], binding='UQ')

    def runTest(self):
        token = self.observe(self.serv, OID.EventLog, 0, RID.EventLog.LogData).token
        self.wait_until_socket_count(0, timeout_s=self.max_transmit_wait() + 2)
        self.communicate('enter-offline')
        time.sleep(2)

        self.communicate('set-event-log-data Papaya')
        time.sleep(1)
        # After exiting offline mode, the client shall deliver the unsent notification
        self.communicate('exit-offline')
        self.assertDtlsReconnect()
        self.assertMsgEqual(Lwm2mNotify(token=token), self.serv.recv())


