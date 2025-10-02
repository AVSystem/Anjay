# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

from framework_tools.utils.lwm2m_test import *
from .access_control import AccessMask
from .bootstrap_client import BootstrapTest
from .register import RegisterUdp
from .retransmissions import RetransmissionTest, RegisterTimeout, DeregisterIcmp
from .queue_mode import QueueMode

import time
import re
import enum


class ConnStatusAPI:
    class Status(enum.Enum):
        INVALID = enum.auto()
        ERROR = enum.auto()
        INITIAL = enum.auto()
        CONNECTING = enum.auto()
        BOOTSTRAPPING = enum.auto()
        BOOTSTRAPPED = enum.auto()
        REGISTERING = enum.auto()
        REGISTERED = enum.auto()
        REG_FAILURE = enum.auto()
        DEREGISTERING = enum.auto()
        DEREGISTERED = enum.auto()
        SUSPENDING = enum.auto()
        SUSPENDED = enum.auto()
        REREGISTERING = enum.auto()
        UPDATING = enum.auto()

    class TestMixin:
        STATUS_CHANGE_REGEX = re.compile(
            rb'Current status of the server with SSID (\d+) is: (.+)\n')

        # we need this to ensure that other commands which search through
        # logs will not accidentally consume statements we're interested in
        log_alt_offset = 0

        def assertStatusChanges(self, statuses, default_ssid=1, timeout_s=0):
            deadline = time.time() + timeout_s

            assert isinstance(statuses, list)

            for status in statuses:
                if isinstance(status, ConnStatusAPI.Status):
                    expected_ssid = default_ssid
                    expected_status = status
                else:
                    assert isinstance(status, tuple)
                    assert [type(field) for field in status] == [
                        int, ConnStatusAPI.Status]
                    expected_ssid, expected_status = status

                self.log_alt_offset, match = self.read_log_until_match(
                    self.STATUS_CHANGE_REGEX,
                    timeout_s=max(0, deadline - time.time()),
                    alt_offset=self.log_alt_offset)
                self.assertIsNotNone(match)

                actual_ssid = int(match.group(1))
                actual_status = ConnStatusAPI.Status[match.group(2).decode()]

                self.assertEqual(actual_ssid, expected_ssid)
                self.assertEqual(actual_status, expected_status)

        def assertNoOutstandingStatusChanges(self):
            _, match = self.read_log_until_match(
                self.STATUS_CHANGE_REGEX, timeout_s=0, alt_offset=self.log_alt_offset)
            self.assertIsNone(match)

    class AutoRegDeregTestMixin(TestMixin):
        def assertDemoRegisters(
                self, server=None, initial=True, first_attempt=True, reregister=False, respond=True, reject=False, *args, **kwargs):
            pkt = super().assertDemoRegisters(server=server,
                                              respond=respond, reject=reject, *args, **kwargs)
            expected_statuses = []

            if initial:
                expected_statuses.append(ConnStatusAPI.Status.INITIAL)

            if first_attempt:
                expected_statuses.append(ConnStatusAPI.Status.CONNECTING)
                expected_statuses.append(ConnStatusAPI.Status.REGISTERING)

            if reregister:
                expected_statuses.append(ConnStatusAPI.Status.REREGISTERING)

            if respond:
                if reject:
                    expected_statuses.append(ConnStatusAPI.Status.REG_FAILURE)
                else:
                    expected_statuses.append(ConnStatusAPI.Status.REGISTERED)

            self.assertStatusChanges(expected_statuses, timeout_s=2)

            return pkt

        def assertDemoUpdatesRegistration(self, first_attempt=True, respond=True, *args, **kwargs):
            pkt = super().assertDemoUpdatesRegistration(respond=respond, *args, **kwargs)
            expected_statuses = []

            if first_attempt:
                expected_statuses.append(ConnStatusAPI.Status.UPDATING)

            if respond:
                expected_statuses.append(ConnStatusAPI.Status.REGISTERED)

            self.assertStatusChanges(expected_statuses, timeout_s=2)

            return pkt

        def assertDemoDeregisters(self, server=None, *args, **kwargs):
            super().assertDemoDeregisters(server=server, *args, **kwargs)
            self.assertStatusChanges([
                ConnStatusAPI.Status.DEREGISTERING,
                ConnStatusAPI.Status.DEREGISTERED], timeout_s=2)

        def tearDown(self, *args, **kwargs):
            self.assertNoOutstandingStatusChanges()
            super().tearDown(*args, **kwargs)


