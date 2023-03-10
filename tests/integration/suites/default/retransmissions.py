# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import contextlib
import time
import socket

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *
from suites.default import bootstrap_client
from suites.default.block_write import equal_chunk_splitter, packets_from_chunks, Block


class RetransmissionTest:
    class TestMixin:
        # Note that these values differ from default ones in Anjay. This is done to
        # speed up the test execution a bit by limiting the number of retransmissions
        # as well as wait intervals between them.
        ACK_RANDOM_FACTOR=1.0
        ACK_TIMEOUT=2.0
        MAX_RETRANSMIT=2
        CONFIRMABLE_NOTIFICATIONS=False

        def tx_params(self):
            return TxParams(ack_random_factor=self.ACK_RANDOM_FACTOR,
                            ack_timeout=self.ACK_TIMEOUT,
                            max_retransmit=self.MAX_RETRANSMIT)

        def setUp(self, *args, **kwargs):
            extra_cmdline_args=[
                '--ack-random-factor', str(self.tx_params().ack_random_factor),
                '--ack-timeout', str(self.tx_params().ack_timeout),
                '--max-retransmit', str(self.tx_params().max_retransmit),
                '--dtls-hs-retry-wait-min', str(self.tx_params().first_retransmission_timeout()),
                '--dtls-hs-retry-wait-max', str(self.tx_params().last_retransmission_timeout()),
            ]
            if self.CONFIRMABLE_NOTIFICATIONS:
                extra_cmdline_args += ['--confirmable-notifications']
            if 'extra_cmdline_args' in kwargs:
                kwargs['extra_cmdline_args'] = extra_cmdline_args + kwargs['extra_cmdline_args']
                super().setUp(*args, **kwargs)
            else:
                super().setUp(*args, **kwargs, extra_cmdline_args=extra_cmdline_args)

        def last_retransmission_timeout(self):
            return self.tx_params().last_retransmission_timeout()

        def wait_for_retransmission_response_timeout(self, margin_s=1.0):
            time.sleep(self.last_retransmission_timeout() + margin_s)

        def max_transmit_wait(self):
            return self.tx_params().max_transmit_wait()


class DtlsHsFailOnIcmpTest(test_suite.PcapEnabledTest,
                           RetransmissionTest.TestMixin,
                           test_suite.Lwm2mDtlsSingleServerTest):
    def setup_demo_with_servers(self, **kwargs):
        for server in kwargs['servers']:
            self._server_close_stack.enter_context(server.fake_close())
        super().setup_demo_with_servers(**kwargs)

    def setUp(self):
        self._server_close_stack = contextlib.ExitStack()
        super().setUp(auto_register=False)

    def tearDown(self):
        self._server_close_stack.close()  # in case runTest() failed
        super().tearDown()

    def runTest(self):
        # wait until ultimate failure
        self.wait_until_icmp_unreachable_count(1, timeout_s=3)

        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())

        # attempt reconnection
        self._server_close_stack.close()  # unclose the server socket
        self.communicate('reconnect')
        self.assertDemoRegisters(self.serv, timeout_s=8)
        self.assertEqual(1, self.count_icmp_unreachable_packets())


class DtlsHsRetryOnTimeoutTest(RetransmissionTest.TestMixin,
                               test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        for i in range(self.MAX_RETRANSMIT + 1):
            self.assertPktIsDtlsClientHello(self.serv._raw_udp_socket.recv(4096), seq_number=i)

        self.wait_for_retransmission_response_timeout()
        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())


class DtlsRegisterTimeoutFallbacksToHsTest(RetransmissionTest.TestMixin,
                                           test_suite.Lwm2mDtlsSingleServerTest):
    # These settings speed up tests considerably.
    MAX_RETRANSMIT=1
    ACK_TIMEOUT=1

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        # force re-registration;
        # Register only falls back to handshake if it's not performed immediately after one
        self.communicate('send-update')
        pkt = self.assertDemoUpdatesRegistration(respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_NOT_FOUND))

        # Ignore register requests.
        for _ in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoRegisters(respond=False, timeout_s=self.last_retransmission_timeout())

        self.wait_for_retransmission_response_timeout()

        # Demo should fall back to DTLS handshake.
        self.assertPktIsDtlsClientHello(self.serv._raw_udp_socket.recv(4096), seq_number=0)

        self.wait_for_retransmission_response_timeout()
        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())


class RegisterTimeout:
    class TestMixin(RetransmissionTest.TestMixin):
        # These settings speed up tests considerably.
        MAX_RETRANSMIT = 1
        ACK_TIMEOUT = 1

        def setUp(self):
            super().setUp(auto_register=False)

        def tearDown(self):
            super().teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            # Ignore register requests.
            for _ in range(self.MAX_RETRANSMIT + 1):
                self.assertDemoRegisters(respond=False,
                                         timeout_s=self.last_retransmission_timeout())

            self.wait_for_retransmission_response_timeout()

            # Ensure that server is considered unreachable, and control given back to the user.
            self.assertEqual(0, self.get_socket_count())
            self.assertTrue(self.get_all_connections_failed())


