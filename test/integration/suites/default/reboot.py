from framework.lwm2m_test import *

class RebootSendsResponseTest(test_suite.Lwm2mSingleServerTest):
    def _get_valgrind_args(self):
        # Reboot cannot be performed when demo is run under valgrind
        return []

    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # should send a response before rebooting
        req = Lwm2mExecute('/3/0/4')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mChanged.matching(req)(), self.serv.recv())

        # should register after rebooting
        self.serv.reset()
        self.assertDemoRegisters(self.serv)
