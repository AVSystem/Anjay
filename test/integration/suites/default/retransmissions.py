# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

from framework.lwm2m_test import *


class ReconnectRetryTest(test_suite.PcapEnabledTest, test_suite.Lwm2mDtlsSingleServerTest):
    def setup_demo_with_servers(self, **kwargs):
        for server in kwargs['servers']:
            self._server_close_stack.enter_context(server.fake_close())
        super().setup_demo_with_servers(**kwargs)

    def setUp(self):
        self._server_close_stack = contextlib.ExitStack()
        super().setUp(extra_cmdline_args=['--max-icmp-failures', '3'], auto_register=False)

    def tearDown(self):
        self._server_close_stack.close()  # in case runTest() failed
        super().tearDown()

    def runTest(self):
        # wait until ultimate failure
        self.wait_until_icmp_unreachable_count(3, timeout_s=16)

        # check that there are no more attempts
        time.sleep(16)
        self.assertEqual(3, len(self.read_icmp_unreachable_packets()))

        # attempt reconnection
        self.communicate('reconnect')
        # let demo make 2 unsuccessful connections
        self.wait_until_icmp_unreachable_count(5, timeout_s=8)
        self._server_close_stack.close()  # unclose the server socket
        self.assertDemoRegisters(self.serv, timeout_s=16)
        self.assertEqual(5, len(self.read_icmp_unreachable_packets()))
