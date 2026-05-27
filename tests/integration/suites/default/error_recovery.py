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

from framework_tools.lwm2m.coap.code import Code
from framework_tools.lwm2m.coap.server import SecurityMode
from framework.lwm2m_test import *


class BootstrapTest:
    """Mixin and base test class for bootstrap-related error recovery scenarios."""

    class TestMixin:
        def perform_bootstrap_finish(self):
            req = Lwm2mBootstrapFinish()
            self.bootstrap_server.send(req)
            self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                                self.bootstrap_server.recv())

        def add_server(self,
                       server_iid,
                       security_iid,
                       server_uri,
                       lifetime=86400,
                       secure_identity=b'',
                       secure_key=b'',
                       security_mode: SecurityMode = SecurityMode.NoSec,
                       binding='U',
                       additional_security_data=b'',
                       additional_server_data=b''):
            # Write the Server Object instance with standard resources
            self.write_instance(self.bootstrap_server, oid=OID.Server, iid=server_iid,
                                content=TLV.make_resource(
                                    RID.Server.Lifetime, lifetime).serialize()
                                        + TLV.make_resource(RID.Server.ShortServerID,
                                                            server_iid).serialize()
                                        + TLV.make_resource(RID.Server.NotificationStoring,
                                                            True).serialize()
                                        + TLV.make_resource(RID.Server.Binding,
                                                            binding).serialize()
                                        + additional_server_data)

            # Write the corresponding Security Object instance
            self.write_instance(self.bootstrap_server, oid=OID.Security, iid=security_iid,
                                content=TLV.make_resource(
                                    RID.Security.ServerURI, server_uri).serialize()
                                        + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                        + TLV.make_resource(RID.Security.Mode,
                                                            security_mode.value).serialize()
                                        + TLV.make_resource(RID.Security.ShortServerID,
                                                            server_iid).serialize()
                                        + TLV.make_resource(RID.Security.PKOrIdentity,
                                                            secure_identity).serialize()
                                        + TLV.make_resource(RID.Security.SecretKey,
                                                            secure_key).serialize()
                                        + additional_security_data)

    class Test(TestMixin, test_suite.Lwm2mSingleServerTest, test_suite.Lwm2mDmOperations):
        def setUp(self, servers=1, num_servers_passed=0, holdoff_s=None, timeout_s=None,
                  bootstrap_server=True, extra_cmdline_args=None, **kwargs):
            assert bootstrap_server
            extra_args = extra_cmdline_args or []
            if holdoff_s is not None:
                extra_args += ['--bootstrap-holdoff', str(holdoff_s)]
            if timeout_s is not None:
                extra_args += ['--bootstrap-timeout', str(timeout_s)]

            self.holdoff_s = holdoff_s
            self.timeout_s = timeout_s
            super().setUp(servers=servers, num_servers_passed=num_servers_passed,
                          bootstrap_server=bootstrap_server,
                          extra_cmdline_args=extra_args, **kwargs)


