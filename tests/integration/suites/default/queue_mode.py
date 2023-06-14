# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import framework.test_suite
from framework.lwm2m_test import *
from . import access_control, retransmissions, firmware_update


class QueueMode:
    @staticmethod
    def autoclose_disabled(test_case):
        import subprocess
        output = subprocess.run([test_case._get_demo_executable(), '-e', 'dummy', '-u', 'invalid'],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.decode(
            'utf-8')
        return 'ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE = ON' in output

    class Test(retransmissions.RetransmissionTest.TestMixin):
        def setUp(self, *args, **kwargs):
            self.autoclose_disabled = QueueMode.autoclose_disabled(self)
            super().setUp(*args, **kwargs)


class QueueModeBehaviour(retransmissions.RetransmissionTest.TestMixin,
                         access_control.AccessControl.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        import unittest
        if QueueMode.autoclose_disabled(self):
            raise unittest.SkipTest('Queue mode autoclose disabled')
        super().setUp(servers=[
            Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)),
            Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
            extra_cmdline_args=['--identity', str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')])

    def tearDown(self):
        self.teardown_demo_with_servers(deregister_servers=[self.servers[1]])

    def runTest(self):
        # create the test object and give read access to servers[0]
        self.create_instance(server=self.servers[1], oid=OID.Test)
        self.update_access(server=self.servers[1], oid=OID.Test, iid=0,
                           acl=[access_control.make_acl_entry(1, access_control.AccessMask.READ),
                                access_control.make_acl_entry(2, access_control.AccessMask.OWNER)])

        # first check if sockets stay online in non-queue mode
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 2)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 2)

        # put servers[0] into queue mode
        self.write_resource(self.servers[0], OID.Server, 1, RID.Server.Binding, b'UQ')
        self.assertDemoUpdatesRegistration(self.servers[0], binding='UQ', content=ANY)

        # Observe the Counter argument
        self.observe(self.servers[0], OID.Test, 0, RID.Test.Counter)

        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 2)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)

        # Trigger Notification from the non-queue server
        self.execute_resource(self.servers[1], OID.Test, 0, RID.Test.IncrementCounter)

        self.assertDtlsReconnect(self.servers[0])
        pkt = self.servers[0].recv()
        self.assertIsInstance(pkt, Lwm2mNotify)
        self.assertEqual(self.get_socket_count(), 2)

        # "queued RPCs"
        self.read_resource(self.servers[0], OID.Test, 0, RID.Test.Timestamp)
        # cancel observation
        self.observe(self.servers[0], OID.Test, 0, RID.Test.Counter, observe=1)

        # assert queue mode operation again
        time.sleep(12)
        self.assertEqual(self.get_socket_count(), 2)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)


class QueueModePreferenceIneffectiveForLwm2m10(QueueModeBehaviour):
    def runTest(self):
        self.communicate('set-queue-mode-preference PREFER_QUEUE_MODE')
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(self.servers[0])
        self.assertDemoUpdatesRegistration(self.servers[1])
        super().runTest()


class ForceQueueMode(QueueMode.Test, framework.test_suite.Lwm2mSingleServerTest):
    def tearDown(self):
        auto_deregister = False
        auto_deregister = self.autoclose_disabled
        super().tearDown(auto_deregister=auto_deregister)

    def runTest(self):
        self.communicate('set-queue-mode-preference FORCE_QUEUE_MODE')

        # change is not applied until Update
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)

        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        if self.autoclose_disabled:
            return
        # effectively queue mode, even though binding is "U"
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)


class ForceOnlineMode(retransmissions.RetransmissionTest.TestMixin,
                      framework.test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--binding=UQ'], auto_register=False)
        self.assertDemoRegisters(self.serv, binding='UQ')

    def runTest(self):
        self.communicate('set-queue-mode-preference FORCE_ONLINE_MODE')
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        # effectively online mode, even though binding is "UQ"
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)


