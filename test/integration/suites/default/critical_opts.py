from framework.lwm2m_test import *

class CriticalOptsTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # This should result in 4.02 Bad Option response.
        pkt = Lwm2mRead('/1/1/0', options=[coap.Option.IF_NONE_MATCH])
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mErrorResponse.matching(pkt)(code=coap.Code.RES_BAD_OPTION),
                            self.serv.recv(timeout_s=1))

        # And this shuld work.
        pkt = Lwm2mRead('/1/1/0')
        self.serv.send(pkt)
        self.assertMsgEqual(Lwm2mContent.matching(pkt)(),
                            self.serv.recv(timeout_s=1))