class DtlsRegisterFirstTimeoutFailsTest(RegisterTimeout.TestMixin,
                                        test_suite.Lwm2mDtlsSingleServerTest):
    pass


class RegisterTimeoutFails(RegisterTimeout.TestMixin,
                           test_suite.Lwm2mSingleServerTest):
    pass


class DtlsRegisterFailsOnIcmpTest(test_suite.PcapEnabledTest,
                                  RetransmissionTest.TestMixin,
                                  test_suite.Lwm2mDtlsSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.assertDemoRegisters(respond=False)
        # Give dumpcap a little bit of time to write to dump file.
        time.sleep(self.ACK_TIMEOUT / 2)
        num_initial_dtls_hs_packets = self.count_dtls_client_hello_packets()
        # Close socket to induce ICMP port unreachable.
        self.serv.close()

        # Wait for ICMP port unreachable.
        self.wait_until_icmp_unreachable_count(1, timeout_s=self.last_retransmission_timeout())

        self.wait_for_retransmission_response_timeout()
        # Ensure that no more retransmissions occurred.
        self.assertEqual(1, self.count_icmp_unreachable_packets())
        # Ensure that no more dtls handshake messages occurred.
        self.assertEqual(num_initial_dtls_hs_packets, self.count_dtls_client_hello_packets())

        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())


class RegisterIcmpTest(test_suite.PcapEnabledTest,
                       RetransmissionTest.TestMixin,
                       test_suite.Lwm2mSingleServerTest):
    MAX_RETRANSMIT=1
    ACK_TIMEOUT=4

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
            # Wait for ICMP port unreachable.
            self.wait_until_icmp_unreachable_count(1, timeout_s=self.last_retransmission_timeout())

        # Ensure that the control is given back to the user.
        with self.assertRaises(socket.timeout, msg="unexpected packets from the client"):
            self.serv.recv()

        self.assertEqual(0, self.get_socket_count())
        self.assertTrue(self.get_all_connections_failed())


class DeregisterTimeout:
    class TestMixin(RetransmissionTest.TestMixin):
        def tearDown(self):
            super().teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            self.communicate('trim-servers 0')
            for _ in range(self.MAX_RETRANSMIT + 1):
                self.assertMsgEqual(Lwm2mDeregister(self.DEFAULT_REGISTER_ENDPOINT),
                                    self.serv.recv(timeout_s=self.last_retransmission_timeout()))

            self.wait_for_retransmission_response_timeout()
            self.assertEqual(0, self.get_socket_count())


class DtlsDeregisterTimeoutTest(DeregisterTimeout.TestMixin,
                                test_suite.Lwm2mDtlsSingleServerTest):
    pass


class DeregisterTimeoutTest(DeregisterTimeout.TestMixin,
                            test_suite.Lwm2mSingleServerTest):
    pass


class DeregisterIcmp:
    class TestMixin(test_suite.PcapEnabledTest,
                    RetransmissionTest.TestMixin):
        def tearDown(self):
            super().teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            # Close socket to induce ICMP port unreachables.
            self.serv.close()
            self.communicate('trim-servers 0')
            self.wait_until_icmp_unreachable_count(1, timeout_s=2 * self.last_retransmission_timeout())
            # Give demo time to realize deregister failed.
            time.sleep(self.ACK_TIMEOUT * self.ACK_RANDOM_FACTOR)
            # Ensure that no more retransmissions occurred.
            self.assertEqual(1, self.count_icmp_unreachable_packets())
            self.assertEqual(0, self.get_socket_count())


class DtlsDeregisterIcmpTest(DeregisterIcmp.TestMixin,
                             test_suite.Lwm2mDtlsSingleServerTest):
    pass


class DeregisterIcmpTest(DeregisterIcmp.TestMixin,
                         test_suite.Lwm2mSingleServerTest):
    pass


