# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *

from . import access_control as ac


class ObserveAttributesTest(test_suite.Lwm2mSingleServerTest,
                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)

        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attribute invariants
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter, query=['st=-1'],
                              expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['lt=9', 'gt=4'], expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['lt=4', 'gt=9', 'st=3'],
                              expect_error_code=coap.Code.RES_BAD_REQUEST)

        # unparsable attributes
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['lt=invalid'], expect_error_code=coap.Code.RES_BAD_OPTION)

        # Write Attributes
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['pmax=2'])

        # now we should get notifications, even though nothing changed
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)

        # and another one
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)


class ObserveResourceInvalidPmax(test_suite.Lwm2mSingleServerTest,
                                 test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # Set invalid pmax (smaller than pmin)
        self.write_attributes(
            self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter, query=['pmin=2', 'pmax=1'])

        self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=3))


class ObserveResourceZeroPmax(test_suite.Lwm2mSingleServerTest,
                              test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # Set invalid pmax (equal to 0)
        self.write_attributes(
            self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter, query=['pmax=0'])

        self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))


class ObserveResourceWithEmptyHandler(test_suite.Lwm2mSingleServerTest,
                                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        # See T832. resource_read handler implemented as 'return 0;'
        # used to cause segfault when observed.

        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        self.write_resource(self.serv, oid=OID.Test, iid=0, rid=RID.Test.ResBytesZeroBegin,
                            content='0')

        # Observe: Empty
        self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.ResBytes,
                     expect_error_code=coap.Code.RES_INTERNAL_SERVER_ERROR)
        # hopefully that does not segfault


class ObserveWithMultipleServers(ac.AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test, iid=0)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[ac.make_acl_entry(1, ac.AccessMask.READ | ac.AccessMask.EXECUTE),
                                ac.make_acl_entry(2, ac.AccessMask.OWNER)])
        # Observe: Counter
        self.observe(self.servers[1], oid=OID.Test, iid=0, rid=RID.Test.Counter)
        # Expecting silence
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.write_attributes(self.servers[1], oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['gt=1'])
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.execute_resource(self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        self.execute_resource(self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        pkt = self.servers[1].recv()
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, b'2')


class ObserveWithDefaultAttributesTest(test_suite.Lwm2mSingleServerTest,
                                       test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)
        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attributes set via public API
        self.communicate('set-attrs %s 1 pmax=1 pmin=1' % (ResPath.Test[0].Counter,))
        # And should now start arriving each second
        pkt = self.serv.recv(timeout_s=2)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)
        # Up until they're reset
        self.communicate('set-attrs %s 1' % (ResPath.Test[0].Counter,))


class ObserveOfflineWithStoredNotificationLimit(test_suite.Lwm2mDtlsSingleServerTest,
                                                test_suite.Lwm2mDmOperations):
    QUEUE_SIZE = 3

    def setUp(self):
        super().setUp(extra_cmdline_args=['--stored-notification-limit', str(self.QUEUE_SIZE)])
        self.write_resource(self.serv, OID.Server, 1, RID.Server.NotificationStoring, '1')

    def runTest(self):
        PMAX_S = 1
        SKIP_NOTIFICATIONS = 3  # number of Notify messages that should be skipped
        EPSILON_S = PMAX_S / 2  # extra time to wait for each Notify

        self.write_attributes(self.serv, OID.Device, 0, RID.Device.CurrentTime,
                              query=['pmax=%d' % PMAX_S])
        observe = self.observe(self.serv, OID.Device, 0, RID.Device.CurrentTime)

        self.communicate('enter-offline')
        # wait long enough to cause dropping oldest notifications
        time.sleep(PMAX_S * (self.QUEUE_SIZE + SKIP_NOTIFICATIONS))
        self.serv.reset()

        # prevent the demo from queueing any additional notifications
        self.communicate('set-attrs %s 1 pmin=9999 pmax=9999' % (ResPath.Device.CurrentTime,))
        # wait until attribute change gets applied during next notification poll
        time.sleep(PMAX_S)

        self.communicate('exit-offline')

        # demo will resume DTLS session without sending any LwM2M messages
        self.serv.listen()

        seen_values = []

        # exactly QUEUE_SIZE notifications should be sent
        for _ in range(self.QUEUE_SIZE):
            pkt = self.serv.recv(timeout_s=EPSILON_S)
            self.assertMsgEqual(Lwm2mContent(msg_id=ANY,
                                             type=coap.Type.NON_CONFIRMABLE,
                                             token=observe.token),
                                pkt)
            seen_values.append(pkt.content)

        with self.assertRaises(socket.timeout):
            self.serv.recv(PMAX_S * 2)

        # make sure the oldest values were dropped
        for idx in range(SKIP_NOTIFICATIONS):
            self.assertNotIn(str(int(observe.content.decode('utf-8')) + idx).encode('utf-8'),
                             seen_values)