class DefaultRegDeregUDP(ConnStatusAPI.AutoRegDeregTestMixin,
                         test_suite.Lwm2mSingleServerTest):
    pass


class DefaultRegDeregDTLS(ConnStatusAPI.AutoRegDeregTestMixin,
                          test_suite.Lwm2mDtlsSingleServerTest):
    pass


class DefaultRegDeregTCP(ConnStatusAPI.AutoRegDeregTestMixin,
                         test_suite.Lwm2mSingleTcpServerTest):
    def setUp(self, *args, **kwargs):
        super().setUp(binding='T', *args, **kwargs)


class DefaultRegDeregTLS(ConnStatusAPI.AutoRegDeregTestMixin,
                         test_suite.Lwm2mTlsSingleServerTest):
    def setUp(self, *args, **kwargs):
        super().setUp(binding='T', *args, **kwargs)


class ServerStaysInRegisteringStateDuringRetries(
        ConnStatusAPI.AutoRegDeregTestMixin, RegisterUdp.TestCase):
    def runTest(self):
        self.assertDemoRegisters(respond=False)
        self.assertNoOutstandingStatusChanges()
        self.assertDemoRegisters(
            initial=False, first_attempt=False, respond=False, timeout_s=6)
        self.assertNoOutstandingStatusChanges()
        self.assertDemoRegisters(
            initial=False, first_attempt=False, timeout_s=12)


class RejectedRegisterChangesStateToRegFailure(ConnStatusAPI.AutoRegDeregTestMixin,
                                               test_suite.Lwm2mSingleServerTest):
    def setUp(self, *args, **kwargs):
        super().setUp(auto_register=False, *args, **kwargs)

    def runTest(self):
        self.assertDemoRegisters(reject=True)

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class ConnectFailureChangesStateToError(ConnStatusAPI.AutoRegDeregTestMixin, test_suite.Lwm2mSingleTcpServerTest):
    def setUp(self, *args, **kwargs):
        super().setUp(binding='T', auto_register=False, *args, **kwargs)

    def runTest(self):
        self.assertStatusChanges(
            [ConnStatusAPI.Status.INITIAL, ConnStatusAPI.Status.CONNECTING], timeout_s=2)
        self.serv.close()
        self.assertStatusChanges([ConnStatusAPI.Status.ERROR], timeout_s=2)

    def tearDown(self):
        super().tearDown(auto_deregister=False)


