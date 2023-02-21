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


class ModifyServersTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=2, auto_register=False)

    def tearDown(self):
        self.coap_ping(self.servers[0])
        self.coap_ping(self.servers[1])
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
