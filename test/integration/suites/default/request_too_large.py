from framework.lwm2m_test import *
import unittest
import struct

class RequestTooLarge(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        req = Lwm2mWrite('/5/0/0', random_stuff(16000))
        self.serv.send(req)
        res = self.serv.recv()
        self.assertMsgEqual(
            Lwm2mErrorResponse.matching(req)(coap.Code.RES_REQUEST_ENTITY_TOO_LARGE), res)
