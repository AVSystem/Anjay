# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import socket

from framework.lwm2m_test import *


class BootstrapFactoryTest(test_suite.Lwm2mTest, test_suite.SingleServerAccessor):
    def setUp(self):
        extra_args = ['--bootstrap-timeout', '5']
        self.setup_demo_with_servers(servers=1,
                                     bootstrap_server=True,
                                     extra_cmdline_args=extra_args)

    def tearDown(self):
        self.teardown_demo_with_servers()

    def runTest(self):
        self.assertEqual(2, self.get_socket_count())

        # no message on the bootstrap socket - already bootstrapped
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=2))

        # no changes
        self.assertEqual(2, self.get_socket_count())

        # still no message
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=4))

        # Bootstrap Finish did not arrive, so Bootstrap Server timeout is not applicable here - no change
        self.assertEqual(2, self.get_socket_count())

        # Registration Update shall not include changes
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        self.assertEqual(2, self.get_socket_count())
