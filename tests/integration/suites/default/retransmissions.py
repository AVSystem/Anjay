# -*- coding: utf-8 -*-
#
# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

import contextlib
import time
import socket

from framework.lwm2m_test import *

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