class ObserveOfflineWithStoredNotificationLimitAndMultipleServers(test_suite.Lwm2mTest,
                                                                  test_suite.Lwm2mDmOperations):
    QUEUE_SIZE = 3

    def setUp(self):
        super().setUp(servers=2, psk_identity=b'test-identity', psk_key=b'test-key',
                      extra_cmdline_args=['--stored-notification-limit', str(self.QUEUE_SIZE)])
        self.write_resource(self.servers[0], OID.Server, 1, RID.Server.NotificationStoring, '1')
        self.write_resource(self.servers[1], OID.Server, 2, RID.Server.NotificationStoring, '1')

    def runTest(self):
        PMAX_S = 1
        SKIP_NOTIFICATIONS = 3  # number of Notify messages that should be skipped per server
        EPSILON_S = PMAX_S / 2  # extra time to wait for each Notify

        self.write_attributes(self.servers[0], OID.Device, 0, RID.Device.CurrentTime,
                              query=['pmax=%d' % PMAX_S])
        self.write_attributes(self.servers[1], OID.Device, 0, RID.Device.CurrentTime,
                              query=['pmax=%d' % PMAX_S])

        observes = [
            self.observe(self.servers[0], OID.Device, 0, RID.Device.CurrentTime),
            self.observe(self.servers[1], OID.Device, 0, RID.Device.CurrentTime),
        ]

        self.communicate('enter-offline')
        # wait long enough to cause dropping oldest notifications
        time.sleep(PMAX_S * (self.QUEUE_SIZE / 2 + SKIP_NOTIFICATIONS))
        for serv in self.servers:
            serv.reset()

        # prevent the demo from queueing any additional notifications
        self.communicate('set-attrs %s 1 pmin=9999 pmax=9999' % (ResPath.Device.CurrentTime,))
        self.communicate('set-attrs %s 2 pmin=9999 pmax=9999' % (ResPath.Device.CurrentTime,))
        # wait until attribute change gets applied during next notification poll
        time.sleep(PMAX_S)

        self.communicate('exit-offline')

        # demo will resume DTLS sessions without sending any LwM2M messages
        for serv in self.servers:
            serv.listen()

        remaining_notifications = self.QUEUE_SIZE
        seen_values = []

        # exactly QUEUE_SIZE notifications in total should be sent
        for observe, serv in zip(observes, self.servers):
            try:
                for _ in range(remaining_notifications):
                    pkt = serv.recv(timeout_s=EPSILON_S)
                    self.assertMsgEqual(Lwm2mContent(msg_id=ANY,
                                                     type=coap.Type.NON_CONFIRMABLE,
                                                     token=observe.token),
                                        pkt)
                    remaining_notifications -= 1
                    seen_values.append(pkt.content)
            except socket.timeout:
                pass

        self.assertEqual(remaining_notifications, 0)

        for serv in self.servers:
            with self.assertRaises(socket.timeout):
                serv.recv(PMAX_S * 2)

        # make sure the oldest values were dropped
        for idx in range(SKIP_NOTIFICATIONS):
            self.assertNotIn(str(int(observe.content.decode('utf-8')) + idx).encode('utf-8'),
                             seen_values)