class BootstrapNoFinishFromServer(BootstrapTest.Test):
    """
    Tests the client's behavior when the bootstrap server writes bootstrap data
    (Server Object and Security Object instances) but never sends Bootstrap-Finish.

    According to the LwM2M specification, the client must treat the bootstrap
    procedure as failed if Bootstrap-Finish is not received within EXCHANGE_LIFETIME
    after the last bootstrap-related exchange. This test sets very short CoAP
    retransmission parameters to make EXCHANGE_LIFETIME short and verifiable,
    then confirms that the client eventually closes the bootstrap socket and
    re-initiates a new bootstrap request.
    """

    ACK_TIMEOUT = 1
    MAX_RETRANSMIT = 1

    def setUp(self):
        # Use short ack-timeout and max-retransmit to obtain a short EXCHANGE_LIFETIME,
        # making the test feasible without waiting many minutes.
        super().setUp(extra_cmdline_args=[
            '--ack-random-factor', '1',
            '--ack-timeout', '%s' % self.ACK_TIMEOUT,
            '--max-retransmit', '%s' % self.MAX_RETRANSMIT,
        ])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # Step 1: The client initiates bootstrap by sending a Bootstrap-Request.
        # We respond with 2.04 Changed to acknowledge it.
        self.assertDemoRequestsBootstrap(endpoint=DEMO_ENDPOINT_NAME)

        # Step 2: The bootstrap server writes the Server Object instance,
        # providing the LwM2M management server configuration.
        self.write_instance(self.bootstrap_server, oid=OID.Server, iid=1,
                            content=TLV.make_resource(
                                RID.Server.Lifetime, 86400).serialize()
                                    + TLV.make_resource(RID.Server.ShortServerID,
                                                        1).serialize()
                                    + TLV.make_resource(RID.Server.NotificationStoring,
                                                        True).serialize()
                                    + TLV.make_resource(RID.Server.Binding,
                                                        'U').serialize())

        # Step 3: The bootstrap server writes the Security Object instance,
        # pointing to a valid management server URI.
        self.write_instance(self.bootstrap_server, oid=OID.Security, iid=2,
                            content=TLV.make_resource(
                                RID.Security.ServerURI,
                                'coap://127.0.0.1:%d' % self.serv.get_listen_port()).serialize()
                                    + TLV.make_resource(RID.Security.Bootstrap, 0).serialize()
                                    + TLV.make_resource(RID.Security.Mode,
                                                        SecurityMode.NoSec.value).serialize()
                                    + TLV.make_resource(RID.Security.ShortServerID,
                                                        1).serialize()
                                    + TLV.make_resource(RID.Security.PKOrIdentity,
                                                        b'').serialize()
                                    + TLV.make_resource(RID.Security.SecretKey,
                                                        b'').serialize())

        # Step 4: The bootstrap server deliberately does NOT send Bootstrap-Finish.
        # The client must detect the timeout and close the bootstrap socket.
        # We verify that the bootstrap socket is still open right after the writes.
        self.assertEqual(1, self.get_socket_count())

        # Step 5: Advance demo time past EXCHANGE_LIFETIME so the client considers
        # the bootstrap procedure as failed and tears down the bootstrap session.
        self.advance_demo_time(TxParams(ack_timeout=self.ACK_TIMEOUT,
                                        max_retransmit=self.MAX_RETRANSMIT).exchange_lifetime())

        # Step 6: The client should close the bootstrap socket after the timeout.
        self.wait_until_socket_count(0, timeout_s=5)


class BootstrapDtlsHandshakeRejected(BootstrapTest.Test):
    """
    Tests the client's behavior when the DTLS bootstrap server rejects the handshake
    because the client provides wrong PSK credentials.

    The bootstrap server is configured with the correct PSK identity and key. The client
    is started with the correct identity but an intentionally wrong key, so that the DTLS
    handshake cannot succeed. The test verifies that:
      - The client sends a DTLS ClientHello (visible on the raw UDP socket).
      - The handshake fails (RuntimeError raised by the DTLS server recv).
      - After the failure, the client closes the bootstrap socket.
      - The client subsequently re-initiates a new Bootstrap-Request, demonstrating
        that it correctly recovers from a DTLS-level connection failure.

    Note: auto_register=False is required so that setup_demo_with_servers does NOT call
    listen() on the bootstrap server during setUp. With wrong PSK credentials, that call
    would immediately fail with a RuntimeError before runTest even begins.
    """

    # Credentials known to the bootstrap server.
    BOOTSTRAP_PSK_IDENTITY = b'bootstrap-identity'
    BOOTSTRAP_PSK_KEY = b'correct-bootstrap-key'

    # The wrong key that the client will use — this will cause the handshake to fail.
    CLIENT_WRONG_KEY = b'wrong-bootstrap-key'

    def setUp(self):
        # The bootstrap server is configured with the real key.
        # The client is given the wrong key via --key, triggering a handshake failure.
        # auto_register=False prevents the framework from calling listen() on the DTLS
        # bootstrap server during setUp, which would fail immediately with wrong credentials.
        super().setUp(
            servers=0,
            auto_register=False,
            bootstrap_server=Lwm2mServer(
                coap.DtlsServer(psk_identity=self.BOOTSTRAP_PSK_IDENTITY,
                                psk_key=self.BOOTSTRAP_PSK_KEY)),
            extra_cmdline_args=[
                '--identity-as-string',
                self.BOOTSTRAP_PSK_IDENTITY.decode('ascii'),
                '--key-as-string',
                self.CLIENT_WRONG_KEY.decode('ascii'),
            ])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # Step 1: The client starts the DTLS handshake toward the bootstrap server.
        # Since DTLS encrypts the CoAP payload, we cannot parse the message yet.
        # We read the raw UDP datagram instead and verify it is a DTLS ClientHello.
        raw_pkt = self.bootstrap_server._raw_udp_socket.recv(4096)
        self.assertPktIsDtlsClientHello(raw_pkt, seq_number=0)

        # Step 2: The DTLS handshake will fail because the client uses the wrong PSK key.
        # Calling recv() on the bootstrap server completes the server side of the handshake
        # attempt, which raises RuntimeError due to the MAC verification failure.
        with self.assertRaises(RuntimeError):
            self.bootstrap_server.recv()

        # Step 3: After the handshake failure the client must tear down the connection.
        # The bootstrap socket count should drop to zero.
        self.wait_until_socket_count(0, timeout_s=5)