class UpdatesChangeStateToUpdating(ConnStatusAPI.AutoRegDeregTestMixin,
                                   test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.assertNoOutstandingStatusChanges()

        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()
        LIFETIME = 2

        self.serv.send(Lwm2mWrite(ResPath.Server[1].Lifetime, str(LIFETIME)))
        self.serv.recv()

        self.assertDemoUpdatesRegistration(lifetime=LIFETIME)

        self.assertNoOutstandingStatusChanges()

        # wait for auto-scheduled Update
        self.assertDemoUpdatesRegistration(timeout_s=LIFETIME)


class UpdateFailureChangesStateToReregistering(ConnStatusAPI.AutoRegDeregTestMixin, test_suite.Lwm2mSingleServerTest):
    LIFETIME = 4

    def setUp(self):
        super().setUp(auto_register=False, lifetime=self.LIFETIME,
                      extra_cmdline_args=['--ack-random-factor', '1', '--ack-timeout', '1',
                                          '--max-retransmit', '1'])
        self.assertDemoRegisters(lifetime=self.LIFETIME)

    def runTest(self):
        self.assertDemoUpdatesRegistration(timeout_s=self.LIFETIME / 2 + 1)

        self.assertDemoUpdatesRegistration(
            timeout_s=self.LIFETIME / 2 + 1, respond=False)
        self.assertDemoUpdatesRegistration(
            timeout_s=self.LIFETIME / 2 + 1, first_attempt=False, respond=False)
        self.assertDemoRegisters(lifetime=self.LIFETIME, timeout_s=self.LIFETIME /
                                 2 + 1, initial=False, first_attempt=False, reregister=True)


class DtlsReregisterFailureDoesntReportRegFailure(ConnStatusAPI.AutoRegDeregTestMixin,
                                                  RetransmissionTest.TestMixin,
                                                  test_suite.Lwm2mDtlsSingleServerTest):
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 1

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        # force re-registration;
        # Register only falls back to handshake if it's not performed immediately after one
        self.communicate('send-update')
        pkt = self.assertDemoUpdatesRegistration(respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)
                       (code=coap.Code.RES_NOT_FOUND))

        # Ignore register requests.
        for i in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoRegisters(
                respond=False,
                timeout_s=self.last_retransmission_timeout(),
                initial=False,
                first_attempt=False,
                reregister=i == 0)

        self.assertNoOutstandingStatusChanges()
        self.wait_for_retransmission_response_timeout()

        # Demo should fall back to DTLS handshake.
        self.assertPktIsDtlsClientHello(
            self.serv._raw_udp_socket.recv(4096), seq_number=0)
        self.assertStatusChanges([ConnStatusAPI.Status.CONNECTING])

        self.wait_for_retransmission_response_timeout()
        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())
        self.assertStatusChanges([ConnStatusAPI.Status.ERROR])
        self.assertNoOutstandingStatusChanges()


class DeregisterFailure:
    class TestMixin(ConnStatusAPI.AutoRegDeregTestMixin, DeregisterIcmp.TestMixin):
        def tearDown(self):
            self.assertStatusChanges(
                [ConnStatusAPI.Status.DEREGISTERING,
                 ConnStatusAPI.Status.ERROR], timeout_s=2)
            super().tearDown()


class DeregisterFailureChangesStateToError(DeregisterFailure.TestMixin,
                                           test_suite.Lwm2mSingleServerTest):
    pass


class DtlsDeregisterFailureChangesStateToError(DeregisterFailure.TestMixin,
                                               test_suite.Lwm2mDtlsSingleServerTest):
    pass


class DisablingServerRemotely(
        ConnStatusAPI.AutoRegDeregTestMixin, test_suite.Lwm2mSingleServerTest):
    def assertSocketsPolled(self, num):
        self.assertEqual(num, self.get_socket_count())

    def runTest(self):
        self.assertNoOutstandingStatusChanges()

        # Write Disable Timeout
        req = Lwm2mWrite(ResPath.Server[1].DisableTimeout, '6')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertNoOutstandingStatusChanges()

        # Execute Disable
        req = Lwm2mExecute(ResPath.Server[1].Disable)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())
        self.assertStatusChanges(
            [ConnStatusAPI.Status.SUSPENDING], timeout_s=2)

        self.assertDemoDeregisters(timeout_s=5)
        self.assertStatusChanges([ConnStatusAPI.Status.SUSPENDED], timeout_s=2)

        # no message for now
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))

        self.assertSocketsPolled(0)
        self.assertFalse(self.ongoing_registration_exists())

        # we should get another Register
        self.assertDemoRegisters(initial=False, timeout_s=3)

        # no message for now
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))

        self.assertSocketsPolled(1)


