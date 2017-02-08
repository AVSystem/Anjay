# Setting up the CoAP stream for communication with a specific server is done
# by calling _anjay_get_server_stream. The stream has to be released with
# _anjay_release_server_stream before acquiring it again, or the library will
# cause an assertion failure.
#
# There was an issue in the Update sending routine that prevented the release
# call from being made in the case of an socket error, such as receiving the
# "Port Unreachable" ICMP message. This caused the demo to terminate on the
# next attempt to use the stream.
#
# This test case ensures that the demo does not crash in such case.

import time

from framework.lwm2m_test import *


class AccessViolationOn256ByteUri(test_suite.Lwm2mSingleServerTest):
    # overrides a method from Lwm2mTest, called from Lwm2mTest.setup_demo_with_servers()
    def make_demo_args(self, *args, **kwargs):
        result = super().make_demo_args(*args, **kwargs)
        url = result[-1]
        self.assertTrue(url.startswith('coap://'))  # assert it is really a URL
        url = url.replace('.0', '.' + '0' * (256 - len(url) + 1), 1)  # "coap://127.000(...)000.0.1:12345/"
        self.assertEqual(len(url), 256)
        result[-1] = url
        return result

    def setUp(self):
        super().setUp(auto_register=False)

    def tearDown(self):
        super().tearDown(auto_deregister=False)

    def runTest(self):
        pass


class CrashAfterFailedUpdateTest(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        # close the server socket, so that demo receives Port Unreachable in response
        # to the next packet
        listen_port = self.serv.get_listen_port()
        self.serv.socket.close()

        self.communicate('send-update')
        time.sleep(1)

        # start the server again
        self.serv = Lwm2mServer(listen_port)

        # the Update failed, wait for retransmission to check if the stream was
        # properly released, i.e. sending another message does not trigger
        # assertion failure

        # demo should still work at this point

        # receive the update packet and send valid reply
        # otherwise demo will not finish until timeout

        # Note: explicit timeout is added on purpose to avoid following scenario,
        #       that happened few times during regular test runs:
        #
        # 1. Demo retrieves 'send-update' command.
        # 2. Test waits for 1s to miss that update request.
        # 3. Demo notices that it failed to deliver an update, therefore
        #    update is being rescheduled after 2s.
        # 4. Time passes, demo sends update again, but Lwm2mServer hasn't started
        #    just yet.
        # 5. Demo reschedules update again after about 4s of delay.
        # 6. Lwm2mServer has finally started.
        # 7. Test waited for 1 second for the update (default assertDemoUpdatesRegistration
        #    timeout), which is too little (see 5.), and failed.
        self.assertDemoUpdatesRegistration(timeout_s=5)


class CrashAfterRequestWithTokenFollwedByNoToken(test_suite.Lwm2mSingleServerTest):
    def runTest(self):
        self.serv.set_timeout(timeout_s=1)

        # failure to clear the token from last message remembered by output
        # buffer combined with not overriding cached token length in case of
        # a zero-length token caused anjay to incorrectly assume a non-empty
        # token was already written to the buffer
        req = Lwm2mRead('/6/0/0', token=b'foo')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())

        # the bug causes assertion failure during handling of this message
        req = Lwm2mRead('/6/0/0', token=b'')
        self.serv.send(req)
        self.assertMsgEqual(Lwm2mContent.matching(req)(),
                            self.serv.recv())
