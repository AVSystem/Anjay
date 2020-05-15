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
from framework.lwm2m.tlv import TLV

from .utils import DataModel, ValueValidator

# Client shall be Bootstrapped with specific Portfolio instances
# /16/0/0:
#   - /16/0/0/0: "Host Device ID #1"
#   - /16/0/0/1: "Host Develce Manufacturer #1"
#   - /16/0/0/2: "Host Device Model #1"
#   - /16/0/0/3: "Host Device Software Version #1"
#
# We avoid explicit bootstrapping and just initialize /16 normally.
IDENTITIES_FOR_INSTANCE = {
    0: [(0, "Host Device ID #1"),
        # Yes, it's "Develce".
        (1, "Host Develce Manufacturer #1"),
        (2, "Host Device Model #1"),
        (3, "Host Device Software Version #1")],
    1: [(0, "Host Device ID #2"),
        (1, "Host Device Model #2")]
}

class Test:
    class Portfolio(DataModel.Test,
                    test_suite.Lwm2mDmOperations):
        def setUp(self):
            super().setUp()
            self.create_instance(self.serv, oid=OID.Portfolio, iid=0)
            self.test_write(path=ResPath.Portfolio[0].Identity,
                            value=TLV.make_multires(RID.Portfolio.Identity,
                                                    IDENTITIES_FOR_INSTANCE[0]).serialize(),
                            format=coap.ContentFormat.APPLICATION_LWM2M_TLV)


class Test1630_CreatePortfolioObjectInstance(Test.Portfolio):
    def runTest(self):
        # 1. Discover is performed by the Server on /16, expecting '</16/0/0>' in the resulting payload
        linklist = self.discover(self.serv, oid=OID.Portfolio).content.decode()
        self.assertLinkListValid(linklist)
        self.assertIn('<%s>' % (ResPath.Portfolio[0].Identity,), linklist.split(','))

        # 2. Create is performed </16/1> with specified payload
        self.create_instance_with_payload(self.serv, oid=OID.Portfolio, iid=1,
                                          payload=[TLV.make_multires(RID.Portfolio.Identity,
                                                                     IDENTITIES_FOR_INSTANCE[1])])
        # 3. Discover is performed by the Server on /16
        linklist = self.discover(self.serv, oid=OID.Portfolio).content.decode()
        self.assertLinkListValid(linklist)
        self.assertIn('<%s>' % (ResPath.Portfolio[0].Identity,), linklist.split(','))
        self.assertIn('<%s>' % (ResPath.Portfolio[1].Identity,), linklist.split(','))

        # 4. Read entire object.
        EXPECTED_TLV = TLV.make_instance(0, [ TLV.make_multires(RID.Portfolio.Identity, IDENTITIES_FOR_INSTANCE[0]) ]).serialize() \
                    + TLV.make_instance(1, [ TLV.make_multires(RID.Portfolio.Identity, IDENTITIES_FOR_INSTANCE[1]) ]).serialize()
        self.assertEqual(EXPECTED_TLV, self.read_object(self.serv, oid=OID.Portfolio).content)

class Test1635_DeletePortfolioObjectInstance(Test.Portfolio):
    def runTest(self):
        # 1. Discover is performed by the Server on /16, expecting '</16/0/0>' in the resulting payload
        linklist = self.discover(self.serv, oid=OID.Portfolio).content.decode()
        self.assertLinkListValid(linklist)
        self.assertIn('<%s>' % (ResPath.Portfolio[0].Identity,), linklist.split(','))

        # 2. Delete on each instance (we have one)
        self.delete_instance(self.serv, oid=OID.Portfolio, iid=0)

        # 3. Discover to confirm object is cleared
        self.assertEqual(self.discover(self.serv, oid=OID.Portfolio).content,
                         b'</%d>' % (OID.Portfolio,))
