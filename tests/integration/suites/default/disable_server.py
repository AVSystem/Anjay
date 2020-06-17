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
from .access_control import AccessMask


class DisableServerTest(test_suite.Lwm2mSingleServerTest):
    def assertSocketsPolled(self, num):
        self.assertEqual(num, self.get_socket_count())

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # Write Disable Timeout
        req = Lwm2mWrite(ResPath.Server[1].DisableTimeout, '6')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # Execute Disable
        req = Lwm2mExecute(ResPath.Server[1].Disable)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        self.assertDemoDeregisters(timeout_s=5)

        # no message for now
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=5))

        self.assertSocketsPolled(0)

        # we should get another Register
        self.assertDemoRegisters(timeout_s=3)

        # no message for now
        with self.assertRaises(socket.timeout):
            print(self.serv.recv(timeout_s=2))

        self.assertSocketsPolled(1)


class DisabledServerUpdateTriggerTest(test_suite.Lwm2mTest, test_suite.Lwm2mDmOperations):
    def setUp(self, **kwargs):
        super().setUp(servers=2, extra_cmdline_args=[
            '--access-entry', '/%d/1,2,%d' % (OID.Server, AccessMask.OWNER)])

    def runTest(self):
        # Disable the first server
        self.execute_resource(
            self.servers[0], OID.Server, 1, RID.Server.Disable)
        self.assertDemoDeregisters(self.servers[0])

        # Update trigger for the disabled server should return BAD REQUEST
        self.execute_resource(self.servers[1], OID.Server, 1, RID.Server.RegistrationUpdateTrigger,
                              expect_error_code=coap.Code.RES_BAD_REQUEST)

    def tearDown(self):
        self.teardown_demo_with_servers(deregister_servers=[self.servers[1]])
