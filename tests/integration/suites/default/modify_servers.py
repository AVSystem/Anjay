# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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


class ModifyServersTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=2, auto_register=False)

    def tearDown(self):
        self.request_demo_shutdown()
        self.assertDemoDeregisters(self.servers[0], path='/rd/demo')
        self.assertDemoDeregisters(self.servers[1], path='/rd/server3')
        self.teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.assertDemoRegisters(server=self.servers[0], location='/rd/demo')
        self.assertDemoRegisters(server=self.servers[1], location='/rd/server2')

        # remove second server
        self.communicate("trim-servers 1")
        self.assertDemoDeregisters(server=self.servers[1], path='/rd/server2')
        self.assertDemoUpdatesRegistration(server=self.servers[0], content=ANY)

        # add another server
        self.communicate("add-server coap://127.0.0.1:%d" % self.servers[1].get_listen_port())
        self.assertDemoUpdatesRegistration(server=self.servers[0], content=ANY)
        self.assertDemoRegisters(server=self.servers[1], location='/rd/server3')
