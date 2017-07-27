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

import unittest

from framework.lwm2m_test import *


class Test101_InitialRegistration(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def runTest(self):
        # Registration message (CoAP POST) is sent from client to server
        pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
            pkt)

        # Client receives Success message (2.01 Created) from the server
        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # 1. Server has received REGISTER operation - assertMsgEqual above
        # 2. Server knows the following:
        #    - Endpoint Client Name - assertMsgEqual above
        #    - registration lifetime (optional) - assertMsgEqual above
        #    - LwM2M version (optional)
        #    - binding mode (optional)
        #    - SMS number (optional)

        #    - Objects and Object instances (mandatory and optional objects/object instances)
        self.assertLinkListValid(pkt.content)

        # 3. Client has received "Success" message from server


class Test102_RegistrationUpdate(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(lifetime=5)

    def runTest(self):
        # Re-Registration message (CoAP PUT) is sent from client to server
        pkt = self.serv.recv(timeout_s=2.5 + 1)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)

        # Client receives Success message (2.04 Changed) from the server
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # 1. Server has received REGISTER operation -- ????? TODO: why does Update test expect Register?
        # 2. Server knows the following:
        #    - Endpoint Client Name -- ????? TODO: there's no Endpoint Name in Update
        #    - registration lifetime (optional)
        #    - LwM2M version (optional) -- ????? TODO: there's no LwM2M Version in Update
        #    - binding mode (optional)
        #    - SMS number (optional)
        #    - Objects and Object instances (optional)

        # 3. Client has received "Success" message from server


class Test103_Deregistration(test_suite.Lwm2mSingleServerTest):
    def tearDown(self):
        self.teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        self.request_demo_shutdown()

        # Deregistration message (CoAP DELETE) is sent from client to server
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mDeregister(self.DEFAULT_REGISTER_ENDPOINT), pkt)

        # Client receives Success message (2.02 Deleted) from the server
        self.serv.send(Lwm2mDeleted.matching(pkt)())

        # 1. Client is removed from the servers registration database




@unittest.skip("That test is invalid, check issue on github")
class Test105_InitialRegistrationToBootstrapServer(test_suite.Lwm2mSingleServerTest):
    pass