class DtlsUpdateTimeoutFallbacksToRegisterTest(RetransmissionTest.TestMixin,
                                               test_suite.Lwm2mDtlsSingleServerTest,
                                               test_suite.Lwm2mDmOperations):
    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        new_lifetime = int(2 * self.max_transmit_wait())
        # Change lifetime to 2*MAX_TRANSMIT_WAIT
        self.write_resource(self.serv, oid=OID.Server, iid=1, rid=RID.Server.Lifetime, content=str(new_lifetime))
        self.assertDemoUpdatesRegistration(lifetime=new_lifetime)
        # Demo should attempt to update registration.
        for _ in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoUpdatesRegistration(respond=False, timeout_s=new_lifetime)
        self.wait_for_retransmission_response_timeout()

        # Demo should re-register
        for _ in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoRegisters(respond=False, lifetime=new_lifetime, timeout_s=self.max_transmit_wait())
        self.wait_for_retransmission_response_timeout()

        self.serv._raw_udp_socket.settimeout(self.last_retransmission_timeout() + 1)
        # Demo should attempt handshake
        for i in range(self.MAX_RETRANSMIT + 1):
            self.assertPktIsDtlsClientHello(self.serv._raw_udp_socket.recv(4096))
        self.wait_for_retransmission_response_timeout()

        with self.assertRaises(socket.timeout, msg="unexpected packets from the client"):
            self.serv._raw_udp_socket.recv(4096)

        self.assertEqual(0, self.get_socket_count())


class UpdateTimeoutFallbacksToRegisterTest(RetransmissionTest.TestMixin,
                                           test_suite.Lwm2mSingleServerTest,
                                           test_suite.Lwm2mDmOperations):
    def runTest(self):
        new_lifetime = int(2 * self.max_transmit_wait())
        # Change lifetime to 2*MAX_TRANSMIT_WAIT
        self.write_resource(self.serv, oid=OID.Server, iid=1, rid=RID.Server.Lifetime, content=str(new_lifetime))
        self.assertDemoUpdatesRegistration(lifetime=new_lifetime)
        # Demo should attempt to update registration.
        for _ in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoUpdatesRegistration(respond=False, timeout_s=new_lifetime)
        self.wait_for_retransmission_response_timeout()

        # Demo should re-register
        self.assertDemoRegisters(lifetime=new_lifetime)


class UpdateTimeoutWithQueueModeTest(RetransmissionTest.TestMixin,
                                     test_suite.Lwm2mSingleServerTest,
                                     test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(binding='UQ', extra_cmdline_args=['--binding=UQ'])

    def runTest(self):
        self.communicate('send-update')
        for _ in range(self.MAX_RETRANSMIT + 1):
            self.assertDemoUpdatesRegistration(respond=False, timeout_s=2 * self.max_transmit_wait())
        self.wait_for_retransmission_response_timeout()

        # Demo should re-register
        self.assertDemoRegisters(binding='UQ')


class UpdateFailsOnIcmpTest:
    class TestMixin(test_suite.PcapEnabledTest,
                    RetransmissionTest.TestMixin,
                    test_suite.Lwm2mDmOperations):
        def tearDown(self):
            super().teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            new_lifetime = int(2 * self.max_transmit_wait())
            # Change lifetime to 2*MAX_TRANSMIT_WAIT
            self.write_resource(self.serv, oid=OID.Server, iid=1, rid=RID.Server.Lifetime,
                                content=str(new_lifetime))
            self.assertDemoUpdatesRegistration(lifetime=new_lifetime)
            # Give dumpcap a little bit of time to write to dump file.
            time.sleep(self.ACK_TIMEOUT / 2)
            num_initial_dtls_hs_packets = self.count_dtls_client_hello_packets()

            self.serv.close()

            # Wait for ICMP port unreachable.
            self.wait_until_icmp_unreachable_count(1, timeout_s=new_lifetime)

            self.wait_for_retransmission_response_timeout()
            # Ensure that no more retransmissions occurred.
            self.assertEqual(1, self.count_icmp_unreachable_packets())
            # Ensure that no more dtls handshake messages occurred.
            self.assertEqual(num_initial_dtls_hs_packets, self.count_dtls_client_hello_packets())

            # Ensure that the control is given back to the user.
            self.assertTrue(self.get_all_connections_failed())


class DtlsUpdateIcmpTest(UpdateFailsOnIcmpTest.TestMixin,
                         test_suite.Lwm2mDtlsSingleServerTest):
    pass


class UpdateIcmpTest(UpdateFailsOnIcmpTest.TestMixin,
                     test_suite.Lwm2mSingleServerTest):
    pass


class DtlsRequestBootstrapTimeoutFallbacksToHsTest(RetransmissionTest.TestMixin,
                                                   test_suite.Lwm2mDtlsSingleServerTest):
    # These settings speed up tests considerably.
    MAX_RETRANSMIT=1
    ACK_TIMEOUT=1

    def setUp(self):
        super().setUp(bootstrap_server=Lwm2mServer(coap.DtlsServer(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY)),
                      auto_register=False)
        self.serv.listen()
        self.bootstrap_server.listen()
        self.assertDemoRegisters(self.serv)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        # force Client-Initiated Bootstrap;
        # Request Bootstrap only falls back to handshake if it's not performed immediately after one
        self.communicate('send-update')
        pkt = self.assertDemoUpdatesRegistration(respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_FORBIDDEN))
        pkt = self.assertDemoRegisters(respond=False)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_FORBIDDEN))

        # Ignore Request Bootstrap requests.
        for _ in range(self.MAX_RETRANSMIT + 1):
            pkt = self.bootstrap_server.recv(timeout_s=self.last_retransmission_timeout())
            self.assertIsInstance(pkt, Lwm2mRequestBootstrap)

        self.wait_for_retransmission_response_timeout()

        # Demo should fall back to DTLS handshake.
        self.assertPktIsDtlsClientHello(self.bootstrap_server._raw_udp_socket.recv(4096), seq_number=0)

        self.wait_for_retransmission_response_timeout()
        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())


