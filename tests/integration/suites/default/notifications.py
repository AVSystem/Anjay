# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.
from framework.lwm2m_test import *


class CancellingConfirmableNotifications(test_suite.Lwm2mSingleServerTest,
                                         test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        observe1 = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)
        observe2 = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)

        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        notify1 = self.serv.recv()
        if bytes(notify1.token) != bytes(observe1.token):
            # which observation is handled first isn't exactly deterministic
            tmp = observe1
            observe1 = observe2
            observe2 = tmp

        self.assertIsInstance(notify1, Lwm2mNotify)
        self.assertEqual(bytes(notify1.token), bytes(observe1.token))
        self.serv.send(Lwm2mEmpty.matching(notify1)())

        notify2 = self.serv.recv()
        self.assertIsInstance(notify2, Lwm2mNotify)
        self.assertEqual(bytes(notify2.token), bytes(observe2.token))
        self.serv.send(Lwm2mEmpty.matching(notify2)())

        self.assertEqual(notify1.content, notify2.content)

        # Cancel the next notification
        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        notify1 = self.serv.recv()
        self.assertIsInstance(notify1, Lwm2mNotify)
        self.assertEqual(bytes(notify1.token), bytes(observe1.token))
        self.assertNotEqual(notify1.content, notify2.content)
        self.serv.send(Lwm2mReset.matching(notify1)())

        # the second observation should still produce notifications
        notify2 = self.serv.recv()
        self.assertIsInstance(notify2, Lwm2mNotify)
        self.assertEqual(bytes(notify2.token), bytes(observe2.token))
        self.assertEqual(notify1.content, notify2.content)
        self.serv.send(Lwm2mEmpty.matching(notify2)())

        self.execute_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        notify2 = self.serv.recv()
        self.assertIsInstance(notify2, Lwm2mNotify)
        self.assertEqual(bytes(notify2.token), bytes(observe2.token))
        self.assertNotEqual(notify1.content, notify2.content)
        self.serv.send(Lwm2mReset.matching(notify2)())


class SelfNotifyDisabled(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=42)

        self.observe(self.serv, oid=OID.Test, iid=42, rid=RID.Test.ResInt)

        self.write_resource(self.serv, oid=OID.Test, iid=42, rid=RID.Test.ResInt, content=b'42')
        # no notification expected in this case
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=3)


class SelfNotifyEnabled(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--enable-self-notify'])

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=42)

        observe = self.observe(self.serv, oid=OID.Test, iid=42, rid=RID.Test.ResInt)

        self.write_resource(self.serv, oid=OID.Test, iid=42, rid=RID.Test.ResInt, content=b'42')
        notify = self.serv.recv()
        self.assertIsInstance(notify, Lwm2mNotify)
        self.assertEqual(bytes(notify.token), bytes(observe.token))
        self.assertEqual(notify.content, b'42')


class RegisterCancelsNotifications(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)
        self.create_instance(self.serv, oid=OID.Test, iid=2)

        self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter)
        self.execute_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.IncrementCounter)
        self.assertIsInstance(self.serv.recv(), Lwm2mNotify)

        self.observe(self.serv, oid=OID.Test, iid=2, rid=RID.Test.Counter)
        self.execute_resource(self.serv, oid=OID.Test, iid=2, rid=RID.Test.IncrementCounter)
        self.assertIsInstance(self.serv.recv(), Lwm2mNotify)

        self.communicate('send-update')
        update = self.assertDemoUpdatesRegistration(respond=False, content=ANY)

        # Notifications shall work while Updating
        self.execute_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.IncrementCounter)
        self.execute_resource(self.serv, oid=OID.Test, iid=2, rid=RID.Test.IncrementCounter)
        notifications = 0
        while True:
            pkt = self.serv.recv()
            if notifications < 2 and isinstance(pkt, Lwm2mNotify):
                notifications += 1
                if notifications == 2:
                    break
            else:
                self.assertMsgEqual(pkt, update)

        # force a Register
        self.serv.send(Lwm2mErrorResponse.matching(update)(coap.Code.RES_FORBIDDEN))
        register = self.assertDemoRegisters(respond=False)

        # Notifications shall no longer work here
        self.execute_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.IncrementCounter)
        self.execute_resource(self.serv, oid=OID.Test, iid=2, rid=RID.Test.IncrementCounter)
        pkt = self.serv.recv()
        self.assertMsgEqual(pkt, register)

        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # Check that notifications still don't work
        self.execute_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.IncrementCounter)
        self.execute_resource(self.serv, oid=OID.Test, iid=2, rid=RID.Test.IncrementCounter)
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))


class LifetimeExpirationCancelsObserveWhileStoring(test_suite.Lwm2mDtlsSingleServerTest,
                                                   test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(lifetime=30)

    def runTest(self):
        expiration_time = float(self.communicate('registration-expiration-time 1',
                                                 match_regex='REGISTRATION_EXPIRATION_TIME=(.*)\n').group(
            1))

        observe = self.observe(self.serv, oid=OID.Device, iid=0, rid=RID.Device.CurrentTime)
        observe_start_time = time.time()

        notifications_generated = 0
        self.communicate('enter-offline')
        # Receive the notifications that might have managed to be sent before entering offline mode
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                self.assertIsInstance(self.serv.recv(deadline=deadline), Lwm2mNotify)
                notifications_generated += 1
            except socket.timeout:
                break

        # Check that the notifications are stored alright during offline mode
        deadline = expiration_time + 5
        while True:
            timeout_s = max(0.0, deadline - time.time())
            if self.read_log_until_match(
                    b'Notify for token %s scheduled:' % (binascii.hexlify(observe.token),),
                    timeout_s=timeout_s) is not None:
                notifications_generated += 1
            else:
                break

        # Assert that the notification has been implicitly cancelled
        self.assertIsNotNone(
            self.read_log_until_match(b'Observe cancel: %s\n' % (binascii.hexlify(observe.token),),
                                      timeout_s=5))

        self.assertAlmostEqual(notifications_generated, expiration_time - observe_start_time,
                               delta=2)

        self.communicate('exit-offline')

        # The client will register again
        self.assertDtlsReconnect()
        self.assertDemoRegisters(lifetime=30)
        # Check that no notifications are sent
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))


class UdpNotificationErrorTest(test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'])

    def runTest(self):
        self.write_resource(self.serv, oid=OID.Server, iid=1, rid=RID.Server.DefaultMaxPeriod,
                            content=b'2')

        self.create_instance(self.serv, oid=OID.Test, iid=1)
        orig_notif = self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter)
        self.delete_instance(self.serv, oid=OID.Test, iid=1)

        notif = self.serv.recv(timeout_s=5)
        self.assertEqual(notif.code, coap.Code.RES_NOT_FOUND)
        self.assertEqual(notif.token, orig_notif.token)
        self.serv.send(Lwm2mEmpty.matching(notif)())


class TcpNotificationErrorTest(test_suite.Lwm2mSingleTcpServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--confirmable-notifications'], binding='T')

    def runTest(self):
        self.write_resource(self.serv, oid=OID.Server, iid=1, rid=RID.Server.DefaultMaxPeriod,
                            content=b'2')

        self.create_instance(self.serv, oid=OID.Test, iid=1)
        orig_notif = self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter)
        self.delete_instance(self.serv, oid=OID.Test, iid=1)

        notif = self.serv.recv(timeout_s=5)
        self.assertEqual(notif.code, coap.Code.RES_NOT_FOUND)
        self.assertEqual(notif.token, orig_notif.token)
