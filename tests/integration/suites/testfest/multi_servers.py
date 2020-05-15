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
from .dm.utils import DataModel


class Test950_MultiServersRegistration(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=2,
                                     auto_register=False)

    def runTest(self):
        # 1. Registration message (CoAP POST) is sent from the Client to
        #    Server #1
        #
        # A. In test step 1., the Server receives the REGISTER command
        #    along with the information related to the Server Account #1
        #    (see LwM2M-1.0-int-101 Test Case for more)
        # B. In test step 1., the Client receives the "Success" message from
        #    Server #1 (2.01 Created)
        self.assertDemoRegisters(self.servers[0])

        # 2. Registration message (CoAP POST) is sent from the Client to
        #    Server #2
        #
        # C. In test step 2., the Server receives the REGISTER command
        #    along with the information related to the Server Account #2
        #    (see LwM2M-1.0-int-101 Test Case for more)
        # D. In test step 2., the Client receives the "Success" message from
        #    Server #2 (2.01 Created)
        self.assertDemoRegisters(self.servers[1])

    def tearDown(self):
        self.teardown_demo_with_servers()


class Test951_MultiServersAttributes(DataModel.Test):
    def setUp(self):
        self.setup_demo_with_servers(servers=2)

    def runTest(self):
        # 1. The Server #1 communicates to the Client pmin=2 and
        #    pmax=10 periods with a WRITE-ATTRIBUTE (CoAP PUT)
        #    operation at the Device Object Instance level.
        #
        # A. In test step 1., the Server#1 receives the "Success" message
        #    from the Client (2.04 Changed)
        self.test_write_attributes('/%d/0' % OID.Device,
                                   server=self.servers[0],
                                   pmin=2, pmax=10)

        # 2. The Server #2 communicates to the Client pmin=15 and
        #    pmax=50 periods with a WRITE-ATTRIBUTE (CoAP PUT)
        #    operation at the Device Object Instance level.
        #
        # B. In test step 2., the Server#2 receives the "Success" message
        #    from the Client (2.04 Changed)
        self.test_write_attributes('/%d/0' % OID.Device,
                                   server=self.servers[1],
                                   pmin=15, pmax=50)

        # 3. The Server #1 sends a DISCOVER command to the Client
        #
        # C. In test step 3., the Server#1 receives the "Success" message
        #    from Client (2.05 Content) related to its DISCOVER command
        #    along with the payload containing the following information
        #    regarding the Device Object Instance :
        #    </3/0>;pmin=2;pmax=10,</3/0/0>,</3/0/1>,</3/0/2>,</3/0/3>,
        #    </3/0/11>,</3/0/16>
        link_list = self.test_discover('/%d/0' % OID.Device,
                                       server=self.servers[0]).decode()
        links = link_list.split(',')
        self.assertIn('</%d/0>;pmin=2;pmax=10' % (OID.Device,), links)
        self.assertIn('<%s>' % (ResPath.Device.Manufacturer,), links)
        self.assertIn('<%s>' % (ResPath.Device.ModelNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.SerialNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.FirmwareVersion,), links)
        # Multiple Resource, with dim attribute
        self.assertTrue(any(x.startswith('<%s>' % (ResPath.Device.ErrorCode,)) for x in links))
        self.assertIn('<%s>' % (ResPath.Device.SupportedBindingAndModes,), links)

        # 4. The Server #2 send a DISCOVER command to the Client
        #
        # D. In test step 4., the Server#2 receives the "Success" message
        #    from Client (2.05 Content) related to the DISCOVER
        #    command along with the payload containing the following
        #    information regarding the Device Object Instance :
        #    </3/0>;pmin=15;pmax=50,</3/0/0>,</3/0/1>,</3/0/2>,</3/0/3>
        #    ,</3/0/11>,</3/0/16>
        link_list = self.test_discover('/%d/0' % OID.Device,
                                       server=self.servers[1]).decode()
        links = link_list.split(',')
        self.assertIn('</%d/0>;pmin=15;pmax=50' % (OID.Device,), links)
        self.assertIn('<%s>' % (ResPath.Device.Manufacturer,), links)
        self.assertIn('<%s>' % (ResPath.Device.ModelNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.SerialNumber,), links)
        self.assertIn('<%s>' % (ResPath.Device.FirmwareVersion,), links)
        # Multiple Resource, with dim attribute
        self.assertTrue(any(x.startswith('<%s>' % (ResPath.Device.ErrorCode,)) for x in links))
        self.assertIn('<%s>' % (ResPath.Device.SupportedBindingAndModes,), links)
