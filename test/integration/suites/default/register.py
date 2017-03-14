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

import socket
import unittest

from framework.lwm2m_test import *


class BlockRegister:
    class Test(unittest.TestCase):
        def __call__(self, server):
            def get_block_option(pkt):
                for i in range(len(pkt.options)):
                    if pkt.options[i].matches(coap.Option.BLOCK1):
                        return pkt.options[i]
                else:
                    raise

            register_content = b''
            while True:
                pkt = server.recv(timeout_s=1)
                block1 = get_block_option(pkt)
                register_content += pkt.content
                if not block1.has_more():
                    break
                server.send(Lwm2mContinue.matching(pkt)(options=[block1]))

            self.assertEquals(expected_content, register_content)
            server.send(Lwm2mCreated.matching(pkt)(location='/rd/demo', options=[block1]))

            with self.assertRaises(socket.timeout, msg='unexpected message'):
                print(server.recv(timeout_s=6))


class Register:
    class TestCase(test_suite.Lwm2mSingleServerTest):
        def setUp(self):
            # skip initial registration
            super().setUp(auto_register=False)


# Security (/0) instances MUST not be a part of the list
# see LwM2M spec, Register/Update operations description
expected_content = (b'</1/1>,</2>,</3/0>,</4/0>,</5/0>,</6/0>,</7/0>,'
                    + b'</10/0>,</11>,</1337>,</11111/0>,</12359/0>,</12360>,</12361/0>')


class RegisterTest(Register.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME),
                          content=expected_content),
            pkt)

        # should retry when no response is sent
        pkt = self.serv.recv(timeout_s=6)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME),
                          content=expected_content),
            pkt)

        # should ignore this message as Message ID does not match
        self.serv.send(Lwm2mCreated(msg_id=((pkt.msg_id + 1) % (1 << 16)),
                                    token=pkt.token,
                                    location='/rd/demo'))

        # should retry
        pkt = self.serv.recv(timeout_s=12)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME),
                          content=expected_content),
            pkt)

        # should not retry after receiving valid response
        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


class RegisterWithLostSeparateAck(Register.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME),
                          content=expected_content),
            pkt)

        # Separate Response: Confirmable; msg_id does not match, but token does
        res = Lwm2mCreated(msg_id=((pkt.msg_id + 1) % (1 << 16)),
                           token=pkt.token,
                           location='/rd/demo')
        res.type = coap.Type.CONFIRMABLE

        # should respond with Empty ACK
        self.serv.send(res)

        self.assertMsgEqual(Lwm2mEmpty.matching(res)(),
                            self.serv.recv())


class RegisterWithBlock(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        extra_args = '-I 64 -O 128'.split()
        self.setup_demo_with_servers(num_servers=1,
                                     extra_cmdline_args=extra_args,
                                     auto_register=False)

    def runTest(self):
        BlockRegister().Test()(self.serv)