class RequestBootstrapTimeoutFails(RetransmissionTest.TestMixin,
                                   test_suite.Lwm2mTest):
    # These settings speed up tests considerably.
    MAX_RETRANSMIT = 1
    ACK_TIMEOUT = 1

    def setUp(self, bootstrap_server=True, *args, **kwargs):
        super().setUp(servers=0, bootstrap_server=bootstrap_server, *args, **kwargs)

    def runTest(self):
        # Ignore Request Bootstrap requests.
        for _ in range(self.MAX_RETRANSMIT + 1):
            pkt = self.bootstrap_server.recv(timeout_s=self.last_retransmission_timeout())
            self.assertIsInstance(pkt, Lwm2mRequestBootstrap)

        self.wait_for_retransmission_response_timeout()

        # Ensure that server is considered unreachable, and control given back to the user.
        self.assertEqual(0, self.get_socket_count())
        self.assertTrue(self.get_all_connections_failed())


class DtlsRequestBootstrapFirstTimeoutFailsTest(RequestBootstrapTimeoutFails):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        super().setUp(bootstrap_server=Lwm2mServer(coap.DtlsServer(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY)),
                      extra_cmdline_args=['--identity', str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                          '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')])


class DtlsRequestBootstrapFailsOnIcmpTest(test_suite.PcapEnabledTest,
                                          RetransmissionTest.TestMixin,
                                          test_suite.Lwm2mDtlsSingleServerTest):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        super().setUp(bootstrap_server=Lwm2mServer(coap.DtlsServer(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY)),
                      auto_register=False)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.serv.listen()
        self.bootstrap_server.listen()
        pkt = self.assertDemoRegisters(respond=False)
        # Give dumpcap a little bit of time to write to dump file.
        time.sleep(self.ACK_TIMEOUT / 2)
        num_initial_dtls_hs_packets = self.count_dtls_client_hello_packets()
        # Close the socket to induce ICMP port unreachable
        self.bootstrap_server.close()

        # respond with Forbidden to Register so that client falls back to Bootstrap
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_FORBIDDEN))

        # Wait for ICMP port unreachable.
        self.wait_until_icmp_unreachable_count(1, timeout_s=self.last_retransmission_timeout())

        self.wait_for_retransmission_response_timeout()
        # Ensure that no more retransmissions occurred.
        self.assertEqual(1, self.count_icmp_unreachable_packets())
        # Ensure that no more dtls handshake messages occurred.
        self.assertEqual(num_initial_dtls_hs_packets, self.count_dtls_client_hello_packets())

        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())


class RequestBootstrapIcmpTest(test_suite.PcapEnabledTest,
                               RetransmissionTest.TestMixin,
                               test_suite.Lwm2mTest):
    MAX_RETRANSMIT=1
    ACK_TIMEOUT=4

    def setUp(self):
        super().setUp(servers=0, bootstrap_server=True)

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        pkt = self.bootstrap_server.recv()
        self.assertIsInstance(pkt, Lwm2mRequestBootstrap)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())
        # Close socket to induce ICMP port unreachable.
        with self.bootstrap_server.fake_close():
            # Force Register
            self.communicate('reconnect')
            # Wait for ICMP port unreachable.
            self.wait_until_icmp_unreachable_count(1, timeout_s=self.last_retransmission_timeout())

        # Ensure that the control is given back to the user.
        with self.assertRaises(socket.timeout, msg="unexpected packets from the client"):
            self.bootstrap_server.recv()

        self.assertEqual(0, self.get_socket_count())
        self.assertTrue(self.get_all_connections_failed())


# Tests below check that Anjay does not go crazy when faced with network connection problems while attempting to send
# Notify messages. Some previous versions could easily get into an infinite loop of repeating the Notify message without
# any backoff, and so on - so we test that the behaviour is sane.


