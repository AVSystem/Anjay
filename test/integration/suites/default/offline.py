import socket
import time

from framework.lwm2m_test import *

OFFLINE_INTERVAL = 4


class OfflineTest(test_suite.Lwm2mSingleServerTest):
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

        self.serv.reset()
        # exit offline mode
        self.communicate('exit-offline')

        # Update shall now come
        self.assertDemoUpdatesRegistration(content=ANY)

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


class OfflineWithReregisterTest(test_suite.Lwm2mSingleServerTest):
    LIFETIME = OFFLINE_INTERVAL - 1

    def setUp(self):
        super().setUp(lifetime=OfflineWithReregisterTest.LIFETIME)

    def runTest(self):
        self.communicate('enter-offline')

        # now no messages shall arrive
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=OFFLINE_INTERVAL)

        self.serv.reset()
        self.communicate('exit-offline')

        # Register shall now come
        self.assertDemoRegisters(lifetime=OfflineWithReregisterTest.LIFETIME)

class OfflineWithSecurityObjectChange(test_suite.Lwm2mSingleServerTest):
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

        self.serv.reset()
        self.communicate('exit-offline')
        self.assertDemoUpdatesRegistration()

class OfflineWithReconnect(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate('enter-offline')
        self.serv.reset()
        self.communicate('reconnect')
        self.assertDemoUpdatesRegistration()

class OfflineWithRegistrationUpdateSchedule(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.communicate('enter-offline')
        self.communicate('send-update 0')
        with self.assertRaises(socket.timeout):
            pkt = self.serv.recv(timeout_s=1)

        self.serv.reset()
        self.communicate('exit-offline')
        self.assertDemoUpdatesRegistration()

class OfflineWithObserve(test_suite.Lwm2mSingleServerTest,
                         test_suite.Lwm2mDmOperations):
    UPDATED_INSTANCES = (b'</1/1>,</2/0>,</2/1>,</2/2>,</2/3>,</2/4>,'
                        + b'</2/5>,</2/6>,</2/7>,</2/8>,</2/9>,</2/10>,'
                        + b'</2/11>,</2/12>,</2/13>,</2/14>,</2/15>,</2/16>,'
                        + b'</2/17>,</2/18>,</2/19>,</2/20>,</2/21>,</2/22>,'
                        + b'</2/23>,</3/0>,</4/0>,</5/0>,</6/0>,</7/0>,'
                        + b'</10/0>,</11>,</1337/1>,</11111/0>,</12359/0>,</12360>,'
                        + b'</12361/0>')

    # Explanation what's the idea:
    # 1. Set min notification period to 3 seconds.
    # 2. Observe current timestamp resource for at most 9 seconds,
    #    which should give us at most 3 notifications.
    # 3. Go offline after receiving first notification and make sure that
    #    they are not received until we go online back.
    def runTest(self):
        self.create_instance(self.serv, oid=1337)
        self.write_attributes(self.serv, oid=1337, query=['pmin=3'])
        self.write_attributes(self.serv, oid=1337, iid=1, rid=0,
                              query=['lt=%d' % (int(time.time()) + 9)])

        # Observe timestamp
        pkt = self.observe(self.serv, oid=1337, iid=1, rid=0)
        self.communicate('enter-offline')
        self.assertMsgEqual(Lwm2mContent.matching(pkt)(), pkt)

        # No notifications during anjay's being offline
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=4)

        self.serv.reset()
        self.communicate('exit-offline')

        self.assertDemoUpdatesRegistration(content=OfflineWithObserve.UPDATED_INSTANCES)

        # Two more notifications left.
        notification = self.serv.recv(timeout_s=3)
        self.assertEquals(notification.type, coap.Type.NON_CONFIRMABLE)
        self.assertEquals(notification.code, coap.Code.RES_CONTENT)

        notification = self.serv.recv(timeout_s=3)
        self.assertEquals(notification.type, coap.Type.NON_CONFIRMABLE)
        self.assertEquals(notification.code, coap.Code.RES_CONTENT)

        # And only two.
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=4)
