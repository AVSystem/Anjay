from framework.lwm2m_test import *

import unittest
import socket
import time

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
            server.send(Lwm2mCreated.matching(pkt)(location='/rd/demo',options=[block1]))

            with self.assertRaises(socket.timeout, msg='unexpected message'):
                print(server.recv(timeout_s=6))

class Register:
    class TestCase(test_suite.Lwm2mSingleServerTest):
        def setUp(self):
            # skip initial registration
            super().setUp(auto_register=False)

# Security (/0) instances MUST not be a part of the list
# see LWM2M spec, 2016-09-08 draft
expected_content = (b'</1/1>,</2/0>,</2/1>,</2/2>,</2/3>,</2/4>,</2/5>,'
                  + b'</2/6>,</2/7>,</2/8>,</2/9>,</2/10>,</2/11>,</2/12>,'
                  + b'</2/13>,</2/14>,</2/15>,</2/16>,</2/17>,</2/18>,</2/19>,'
                  + b'</2/20>,</2/21>,</2/22>,</3/0>,</4/0>,</5/0>,</6/0>,'
                  + b'</7/0>,</10/0>,</11>,</1337>,</11111/0>,</12359/0>,</12360>,'
                  + b'</12361/0>')

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
