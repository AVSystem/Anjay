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

from framework.lwm2m.tlv import TLV
from framework.lwm2m_test import *
from framework.test_utils import *
from . import block_response as br


class CacheTest(test_suite.Lwm2mSingleServerTest,
                test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(extra_cmdline_args=['--cache-size', '4096'])

        self.serv.set_timeout(timeout_s=1)
        self.create_instance(self.serv, oid=OID.Test, iid=1)

    def runTest(self):
        req = Lwm2mRead(ResPath.Test[1].Counter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'0'),
                            self.serv.recv())

        # execute Increment Counter
        inc_req = Lwm2mExecute(ResPath.Test[1].IncrementCounter)
        self.serv.send(inc_req)
        inc_res = self.serv.recv()
        self.assertMsgEqual(Lwm2mChanged.matching(inc_req)(), inc_res)

        req = Lwm2mRead(ResPath.Test[1].Counter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'1'),
                            self.serv.recv())

        # retransmit Increment Counter
        self.serv.send(inc_req)
        # should receive identical response
        self.assertMsgEqual(inc_res, self.serv.recv())

        # Counter should not increment second time
        req = Lwm2mRead(ResPath.Test[1].Counter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'1'),
                            self.serv.recv())

        # a new Execute should increment it though
        req = Lwm2mExecute(ResPath.Test[1].IncrementCounter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        req = Lwm2mRead(ResPath.Test[1].Counter)
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(content=b'2'),
                            self.serv.recv())


class MultipleServerCacheTest(test_suite.Lwm2mTest):
    def setUp(self):
        self.setup_demo_with_servers(servers=2,
                                     extra_cmdline_args=['--cache-size', '4096'])

        self.servers[0].set_timeout(timeout_s=1)
        self.servers[1].set_timeout(timeout_s=1)

    def tearDown(self):
        self.teardown_demo_with_servers()

    def runTest(self):
        s0_req = Lwm2mRead(ResPath.Device.SerialNumber)
        s1_req = Lwm2mWrite(ResPath.Server[2].Lifetime, '60')

        s0_req.msg_id = 1234
        s1_req.msg_id = 1234

        self.servers[0].send(s0_req)
        s0_res = self.servers[0].recv()
        self.assertMsgEqual(Lwm2mContent.matching(s0_req)(), s0_res)

        # send a different message with same ID from a different server
        # client should be able to distinguish those and not consider
        # this a retransmission of previous request
        self.servers[1].send(s1_req)
        s1_res = self.servers[1].recv()
        self.assertMsgEqual(Lwm2mChanged.matching(s1_req)(), s1_res)
        self.assertDemoUpdatesRegistration(self.servers[1], lifetime=60)

        # we should still be able to get both responses after retransmitting
        self.servers[0].send(s0_req)
        self.assertMsgEqual(s0_res, self.servers[0].recv())

        self.servers[1].send(s1_req)
        self.assertMsgEqual(s1_res, self.servers[1].recv())

        # ...even if the requests are swapped
        self.servers[0].send(s1_req)
        self.assertMsgEqual(s0_res, self.servers[0].recv())

        self.servers[1].send(s0_req)
        self.assertMsgEqual(s1_res, self.servers[1].recv())


class CachedBlockResponse(br.BlockResponseTest,
                          test_suite.Lwm2mDmOperations):
    def setUp(self):
        super().setUp(bytes_size=2048,
                      extra_cmdline_args=['--cache-size', '4096'])

    def runTest(self):
        # Induce a block transfer.
        req = Lwm2mRead(ResPath.Test[0].ResBytes)
        self.serv.send(req)

        res0 = self.serv.recv()
        self.assertMsgEqual(Lwm2mContent.matching(req)(), res0)

        # Retransmit, expecting exactly the same message
        self.serv.send(req)
        res1 = self.serv.recv()
        self.assertMsgEqual(res0, res1)

        # Read everything till the end
        self.read_blocks(iid=0, base_seq=1)