class NotificationIcmpTest(test_suite.PcapEnabledTest,
                           RetransmissionTest.TestMixin,
                           test_suite.Lwm2mSingleServerTest,
                           test_suite.Lwm2mDmOperations):
    CONFIRMABLE_NOTIFICATIONS=True

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)
        self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Timestamp)

        with self.serv.fake_close():
            self.wait_until_icmp_unreachable_count(1)

        # client should give up on retransmitting the notification after ICMP error
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=self.last_retransmission_timeout() + 3)
        self.assertEqual(1, self.count_icmp_unreachable_packets())

        # the client should not attempt to reach the server again
        self.assertTrue(self.get_all_connections_failed())
        self.assertEqual(0, self.get_socket_count())


class NotificationDtlsFailsOnIcmpTest(test_suite.PcapEnabledTest,
                                      RetransmissionTest.TestMixin,
                                      test_suite.Lwm2mDtlsSingleServerTest,
                                      test_suite.Lwm2mDmOperations):
    CONFIRMABLE_NOTIFICATIONS=True

    def tearDown(self):
        super().teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.create_instance(self.serv, oid=OID.Test, iid=1)

        # force an Update so that change to the data model does not get notified later
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(content=ANY)
        # Give dumpcap a little bit of time to write to dump file.
        time.sleep(self.ACK_TIMEOUT / 2)
        num_initial_dtls_hs_packets = self.count_dtls_client_hello_packets()

        self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Timestamp)

        self.serv.close()
        self.wait_until_icmp_unreachable_count(1, timeout_s=8)

        self.wait_for_retransmission_response_timeout()
        # Ensure that no more retransmissions occurred.
        self.assertEqual(1, self.count_icmp_unreachable_packets())
        # Ensure that no more dtls handshake messages occurred.
        self.assertEqual(num_initial_dtls_hs_packets, self.count_dtls_client_hello_packets())

        # Ensure that the control is given back to the user.
        self.assertTrue(self.get_all_connections_failed())


class NotificationTimeoutIsIgnored:
    class TestMixin(RetransmissionTest.TestMixin,
                    test_suite.Lwm2mDmOperations):
        CONFIRMABLE_NOTIFICATIONS = True

        def runTest(self):
            # Trigger a CON notification
            self.create_instance(self.serv, oid=OID.Test, iid=1)

            # force an Update so that change to the data model does not get notified later
            self.communicate('send-update')
            self.assertDemoUpdatesRegistration(content=ANY)

            self.observe(self.serv, oid=OID.Test, iid=1, rid=RID.Test.Counter)
            self.execute_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.IncrementCounter)

            first_pkt = self.serv.recv(timeout_s=2)
            first_attempt = time.time()
            self.assertIsInstance(first_pkt, Lwm2mNotify)

            for attempt in range(self.MAX_RETRANSMIT):
                self.assertIsInstance(self.serv.recv(timeout_s=30), Lwm2mNotify)
            last_attempt = time.time()

            transmit_span_lower_bound = self.ACK_TIMEOUT * ((2 ** self.MAX_RETRANSMIT) - 1)
            transmit_span_upper_bound = transmit_span_lower_bound * self.ACK_RANDOM_FACTOR

            self.assertGreater(last_attempt - first_attempt, transmit_span_lower_bound - 1)
            self.assertLess(last_attempt - first_attempt, transmit_span_upper_bound + 1)

            time.sleep(self.last_retransmission_timeout() + 1)

            # check that following notifications still trigger attempts to send the value
            self.execute_resource(self.serv, oid=OID.Test, iid=1, rid=RID.Test.IncrementCounter)
            pkt = self.serv.recv(timeout_s=self.last_retransmission_timeout() + 2)
            self.assertIsInstance(pkt, Lwm2mNotify)
            self.assertEqual(pkt.content, first_pkt.content)
            self.serv.send(Lwm2mReset.matching(pkt)())



class NotificationDtlsTimeoutIsIgnoredTest(NotificationTimeoutIsIgnored.TestMixin,
                                           test_suite.Lwm2mDtlsSingleServerTest):
    pass


class NotificationTimeoutIsIgnoredTest(NotificationTimeoutIsIgnored.TestMixin,
                                       test_suite.Lwm2mSingleServerTest):
    pass


