# -*- coding: utf-8 -*-
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from framework.lwm2m_test import *

from .access_control import AccessMask
from . import lwm2m_gateway as gw
import base64


class ObserveWithGatewayBase(gw.Gateway.Base):
    prefixes = []
    button_input_counter_path = ""
    button_input_state_path = ""
    badc_data_path = ""
    
    def setUp(self, *args, **kwargs):
        super().setUp(*args, **kwargs)
        self.prefixes = self.extractPrefixes(self.serv)
        self.button_input_counter_path = "/%s/%d/0/%d" % (self.prefixes[0],
                                                          OID.PushButton,
                                                          RID.PushButton.DigitalInputCounter)
        self.button_input_state_path = "/%s/%d/0/%d" % (self.prefixes[0],
                                                        OID.PushButton,
                                                        RID.PushButton.DigitalInputState)
        self.badc_data_path = "/%s/%d/0/%d" % (self.prefixes[0],
                                               OID.BinaryAppDataContainer,
                                               RID.BinaryAppDataContainer.Data)

    def clickButtonNTimesInOfflineMode(self, servers, count):
        self.communicate('enter-offline')

        for click in range(count):
            self.communicate("gw_press_button 0")
            time.sleep(0.1)
            self.communicate("gw_release_button 0")
            time.sleep(0.1)

        for serv in servers:
            serv.reset()

        self.communicate('exit-offline')

        # demo will resume DTLS session without sending any LwM2M messages
        for serv in servers:
            serv.listen()

class ObserveAttributesTest(ObserveWithGatewayBase,
                            test_suite.Lwm2mDtlsSingleServerTest,
                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Observe: Counter
        counter_pkt = self.observe_path(self.serv, path=self.button_input_counter_path)

        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attribute invariants
        self.write_attributes_path(self.serv, path=self.button_input_counter_path,
                                   query=['st=-1'],
                                   expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.write_attributes_path(self.serv, path=self.button_input_counter_path,
                                   query=['lt=9', 'gt=4'],
                                   expect_error_code=coap.Code.RES_BAD_REQUEST)
        self.write_attributes_path(self.serv, path=self.button_input_counter_path,
                                   query=['lt=4', 'gt=9', 'st=3'],
                                   expect_error_code=coap.Code.RES_BAD_REQUEST)

        # unparsable attributes
        self.write_attributes_path(self.serv, path=self.button_input_counter_path,
                                   query=['lt=invalid'],
                                   expect_error_code=coap.Code.RES_BAD_OPTION)

        # Write Attributes
        self.write_attributes_path(self.serv, path=self.button_input_counter_path,
                                   query=['pmax=2'])

        # now we should get notifications, even though nothing changed
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)

        # and another one
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)


class ObserveResourceInvalidPmax(ObserveWithGatewayBase,
                                 test_suite.Lwm2mDtlsSingleServerTest,
                                 test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Set invalid pmax (smaller than pmin)
        self.write_attributes_path(self.serv, path=self.badc_data_path, query=['pmin=2', 'pmax=1'])

        self.observe_path(self.serv, path=self.badc_data_path)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=3))


class ObserveResourceZeroPmax(ObserveWithGatewayBase,
                              test_suite.Lwm2mDtlsSingleServerTest,
                              test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Set invalid pmax (equal to 0)
        self.write_attributes_path(
            self.serv, path=self.badc_data_path, query=['pmax=0']
        )

        self.observe_path(self.serv, path=self.badc_data_path)

        # No notification should arrive
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))


