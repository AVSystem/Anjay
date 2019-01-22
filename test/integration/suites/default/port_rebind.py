# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

        with contextlib.closing(socket.socket(type=socket.SOCK_DGRAM)) as conflict_socket:
            conflict_socket.bind(('0.0.0.0', self._demo_port))

            self.serv.reset()
            self.communicate('exit-offline')
            with self.assertRaises(socket.timeout):
                self.serv.listen(timeout_s=5)

        # inability to bind on predefined port is fatal, check that demo does not retry
        with self.assertRaises(socket.timeout):
            self.serv.listen(timeout_s=15)
        self.assertEqual(self.get_socket_count(), 0)