class DisablingSecondTimeDoesntChangeState(ConnStatusAPI.TestMixin,
                                           test_suite.Lwm2mTest,
                                           test_suite.Lwm2mDmOperations):
    def setUp(self, **kwargs):
        super().setUp(servers=2, extra_cmdline_args=['--access-entry',
                                                     '/%d/1,2,%d' % (OID.Server, AccessMask.OWNER)])

    def runTest(self):
        expected_states = []

        for ssid in range(1, 3):
            expected_states.append((ssid, ConnStatusAPI.Status.INITIAL))
        for ssid in range(1, 3):
            expected_states.append((ssid, ConnStatusAPI.Status.CONNECTING))
            expected_states.append((ssid, ConnStatusAPI.Status.REGISTERING))
        for ssid in range(1, 3):
            expected_states.append((ssid, ConnStatusAPI.Status.REGISTERED))

        self.assertStatusChanges(expected_states, timeout_s=2)
        self.assertNoOutstandingStatusChanges()

        self.write_resource(
            self.servers[1], OID.Server, 1, RID.Server.DisableTimeout, b'6')
        self.execute_resource(
            self.servers[1], OID.Server, 1, RID.Server.Disable)
        first_disable_timestamp = time.time()
        self.assertDemoDeregisters(self.servers[0])

        self.assertStatusChanges([
            ConnStatusAPI.Status.SUSPENDING,
            ConnStatusAPI.Status.DEREGISTERING,
            ConnStatusAPI.Status.DEREGISTERED,
            ConnStatusAPI.Status.SUSPENDED], timeout_s=2)
        self.assertNoOutstandingStatusChanges()

        # no message for now
        with self.assertRaises(socket.timeout):
            print(self.servers[0].recv(timeout_s=3))

        # execute Disable again, this should reset the timer
        self.execute_resource(
            self.servers[1], OID.Server, 1, RID.Server.Disable)
        with self.assertRaises(socket.timeout):
            print(self.servers[0].recv(timeout_s=5))

        self.assertNoOutstandingStatusChanges()

        self.assertFalse(self.ongoing_registration_exists())

        # only now the server should re-register
        self.assertDemoRegisters(server=self.servers[0], timeout_s=3)
        register_timestamp = time.time()

        self.assertGreater(register_timestamp - first_disable_timestamp, 8)

        self.assertStatusChanges([
            ConnStatusAPI.Status.CONNECTING,
            ConnStatusAPI.Status.REGISTERING,
            ConnStatusAPI.Status.REGISTERED], timeout_s=2)
        self.assertNoOutstandingStatusChanges()


