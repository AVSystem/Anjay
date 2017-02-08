from framework.lwm2m_test import *

import socket

class SeparateResponseTest(test_suite.Lwm2mSingleServerTest):
    def setUp(self):
        # skip initial registration
        super().setUp(auto_register=False)

    def runTest(self):
        # receive Register
        req = self.serv.recv(timeout_s=1)
        self.assertMsgEqual(
                Lwm2mRegister('/rd?lwm2m=%s&ep=%s&lt=86400' % (DEMO_LWM2M_VERSION, DEMO_ENDPOINT_NAME)),
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
                            self.serv.recv(timeout_s=1))

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

        self.serv.send(invalid_req)

        # it should trigger a Reset response
        self.assertMsgEqual(Lwm2mReset(msg_id=invalid_req.msg_id),
                            self.serv.recv(timeout_s=1))

        # Separate Response: actual response
        req = Lwm2mChanged(msg_id=next(msg_id_generator),
                           token=req.token)
        req.type = coap.Type.CONFIRMABLE
        self.serv.send(req)

        self.assertMsgEqual(Lwm2mEmpty.matching(req)(),
                            self.serv.recv(timeout_s=1))

        # should not send more messages after receiving a correct response
        with self.assertRaises(socket.timeout, msg='unexpected message'):
            print(self.serv.recv(timeout_s=6))
