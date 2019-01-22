# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

from . import access_control as ac


class ObserveAttributesTest(test_suite.Lwm2mSingleServerTest,
                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)
        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=1337, iid=0, rid=1)

        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attribute invariants
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['st=-1'],
                              expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['lt=9', 'gt=4'],
                              expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['lt=4', 'gt=9', 'st=3'],
                              expect_error_code=coap.Code.RES_BAD_REQUEST)

        # unparsable attributes
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['lt=invalid'],
                              expect_error_code=coap.Code.RES_BAD_OPTION)

        # Write Attributes
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['pmax=2'])

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
        self.create_instance(self.serv, oid=1337, iid=1)

        # Set invalid pmax (smaller than pmin)
        self.write_attributes(
            self.serv, oid=1337, iid=1, rid=1, query=['pmin=2', 'pmax=1'])

        self.observe(self.serv, oid=1337, iid=1, rid=1)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=3))


class ObserveResourceZeroPmax(test_suite.Lwm2mSingleServerTest,
                              test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=1)

        # Set invalid pmax (equal to 0)
        self.write_attributes(
            self.serv, oid=1337, iid=1, rid=1, query=['pmax=0'])

        self.observe(self.serv, oid=1337, iid=1, rid=1)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))


class ObserveResourceWithEmptyHandler(test_suite.Lwm2mSingleServerTest,
                                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        # See T832. resource_read handler implemented as 'return 0;'
        # used to cause segfault when observed.

        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)

        # Observe: Empty
        self.observe(self.serv, oid=1337, iid=0, rid=5,
                     expect_error_code=coap.Code.RES_INTERNAL_SERVER_ERROR)
        # hopefully that does not segfault


class ObserveWithMultipleServers(ac.AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=1337, iid=0)
        self.update_access(server=self.servers[1], oid=1337, iid=0,
                           acl=[ac.make_acl_entry(1, ac.ACCESS_MASK_READ | ac.ACCESS_MASK_EXECUTE),
                                ac.make_acl_entry(2, ac.ACCESS_MASK_OWNER)])
        # Observe: Counter
        self.observe(self.servers[1], oid=1337, iid=0, rid=1)
        # Expecting silence
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.write_attributes(self.servers[1], oid=1337, iid=0, rid=1, query=['gt=1'])
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2)  # ++counter
        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2)  # ++counter
        pkt = self.servers[1].recv()
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, b'2')

class ObserveWithDefaultAttributesTest(test_suite.Lwm2mSingleServerTest,
                                       test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)
        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=1337, iid=0, rid=1)
        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attributes set via public API
        self.communicate('set-attrs /1337/0/1 1 pmax=1 pmin=1')
        # And should now start arriving each second
        pkt = self.serv.recv(timeout_s=2)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)
        # Up until they're reset
        self.communicate('set-attrs /1337/0/1 1')


class ObserveOfflineWithStoredNotificationLimit(test_suite.Lwm2mSingleServerTest,
                                                test_suite.Lwm2mDmOperations):
    QUEUE_SIZE = 3

    def setUp(self):
        super().setUp(extra_cmdline_args=['--stored-notification-limit', str(self.QUEUE_SIZE)])
        self.write_resource(self.serv, OID.Server, 1, RID.Server.NotificationStoring, '1')

    def runTest(self):
        PMAX_S = 1
        SKIP_NOTIFICATIONS = 3  # number of Notify messages that should be skipped
        EPSILON_S = PMAX_S / 2  # extra time to wait for each Notify

        self.write_attributes(self.serv, OID.Device, 0, RID.Device.CurrentTime, query=['pmax=%d' % PMAX_S])
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

        self.assertDemoRegisters()

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
            self.assertNotIn(str(int(observe.content.decode('utf-8')) + idx).encode('utf-8'), seen_values)


class ObserveOfflineWithStoredNotificationLimitAndMultipleServers(test_suite.Lwm2mTest,
                                                                  test_suite.Lwm2mDmOperations):
    QUEUE_SIZE = 3

    def setUp(self):
        super().setUp(servers=2,
                      extra_cmdline_args=['--stored-notification-limit', str(self.QUEUE_SIZE)])
        self.write_resource(self.servers[0], OID.Server, 1, RID.Server.NotificationStoring, '1')
        self.write_resource(self.servers[1], OID.Server, 2, RID.Server.NotificationStoring, '1')

    def runTest(self):
        PMAX_S = 1
        SKIP_NOTIFICATIONS = 3  # number of Notify messages that should be skipped per server
        EPSILON_S = PMAX_S / 2  # extra time to wait for each Notify

        self.write_attributes(self.servers[0], OID.Device, 0, RID.Device.CurrentTime, query=['pmax=%d' % PMAX_S])
        self.write_attributes(self.servers[1], OID.Device, 0, RID.Device.CurrentTime, query=['pmax=%d' % PMAX_S])

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

        # TODO: why does Anjay reconnect in reverse order? It doesn't really matter, but... why?
        for serv in reversed(self.servers):
            self.assertDemoRegisters(serv)

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

        for serv in self.servers:
            with self.assertRaises(socket.timeout):
                serv.recv(PMAX_S * 2)

        # make sure the oldest values were dropped
        for idx in range(SKIP_NOTIFICATIONS):
            self.assertNotIn(str(int(observe.content.decode('utf-8')) + idx).encode('utf-8'), seen_values)
