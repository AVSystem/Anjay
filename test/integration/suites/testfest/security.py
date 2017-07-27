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

import binascii

from framework.lwm2m_test import *

from .dm.utils import DataModel


class Test401_UDPChannelSecurity_PreSharedKeyMode(DataModel.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        self.setup_demo_with_servers(servers=[Lwm2mServer(coap.DtlsServer(self.PSK_IDENTITY, self.PSK_KEY))],
                                     extra_cmdline_args=['--identity',
                                                         str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                                         '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')],
                                     auto_register=False)

    def runTest(self):
        # a. Registration message (COAP POST) is sent from client to server.
        req = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
            req)

        # b. Client receives Success message (2.01 Created) from the server.
        self.serv.send(Lwm2mCreated.matching(req)(location=self.DEFAULT_REGISTER_ENDPOINT))

        # c. READ (COAP GET) on e.g. ACL object resources
        req = Lwm2mRead('/2')
        self.serv.send(req)

        # d. Server receives success message (2.05 Content) and the
        # requested values (encrypted)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())
