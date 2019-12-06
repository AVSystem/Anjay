# -*- coding: utf-8 -*-
#
# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

import concurrent.futures
import socket
import unittest

from framework.lwm2m.coap.server import SecurityMode
from framework.lwm2m.coap.transport import Transport
from framework.lwm2m_test import *
from suites.default import bootstrap_client


class BlockRegister:
    class Test(unittest.TestCase):
        def __call__(self, server, timeout_s=None, verify=True):
            register_content = b''
            while True:
                if timeout_s is None:
                    pkt = server.recv()
                else:
                    pkt = server.recv(timeout_s=timeout_s)

                block1 = pkt.get_options(coap.Option.BLOCK1)
                self.assertIn(len(block1), {0, 1})
                register_content += pkt.content
                if len(block1) < 1 or not block1[0].has_more():
                    break
                server.send(Lwm2mContinue.matching(pkt)(options=block1))

            if verify:
                self.assertEquals(expected_content(), register_content)

            server.send(Lwm2mCreated.matching(pkt)(location='/rd/demo', options=block1))


class Register:
    class TestCase(test_suite.Lwm2mDmOperations):
        def setUp(self):
            # skip initial registration
            super().setUp(auto_register=False)


class RegisterUdp:
    class TestCase(Register.TestCase, test_suite.Lwm2mSingleServerTest):
        pass




def expected_content():
    result = []
    for obj in ResPath.objects():
        if obj.oid == OID.Security:
            # Security (/0) instances MUST not be a part of the list
            # see LwM2M spec, Register/Update operations description
            continue

        if obj.oid == OID.Server:
            result.append('</%d/1>' % (obj.oid,))
        elif obj.is_multi_instance or obj.version is not None:
            entry = '</%d>' % (obj.oid,)
            if obj.version is not None:
                entry += ';ver="%s"' % (obj.version,)
            result.append(entry)
        if not obj.is_multi_instance:
            result.append('</%d/0>' % (obj.oid,))
    return ','.join(result).encode()


class RegisterTest(RegisterUdp.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
            pkt)

        # should retry when no response is sent
        pkt = self.serv.recv(timeout_s=6)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
            pkt)

        # should ignore this message as Message ID does not match
        self.serv.send(Lwm2mCreated(msg_id=((pkt.msg_id + 1) % (1 << 16)),
                                    token=pkt.token,
                                    location='/rd/demo'))

        # should retry
        pkt = self.serv.recv(timeout_s=12)

        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
            pkt)

        # should not retry after receiving valid response
        self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))

        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


class RegisterWithLostSeparateAck(RegisterUdp.TestCase):
    def runTest(self):
        # should send Register request at start
        pkt = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mRegister('/rd?lwm2m=1.0&ep=%s&lt=86400' % (DEMO_ENDPOINT_NAME,),
                          content=expected_content()),
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
        self.setup_demo_with_servers(servers=1,
                                     extra_cmdline_args=extra_args,
                                     auto_register=False)

    def runTest(self):
        BlockRegister().Test()(self.serv)
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))


class ConcurrentRequestWhileWaitingForResponse:
    class TestMixin:
        def runTest(self):
            pkt = self.serv.recv()
            path = '/rd?lwm2m=1.0&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME
            self.assertMsgEqual(Lwm2mRegister(path, content=expected_content()), pkt)
            self.read_path(self.serv, ResPath.Device.Manufacturer)
            self.serv.send(Lwm2mCreated.matching(pkt)(location='/rd/demo'))


class ConcurrentRequestWhileWaitingForResponseUdp(
    ConcurrentRequestWhileWaitingForResponse.TestMixin, RegisterUdp.TestCase):
    pass




class RegisterUri:
    class TestMixin:
        def make_demo_args(self, *args, **kwargs):
            args = super().make_demo_args(*args, **kwargs)
            for i in range(len(args)):
                if args[i].startswith('coap'):
                    args[i] += '/i/am/crazy/and?lwm2m=i&ep=know&lt=it'
            return args

        def runTest(self):
            pkt = self.serv.recv()
            path = '/i/am/crazy/and/rd?lwm2m=i&ep=know&lt=it&lwm2m=1.0&ep=%s&lt=86400' % DEMO_ENDPOINT_NAME
            self.assertMsgEqual(Lwm2mRegister(path, content=expected_content()), pkt)
            self.serv.send(Lwm2mCreated.matching(pkt)(location='/some/weird/rd/point'))

            # Update shall not contain the path and query from Server URI
            self.communicate('send-update')
            pkt = self.serv.recv()
            self.assertMsgEqual(Lwm2mUpdate('/some/weird/rd/point', query=[], content=b''),
                                pkt)
            self.serv.send(Lwm2mChanged.matching(pkt)())

        def tearDown(self):
            self.teardown_demo_with_servers(path='/some/weird/rd/point')


class RegisterUriUdp(RegisterUri.TestMixin, RegisterUdp.TestCase):
    pass


