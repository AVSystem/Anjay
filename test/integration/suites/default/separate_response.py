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
import unittest

from framework.lwm2m_test import *

class SeparateResponseTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        # skip initial registration
        super().setUp(auto_register=False)

    def runTest(self):
        # receive Register
        req = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,)),
            req)

        # Separate Response: empty ACK
        self.serv.send(Lwm2mEmpty.matching(req)())

        # Separate Response: actual response
        msg_id_generator = SequentialMsgIdGenerator(req.msg_id + 100)

        req = Lwm2mCreated(msg_id=next(msg_id_generator),
                           token=req.token,
                           location=self.DEFAULT_REGISTER_ENDPOINT)
        req.type = coap.Type.CONFIRMABLE

        self.serv.send(req)
        self.assertMsgEqual(Lwm2mEmpty.matching(req)(),
                            self.serv.recv())

        # check Separate Response to an Update
        self.communicate('send-update')
        req = self.serv.recv()

        self.assertMsgEqual(Lwm2mUpdate(path=self.DEFAULT_REGISTER_ENDPOINT,
                                        content=b''),
                            req)

        # Separate Response: empty ACK
        self.serv.send(Lwm2mEmpty.matching(req)())

        # Separate Response with invalid token
        invalid_req = Lwm2mChanged(msg_id=next(msg_id_generator),
                                   token=get_another_token(req.token))
        invalid_req.type = coap.Type.CONFIRMABLE

        # Anjay sees it as unknown message and responds with Reset
        self.serv.send(invalid_req)
        self.assertMsgEqual(Lwm2mReset.matching(invalid_req)(),
                            self.serv.recv())

        # Separate Response: actual response
        req = Lwm2mChanged(msg_id=next(msg_id_generator),
                           token=req.token)
        req.type = coap.Type.CONFIRMABLE
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mEmpty.matching(req)(),
                            self.serv.recv())

        # should not send more messages after receiving a correct response
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=3))