class ObserveWithMultipleServers(ObserveWithGatewayBase,
                                 test_suite.Lwm2mDtlsSingleServerTest,
                                 test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(servers=2,
                      extra_cmdline_args=['--access-entry', '/%s/0,0,%s' % (OID.Lwm2mGateway, AccessMask.READ),
                                          '--access-entry', '/%s/1,0,%s' % (OID.Lwm2mGateway, AccessMask.READ)])

    def runTest(self):
        path = '/%s/%d/0/%d' % (self.prefixes[0], OID.Temperature, RID.Temperature.ApplicationType)
        observe = self.observe_path(self.servers[1], path=path)

        # Expecting silence
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.write_path(self.servers[0], path=path, content=b'app1')

        pkt = self.servers[1].recv()
        self.assertMsgEqual(Lwm2mContent(msg_id=ANY,
                                         type=coap.Type.NON_CONFIRMABLE,
                                         token=observe.token),
                            pkt)


class ObserveWithDefaultAttributesTest(ObserveWithGatewayBase,
                                       test_suite.Lwm2mDtlsSingleServerTest,
                                       test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Observe
        observe_res_pkt = self.observe_path(self.serv, self.badc_data_path)
        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Attributes set
        self.write_attributes_path(self.serv, path=self.badc_data_path, query=['pmax=1', 'pmin=1'])
        # And should now start arriving each second
        for _ in range(3):
            pkt = self.serv.recv(timeout_s=2)
            self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
            self.assertEqual(pkt.content, observe_res_pkt.content)
        # Up until they're reset
        self.write_attributes_path(self.serv, path=self.badc_data_path, query=['pmax=0', 'pmin=0'])
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)


class ObserveOfflineWithStoredNotificationLimit(ObserveWithGatewayBase,
                                                test_suite.Lwm2mDtlsSingleServerTest,
                                                test_suite.Lwm2mDmOperations):
    QUEUE_SIZE = 3

    def setUp(self):
        super().setUp(extra_cmdline_args=['--stored-notification-limit', str(self.QUEUE_SIZE)])
        self.write_resource(server=self.serv, oid=OID.Server, iid=1,
                            rid=RID.Server.NotificationStoring, content='1')

    def runTest(self):
        SKIP_NOTIFICATIONS = 3  # number of Notify messages that should be skipped

        observe = self.observe_path(self.serv, path=self.button_input_counter_path)
        self.assertEqual(int(observe.content.decode('utf-8')), 0)

        # # generate enough notifications to cause dropping the oldest ones
        self.clickButtonNTimesInOfflineMode(self.servers, self.QUEUE_SIZE + SKIP_NOTIFICATIONS)

        seen_values = []

        # exactly QUEUE_SIZE notifications should be sent
        for _ in range(self.QUEUE_SIZE):
            pkt = self.serv.recv(timeout_s=0.5)
            self.assertMsgEqual(Lwm2mContent(msg_id=ANY,
                                             type=coap.Type.NON_CONFIRMABLE,
                                             token=observe.token),
                                pkt)
            seen_values.append(pkt.content)

        with self.assertRaises(socket.timeout):
            self.serv.recv(2)

        # make sure the oldest values were dropped
        for idx in range(SKIP_NOTIFICATIONS + 1):
            self.assertNotIn(str(idx).encode('utf-8'), seen_values)


class ObserveOfflineWithStoredNotificationLimitAndMultipleServers(ObserveWithGatewayBase,
                                                                  test_suite.Lwm2mDtlsSingleServerTest,
                                                                  test_suite.Lwm2mDmOperations):
    # value divisible by number of servers
    QUEUE_SIZE = 4

    def setUp(self):
        super().setUp(servers=2, psk_identity=b'test-identity', psk_key=b'test-key',
                      extra_cmdline_args=['--stored-notification-limit', str(self.QUEUE_SIZE),
                                          '--access-entry', '/%s/0,0,%s' % (OID.Lwm2mGateway, AccessMask.READ),
                                          '--access-entry', '/%s/1,0,%s' % (OID.Lwm2mGateway, AccessMask.READ)],)
        self.write_resource(self.servers[0], OID.Server, 1, RID.Server.NotificationStoring, '1')
        self.write_resource(self.servers[1], OID.Server, 2, RID.Server.NotificationStoring, '1')

    def runTest(self):
        SKIP_NOTIFICATIONS = 3  # number of Notify messages that should be skipped per server

        observes = [
            self.observe_path(self.servers[0], self.button_input_counter_path),
            self.observe_path(self.servers[1], self.button_input_counter_path),
        ]
        self.assertEqual(int(observes[0].content.decode('utf-8')), 0)
        self.assertEqual(int(observes[1].content.decode('utf-8')), 0)

        # generate enough notifications to cause dropping the oldest ones
        self.clickButtonNTimesInOfflineMode(self.servers, int(self.QUEUE_SIZE / 2 + SKIP_NOTIFICATIONS))

        remaining_notifications = self.QUEUE_SIZE
        seen_values = []

        # exactly QUEUE_SIZE notifications in total should be sent
        for observe, serv in zip(observes, self.servers):
            try:
                for _ in range(remaining_notifications):
                    pkt = serv.recv(timeout_s=0.5)
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
                serv.recv(2)

        # make sure the oldest values were dropped
        for idx in range(SKIP_NOTIFICATIONS + 1):
            self.assertNotIn(str(idx).encode('utf-8'), seen_values)


class ObserveOfflineWithStoringDisabled(ObserveWithGatewayBase,
                                        test_suite.Lwm2mDtlsSingleServerTest,
                                        test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.NotificationStoring, '0')
        observe = self.observe_path(self.serv, path="/%s/%d/0/%d" % 
                                    (self.prefixes[0], OID.Temperature, RID.Temperature.SensorValue))

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


class ObserveOfflineUnchangingPmaxWithStoringDisabled(ObserveWithGatewayBase,
                                                      test_suite.Lwm2mDtlsSingleServerTest,
                                                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        self.write_resource(self.serv, OID.Server, 1, RID.Server.NotificationStoring, '0')
        self.write_attributes_path(self.serv, self.badc_data_path, ['pmax=1'])
        observe = self.observe_path(self.servers[0], self.badc_data_path)

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


class ObserveResourceInstance(ObserveWithGatewayBase,
                              test_suite.Lwm2mDtlsSingleServerTest,
                              test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(maximum_version='1.1')

    def runTest(self):
        path = "/%s/%d/0/%d/0" % (self.prefixes[0],
                                  OID.BinaryAppDataContainer,
                                  RID.BinaryAppDataContainer.Data)
        
        # Observe resource instance
        observe_pkt = self.observe_path(self.serv, path)
        self.assertEqual("", observe_pkt.content.decode())

        # No notifications without value change
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Change value of a not observed resource instance
        self.communicate("gw-badc-write 1 0 0 value1")
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=1)
        self.communicate("gw-badc-write 0 0 1 value1")
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=1)

        # Change value of an observed resource instance, notification is expected
        self.communicate("gw-badc-write 0 0 0 value2")
        notify_pkt = self.serv.recv(timeout_s=1)
        encoded =  base64.b64encode(b"value2")
        self.assertMsgEqual(Lwm2mNotify(token=observe_pkt.token, content=encoded), notify_pkt)


