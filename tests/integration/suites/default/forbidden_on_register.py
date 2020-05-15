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

from framework.lwm2m_test import *
from framework.test_utils import *
from framework.lwm2m.coap.server import SecurityMode

from . import bootstrap_client as bs

import socket

class Test:
    PSK_KEY=b'key'
    PSK_IDENTITY=b'identity'

    class ClientReceivesForbiddenOnRegisterMixin:
        def setUp(self):
            self.setup_demo_with_servers(servers=1, auto_register=False)

        def tearDown(self):
            self.teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            register_pkt = self.assertDemoRegisters(self.serv, respond=False)
            self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))
            # Client shouldn't even retry
            with self.assertRaises(socket.timeout):
                self.serv.recv()


    class ClientSessionRevokedMixin(test_suite.Lwm2mDmOperations):
        def tearDown(self):
            self.teardown_demo_with_servers(auto_deregister=False)

        def runTest(self):
            # Trigger update.
            self.communicate('send-update')
            update_pkt = self.assertDemoUpdatesRegistration(self.serv, respond=False)
            # Respond to it with 4.00 Bad Request to simulate some kind of client account expiration on server side.
            self.serv.send(Lwm2mErrorResponse.matching(update_pkt)(code=coap.Code.RES_BAD_REQUEST))
            # This should cause client attempt to re-register.
            register_pkt = self.assertDemoRegisters(self.serv, respond=False)
            # To which we respond with 4.03 Forbidden, finishing off the communication.
            self.serv.send(Lwm2mErrorResponse.matching(register_pkt)(code=coap.Code.RES_FORBIDDEN))
            # Client shouldn't even retry
            with self.assertRaises(socket.timeout):
                self.serv.recv()



class ClientReceivesForbiddenOnRegister(Test.ClientReceivesForbiddenOnRegisterMixin,
                                        test_suite.Lwm2mSingleServerTest):
    pass


class DtlsClientReceivesForbiddenOnRegister(Test.ClientReceivesForbiddenOnRegisterMixin,
                                            test_suite.Lwm2mDtlsSingleServerTest):
    pass


class ClientSessionRevoked(Test.ClientSessionRevokedMixin,
                           test_suite.Lwm2mSingleServerTest):
    pass


class DtlsClientSessionRevoked(Test.ClientSessionRevokedMixin,
                               test_suite.Lwm2mDtlsSingleServerTest):
    pass