class BootstrapDtlsNoResponseToRequest(BootstrapTest.Test):
    """
    Tests the client's behavior when the DTLS handshake to the bootstrap server
    succeeds, but the server ignores the subsequent Bootstrap-Request.

    The client should retransmit the Bootstrap-Request according to CoAP
    retransmission parameters (ACK_TIMEOUT and MAX_RETRANSMIT). Once all
    attempts time out, it should close the socket.
    """

    ACK_TIMEOUT = 1
    MAX_RETRANSMIT = 1

    BOOTSTRAP_PSK_IDENTITY = b'bootstrap-identity'
    BOOTSTRAP_PSK_KEY = b'correct-bootstrap-key'

    def setUp(self):
        super().setUp(
            servers=0,
            auto_register=False,
            bootstrap_server=Lwm2mServer(
                coap.DtlsServer(psk_identity=self.BOOTSTRAP_PSK_IDENTITY,
                                psk_key=self.BOOTSTRAP_PSK_KEY)),
            extra_cmdline_args=[
                '--ack-random-factor', '1',
                '--ack-timeout', '%s' % self.ACK_TIMEOUT,
                '--max-retransmit', '%s' % self.MAX_RETRANSMIT,
                '--identity-as-string', self.BOOTSTRAP_PSK_IDENTITY.decode('ascii'),
                '--key-as-string', self.BOOTSTRAP_PSK_KEY.decode('ascii'),
            ])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        # The DTLS handshake is performed implicitly by the framework on the first
        # recv() call. We loop to catch all retransmissions of the Bootstrap-Request.
        for _ in range(self.MAX_RETRANSMIT + 1):
            # timeout_s provides enough slack for latency
            pkt = self.bootstrap_server.recv(timeout_s=self.ACK_TIMEOUT * 2 + 2)
            self.assertIsInstance(pkt, Lwm2mRequestBootstrap)

        # The client must eventually consider the procedure failed after EXCHANGE_LIFETIME
        # and close the socket.
        self.wait_until_socket_count(0, timeout_s=self.ACK_TIMEOUT * 4 + 2)


