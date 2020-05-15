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

import binascii

from framework.lwm2m_test import *

from .dm.utils import DataModel


class Test401_UDPChannelSecurity_PreSharedKeyMode(DataModel.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        from pymbedtls import PskSecurity
        self.setup_demo_with_servers(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
                                     extra_cmdline_args=['--identity',
                                                         str(binascii.hexlify(self.PSK_IDENTITY), 'ascii'),
                                                         '--key', str(binascii.hexlify(self.PSK_KEY), 'ascii')],
                                     auto_register=False)

    def runTest(self):
        # 1. DTLS Session is established
        # 2. Registration message (CoAP POST) is sent from LwM2M
        #    Client to LwM2M Server.
        # 3. Client receives Success message (2.01 Created) from the Server.
        #
        # A. In test step 2 & 3, Registration command of the Client on the Server
        #    is performed successfully over the DTLS session
        self.assertDemoRegisters()

        # 4. READ (CoAP GET) on the Instance of the Device Object is
        #    performed using the default TLV data format (cf Test
        #    LwM2M-1.0-int-203)
        # 5. Server receives success message (2.05 Content) and the
        #    requested values (encrypted)
        #
        # B. In test step 4 & 5 the READ command work successfully over the
        #    DTLS session.
        self.test_read('/%d/0' % OID.Device)