class ReplacedBootstrapServerReconnectTest(RetransmissionTest.TestMixin,
                                           test_suite.Lwm2mDtlsSingleServerTest,
                                           test_suite.Lwm2mDmOperations):
    MAX_RETRANSMIT=1
    ACK_TIMEOUT=1

    def setUp(self):
        super().setUp(bootstrap_server=Lwm2mServer(coap.DtlsServer(psk_identity=self.PSK_IDENTITY, psk_key=self.PSK_KEY)),
                      num_servers_passed=0)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        lifetime = 2 * self.max_transmit_wait()

        pkt = self.bootstrap_server.recv()
        self.assertIsInstance(pkt, Lwm2mRequestBootstrap)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        pkt = Lwm2mDiscover('/%d' % (OID.Security,))
        self.bootstrap_server.send(pkt)
        self.assertMsgEqual(Lwm2mContent.matching(pkt)(content=b'lwm2m="1.0",</%d>,</%d/1>' % (OID.Security, OID.Security)),
                            self.bootstrap_server.recv())

        # replace the existing instance
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=1,
                            content=TLV.make_resource(RID.Security.ServerURI, 'coaps://127.0.0.1:%d' % self.bootstrap_server.get_listen_port()).serialize()
                                     + TLV.make_resource(RID.Security.Bootstrap, 1).serialize()
                                     + TLV.make_resource(RID.Security.Mode, coap.server.SecurityMode.PreSharedKey.value).serialize()
                                     + TLV.make_resource(RID.Security.PKOrIdentity, self.PSK_IDENTITY).serialize()
                                     + TLV.make_resource(RID.Security.SecretKey, self.PSK_KEY).serialize())

        # provision the regular Server instance
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=2,
                            content=TLV.make_resource(RID.Security.ServerURI, 'coaps://127.0.0.1:%d' % self.serv.get_listen_port()).serialize()
                                     + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                     + TLV.make_resource(RID.Security.Mode, coap.server.SecurityMode.PreSharedKey.value).serialize()
                                     + TLV.make_resource(RID.Security.ShortServerID, 2).serialize()
                                     + TLV.make_resource(RID.Security.PKOrIdentity, self.PSK_IDENTITY).serialize()
                                     + TLV.make_resource(RID.Security.SecretKey, self.PSK_KEY).serialize())
        self.write_instance(self.bootstrap_server, oid=OID.Server, iid=2,
                            content=TLV.make_resource(RID.Server.Lifetime, int(lifetime)).serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID, 2).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring, True).serialize()
                                    + TLV.make_resource(RID.Server.Binding, "U").serialize()
                                    + TLV.make_resource(RID.Server.ServerCommunicationRetryCount, 1).serialize()
                                    + TLV.make_resource(RID.Server.ServerCommunicationSequenceRetryCount, 1).serialize()
                                    )

        # Bootstrap Finish
        pkt = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(pkt)
        self.assertMsgEqual(Lwm2mChanged.matching(pkt)(), self.bootstrap_server.recv())

        # demo will refresh Bootstrap connection...
        self.assertDtlsReconnect(self.bootstrap_server)

        # ...and Register with the regular server
        self.assertDemoRegisters(lifetime=int(lifetime))

        # let the Update fail
        self.assertIsInstance(self.serv.recv(timeout_s=lifetime), Lwm2mUpdate)
        self.assertIsInstance(self.serv.recv(timeout_s=self.ACK_TIMEOUT + 1), Lwm2mUpdate)

        # client falls back to Register
        pkt = self.serv.recv(timeout_s=lifetime)
        self.assertIsInstance(pkt, Lwm2mRegister)
        self.serv.send(Lwm2mErrorResponse.matching(pkt)(coap.Code.RES_FORBIDDEN))

        # now the client falls back to Bootstrap, and doesn't get response
        self.assertIsInstance(self.bootstrap_server.recv(), Lwm2mRequestBootstrap)
        self.assertIsInstance(self.bootstrap_server.recv(timeout_s=self.ACK_TIMEOUT + 1), Lwm2mRequestBootstrap)

        # rehandshake should appear here
        self.assertDtlsReconnect(self.bootstrap_server, timeout_s=2*self.ACK_TIMEOUT + 1)
        self.assertIsInstance(self.bootstrap_server.recv(), Lwm2mRequestBootstrap)


