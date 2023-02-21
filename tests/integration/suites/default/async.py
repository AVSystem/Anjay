# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import socket
import time

from framework.lwm2m_test import *
from framework.test_utils import ResponseFilter


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


class AsyncNotifications:
    class Test(test_suite.Lwm2mSingleServerTest,
                             test_suite.Lwm2mDmOperations):
        def clearObservation(self, respond=False):
            notify_filter = ResponseFilter(Lwm2mNotify)
            self.observe(self.serv,
                        OID.Device,
                        0,
                        RID.Device.CurrentTime,
                        observe=1,
                        response_filter=notify_filter)

            if respond:
                for message in notify_filter.filtered_messages:
                    self.serv.send(Lwm2mEmpty.matching(message)())

            # consume possibly outstanding Notify interrupting clean deregister
            try:
                pkt = self.serv.recv(timeout_s=1.5)
                self.assertIsInstance(pkt, Lwm2mNotify)
                if respond:
                    self.serv.send(Lwm2mEmpty.matching(pkt)())
            except socket.timeout:
                pass


class NonconfirmableNotificationsDuringUpdate(AsyncNotifications.Test):
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

        self.serv.send(Lwm2mChanged.matching(pkt)())

        while True:
            try:
                pkt = self.serv.recv(timeout_s=0.5)
                self.assertIsInstance(pkt, Lwm2mNotify)
                received_notifications += 1
            except socket.timeout:
                break

        end_time = time.time()
        self.assertAlmostEqual(received_notifications, end_time - start_time, delta=1.1)

        self.clearObservation(respond=False)


class ConfirmableNotificationsDuringUpdate(AsyncNotifications.Test):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)
        self.communicate('send-update')

        # Confirmable notifications honor NSTART
        received_update_requests = 0
        update_id = None

        start_time = time.time()
        early_notify_checked_for = False
        while received_update_requests < 3:
            pkt = self.serv.recv(timeout_s=10)

            # one Notify could be sent by the device before the Update message
            if not early_notify_checked_for:
                early_notify_checked_for = True
                if isinstance(pkt, Lwm2mNotify):
                    continue

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
        self.assertAlmostEqual(received_notifications, end_time - start_time, delta=1.1)

        self.clearObservation(respond=True)


class NonconfirmableNotificationsDuringRegister(AsyncNotifications.Test):
    def runTest(self, respond_to_notifications=False):
        self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)
        notify_filter = ResponseFilter(Lwm2mNotify)

        # force a Register
        self.communicate('send-update')
        update = self.assertDemoUpdatesRegistration(respond=False, response_filter=notify_filter)
        self.serv.send(Lwm2mErrorResponse.matching(update)(coap.Code.RES_FORBIDDEN))
        register = self.assertDemoRegisters(respond=False, response_filter=notify_filter)

        if respond_to_notifications:
            for message in notify_filter.filtered_messages:
                self.serv.send(Lwm2mEmpty.matching(message)())

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
        self.assertAlmostEqual(received_notifications, end_time - start_time, delta=1.1)

        self.clearObservation(respond=respond_to_notifications)


class ConfirmableNotificationsDuringRegister(NonconfirmableNotificationsDuringRegister):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        super().runTest(respond_to_notifications=True)
