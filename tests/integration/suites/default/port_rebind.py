# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import contextlib
import socket
import time

from framework.lwm2m_test import *


class RandomPortRebind(test_suite.Lwm2mDtlsSingleServerTest, test_suite.Lwm2mDmOperations):
    def runTest(self):
        demo_port = self.serv.get_remote_addr()[1]

        self.communicate('enter-offline')

        deadline = time.time() + 5
        while self.get_socket_count() > 0:
            if time.time() > deadline:
                self.fail('Socket not closed')
            time.sleep(0.1)

        with contextlib.closing(socket.socket(type=socket.SOCK_DGRAM)) as conflict_socket:
            conflict_socket.bind(('0.0.0.0', demo_port))

            self.serv.reset()
            self.communicate('exit-offline')
            self.serv.listen(timeout_s=5)

            # check that everything is working
            self.read_path(self.serv, ResPath.Device.Manufacturer)

        self.assertNotEqual(self.serv.get_remote_addr()[1], demo_port)


class PredefinedPortRebind(test_suite.Lwm2mDtlsSingleServerTest, test_suite.Lwm2mDmOperations):
    def setUp(self):
        with contextlib.closing(socket.socket(type=socket.SOCK_DGRAM)) as ephemeral_probe:
            ephemeral_probe.bind(('0.0.0.0', 0))
            self._demo_port = ephemeral_probe.getsockname()[1]

        self.assertNotEqual(self._demo_port, 0)
        super().setUp(extra_cmdline_args=['--port', '%s' % (self._demo_port,)])

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        self.assertEqual(self._demo_port, self.serv.get_remote_addr()[1])

        self.communicate('enter-offline')

        deadline = time.time() + 5
        while self.get_socket_count() > 0:
            if time.time() > deadline:
                self.fail('Socket not closed')
            time.sleep(0.1)

        # Anjay first attempts to re-bind on the same address family, and if that fails
        # it tries another (if possible). The thing is, in both cases it uses the same
        # port. Now, on macOS you can have an IPv4 socket bound to some port, and still
        # be able to bind using IPv6 on the same port. On Linux, you can't. To mitigate
        # this mess, we may bind on IPv6 (if available) because then due to IPv4-mapping
        # it should be not possible to bind using IPv4 to the same port.
        for family, addr in zip((socket.AF_INET6, socket.AF_INET), ('::', '0.0.0.0')):
            try:
                with contextlib.closing(socket.socket(family, socket.SOCK_DGRAM)) as conflict_socket:
                    conflict_socket.bind((addr, self._demo_port))

                    self.serv.reset()
                    self.communicate('exit-offline')
                    with self.assertRaises(socket.timeout):
                        self.serv.listen(timeout_s=5)
                break
            except socket.error:
                continue

        # inability to bind on predefined port is fatal, check that demo does not retry
        with self.assertRaises(socket.timeout):
            self.serv.listen(timeout_s=15)
        self.assertEqual(self.get_socket_count(), 0)