class DtlsCommunicationSequenceTimers:
    """Namespace and base test class for communication sequence timer tests over DTLS."""

    class Test(test_suite.PcapEnabledTest,
               test_suite.Lwm2mDtlsSingleServerTest,
               test_suite.Lwm2mDmOperations):
        """
        Evaluates that communication sequence timers work correctly over DTLS,
        and specifically asserts that DTLS handshakes are NOT repeated during
        retries within a sequence, but ARE repeated when moving to the next sequence.
        """
        RETRY_COUNT = 5
        RETRY_TIMER = 60
        SEQUENCE_RETRY_COUNT = 1
        SEQUENCE_DELAY_TIMER = 86400
        MAX_RETRANSMIT = 0
        PASS_SEQUENCE_TIMER_ARGS = False

        def setUp(self):
            self.expected_handshakes = 0
            extra_cmdline_args = [
                '--max-retransmit', str(self.MAX_RETRANSMIT),
                '--ack-timeout', str(1),
                '--dtls-hs-retry-wait-min', str(1),
                '--dtls-hs-retry-wait-max', str(1.5),
            ]
            if self.PASS_SEQUENCE_TIMER_ARGS:
                extra_cmdline_args += [
                    '--retry-count', str(self.RETRY_COUNT),
                    '--retry-timer', str(self.RETRY_TIMER),
                    '--sequence-retry-count', str(self.SEQUENCE_RETRY_COUNT),
                    '--sequence-delay-timer', str(self.SEQUENCE_DELAY_TIMER),
                ]
            super().setUp(auto_register=False, minimum_version="1.1", maximum_version="1.1", extra_cmdline_args=extra_cmdline_args)

        def tearDown(self):
            super().tearDown(auto_deregister=False)

        def runTest(self):
            def run_retry_sequence(reset_at_first=True, respond_on_last=False):
                respond = False
                
                for sequence_no in range(self.SEQUENCE_RETRY_COUNT):
                    print(f"Sequence {sequence_no + 1} start")

                    # server restart to handle the incoming handshakes
                    if reset_at_first or sequence_no > 0:
                        self.expected_handshakes += 5
                        print(f"server reset")
                        self.serv.reset()

                    if sequence_no > 0:
                        expected_seq_delay = self.SEQUENCE_DELAY_TIMER
                        seq_log = f"Sequence Retry {sequence_no}/{self.SEQUENCE_RETRY_COUNT - 1} scheduled in {self.SEQUENCE_DELAY_TIMER}".encode('utf-8')
                        
                        # Check if Anjay logs the start of new sequence
                        if self.read_log_until_match(seq_log, timeout_s=2) is None:
                            self.fail(f'Sequence not scheduled with {expected_seq_delay} sec')

                        with self.assertRaises(socket.timeout):
                            self.serv.recv(timeout_s=expected_seq_delay-1)

                    # Check consecutive sequences
                    for attempt_idx in range(self.RETRY_COUNT):
                        is_last_attempt_in_seq = (attempt_idx == self.RETRY_COUNT - 1)
                        is_last_sequence = (sequence_no == self.SEQUENCE_RETRY_COUNT - 1)

                        if respond_on_last and is_last_sequence and is_last_attempt_in_seq:
                            respond = True

                        # Calculate this attempt delay
                        if attempt_idx == 0:
                             # First attempt in a sequence has no delay
                            expected_delay = 0
                        else:
                            # For attempts > 0 (actual retries) calculate backoff: timer * 2^(attempt - 1)
                            expected_delay = self.RETRY_TIMER * (2 ** (attempt_idx - 1))

                        # Check concesutive attempts
                        if attempt_idx > 0:
                            time.sleep(0.5) # wait for dumpcap
                            self.expected_handshakes += 5
                            self.serv.reset()
                            time.sleep(0.1)

                            reg_log = f"Registration Retry {attempt_idx}/{self.RETRY_COUNT - 1} scheduled in {expected_delay}".encode('utf-8')

                            if self.read_log_until_match(reg_log, timeout_s=3) is None:
                                self.fail(f'Registration not scheduled with {expected_delay} sec (Attempt {attempt_idx})')

                        print(f"Seq {sequence_no}, Try {attempt_idx}, Expected delay {expected_delay}s")

                        # wait for timer to pass before client sends register
                        if expected_delay >= 1:
                            # speed it up a bit
                            if (expected_delay > 10):
                                self.advance_demo_time(expected_delay - 10)
                                expected_delay = 10

                            with self.assertRaises(socket.timeout):
                                self.serv.recv(timeout_s=expected_delay-1)

                        pkt = self.serv.recv(timeout_s=2.0)

                        self.assertIsInstance(pkt, Lwm2mRegister)
                        if respond:
                            self.serv.send(Lwm2mCreated.matching(pkt)(location='/1/0'))

                    # Check the number of handshakes
                    self.assertEqual(
                        self.expected_handshakes,
                        self.count_dtls_handshake_packets_stable(
                            timeout_s=5,
                            poll_interval_s=0.25,
                            stable_for_s=0.5))

            # first full sequence
            run_retry_sequence(respond_on_last=True)


class DtlsCommunicationSequenceTimersTypicalApplication(DtlsCommunicationSequenceTimers.Test):
    RETRY_COUNT = 2
    RETRY_TIMER = 1
    SEQUENCE_RETRY_COUNT = 3
    SEQUENCE_DELAY_TIMER = 5
    PASS_SEQUENCE_TIMER_ARGS = True


class DtlsCommunicationSequenceTimersDefaults(DtlsCommunicationSequenceTimers.Test):
    pass
