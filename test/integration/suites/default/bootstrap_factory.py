# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

import socket

from framework.lwm2m_test import *


class BootstrapFactoryTest(test_suite.Lwm2mTest, test_suite.SingleServerAccessor):
    def setUp(self):
        extra_args = ['--bootstrap-timeout', '5']
        self.setup_demo_with_servers(num_servers=1,
                                     bootstrap_server=True,
                                     extra_cmdline_args=extra_args)

    def tearDown(self):
        self.teardown_demo_with_servers()

    def runTest(self):
        # no message on the bootstrap socket - already bootstrapped
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=2))

        # no changes
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration()

        # still no message
        with self.assertRaises(socket.timeout):
            print(self.bootstrap_server.recv(timeout_s=4))

        # now the Bootstrap Server Account should be gone
        self.communicate('send-update')
        self.assertDemoUpdatesRegistration(content=ANY)

        req = Lwm2mDiscover('/0')
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res)

        self.assertIn(b'</0/2/', res.content)
        self.assertNotIn(b'</0/1/', res.content)