class ObserveOfflineWithStoringDisabled(test_suite.Lwm2mDtlsSingleServerTest,
                                        test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.NotificationStoring, '0')
        observe = self.observe(self.servers[0], OID.Device, 0, RID.Device.CurrentTime)

        self.communicate('enter-offline')
        # wait long enough to cause dropping and receive any outstanding notifications
        deadline = time.time() + 2.0
        while True:
            timeout = deadline - time.time()
            if timeout <= 0.0:
                break
            try:
                self.assertMsgEqual(Lwm2mNotify(token=observe.token),
                                    self.serv.recv(timeout_s=timeout))
            except socket.timeout:
                pass

        time.sleep(5.0)

        self.communicate('exit-offline')
        self.assertDtlsReconnect()
        pkt = self.serv.recv(timeout_s=2)
        self.assertMsgEqual(Lwm2mNotify(token=observe.token), pkt)
        self.assertAlmostEqual(float(pkt.content.decode('utf-8')), time.time(), delta=1.0)


class ObserveOfflineUnchangingPmaxWithStoringDisabled(test_suite.Lwm2mDtlsSingleServerTest,
                                                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.NotificationStoring, '0')
        self.write_attributes(self.serv, OID.Device, 0, RID.Device.SerialNumber, ['pmax=1'])
        observe = self.observe(self.servers[0], OID.Device, 0, RID.Device.SerialNumber)

        self.communicate('enter-offline')
        # wait long enough to cause dropping and receive any outstanding notifications
        deadline = time.time() + 2.0
        while True:
            timeout = deadline - time.time()
            if timeout <= 0.0:
                break
            try:
                self.assertMsgEqual(Lwm2mNotify(token=observe.token),
                                    self.serv.recv(timeout_s=timeout))
            except socket.timeout:
                pass

        time.sleep(5.0)

        self.communicate('exit-offline')
        self.assertDtlsReconnect()
        pkt = self.serv.recv(timeout_s=2)
        self.assertMsgEqual(Lwm2mNotify(token=observe.token), pkt)


class ResetWithoutMatchingObservation(test_suite.Lwm2mSingleServerTest):
    # T2359 - receiving a Reset message that cannot be matched to any existing
    # observation used to crash the application.
    def runTest(self):
        self.serv.send(Lwm2mReset(msg_id=0))


class ObserveResourceInstance(test_suite.Lwm2mSingleServerTest,
                              test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.1')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        # Initialize integer array
        self.execute_resource(self.serv, oid=OID.Test, iid=0,
                              rid=RID.Test.ResInitIntArray, content=b"0='1337'")
        discover_output = self.discover(self.serv, oid=OID.Test,
                                        iid=0, rid=RID.Test.IntArray).content
        self.assertEqual(b'</%d/0/%d>;dim=1,</%d/0/%d/0>' %
                         (OID.Test, RID.Test.IntArray, OID.Test, RID.Test.IntArray),
                         discover_output)
        # Write gt attribute
        self.write_attributes(self.serv, oid=OID.Test, iid=0,
                              rid=RID.Test.IntArray, query=['gt=2000'])
        discover_output = self.discover(self.serv, oid=OID.Test,
                                        iid=0, rid=RID.Test.IntArray).content
        self.assertEqual(b'</%d/0/%d>;dim=1;gt=2000,</%d/0/%d/0>' %
                         (OID.Test, RID.Test.IntArray, OID.Test, RID.Test.IntArray),
                         discover_output)
        # Observe resource instance
        observe_pkt = self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.IntArray, riid=0)
        self.assertEqual(b'1337', observe_pkt.content)
        # Change value of /33605/0/3/0 to 1500
        self.execute_resource(self.serv, oid=OID.Test, iid=0,
                              rid=RID.Test.ResInitIntArray, content=b"0='1500'")
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=1)
        # Change value of /33605/0/3/0 to 2000
        self.execute_resource(self.serv, oid=OID.Test, iid=0,
                              rid=RID.Test.ResInitIntArray, content=b"0='2000'")
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=1)
        # Change value of /33605/0/3/0 to 2001, notification is expected
        self.execute_resource(self.serv, oid=OID.Test, iid=0,
                              rid=RID.Test.ResInitIntArray, content=b"0='2001'")
        notify_pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(Lwm2mNotify(token=observe_pkt.token, content=b'2001'), notify_pkt)


