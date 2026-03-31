# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

import socket
import time
import unittest

from framework.lwm2m_test import *

from . import access_control as ac

# Most of these tests are slightly modified versions of those for the classic
# observation attributes


class ObserveWithAttributesBasicTest(test_suite.Lwm2mSingleServerTest,
                                     test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attribute invariants
        self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter, st=-1,
                     expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                     lt=9, gt=4, expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.observe(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                     lt=4, gt=9, st=3, expect_error_code=coap.Code.RES_BAD_REQUEST)

        # Write Attributes
        counter_pkt = self.observe(
            self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter, pmax=2)

        # now we should get notifications, even though nothing changed
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)

        # and another one
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)


class ObserveWithAttributesResourceInvalidPmax(test_suite.Lwm2mSingleServerTest,
                                               test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # Observe with invalid pmax (smaller than pmin)
        self.observe(self.serv, oid=OID.Test, iid=1,
                     rid=RID.Test.Counter, pmin=2, pmax=1)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=3))


class ObserveWithAttributesResourceZeroPmax(test_suite.Lwm2mSingleServerTest,
                                            test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # Observe with invalid pmax (equal to 0)
        self.observe(self.serv, oid=OID.Test, iid=1,
                     rid=RID.Test.Counter, pmax=0)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))


class ObserveWithAttributesResourceZeroPmax(test_suite.Lwm2mSingleServerTest,
                                            test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # Observe with invalid pmax (equal to 0)
        self.observe(self.serv, oid=OID.Test, iid=1,
                     rid=RID.Test.Counter, pmax=0)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))


class ObserveWithAttributesWithMultipleServers(ac.AccessControl.Test):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        self.create_instance(server=self.servers[1], oid=OID.Test, iid=0)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[ac.make_acl_entry(1, ac.AccessMask.READ | ac.AccessMask.EXECUTE),
                                ac.make_acl_entry(2, ac.AccessMask.OWNER)])
        # Observe: Counter
        self.observe(self.servers[1], oid=OID.Test,
                     iid=0, rid=RID.Test.Counter, gt=1)
        # Expecting silence
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=4)

        self.execute_resource(
            self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        self.execute_resource(
            self.servers[0], oid=OID.Test, iid=0, rid=RID.Test.IncrementCounter)
        pkt = self.servers[1].recv()
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, b'2')


class ObserveWithAttributesOfflineUnchangingPmaxWithStoringDisabled(test_suite.Lwm2mDtlsSingleServerTest,
                                                                    test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1,
                            RID.Server.NotificationStoring, '0')
        observe = self.observe(
            self.servers[0], OID.Device, 0, RID.Device.SerialNumber, pmax=1)

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


class ObserveWithAttributesResourceInstance(test_suite.Lwm2mSingleServerTest,
                                            test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)
        # Initialize integer array
        self.execute_resource(self.serv, oid=OID.Test, iid=0,
                              rid=RID.Test.ResInitIntArray, content=b"0='1337'")
        discover_output = self.discover(self.serv, oid=OID.Test,
                                        iid=0, rid=RID.Test.IntArray).content
        self.assertEqual(b'</%d/0/%d>;dim=1,</%d/0/%d/0>' %
                         (OID.Test, RID.Test.IntArray, OID.Test, RID.Test.IntArray), discover_output)
        # Observe resource instance
        observe_pkt = self.observe(
            self.serv, oid=OID.Test, iid=0, rid=RID.Test.IntArray, riid=0, gt=2000)
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
        self.assertMsgEqual(Lwm2mNotify(
            token=observe_pkt.token, content=b'2001'), notify_pkt)


class ObserveWithAttributesConflict(test_suite.Lwm2mSingleServerTest,
                                    test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        # Write an sttribute
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['gt=2'])

        # Set up an observation with the same attibute attached
        observe_pkt = self.observe(
            self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter, gt=7)

        # Increase counter to cross the treshold
        for _ in range(8):
            self.execute_resource(self.serv, oid=OID.Test,
                                  iid=0, rid=RID.Test.IncrementCounter)

        # At this point we should receive the first notification (ignoring gt=2 attached to the
        # resource)
        notify_pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(Lwm2mNotify(
            token=observe_pkt.token, content=b'8'), notify_pkt)


class ObserveWithAttributesDefaultPmin(test_suite.Lwm2mSingleServerTest,
                                       test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        # Write some large pmin which should not be used
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['pmin=9999'])

        # Observe with gt attribute which requires pmin to be exceded to send notification
        observe_pkt = self.observe(
            self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter, gt=7)

        # Increase counter to cross the treshold
        for _ in range(8):
            self.execute_resource(self.serv, oid=OID.Test,
                                  iid=0, rid=RID.Test.IncrementCounter)

        # The notification should be received without waiting for pmin set for the resource
        notify_pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(Lwm2mNotify(
            token=observe_pkt.token, content=b'8'), notify_pkt)


class ObserveWithAttributesPreserveAttrs(test_suite.Lwm2mSingleServerTest,
                                         test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.2')

    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=OID.Test, iid=0)

        # Write some large gt which should not be used
        self.write_attributes(self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter,
                              query=['gt=9999'])

        # Observe with gt attribute which requires pmin to be exceeded to send notification
        observe_pkt = self.observe(
            self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter, lt=3, gt=7)

        # Discover should return the value of the attribute attached to the resource, not to
        # the observation
        discover_pkt = self.discover(
            self.serv, oid=OID.Test, iid=0, rid=RID.Test.Counter)
        self.assertEqual(discover_pkt.content, b'</33605/0/1>;gt=9999')
