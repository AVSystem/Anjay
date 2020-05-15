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

from framework.lwm2m_test import *


class DmChangeDuringRegistration(test_suite.Lwm2mTest):
    def setUp(self):
        super().setUp(servers=2, num_servers_passed=1, auto_register=False)

    def runTest(self):
        register = self.assertDemoRegisters(self.servers[0], respond=False)
        self.assertTrue(register.content.startswith(b'</1/1>,</2>'))

        self.communicate("add-server coap://127.0.0.1:%d" % self.servers[1].get_listen_port())
        self.assertDemoRegisters(self.servers[1])

        register = self.assertDemoRegisters(self.servers[0], respond=False)
        self.assertTrue(register.content.startswith(b'</1/1>,</1/2>,</2>'))
        self.servers[0].send(
            Lwm2mCreated.matching(register)(location=self.DEFAULT_REGISTER_ENDPOINT))


class DmChangeDuringUpdate(test_suite.Lwm2mTest):
    def setUp(self):
        super().setUp(servers=2, num_servers_passed=1)

    def runTest(self):
        self.communicate('send-update')

        update = self.assertDemoUpdatesRegistration(self.servers[0], respond=False)
        self.assertEqual(update.content, b'')

        self.communicate("add-server coap://127.0.0.1:%d" % self.servers[1].get_listen_port())
        self.assertDemoRegisters(self.servers[1])

        update = self.assertDemoUpdatesRegistration(self.servers[0], content=ANY, respond=False)
        self.assertTrue(update.content.startswith(b'</1/1>,</1/2>,</2>'))
        self.servers[0].send(Lwm2mChanged.matching(update)())


class OfflineModeDuringRegistration(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def runTest(self):
        register1 = self.assertDemoRegisters(respond=False)

        self.communicate('enter-offline')

        # Register shall not be retried
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        self.communicate('exit-offline')

        # Register shall be retried immediately, with different ID/token
        register2 = self.assertDemoRegisters(timeout_s=1, respond=False)
        self.assertNotEqual(register1, register2)
        self.serv.send(Lwm2mCreated.matching(register2)(location=self.DEFAULT_REGISTER_ENDPOINT))


class OfflineModeDuringUpdate(test_suite.Lwm2mDtlsSingleServerTest):
    def runTest(self):
        self.communicate('send-update')

        update1 = self.assertDemoUpdatesRegistration(respond=False)

        self.communicate('enter-offline')

        # Update shall not be retried
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        self.communicate('exit-offline')

        # Update shall be retried immediately, with different ID/token
        self.assertDtlsReconnect(timeout_s=1)
        update2 = self.assertDemoUpdatesRegistration(timeout_s=1, respond=False)
        self.assertNotEqual(update1, update2)
        self.serv.send(Lwm2mChanged.matching(update2)())


class OfflineModeDuringUpdateAndExpiringLifetime(test_suite.Lwm2mDtlsSingleServerTest):
    LIFETIME = 5

    def setUp(self):
        super().setUp(lifetime=self.LIFETIME)

    def runTest(self):
        update = self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME, respond=False)

        self.communicate('enter-offline')

        # Neither Update retry nor Register shall be sent
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=self.LIFETIME)

        self.communicate('exit-offline')

        # Register shall be attempted immediately
        self.assertDtlsReconnect(timeout_s=1)
        self.assertDemoRegisters(timeout_s=1, lifetime=self.LIFETIME)


class RegisterAttemptDuringOfflineMode(test_suite.Lwm2mTest):
    def setUp(self):
        super().setUp(servers=2, num_servers_passed=1)

    def runTest(self):
        self.communicate('enter-offline')
        self.communicate("add-server coap://127.0.0.1:%d" % self.servers[1].get_listen_port())

        # Register shall not be attempted
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=5)

        self.communicate('exit-offline')
        self.assertDemoRegisters(self.servers[0])
        self.assertDemoRegisters(self.servers[1])


class SocketUpdateDuringRegister(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def runTest(self):
        register1 = self.assertDemoRegisters(respond=False)

        self.communicate('notify /0/1/1')
        # this triggers _anjay_schedule_socket_update()
        # and causes a new Register to replace the old one
        register2 = self.assertDemoRegisters(respond=False)
        self.assertNotEqual(register1, register2)
        self.serv.send(Lwm2mCreated.matching(register2)(location=self.DEFAULT_REGISTER_ENDPOINT))


class NonconfirmableNotificationsDuringUpdate(test_suite.Lwm2mSingleServerTest,
                                              test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)
        self.communicate('send-update')

        received_update_requests = 0
        update_id = None
        received_notifications = 0

        start_time = time.time()
        while received_update_requests < 3:
            pkt = self.serv.recv(timeout_s=10)
            if isinstance(pkt, Lwm2mUpdate):
                if update_id is None:
                    update_id = pkt.msg_id
                else:
                    # assert that this is indeed retransmission of the previous Update
                    self.assertEqual(pkt.msg_id, update_id)
                received_update_requests += 1
            elif isinstance(pkt, Lwm2mNotify):
                received_notifications += 1
        end_time = time.time()

        self.assertAlmostEqual(received_notifications, end_time - start_time, delta=1)
        self.serv.send(Lwm2mChanged.matching(pkt)())


class ConfirmableNotificationsDuringUpdate(test_suite.Lwm2mSingleServerTest,
                                           test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)
        self.communicate('send-update')

        # Confirmable notifications honor NSTART
        received_update_requests = 0
        update_id = None

        start_time = time.time()
        while received_update_requests < 3:
            pkt = self.serv.recv(timeout_s=10)
            self.assertIsInstance(pkt, Lwm2mUpdate)
            if update_id is None:
                update_id = pkt.msg_id
            else:
                # assert that this is indeed retransmission of the previous Update
                self.assertEqual(pkt.msg_id, update_id)
            received_update_requests += 1

        self.serv.send(Lwm2mChanged.matching(pkt)())

        received_notifications = 0
        while True:
            try:
                pkt = self.serv.recv(timeout_s=0.5)
                self.assertIsInstance(pkt, Lwm2mNotify)
                received_notifications += 1
                self.serv.send(Lwm2mEmpty.matching(pkt)())
            except socket.timeout:
                break

        end_time = time.time()
        self.assertAlmostEqual(received_notifications, end_time - start_time, delta=1)


class NonconfirmableNotificationsDuringRegister(test_suite.Lwm2mSingleServerTest,
                                                test_suite.Lwm2mDmOperations):
    def runTest(self, respond_to_notifications=False):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)

        # force a Register
        self.communicate('send-update')
        update = self.assertDemoUpdatesRegistration(respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(update)(coap.Code.RES_FORBIDDEN))
        register = self.assertDemoRegisters(respond=False)

        start_time = time.time()
        received_register_requests = 1
        while received_register_requests < 3:
            pkt = self.serv.recv(timeout_s=10)
            self.assertMsgEqual(pkt, register)
            received_register_requests += 1

        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        received_notifications = 0
        while True:
            try:
                pkt = self.serv.recv(timeout_s=0.5)
                self.assertIsInstance(pkt, Lwm2mNotify)
                received_notifications += 1
                if respond_to_notifications:
                    self.serv.send(Lwm2mEmpty.matching(pkt)())
            except socket.timeout:
                break

        end_time = time.time()
        self.assertAlmostEqual(received_notifications, end_time - start_time, delta=1)


class ConfirmableNotificationsDuringRegister(NonconfirmableNotificationsDuringRegister):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        super().runTest(respond_to_notifications=True)