class ModifyingTxParams(RetransmissionTest.TestMixin, bootstrap_client.BootstrapTest.Test):
    def setUp(self):
        super().setUp(minimum_version='1.0', maximum_version='1.1')

    def runTest(self):
        # The client will attempt Request Bootstrap as version 1.1 (with pct= option)
        pkt1 = self.bootstrap_server.recv(timeout_s=self.max_transmit_wait())
        pkt1_time = time.time()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME,
                                                  preferred_content_format=coap.ContentFormat.APPLICATION_LWM2M_SENML_CBOR),
                            pkt1)

        pkt2 = self.bootstrap_server.recv(timeout_s=self.max_transmit_wait())
        pkt2_time = time.time()
        self.assertMsgEqual(pkt1, pkt2)

        pkt3 = self.bootstrap_server.recv(timeout_s=self.max_transmit_wait())
        pkt3_time = time.time()
        self.assertMsgEqual(pkt2, pkt3)

        # check that it used the initial transmission params
        self.assertAlmostEqual(pkt2_time - pkt1_time, self.ACK_TIMEOUT, delta=0.5)
        self.assertAlmostEqual(pkt3_time - pkt2_time, 2.0 * self.ACK_TIMEOUT, delta=0.5)

        # Now let's change the transmission params
        # ACK_TIMEOUT=5, ACK_RANDOM_FACTOR=1, MAX_RETRANSMIT=1, NSTART=1
        self.communicate('set-tx-param udp 5 1 1 1')
        self.ACK_TIMEOUT = 5.0
        # Respond with an error so that the client falls back to 1.0
        self.bootstrap_server.send(
            Lwm2mErrorResponse.matching(pkt3)(code=coap.Code.RES_BAD_REQUEST))

        # Transmission params should have changed,
        # so check that the new ACK_TIMEOUT is in effect for the 1.0 attempt
        pkt1 = self.bootstrap_server.recv(timeout_s=self.max_transmit_wait())
        pkt1_time = time.time()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME), pkt1)

        pkt2 = self.bootstrap_server.recv(timeout_s=self.max_transmit_wait())
        pkt2_time = time.time()
        self.assertMsgEqual(pkt1, pkt2)

        self.assertAlmostEqual(pkt2_time - pkt1_time, self.ACK_TIMEOUT, delta=0.5)

        # Respond to Request Bootstrap
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt2)())
        self.perform_typical_bootstrap(server_iid=1, security_iid=2,
                                       server_uri='coap://127.0.0.1:%d' % self.serv.get_listen_port(),
                                       bootstrap_request_timeout_s=-1)

        # Client shall now register; check that it also uses the new transmission params
        pkt1 = self.serv.recv(timeout_s=self.max_transmit_wait())
        pkt1_time = time.time()
        self.assertMsgEqual(Lwm2mRegister('/rd?lwm2m=1.1&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME),
                            pkt1)

        pkt2 = self.serv.recv(timeout_s=self.max_transmit_wait())
        pkt2_time = time.time()
        self.assertMsgEqual(pkt1, pkt2)

        self.assertAlmostEqual(pkt2_time - pkt1_time, self.ACK_TIMEOUT, delta=0.5)

        self.serv.send(Lwm2mCreated.matching(pkt2)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # check that downloads also use the new transmission params
        dl_server = coap.Server()
        try:
            with tempfile.NamedTemporaryFile() as tmp:
                self.communicate(
                    'download coap://127.0.0.1:%d %s' % (dl_server.get_listen_port(), tmp.name))

                pkt1 = dl_server.recv(timeout_s=self.max_transmit_wait())
                pkt1_time = time.time()

                pkt2 = dl_server.recv(timeout_s=self.max_transmit_wait())
                pkt2_time = time.time()
                self.assertMsgEqual(pkt1, pkt2)

                self.assertAlmostEqual(pkt2_time - pkt1_time, self.ACK_TIMEOUT, delta=0.5)

                dl_server.send(Lwm2mErrorResponse.matching(pkt2)(
                    code=coap.Code.RES_NOT_FOUND).fill_placeholders())
        finally:
            dl_server.close()


class ModifyingExchangeTimeout(RetransmissionTest.TestMixin, bootstrap_client.BootstrapTest.Test):
    EXCHANGE_LIFETIME = 2.0

    def runTest(self):
        self.assertDemoRequestsBootstrap()

        # change exchange lifetime to 2 seconds
        self.communicate('set-coap-exchange-timeout udp %s' % (self.EXCHANGE_LIFETIME,))

        # Create typical Server Object instance
        server_entries_no_ssid = [TLV.make_resource(RID.Server.Lifetime, 86400),
                                  TLV.make_resource(RID.Server.NotificationStoring, True),
                                  TLV.make_resource(RID.Server.Binding, "U"),
                                  TLV.make_resource(RID.Server.ServerCommunicationRetryCount, 1),
                                  TLV.make_resource(RID.Server.ServerCommunicationRetryTimer, 0),
                                  TLV.make_resource(
                                      RID.Server.ServerCommunicationSequenceRetryCount, 1),
                                  TLV.make_resource(
                                      RID.Server.ServerCommunicationSequenceDelayTimer, 0)]
        server_tlv = b''.join(entry.serialize() for entry in [
            TLV.make_resource(RID.Server.ShortServerID, 1)] + server_entries_no_ssid)
        # Create typical (corresponding) Security Object instance
        security_tlv = b''.join(entry.serialize() for entry in [
            TLV.make_resource(RID.Security.ServerURI,
                              'coap://127.0.0.1:%d' % self.serv.get_listen_port()),
            TLV.make_resource(RID.Security.Bootstrap, 0),
            TLV.make_resource(RID.Security.Mode, SecurityMode.NoSec.value),
            TLV.make_resource(RID.Security.ShortServerID, 1),
            TLV.make_resource(RID.Security.PKOrIdentity, b''),
            TLV.make_resource(RID.Security.SecretKey, b'')])

        # Try sending that as Block
        assert len(server_tlv) > 16
        server_chunks = list(equal_chunk_splitter(16)(server_tlv))
        packets = list(packets_from_chunks(server_chunks, path='/%d/%d' % (OID.Server, 1),
                                           format=coap.ContentFormat.APPLICATION_LWM2M_TLV))

        req = packets[0]
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mContinue.matching(req)(), self.bootstrap_server.recv())

        # Wait for the exchange to time out
        time.sleep(self.EXCHANGE_LIFETIME + 0.5)

        # The second packet shall no longer match to any exchange
        req = packets[1]
        self.bootstrap_server.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
            self.bootstrap_server.recv())

        # That's tested, now bootstrap normally
        self.write_instance(self.bootstrap_server, oid=OID.Server, iid=1, content=server_tlv)
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=1, content=security_tlv)
        self.perform_bootstrap_finish()

        self.assertDemoRegisters()
        self.wait_until_socket_count(1, timeout_s=2)

        # Check that the new connection also uses the new exchange timeout
        server_chunks = list(equal_chunk_splitter(16)(
            b''.join(entry.serialize() for entry in server_entries_no_ssid)))
        packets = list(packets_from_chunks(server_chunks, path='/%d/%d' % (OID.Server, 1),
                                           format=coap.ContentFormat.APPLICATION_LWM2M_TLV))

        req = packets[0]
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContinue.matching(req)(), self.serv.recv())

        # Wait for the exchange to time out
        time.sleep(self.EXCHANGE_LIFETIME + 0.5)

        # The second packet shall no longer match to any exchange
        req = packets[1]
        self.serv.send(req)
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(code=coap.Code.RES_REQUEST_ENTITY_INCOMPLETE),
            self.serv.recv())


