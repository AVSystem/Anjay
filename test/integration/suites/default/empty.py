from framework.lwm2m_test import *

import socket
import time

class EmptyTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        # skip initial registration
        super().setUp(auto_register=False)

    def runTest(self):
        pkt0 = self.serv.recv()
        register_send_time = time.time()

        # We are ignoring demo registration. It will be retried in 2s + delta, such that
        # 0ms <= delta < 1000ms.
        pkt1 = self.serv.recv(timeout_s=4)
        reregister_send_time = time.time()

        # Now, since we know a rough estimate of the choosen timeout by the demo, and knowing
        # that the next retry will be performed after 2 * current_timeout we can test whether
        # demo doesn't respond to invalid empty messages.
        next_timeout = 2.0 * (reregister_send_time - register_send_time)

        # Unfortunately timing isn't perfect and it also depends on the speed of python
        # execution, therefore computing next_timeout as above will sometimes cause test to
        # fail due to unexpected message within forbidden timespan. To resolve this issue,
        # bias of 1s is introduced, which of course helps, but makes our test less effective.
        next_timeout -= 1.0

        # packets validation performed now, to not waste time in a let's say: critical moment
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
                pkt0)
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
                pkt1)

        # the device should ignore 0.00 Empty with anything other than the header
        self.serv.send(coap.Packet(type=coap.Type.ACKNOWLEDGEMENT,
                                   code=coap.Code.EMPTY,
                                   msg_id=pkt1.msg_id,
                                   token=b'foo'))
        self.serv.send(coap.Packet(type=coap.Type.ACKNOWLEDGEMENT,
                                   code=coap.Code.EMPTY,
                                   msg_id=pkt1.msg_id,
                                   token=b'',
                                   options=[coap.Option.CONTENT_FORMAT(coap.ContentFormat.TEXT_PLAIN)]))
        self.serv.send(coap.Packet(type=coap.Type.ACKNOWLEDGEMENT,
                                   code=coap.Code.EMPTY,
                                   msg_id=pkt1.msg_id,
                                   token=b'',
                                   content=b"hurr durr i'ma content"))

        # device should not respond before timeout (next_timeout since receiving the re-Register)
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=next_timeout-(time.time()-reregister_send_time)))

        # should retry
        self.assertDemoRegisters(server=self.serv)

        ### CoAP ping test
        # device should respond to CoAP ping by sending Reset
        ping_req = Lwm2mEmpty(type=coap.Type.CONFIRMABLE)
        self.serv.send(ping_req)
        self.assertMsgEqual(Lwm2mReset.matching(ping_req)(),
                            self.serv.recv(timeout_s=1))