class Lwm2m11QueueMode(retransmissions.RetransmissionTest.TestMixin,
                       framework.test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        import unittest
        if QueueMode.autoclose_disabled(self):
            raise unittest.SkipTest('Queue mode autoclose disabled')
        super().setUp(maximum_version='1.1')

    def runTest(self):
        # default: Prefer Online Mode, no queue mode
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)

        # Force Online Mode, no queue mode
        self.communicate('set-queue-mode-preference FORCE_ONLINE_MODE')
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)

        # Prefer Queue Mode, queue mode
        self.communicate('set-queue-mode-preference PREFER_QUEUE_MODE')
        self.communicate('send-update')
        # enabling queue mode on 1.1, needs re-registration
        self.assertDemoRegisters(self.serv, version='1.1', lwm2m11_queue_mode=True)
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        # Force Queue Mode, queue mode
        self.communicate('set-queue-mode-preference FORCE_QUEUE_MODE')
        self.communicate('send-update')
        self.assertDtlsReconnect(self.serv)
        self.assertDemoUpdatesRegistration()
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        # Prefer Online Mode again, no queue mode
        self.communicate('set-queue-mode-preference PREFER_ONLINE_MODE')
        self.communicate('send-update')
        # disabling queue mode on 1.1, needs re-registration
        self.assertDtlsReconnect(self.serv)
        self.assertDemoRegisters(self.serv, version='1.1', lwm2m11_queue_mode=False)
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)


class Lwm2m11UQBinding(QueueMode.Test, framework.test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        # UQ binding is not LwM2M 1.1-compliant, but we support it anyway
        super().setUp(extra_cmdline_args=['--binding=UQ'], auto_register=False,
                      maximum_version='1.1')
        self.assertDemoRegisters(self.serv, version='1.1', lwm2m11_queue_mode=True)

    def runTest(self):
        if self.autoclose_disabled:
            return
        # default: Prefer Online Mode, queue mode
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        # Prefer Queue Mode, queue mode
        self.communicate('set-queue-mode-preference PREFER_QUEUE_MODE')
        self.communicate('send-update')
        self.assertDtlsReconnect(self.serv)
        self.assertDemoUpdatesRegistration()
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        # Force Queue Mode, queue mode
        self.communicate('set-queue-mode-preference FORCE_QUEUE_MODE')
        self.communicate('send-update')
        self.assertDtlsReconnect(self.serv)
        self.assertDemoUpdatesRegistration()
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        # Force Online Mode, no queue mode
        self.communicate('set-queue-mode-preference FORCE_ONLINE_MODE')
        self.communicate('send-update')
        # disabling queue mode on 1.1, needs re-registration
        self.assertDtlsReconnect(self.serv)
        self.assertDemoRegisters(self.serv, version='1.1', lwm2m11_queue_mode=False)
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 1)


class QueueModeAfterManualReconnect(retransmissions.RetransmissionTest.TestMixin,
                                    firmware_update.SameSocketDownload.Test):
    def setUp(self):
        import unittest
        if QueueMode.autoclose_disabled(self):
            raise unittest.SkipTest('Queue mode autoclose disabled')
        super().setUp(extra_cmdline_args=['--binding=UQ'], maximum_version='1.1', binding=None,
                      lwm2m11_queue_mode=True, psk_identity=b'test-identity', psk_key=b'test-key')

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.start_download()
        self.serv.recv()
        with self.serv.fake_close():
            self.wait_until_socket_count(0, timeout_s=5)

        # reconnect after a failure
        self.serv.reset()
        self.communicate('reconnect')
        self.serv.listen(timeout_s=5)
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)