class ShutdownDuringQueueModeChangesStateDirectlyToDeregistered(ConnStatusAPI.AutoRegDeregTestMixin,
                                                                QueueMode.Test,
                                                                test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        self.skipIfFeatureStatus('ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE = ON',
                                 'If socket autoclose is disabled, the client deregisters as usual')
        super().setUp()

    def runTest(self):
        self.assertNoOutstandingStatusChanges()

        self.communicate('set-queue-mode-preference FORCE_QUEUE_MODE')

        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        # # await for the client to close the socket
        time.sleep(self.max_transmit_wait() - 2)
        self.assertEqual(self.get_socket_count(), 1)
        time.sleep(4)
        self.assertEqual(self.get_socket_count(), 0)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def _terminate_demo(self, *args, **kwargs):
        # since the state change is induced during shutdown of the client, this
        # is the only way to catch this state
        self.assertStatusChanges(
            [ConnStatusAPI.Status.DEREGISTERED], timeout_s=2)
        super()._terminate_demo(*args, **kwargs)




class IcmpErrorDuringRegisterCausesError(ConnStatusAPI.AutoRegDeregTestMixin,
                                         test_suite.PcapEnabledTest,
                                         RetransmissionTest.TestMixin,
                                         test_suite.Lwm2mSingleServerTest):
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 4

    def setUp(self):
        super().setUp(auto_register=False)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.assertDemoRegisters()

        # Close socket to induce ICMP port unreachable.
        with self.serv.fake_close():
            # Force Register
            self.communicate('reconnect')
            self.assertStatusChanges([
                ConnStatusAPI.Status.CONNECTING,
                ConnStatusAPI.Status.REGISTERING], timeout_s=2)
            # Wait for ICMP port unreachable.
            self.wait_until_icmp_unreachable_count(
                1, timeout_s=self.last_retransmission_timeout())

        # Ensure that the control is given back to the user.
        with self.assertRaises(socket.timeout, msg="unexpected packets from the client"):
            self.serv.recv()

        self.assertStatusChanges([ConnStatusAPI.Status.ERROR], timeout_s=2)

        self.assertEqual(0, self.get_socket_count())
        self.assertTrue(self.get_all_connections_failed())

        self.assertNoOutstandingStatusChanges()


class RegisterTimeoutRegFailure:
    class TestMixin(ConnStatusAPI.AutoRegDeregTestMixin, RegisterTimeout.TestMixin):
        MAX_RETRANSMIT = 3

        def runTest(self):
            # Required for DTLS variant, in which this completes a handshake
            # which is a part of connect() call.
            self.serv.listen(timeout_s=10)

            # Ignore register requests.
            for i in range(self.MAX_RETRANSMIT + 1):
                self.assertDemoRegisters(respond=False,
                                         timeout_s=self.last_retransmission_timeout() + 5,
                                         initial=i == 0,
                                         first_attempt=i == 0)

            self.assertNoOutstandingStatusChanges()
            self.wait_for_retransmission_response_timeout()

            self.assertStatusChanges(
                [ConnStatusAPI.Status.REG_FAILURE], timeout_s=2)

            # Ensure that server is considered unreachable, and control given back to the user.
            self.assertEqual(0, self.get_socket_count())
            self.assertTrue(self.get_all_connections_failed())

            self.assertNoOutstandingStatusChanges()


class UdpRegisterTimeoutCausesRegFailure(RegisterTimeoutRegFailure.TestMixin,
                                         test_suite.Lwm2mSingleServerTest):
    pass


class DtlsRegisterTimeoutCausesRegFailure(RegisterTimeoutRegFailure.TestMixin,
                                          test_suite.Lwm2mDtlsSingleServerTest):
    pass


class ReportsErrorIfRegistrationFailsDueToNetworkIssues(ConnStatusAPI.AutoRegDeregTestMixin,
                                                        test_suite.PcapEnabledTest,
                                                        test_suite.Lwm2mDtlsSingleServerTest):
    RETRY_COUNT = 3
    MAX_RETRANSMIT = 1

    def setUp(self):
        extra_cmdline_args = ['--retry-count', str(self.RETRY_COUNT), '--max-retransmit',
                              str(self.MAX_RETRANSMIT), '--ack-timeout', str(1)]
        super().setUp(extra_cmdline_args=extra_cmdline_args, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        for i in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoRegisters(respond=False, timeout_s=5, initial=i == 0, first_attempt=i == 0)

        num_initial_dtls_hs_packets = self.count_dtls_client_hello_packets()
        # Close socket to induce ICMP port unreachable.
        self.serv.close()

        # Wait for ICMP port unreachable.
        self.wait_until_icmp_unreachable_count(1, timeout_s=5)

        self.assertStatusChanges([
            ConnStatusAPI.Status.REG_FAILURE,
            ConnStatusAPI.Status.CONNECTING,
            ConnStatusAPI.Status.ERROR], timeout_s=2)

        time.sleep(5)
        # Ensure that no more retransmissions occurred.
        self.assertEqual(1, self.count_icmp_unreachable_packets())
        # Ensure that only one more dtls handshake messages occurred.
        self.assertEqual(num_initial_dtls_hs_packets + 1,
                         self.count_dtls_client_hello_packets())

        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())
        self.assertNoOutstandingStatusChanges()


class ReportsRegFailureIfConnectionErrorIsRegFailure(ConnStatusAPI.AutoRegDeregTestMixin,
                                                     test_suite.PcapEnabledTest,
                                                     test_suite.Lwm2mDtlsSingleServerTest):
    RETRY_COUNT = 3
    MAX_RETRANSMIT = 1

    def setUp(self):
        extra_cmdline_args = ['--retry-count', str(self.RETRY_COUNT), '--max-retransmit',
                              str(self.MAX_RETRANSMIT), '--ack-timeout', str(1),
                              '--connection-error-is-registration-failure']
        super().setUp(extra_cmdline_args=extra_cmdline_args, auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        for i in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoRegisters(respond=False, timeout_s=5, initial=i == 0, first_attempt=i == 0)

        num_initial_dtls_hs_packets = self.count_dtls_client_hello_packets()
        # Close socket to induce ICMP port unreachable.
        self.serv.close()

        # Wait for ICMP port unreachable.
        self.wait_until_icmp_unreachable_count(1, timeout_s=5)

        self.assertStatusChanges([
            ConnStatusAPI.Status.REG_FAILURE,
            ConnStatusAPI.Status.CONNECTING], timeout_s=2)

        # And another one - the third registration attempt
        self.wait_until_icmp_unreachable_count(1, timeout_s=5)

        self.assertStatusChanges([
            ConnStatusAPI.Status.REG_FAILURE,
            ConnStatusAPI.Status.CONNECTING], timeout_s=2)

        time.sleep(5)
        # Ensure that no more retransmissions occurred.
        self.assertEqual(2, self.count_icmp_unreachable_packets())
        # Ensure that only one more dtls handshake messages occurred.
        self.assertEqual(num_initial_dtls_hs_packets + 2,
                         self.count_dtls_client_hello_packets())

        self.assertStatusChanges([ConnStatusAPI.Status.REG_FAILURE])

        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())
        self.assertNoOutstandingStatusChanges()


class Bootstrap:
    class TestMixin(ConnStatusAPI.AutoRegDeregTestMixin):
        def assertDemoRequestsBootstrap(self, *args, **kwargs):
            super().assertDemoRequestsBootstrap(*args, **kwargs)
            self.assertStatusChanges([
                ConnStatusAPI.Status.INITIAL,
                ConnStatusAPI.Status.CONNECTING,
                ConnStatusAPI.Status.BOOTSTRAPPING], default_ssid=65535, timeout_s=2)

        def perform_bootstrap_finish(self, *args, **kwargs):
            super().perform_bootstrap_finish(*args, **kwargs)
            self.assertStatusChanges(
                [ConnStatusAPI.Status.BOOTSTRAPPED], default_ssid=65535, timeout_s=2)


class DefaultBootstrapTest(Bootstrap.TestMixin, BootstrapTest.Test):
    def setUp(self):
        super().setUp(holdoff_s=3, timeout_s=3)

    def runTest(self):
        self.perform_typical_bootstrap(server_iid=1,
                                       security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       lifetime=60)

        # should now register with the non-bootstrap server
        self.assertDemoRegisters(self.serv, lifetime=60)


class BootstrapNoResponseTest(Bootstrap.TestMixin, BootstrapTest.Test):
    ACK_TIMEOUT = 1
    MAX_RETRANSMIT = 1

    def setUp(self):
        # Done to have a relatively short EXCHANGE_LIFETIME
        super().setUp(extra_cmdline_args=['--ack-random-factor', '1',
                                          '--ack-timeout', '%s' % self.ACK_TIMEOUT,
                                          '--max-retransmit', '%s' % self.MAX_RETRANSMIT])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # We should get Bootstrap Request now
        self.assertDemoRequestsBootstrap()

        self.assertEqual(1, self.get_socket_count())
        self.advance_demo_time(TxParams(ack_timeout=self.ACK_TIMEOUT,
                                        max_retransmit=self.MAX_RETRANSMIT).exchange_lifetime())
        self.wait_until_socket_count(0, timeout_s=5)
        self.assertStatusChanges(
            [ConnStatusAPI.Status.ERROR], default_ssid=65535)