class ModifyingDtlsHsTimers(RetransmissionTest.TestMixin, bootstrap_client.DtlsBootstrap.Test):
    HANDSHAKE_TIMEOUT = 3

    def setUp(self):
        super().setUp(bootstrap_server=Lwm2mServer(
            coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY)),
            extra_cmdline_args=['--identity', str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')],
            legacy_server_initiated_bootstrap_allowed=False)

    def runTest(self):
        self.assertDemoRequestsBootstrap()

        # change DTLS handshake timeouts
        self.communicate(
            'set-dtls-handshake-timeout %d %d' % (self.HANDSHAKE_TIMEOUT, self.HANDSHAKE_TIMEOUT))

        self.perform_typical_bootstrap(server_iid=1, security_iid=2,
                                       server_uri='coaps://127.0.0.1:%s' % (
                                           self.serv.get_listen_port(),),
                                       security_mode=SecurityMode.PreSharedKey,
                                       secure_identity=self.PSK_IDENTITY, secure_key=self.PSK_KEY,
                                       bootstrap_request_timeout_s=-1)

        self.assertPktIsDtlsClientHello(self.serv._raw_udp_socket.recv(65536))
        mgmt_hello_time = time.time()

        self.assertPktIsDtlsClientHello(self.bootstrap_server._raw_udp_socket.recv(65536))
        bootstrap_hello_time = time.time()

        self.wait_until_socket_count(0, timeout_s=self.HANDSHAKE_TIMEOUT + 1)
        everything_failed_time = time.time()

        self.assertAlmostEqual(bootstrap_hello_time - mgmt_hello_time, self.HANDSHAKE_TIMEOUT,
                               delta=0.5)
        self.assertAlmostEqual(everything_failed_time - bootstrap_hello_time,
                               self.HANDSHAKE_TIMEOUT, delta=0.5)

        self.communicate('reconnect')
        self.assertDemoRegisters()

        # Change DTLS handshake timeouts again, but this time in offline mode
        self.communicate('enter-offline')
        self.wait_until_socket_count(0, timeout_s=5)
        self.communicate('set-dtls-handshake-timeout %d %d' % (
            self.HANDSHAKE_TIMEOUT, 2 * self.HANDSHAKE_TIMEOUT))
        self.communicate('exit-offline')

        self.assertPktIsDtlsClientHello(self.serv._raw_udp_socket.recv(65536))
        first_hello_time = time.time()

        # Second Client Hello shall come now
        self.assertDtlsReconnect(timeout_s=self.HANDSHAKE_TIMEOUT + 2.0)
        second_hello_time = time.time()

        self.assertAlmostEqual(second_hello_time - first_hello_time, self.HANDSHAKE_TIMEOUT,
                               delta=0.5)
