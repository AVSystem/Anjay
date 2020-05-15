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


class Test0_ClientInitiatedBootstrap(DataModel.Test):
    PSK_IDENTITY = b'test-identity'
    PSK_KEY = b'test-key'

    def setUp(self):
        self.setup_demo_with_servers(servers=[Lwm2mServer(coap.DtlsServer(psk_key=self.PSK_KEY, psk_identity=self.PSK_IDENTITY))],
                                     num_servers_passed=0,
                                     bootstrap_server=True)

    def tearDown(self):
        self.assertDemoRegisters()
        super().teardown_demo_with_servers()

    def runTest(self):
        # 1. Without Instance of Server Object, the Client performs a
        #    BOOTSTRAP-REQUEST (CoAP POST /bs?ep{Endpoint Client Name})
        #    in using the Resource values of the Instance 1 of
        #    Security Object ID:0 to contact the Bootstrap Server
        #
        # A. In test step 1., the Bootstrap Server received a Success Message
        #    ("2.04" Changed) related to the BOOTSTRAP-REQUEST of the Client
        pkt = self.bootstrap_server.recv()
        self.assertMsgEqual(Lwm2mRequestBootstrap(endpoint_name=DEMO_ENDPOINT_NAME),
                            pkt)
        self.bootstrap_server.send(Lwm2mChanged.matching(pkt)())

        # 2. The Bootstrap Server uploads the Configuration C.1 in the
        #    Client in performing two BOOTSTRAP-WRITE (CoAP PUT /0,
        #    CoAP PUT /1) in using the set of values defined in
        #    0-SetOfValue_1 and 0-SetOfValue_2 (Object Device ID:3,is
        #    automatically created and filled-up by the Client)
        #
        # B. In test step 2., Client received WRITE operation(s) to setup the
        #    C.1 configuration (0-SetOfValue)
        # C. In test step 2., Server received Success ("2.04" Changed)
        #    message(s) from Server related to WRITE operation(s)
        regular_serv_uri = 'coaps://127.0.0.1:%d' % self.serv.get_listen_port()
        self.test_write('/%d' % OID.Security,
                        TLV.make_instance(0, [TLV.make_resource(RID.Security.ServerURI, regular_serv_uri),
                            TLV.make_resource(RID.Security.Bootstrap, 0),
                            TLV.make_resource(RID.Security.Mode, 0),
                            TLV.make_resource(RID.Security.ShortServerID, 1),
                            TLV.make_resource(RID.Security.PKOrIdentity, self.PSK_IDENTITY),
                            TLV.make_resource(RID.Security.SecretKey, self.PSK_KEY)]).serialize(),
                        format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                        server=self.bootstrap_server)
        self.test_write('/%d' % OID.Server,
                        TLV.make_instance(0, [
                            TLV.make_resource(RID.Server.ShortServerID, 1),
                            TLV.make_resource(RID.Server.Lifetime, 86400),
                            TLV.make_resource(RID.Server.NotificationStoring, False),
                            TLV.make_resource(RID.Server.Binding, "U")]).serialize(),
                        format=coap.ContentFormat.APPLICATION_LWM2M_TLV,
                        server=self.bootstrap_server)

        # 3. The Bootstrap Server performs a BOOTSTRAP-DISCOVER
        #    operation (CoAP GET Accept:40 / ) to verify the Client setup
        # D. In test step 3., the Bootstrap Server received the Success
        #    message ("2.05" Content) along with the payload related to the
        #    BOOTSTRAP-DISCOVER request and containing :
        #    lwm2m="1.0",</0/0>;ssid=1,</0/1>, </1/0>;ssid=1,</3/0>
        link_list = self.test_discover('/', server=self.bootstrap_server)
        links = link_list.split(b',')
        self.assertIn(b'lwm2m="1.0"', links)
        self.assertIn(b'</%d/0>;ssid=1' % (OID.Security,), links)
        self.assertIn(b'</%d/1>' % (OID.Security,), links)
        self.assertIn(b'</%d/0>;ssid=1' % (OID.Server,), links)
        self.assertIn(b'</%d/0>' % (OID.Device,), links)

        # 4. The Bootstrap Server performs a BOOTSTRAP-FINISH
        #    operation (CoAP POST /bs) to end-up that BS phase
        # Registration message (CoAP POST) is sent from client to server
        #
        # E. In test step 4., the Bootstrap Server received the Success
        #    message ("2.04" Changed) : no inconsistency detected
        req = Lwm2mBootstrapFinish()
        self.bootstrap_server.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(),
                            self.bootstrap_server.recv())
