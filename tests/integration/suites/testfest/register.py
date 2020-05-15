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

import unittest

from framework.lwm2m_test import *
from .dm.utils import DataModel


class Test101_InitialRegistration(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        super().setUp(auto_register=False)

    def runTest(self):
        # 1. Registration message (CoAP POST) is sent from Client to Server.
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,)),
            pkt)

        # A. In test step 1. , the Server receives the REGISTER command
        # along with the following information:
        # - Endpoint Client Name
        # - registration lifetime
        # - LwM2M version
        # - binding mode (optional)
        # - SMS number (optional)
        # - Objects and Object Instances (mandatory and optional objects /
        #   object instances) ; possibly with Version of Objects
        #    - Objects and Object instances (mandatory and optional
        #      objects/object instances)
        self.assertLinkListValid(pkt.content.decode())

        # B. In test step 1. , Client received "Success" message from Server
        #    (2.01 Created) related to the REGISTER command.
        self.serv.send(Lwm2mCreated.matching(pkt)(location=self.DEFAULT_REGISTER_ENDPOINT))


class Test102_RegistrationUpdate(DataModel.Test):
    def runTest(self):
        # 1. The Server set the lifetime resource of the Server Object
        #    Instance to 20 sec (CoAP PUT /1/0/1)
        #
        # A. In test step 1., the Server received a Success Message (2.04
        #    Changed) related to its setting request
        self.test_write(ResPath.Server[0].Lifetime, '20')

        # 2. UPDATE (Registration) message (CoAP POST) is sent from
        #    Client to Server with Lifetime parameter set to 20 sec
        #
        # B. In test step 2., Server has received UPDATE operation with
        #    lifetime parameter = 20 sec
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=['lt=20'],
                                        content=b''),
                            pkt)
        # C. In test step 2., Client has received "Success" (2.04) message
        #    from Server related to the UPDATE command
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # 3. On option, before the registration expires (20 sec) the Client
        #    send a new UPDATE message without parameter to the Server
        #
        # D. In test step 3., either the Server received an UPDATE operation
        #    with no parameter or a de_registratoin occurs in the Server
        #    after the 20s
        pkt = self.serv.recv(timeout_s=20)
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)
        self.serv.send(Lwm2mChanged.matching(pkt)())


class Test103_Deregistration(DataModel.Test):
    def tearDown(self):
        self.teardown_demo_with_servers(auto_deregister=False)

    def runTest(self):
        # 1. The Server sends EXECUTE command on the Disable
        #    Resource of the Server Object (ID:1) Instance.
        # A. In test step 1., the Server receives the "Success" message (2.04
        #    Changed) from the Client
        self.test_execute(ResPath.Server[0].Disable)

        # 2. Deregistration message (CoAP DELETE) is sent from Client to Server.
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mDeregister(self.DEFAULT_REGISTER_ENDPOINT), pkt)

        # B. In test step 2., the Client receives "Success" message (2.02
        #    Deleted) from the Server
        self.serv.send(Lwm2mDeleted.matching(pkt)())

        # C. Client is removed from the servers registration database


class Test104_RegistrationUpdateTrigger(DataModel.Test):
    def setUp(self):
        super().setUp(lifetime=20)

    def runTest(self):
        # 1. Before Client registration expires on the Server (for test
        #    purposes a short registration lifetime is chosen 20 sec) a
        #    Registration Update Trigger message CoAP POST /1/0/8 is
        #    sent from Server to Client
        #
        # A. In test step 1., Client received a Registration Update Trigger
        # B. In test step 1., Server received a "Success" message (2.04
        #    Changed) related to the EXECUTE command (Registration
        #    Update Trigger).
        self.test_execute(ResPath.Server[0].RegistrationUpdateTrigger)

        # 2. UPDATE (Registration) message (CoAP POST) is sent from
        #    Client to Server
        # C. In test step 2., Server received UPDATE operation without
        #    parameter
        pkt = self.serv.recv()
        self.assertMsgEqual(Lwm2mUpdate(self.DEFAULT_REGISTER_ENDPOINT,
                                        query=[],
                                        content=b''),
                            pkt)
        # D. In test step 2., Client has received "Success" message (2.04
        #    Changed) from Server related to its UPDATE message
        self.serv.send(Lwm2mChanged.matching(pkt)())

        # E. After test step 2., the Client is still registered in the Server
        #    while the initial Registration lifetime expired