class QueueModeAfterTimedOutSend(QueueMode.Test, firmware_update.SameSocketDownload.Test):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--binding=UQ'], maximum_version='1.1', binding=None,
                      lwm2m11_queue_mode=True, psk_identity=b'test-identity', psk_key=b'test-key')

    def tearDown(self):
        auto_deregister = False
        auto_deregister = self.autoclose_disabled
        super().tearDown(auto_deregister=auto_deregister)

    def runTest(self):
        self.start_download()
        self.serv.recv()
        with self.serv.fake_close():
            self.wait_until_socket_count(0, timeout_s=5)

        # reconnect after a failure
        self.serv.reset()
        self.communicate('reconnect')
        self.serv.listen(timeout_s=5)
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)

        self.communicate('send 1 %s' % (ResPath.Device.ModelNumber,))
        sent_time = time.time()
        expected_close = sent_time + self.max_transmit_wait()

        for i in range(self.MAX_RETRANSMIT + 1):
            pkt = self.serv.recv(timeout_s=max(1, expected_close - time.time()))
            self.assertMsgEqual(Lwm2mSend(), pkt)

        timeout = expected_close - time.time() - 2
        if timeout > 0.0:
            time.sleep(timeout)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        if self.autoclose_disabled:
            return
        self.assertEqual(self.get_socket_count(), 0)


class TlsQueueMode(framework.test_suite.Lwm2mSingleTcpServerTest):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self, *args, **kwargs):
        import unittest
        if QueueMode.autoclose_disabled(self):
            raise unittest.SkipTest('Queue mode autoclose disabled')
        super().setUp(extra_cmdline_args=['--tcp-request-timeout', '5'],
                      psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY, maximum_version='1.1',
                      binding='T')

    def runTest(self):
        self.communicate('set-queue-mode-preference PREFER_QUEUE_MODE')
        self.communicate('send-update')
        self.assertDemoRegisters(version='1.1', binding='T', lwm2m11_queue_mode=True)
        self.wait_until_socket_count(0, timeout_s=6)

        time.sleep(1)
        self.serv.reset()
        self.communicate('send-update')
        self.serv.listen(timeout_s=5)
        self.read_log_until_match(b'statefully resumed connection', timeout_s=5)
        # no CSM message here
        self.assertDemoUpdatesRegistration()


class NosecTcpQueueMode(framework.test_suite.Lwm2mSingleTcpServerTest):
    def setUp(self, *args, **kwargs):
        import unittest
        if QueueMode.autoclose_disabled(self):
            raise unittest.SkipTest('Queue mode autoclose disabled')
        super().setUp(extra_cmdline_args=['--tcp-request-timeout', '5'], maximum_version='1.1',
                      binding='T')

    def runTest(self):
        self.communicate('set-queue-mode-preference PREFER_QUEUE_MODE')
        self.communicate('send-update')
        self.assertDemoRegisters(version='1.1', binding='T', lwm2m11_queue_mode=True)
        self.wait_until_socket_count(0, timeout_s=6)

        time.sleep(1)
        self.serv.reset()
        self.communicate('send-update')
        self.assertTcpCsm()
        self.assertDemoRegisters(version='1.1', binding='T', lwm2m11_queue_mode=True)


class ReconnectServerIgnoredDuringQueueMode(QueueModeAfterManualReconnect):
    def runTest(self):
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        self.communicate('reconnect-server 1')
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)


class ReconnectServerDuringQueueMode(retransmissions.RetransmissionTest.TestMixin,
                                          firmware_update.SameSocketDownload.Test):
    def setUp(self):
        import unittest
        if not QueueMode.autoclose_disabled(self):
            raise unittest.SkipTest('Queue mode autoclose enabled')
        super().setUp(extra_cmdline_args=['--binding=UQ'], maximum_version='1.1', binding=None,
                      lwm2m11_queue_mode=True, psk_identity=b'test-identity', psk_key=b'test-key')

    def runTest(self):
        time.sleep(self.max_transmit_wait() + 2)
        self.assertEqual(self.get_socket_count(), 1)
        self.communicate('reconnect-server 1')
        self.assertDtlsReconnect()


class ForceReregisterDuringQueueMode(QueueModeAfterManualReconnect):
    def runTest(self):
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

        self.communicate('send-register 1')
        self.assertDtlsReconnect()
        self.assertDemoRegisters(version='1.1', lwm2m11_queue_mode=True)
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)
